/* M5b-1：WiFi 事件通道 + 配网 + NTP（契约 docs/contracts/C08-wifi-channel.md，2026-10-31 冻结）。
 *
 * 组成：
 *   1. WiFi STA（2.4 GHz）：NVS `net/ssid`,`net/pass` 有凭据开机直连；断开按
 *      1/2/4/8/16/30 s 退避重连；有凭据 30 s 未拿到 IP → 回落 AP 配网 120 s → 再试 STA（循环）。
 *   2. AP 配网：SSID `ESP32-Touch-<MAC 后4>`、WPA2 口令 `touch154`、192.168.4.1；
 *      HTTP `GET /`（扫描列表+SSID+密码+保存）、`GET /scan`、`POST /save`（存 NVS 后
 *      esp_restart）、`GET /status`。AP 用 APSTA 模式以便 /scan 扫描（STA 接口不发起连接）。
 *   3. TCP server :8766（单客户端，新连接顶替旧连接）：字节流 → pet_linebuf 切行（≤512 B，
 *      剥 \r）→ 跨通道去重 → pet_net_set_line_cb（在内部 TCP 任务上下文调用，不是 app_main）。
 *   4. SNTP：STA 获得 IP 后启动 pool.ntp.org，TZ=CST-8；同步成功 time_valid=true。
 *
 * 失败降级（C08 §4）：NVS / WiFi 驱动失败 → 记日志、状态 OFF、pet_net_start() 返回错误；
 * HTTP / TCP / SNTP 失败只记日志，不阻塞显示/按键（设备其余功能照常）。
 *
 * 与 main 的接线（main 不在本组件修改；详见 pet_net.h 顶部）：
 *   - 开机先 pet_net_set_line_cb(cb)，再 pet_net_start()；
 *   - cb 内用 pet_link_parse 解析并投递既有事件队列（回调里不要阻塞）；
 *   - USB 路径在解析前调用 pet_net_line_accept(now_ms, line, len) 实现跨通道去重；
 *   - 菜单 WIFI / 串口命令 W → pet_net_enter_provisioning()。
 *
 * 行为参考上游 ESP32-buddy（MIT）pet_net.c / pet_prov.c / pet_evt_tcp.c：按 C08 重写，
 * 非逐字节复制；上游实现里与本项目无关的控制通道/PTT/DNS 劫持等均未引入。
 */
#include "pet_net.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "pet_linebuf.h"
#include "pet_net_dedupe.h"

static const char *TAG = "pet-net";

#define NET_NVS_NAMESPACE "net"
#define NET_SSID_MAX 33 /* 802.11 SSID ≤32 B + NUL */
#define NET_PASS_MAX 65 /* WPA2 口令 ≤64 B + NUL */
#define NET_AP_CHANNEL 1
#define NET_AP_MAX_CONN 4
#define NET_TCP_CHUNK 128
#define NET_TCP_ACCEPT_TICK_MS 500

#define NET_NOTIFY_EVT 0x01u  /* WiFi/IP 事件到达（重新评估状态） */
#define NET_NOTIFY_PROV 0x02u /* 请求进入 AP 配网（菜单/串口命令） */

#define NET_TASK_STACK 4096
#define NET_TCP_TASK_STACK 4096
#define NET_TICK_MS 200
#define NET_RSSI_REFRESH_MS 30000u
#define NET_SCAN_MAX 20
#define NET_SCAN_OUT 20
#define NET_SAVE_GRACE_MS 600

/* 共享状态（net 任务 / event 任务 / httpd 任务 / 主任务都会读；锁保护）。 */
typedef struct {
    pet_net_state_t state;
    char ssid[NET_SSID_MAX];
    char ip[16];
    int rssi;
    bool time_valid;
} net_status_t;

static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_dedupe_lock;
static TaskHandle_t s_net_task;
static net_status_t s_status = {
    .state = PET_NET_OFF,
    .ssid = {0},
    .ip = {0},
    .rssi = 0,
    .time_valid = false,
};
static pet_net_line_cb_t s_line_cb;
static pet_net_dedupe_t s_dedupe;

/* 运行状态（只在 start / net 任务 / event 任务内改；32 位读写不加锁，见各字段注释）。 */
static bool s_started;
static bool s_wifi_started;
static volatile bool s_tcp_go;   /* 首次拿到 IP（STA 或 AP）后置位：TCP 任务开始监听 */
static bool s_sntp_started;
static bool s_ap_fallback;       /* AP 是否由 STA 失败回落而来（120 s 后重试 STA） */
static volatile int32_t s_sta_deadline_ms; /* 30 s 未连上 → AP；0 = 无 */
static volatile int32_t s_ap_deadline_ms;  /* 回落 AP 的 120 s 期限；0 = 无 */
static volatile int32_t s_retry_at_ms;
static volatile bool s_retry_pending;
static int32_t s_rssi_refresh_ms;
static int s_fail_count;

static char s_ssid[NET_SSID_MAX];
static char s_pass[NET_PASS_MAX];
static char s_ap_ssid[NET_SSID_MAX];
static esp_netif_t *s_ap_netif;
static httpd_handle_t s_httpd;
static esp_event_handler_instance_t s_ev_wifi;
static esp_event_handler_instance_t s_ev_ip;

/* 扫描结果（httpd 单任务串行处理请求，静态缓冲安全）。 */
static wifi_ap_record_t s_scan_recs[NET_SCAN_MAX];

/* ---- 前向声明（net 任务 / TCP 任务 / 事件处理互相引用） ---- */
static esp_err_t net_ap_start(bool fallback);
static esp_err_t net_sta_start(void);
static void net_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);
static void net_task(void *arg);
static void tcp_task(void *arg);
static void sntp_ensure_started(void);
static esp_err_t prov_start(void);
static void prov_stop(void);

/* ---- 时间 / 字符串小工具 ---- */

static uint32_t now_ms32(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static int32_t now_ms_signed(void)
{
    return (int32_t)now_ms32();
}

/* 有界拷贝（总是 NUL 结尾，超长截断）：替代 strncpy/snprintf，避免截断告警。 */
static void net_copy_str(char *dst, size_t dst_sz, const char *src)
{
    size_t n = 0;
    if (dst == NULL || dst_sz == 0) {
        return;
    }
    if (src != NULL) {
        while (n + 1 < dst_sz && src[n] != '\0') {
            dst[n] = src[n];
            n++;
        }
    }
    dst[n] = '\0';
}

/* WiFi SSID 定长字段拷贝：不足补 0；恰好 32 B 时不补 NUL（802.11 上限）。 */
static void net_copy_ssid(uint8_t *dst, size_t dst_sz, const char *ssid)
{
    size_t n = 0;
    if (dst == NULL || dst_sz == 0) {
        return;
    }
    memset(dst, 0, dst_sz);
    if (ssid != NULL) {
        n = strlen(ssid);
        if (n > dst_sz) {
            n = dst_sz;
        }
        memcpy(dst, ssid, n);
    }
}

/* ---- 共享状态访问（锁保护；只在任务上下文调用，不在 ISR） ---- */

static void net_lock(void)
{
    if (s_lock != NULL) {
        (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void net_unlock(void)
{
    if (s_lock != NULL) {
        (void)xSemaphoreGive(s_lock);
    }
}

static pet_net_state_t net_state_get(void)
{
    pet_net_state_t st;
    net_lock();
    st = s_status.state;
    net_unlock();
    return st;
}

static void net_state_set(pet_net_state_t state)
{
    net_lock();
    s_status.state = state;
    net_unlock();
}

static void net_ip_set(const char *ip)
{
    net_lock();
    net_copy_str(s_status.ip, sizeof s_status.ip, ip);
    net_unlock();
}

static void net_ssid_set(const char *ssid)
{
    net_lock();
    net_copy_str(s_status.ssid, sizeof s_status.ssid, ssid);
    net_unlock();
}

static void net_rssi_set(int rssi)
{
    net_lock();
    s_status.rssi = rssi;
    net_unlock();
}

static void net_time_valid_set(bool valid)
{
    net_lock();
    s_status.time_valid = valid;
    net_unlock();
}

/* ---- NVS 凭据（C08 §2：namespace `net`，键 ssid/pass） ---- */

static bool net_cred_load(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz)
{
    nvs_handle_t h;
    if (nvs_open(NET_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len_ssid = ssid_sz;
    size_t len_pass = pass_sz;
    const esp_err_t e_ssid = nvs_get_str(h, "ssid", ssid, &len_ssid);
    const esp_err_t e_pass = nvs_get_str(h, "pass", pass, &len_pass);
    nvs_close(h);
    if (e_ssid != ESP_OK || e_pass != ESP_OK) {
        return false;
    }
    const size_t n_ssid = strlen(ssid);
    const size_t n_pass = strlen(pass);
    if (n_ssid == 0 || n_ssid > 32 || n_pass > 64) {
        return false;
    }
    return true;
}

static esp_err_t net_cred_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NET_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, "ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, "pass", pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/* ---- /scan 结果整理：同名留最强、RSSI 降序、上限 NET_SCAN_OUT ---- */

typedef struct {
    char ssid[NET_SSID_MAX];
    int8_t rssi;
    uint8_t auth;
} net_scan_item_t;

static net_scan_item_t s_scan_items[NET_SCAN_MAX];

static size_t net_scan_collect(uint16_t num)
{
    size_t n = 0;
    for (uint16_t i = 0; i < num && n < NET_SCAN_MAX; i++) {
        const wifi_ap_record_t *r = &s_scan_recs[i];
        if (r->ssid[0] == '\0') {
            continue; /* 空/隐藏 SSID 跳过 */
        }
        size_t j = 0;
        while (j < n && strncmp(s_scan_items[j].ssid, (const char *)r->ssid,
                                sizeof s_scan_items[j].ssid) != 0) {
            j++;
        }
        if (j < n) { /* 同名：留最强 */
            if (r->rssi > s_scan_items[j].rssi) {
                s_scan_items[j].rssi = r->rssi;
                s_scan_items[j].auth = (uint8_t)r->authmode;
            }
            continue;
        }
        net_copy_str(s_scan_items[n].ssid, sizeof s_scan_items[n].ssid,
                     (const char *)r->ssid);
        s_scan_items[n].rssi = r->rssi;
        s_scan_items[n].auth = (uint8_t)r->authmode;
        n++;
    }
    /* RSSI 降序（n ≤ 20，选择排序足够） */
    for (size_t a = 0; a + 1 < n; a++) {
        size_t best = a;
        for (size_t b = a + 1; b < n; b++) {
            if (s_scan_items[b].rssi > s_scan_items[best].rssi) {
                best = b;
            }
        }
        if (best != a) {
            const net_scan_item_t tmp = s_scan_items[a];
            s_scan_items[a] = s_scan_items[best];
            s_scan_items[best] = tmp;
        }
    }
    return n;
}

/* ---- JSON 字符串转义 / 表单解析（配网页用；无外部依赖） ---- */

static void json_escape(const char *in, char *out, size_t out_sz)
{
    size_t n = 0;
    if (in == NULL || out == NULL || out_sz == 0) {
        return;
    }
    for (; *in != '\0'; in++) {
        const unsigned char c = (unsigned char)*in;
        char tmp[8];
        size_t len = 1;
        if (c == '"' || c == '\\') {
            tmp[0] = '\\';
            tmp[1] = (char)c;
            len = 2;
        } else if (c < 0x20u) {
            (void)snprintf(tmp, sizeof tmp, "\\u%04X", (unsigned)c);
            len = strlen(tmp);
        } else {
            tmp[0] = (char)c;
        }
        if (n + len + 1 > out_sz) {
            break;
        }
        memcpy(&out[n], tmp, len);
        n += len;
    }
    out[n] = '\0';
}

static int net_hex_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool net_url_decode(const char *raw, size_t raw_len, char *out, size_t out_sz)
{
    size_t n = 0;
    for (size_t i = 0; i < raw_len; i++) {
        char c = raw[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            if (i + 2 >= raw_len) {
                return false;
            }
            const int hi = net_hex_val(raw[i + 1]);
            const int lo = net_hex_val(raw[i + 2]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            c = (char)((hi << 4) | lo);
            i += 2;
        }
        if (n + 1 >= out_sz) {
            return false;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return true;
}

static bool net_form_field(const char *body, const char *key, char *out, size_t out_sz)
{
    const size_t klen = strlen(key);
    const char *p = body;
    while (*p != '\0') {
        const char *amp = strchr(p, '&');
        const size_t seg = (amp != NULL) ? (size_t)(amp - p) : strlen(p);
        if (seg > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            return net_url_decode(p + klen + 1, seg - klen - 1, out, out_sz);
        }
        if (amp == NULL) {
            break;
        }
        p = amp + 1;
    }
    return false;
}

/* ---- AP 配网页（内联样式/脚本，无外部资源；GET / 表单 = 扫描列表 + SSID + 密码 + 保存） ---- */

static const char PAGE_HOME[] =
    "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32-Touch 配网</title><style>"
    "body{font-family:system-ui,sans-serif;margin:0;padding:16px;background:#f3f5f7;color:#1f2328}"
    ".card{max-width:380px;margin:20px auto;background:#fff;border-radius:12px;padding:20px;"
    "box-shadow:0 2px 10px rgba(0,0,0,.08)}"
    "h1{font-size:20px;margin:0 0 4px}"
    ".muted{color:#57606a;font-size:14px;margin:0 0 12px}"
    "label{display:block;margin:12px 0 4px;font-size:14px;color:#57606a}"
    "input{width:100%;box-sizing:border-box;padding:10px;font-size:16px;border:1px solid #d0d7de;"
    "border-radius:8px}"
    "button{font:inherit;font-size:16px;border:0;border-radius:8px;cursor:pointer}"
    "#rescan{width:100%;margin:4px 0 8px;padding:10px;background:#eaeef2;color:#0969da}"
    "#list{display:flex;flex-direction:column;gap:8px;max-height:260px;overflow-y:auto}"
    ".net{display:flex;justify-content:space-between;align-items:center;gap:10px;text-align:left;"
    "padding:12px;background:#f6f8fa;color:#1f2328;border:1px solid #d0d7de}"
    ".net.on{background:#ddf4ff;border-color:#0969da}"
    ".sig{color:#0969da;letter-spacing:2px}"
    ".empty{color:#8c959f;font-size:14px;margin:8px 0}"
    ".sb{width:100%;margin-top:16px;padding:12px;background:#0969da;color:#fff}"
    ".hint{color:#8c959f;font-size:13px;margin:12px 0 0}"
    "summary{cursor:pointer;margin-top:12px;font-size:14px;color:#57606a}"
    "</style></head><body><main class='card'>"
    "<h1>ESP32-Touch 配网</h1>"
    "<p class='muted'>设备状态：<b id='state'>读取中…</b></p>"
    "<button id='rescan' type='button'>重新扫描</button>"
    "<div id='list'><p class='empty'>扫描中…</p></div>"
    "<details><summary>手动输入网络名（隐藏网络）</summary>"
    "<label for='manual'>网络名称 SSID</label>"
    "<input id='manual' maxlength='32' autocomplete='off' placeholder='1-32 字节'>"
    "</details>"
    "<form method='POST' action='/save' onsubmit='return prepare()'>"
    "<input type='hidden' name='ssid' id='ssid'>"
    "<label for='password'>WiFi 密码</label>"
    "<input id='password' name='password' type='password' maxlength='64' autocomplete='off'>"
    "<button class='sb' type='submit'>保存并重启</button></form>"
    "<p class='hint'>保存后设备重启并连接所选 WiFi；若 30 秒内连不上，会自动回到本热点供重新配置。</p>"
    "</main><script>"
    "var picked='';"
    "function bars(r){return r>-55?'\\u2582\\u2584\\u2586\\u2588':r>-67?'\\u2582\\u2584\\u2586':r>-78?'\\u2582\\u2584':'\\u2582'}"
    "function msg(t){var l=document.getElementById('list');l.innerHTML='';"
    "var p=document.createElement('p');p.className='empty';p.textContent=t;l.appendChild(p)}"
    "function clearPick(){picked='';var xs=document.querySelectorAll('.net');"
    "for(var i=0;i<xs.length;i++){xs[i].classList.remove('on')}}"
    "function scan(){var b=document.getElementById('rescan');b.disabled=true;clearPick();msg('扫描中…');"
    "fetch('/scan').then(function(r){return r.json()}).then(function(j){b.disabled=false;"
    "if(!j.length){msg('未扫描到网络，可重试或手动输入');return}"
    "var l=document.getElementById('list');l.innerHTML='';"
    "j.forEach(function(n){var el=document.createElement('button');el.type='button';el.className='net';"
    "var nm=document.createElement('span');nm.textContent=n.ssid;"
    "var sg=document.createElement('span');sg.className='sig';sg.textContent=bars(n.rssi);"
    "el.appendChild(nm);el.appendChild(sg);"
    "el.onclick=function(){picked=n.ssid;document.getElementById('manual').value='';"
    "var xs=document.querySelectorAll('.net');for(var k=0;k<xs.length;k++){xs[k].classList.remove('on')}"
    "el.classList.add('on')};l.appendChild(el)})})"
    ".catch(function(){b.disabled=false;msg('扫描失败，可重试或手动输入')})}"
    "function prepare(){var m=document.getElementById('manual').value.trim();"
    "var s=m||picked;if(!s){alert('请选择一个网络，或手动输入 SSID');return false}"
    "document.getElementById('ssid').value=s;return true}"
    "document.getElementById('rescan').onclick=scan;"
    "document.getElementById('manual').oninput=clearPick;"
    "scan();"
    "fetch('/status').then(function(r){return r.json()}).then(function(s){"
    "var el=document.getElementById('state');"
    "if(s.state=='online'){el.textContent='已连接 '+s.ip+(s.time_valid?'（时间已同步）':'')}"
    "else if(s.state=='ap'){el.textContent='配网热点 '+s.ssid}"
    "else if(s.state=='connecting'){el.textContent='正在连接 '+s.ssid}"
    "else{el.textContent='未连接'}})"
    ".catch(function(){document.getElementById('state').textContent='未连接'});"
    "</script></body></html>";

static const char PAGE_SAVED[] =
    "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>已保存</title></head><body style='font-family:system-ui,sans-serif;padding:24px'>"
    "<h2>已保存，设备正在重启并连接 WiFi</h2>"
    "<p>可以断开本热点；若 30 秒内未连接成功，设备会自动重新开启热点供再次配置。</p>"
    "</body></html>";

static esp_err_t send_html(httpd_req_t *req, const char *status, const char *body)
{
    if (status != NULL) {
        (void)httpd_resp_set_status(req, status);
    }
    (void)httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t save_bad_request(httpd_req_t *req, const char *msg)
{
    char page[640];
    (void)snprintf(page, sizeof page,
                   "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
                   "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
                   "<body style='font-family:system-ui,sans-serif;padding:24px'>"
                   "<h2>配网保存失败</h2><p>%.200s</p><p><a href='/'>返回重试</a></p></body></html>",
                   msg);
    return send_html(req, "400 Bad Request", page);
}

/* ---- HTTP 处理：GET /、GET /scan、GET /status、POST /save ---- */

static esp_err_t h_root(httpd_req_t *req)
{
    return send_html(req, NULL, PAGE_HOME);
}

static esp_err_t h_status(httpd_req_t *req)
{
    pet_net_status_t st;
    char essid[256];
    char out[512];
    pet_net_get_status(&st);
    json_escape(st.ssid, essid, sizeof essid);
    (void)snprintf(out, sizeof out,
                   "{\"state\":\"%s\",\"ssid\":\"%.240s\",\"ip\":\"%.15s\",\"rssi\":%d,\"time_valid\":%s}",
                   pet_net_state_name(st.state), essid, st.ip, st.rssi,
                   st.time_valid ? "true" : "false");
    (void)httpd_resp_set_type(req, "application/json");
    (void)httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_scan(httpd_req_t *req)
{
    (void)httpd_resp_set_type(req, "application/json");
    wifi_scan_config_t scfg = {0};
    scfg.show_hidden = false;
    if (esp_wifi_scan_start(&scfg, true) != ESP_OK) {
        return httpd_resp_send(req, "[]", 2); /* 失败/超时/正在扫描 → 空数组 + 200 */
    }
    uint16_t num = NET_SCAN_MAX;
    if (esp_wifi_scan_get_ap_records(&num, s_scan_recs) != ESP_OK || num == 0) {
        return httpd_resp_send(req, "[]", 2);
    }
    const size_t n = net_scan_collect(num);
    esp_err_t err = httpd_resp_send_chunk(req, "[", 1);
    for (size_t i = 0; err == ESP_OK && i < n && i < NET_SCAN_OUT; i++) {
        char essid[256];
        char item[384];
        json_escape(s_scan_items[i].ssid, essid, sizeof essid);
        (void)snprintf(item, sizeof item,
                       "%s{\"ssid\":\"%.240s\",\"rssi\":%d,\"auth\":%u}",
                       (i == 0) ? "" : ",", essid, (int)s_scan_items[i].rssi,
                       (unsigned)s_scan_items[i].auth);
        err = httpd_resp_send_chunk(req, item, HTTPD_RESP_USE_STRLEN);
    }
    if (err != ESP_OK) {
        return err; /* 手机端提前断开等：中止即可 */
    }
    err = httpd_resp_send_chunk(req, "]", 1);
    if (err != ESP_OK) {
        return err;
    }
    return httpd_resp_send_chunk(req, NULL, 0); /* 收尾 0 长度块 */
}

static esp_err_t h_save(httpd_req_t *req)
{
    char body[320];
    if (req->content_len <= 0 || (size_t)req->content_len >= sizeof body) {
        return save_bad_request(req, "请求长度无效");
    }
    size_t got = 0;
    while (got < (size_t)req->content_len) {
        const int n = httpd_req_recv(req, &body[got], (size_t)req->content_len - got);
        if (n <= 0) {
            return save_bad_request(req, "读取请求失败");
        }
        got += (size_t)n;
    }
    body[got] = '\0';

    char ssid[NET_SSID_MAX];
    char pass[NET_PASS_MAX];
    /* 兼容 `password`（页面）与 `pass`（命令行/调试）两种字段名 */
    const bool has_pass = net_form_field(body, "password", pass, sizeof pass) ||
                          net_form_field(body, "pass", pass, sizeof pass);
    if (!net_form_field(body, "ssid", ssid, sizeof ssid) || !has_pass ||
        ssid[0] == '\0' || strlen(ssid) > 32 || strlen(pass) > 64) {
        return save_bad_request(req, "参数无效：SSID 需 1-32 字节，密码 0-64 字节");
    }
    const esp_err_t err = net_cred_save(ssid, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "prov: save failed (%s)", esp_err_to_name(err));
        return save_bad_request(req, "保存失败（NVS 不可写）");
    }
    ESP_LOGI(TAG, "prov: credentials saved ssid=%s (restart)", ssid); /* 口令不入日志 */
    /* 先应答再重启：给手机留出收到页面的时间（C08 §2：保存后 esp_restart） */
    (void)httpd_resp_set_hdr(req, "Connection", "close");
    (void)send_html(req, NULL, PAGE_SAVED);
    vTaskDelay(pdMS_TO_TICKS(NET_SAVE_GRACE_MS));
    esp_restart();
    return ESP_OK;
}

static const httpd_uri_t URI_ROOT = {.uri = "/", .method = HTTP_GET, .handler = h_root};
static const httpd_uri_t URI_SCAN = {.uri = "/scan", .method = HTTP_GET, .handler = h_scan};
static const httpd_uri_t URI_STATUS = {.uri = "/status", .method = HTTP_GET,
                                       .handler = h_status};
static const httpd_uri_t URI_SAVE = {.uri = "/save", .method = HTTP_POST,
                                     .handler = h_save};

static esp_err_t prov_start(void)
{
    if (s_httpd != NULL) {
        return ESP_OK;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144; /* /save 栈上表单缓冲 + snprintf 需要余量 */
    cfg.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_httpd, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "prov: http start failed (%s)", esp_err_to_name(err));
        s_httpd = NULL;
        return err;
    }
    if (httpd_register_uri_handler(s_httpd, &URI_ROOT) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &URI_SCAN) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &URI_STATUS) != ESP_OK ||
        httpd_register_uri_handler(s_httpd, &URI_SAVE) != ESP_OK) {
        ESP_LOGE(TAG, "prov: uri register failed");
        (void)httpd_stop(s_httpd);
        s_httpd = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "prov: http://%s/", PET_NET_AP_IP);
    return ESP_OK;
}

static void prov_stop(void)
{
    if (s_httpd != NULL) {
        (void)httpd_stop(s_httpd); /* 停收新请求并回收 httpd 任务 */
        s_httpd = NULL;
        ESP_LOGI(TAG, "prov: http stopped");
    }
}

/* AP netif 静态地址 192.168.4.1（默认值一致，显式设置防默认漂移；C08 §2）。 */
static esp_err_t net_ap_netif_setup(void)
{
    if (s_ap_netif == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_ip_info_t info = {0};
    if (esp_netif_str_to_ip4(PET_NET_AP_IP, &info.ip) != ESP_OK ||
        esp_netif_str_to_ip4(PET_NET_AP_IP, &info.gw) != ESP_OK ||
        esp_netif_str_to_ip4("255.255.255.0", &info.netmask) != ESP_OK) {
        ESP_LOGE(TAG, "ap: ip parse failed");
        return ESP_FAIL;
    }
    const esp_err_t err = esp_netif_set_ip_info(s_ap_netif, &info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ap: set ip failed (%s)", esp_err_to_name(err));
        return err;
    }
    (void)esp_netif_dhcps_start(s_ap_netif);
    return ESP_OK;
}

/* ---- SNTP：STA 获得 IP 后启动（C08 §3：pool.ntp.org，TZ=CST-8，同步后 time_valid） ---- */

static void net_sntp_sync_cb(struct timeval *tv)
{
    (void)tv; /* lwip 回调：只做静态写入 + 日志 */
    net_time_valid_set(true);
    const time_t now = time(NULL);
    struct tm lt;
    if (localtime_r(&now, &lt) != NULL) {
        ESP_LOGI(TAG, "sntp: synced %04d-%02d-%02d %02d:%02d:%02d CST-8",
                 lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour,
                 lt.tm_min, lt.tm_sec);
    } else {
        ESP_LOGI(TAG, "sntp: synced (time_valid=true)");
    }
}

static void sntp_ensure_started(void)
{
    if (s_sntp_started) {
        return;
    }
    s_sntp_started = true;
    (void)setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(net_sntp_sync_cb);
    esp_sntp_init();
    ESP_LOGI(TAG, "sntp: started pool.ntp.org (TZ=CST-8)");
}

/* ---- WiFi 模式切换（net 任务上下文；boot 时由 pet_net_start 调用一次） ---- */

static void net_refresh_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        net_rssi_set(ap.rssi);
    }
}

/* 进入 STA：停 AP/HTTP → STA 模式 → 连接；30 s 未拿到 IP 由 net 任务回落 AP。 */
static esp_err_t net_sta_start(void)
{
    net_state_set(PET_NET_CONNECTING); /* 先置态：停机期间的 DISCONNECTED 事件被忽略 */
    net_ip_set("");
    net_rssi_set(0);
    prov_stop();
    if (s_wifi_started) {
        (void)esp_wifi_disconnect();
        (void)esp_wifi_stop();
        s_wifi_started = false;
    }
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        ESP_LOGE(TAG, "sta: set mode failed");
        net_state_set(PET_NET_OFF);
        return ESP_FAIL;
    }
    wifi_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    net_copy_ssid(cfg.sta.ssid, sizeof cfg.sta.ssid, s_ssid);
    net_copy_ssid(cfg.sta.password, sizeof cfg.sta.password, s_pass);
    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "sta: set config failed");
        net_state_set(PET_NET_OFF);
        return ESP_FAIL;
    }
    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "sta: wifi start failed");
        net_state_set(PET_NET_OFF);
        return ESP_FAIL;
    }
    s_wifi_started = true;
    net_ssid_set(s_ssid);
    s_fail_count = 0;
    s_retry_pending = false;
    s_ap_fallback = false;
    s_ap_deadline_ms = 0;
    s_sta_deadline_ms = (int32_t)(now_ms32() + PET_NET_STA_TIMEOUT_MS);
    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sta: connect failed (%s), retry in 1 s", esp_err_to_name(err));
        s_retry_at_ms = (int32_t)(now_ms32() + 1000u);
        s_retry_pending = true;
    }
    ESP_LOGI(TAG, "sta: connecting ssid=%s", s_ssid);
    return ESP_OK;
}

/* 进入 AP 配网（C08 §2）：WPA2 touch154 / 192.168.4.1 / HTTP 页面。
 * fallback=true 表示由 STA 30 s 超时回落，120 s 后自动回 STA。 */
static esp_err_t net_ap_start(bool fallback)
{
    net_state_set(PET_NET_AP); /* 先置态：停机期间的 DISCONNECTED 事件被忽略 */
    net_ip_set("");
    net_rssi_set(0);
    prov_stop();
    if (s_wifi_started) {
        (void)esp_wifi_disconnect();
        (void)esp_wifi_stop();
        s_wifi_started = false;
    }
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
        /* APSTA：AP 服务手机的同时 STA 接口可扫描（/scan）；STA 不发起连接 */
        ESP_LOGE(TAG, "ap: set mode failed");
        net_state_set(PET_NET_OFF);
        return ESP_FAIL;
    }
    wifi_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    net_copy_str((char *)cfg.ap.ssid, sizeof cfg.ap.ssid, s_ap_ssid);
    cfg.ap.ssid_len = (uint8_t)strlen(s_ap_ssid);
    cfg.ap.channel = NET_AP_CHANNEL;
    cfg.ap.max_connection = NET_AP_MAX_CONN;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    net_copy_ssid(cfg.ap.password, sizeof cfg.ap.password, PET_NET_AP_PASSWORD);
    if (esp_wifi_set_config(WIFI_IF_AP, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "ap: set config failed");
        net_state_set(PET_NET_OFF);
        return ESP_FAIL;
    }
    if (net_ap_netif_setup() != ESP_OK) {
        ESP_LOGW(TAG, "ap: netif ip setup failed (继续用默认 192.168.4.1)");
    }
    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "ap: wifi start failed");
        net_state_set(PET_NET_OFF);
        return ESP_FAIL;
    }
    s_wifi_started = true;
    net_ssid_set(s_ap_ssid);
    net_ip_set(PET_NET_AP_IP);
    s_ap_fallback = fallback;
    s_ap_deadline_ms = fallback ? (int32_t)(now_ms32() + PET_NET_AP_FALLBACK_MS) : 0;
    s_sta_deadline_ms = 0;
    s_retry_pending = false;
    s_tcp_go = true; /* AP 也有 IP：TCP 8766 可用（调试/宿主直连 192.168.4.1） */
    if (prov_start() != ESP_OK) {
        ESP_LOGW(TAG, "ap: http unavailable (热点仍在，可稍后重试)");
    }
    ESP_LOGI(TAG, "ap: provisioning ssid=%s ip=%s%s", s_ap_ssid, PET_NET_AP_IP,
             fallback ? " (sta fallback, 120 s)" : "");
    return ESP_OK;
}

/* ---- WiFi/IP 事件：只更新状态 + 通知 net 任务（不在事件回调里阻塞/连接） ---- */

static void net_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disc =
            (const wifi_event_sta_disconnected_t *)data;
        const int reason = (disc != NULL) ? (int)disc->reason : -1;
        const pet_net_state_t st = net_state_get();
        if (st != PET_NET_CONNECTING && st != PET_NET_ONLINE) {
            return; /* 切 AP/停机/关机过程中的断开事件：忽略 */
        }
        static const uint32_t backoff_ms[] = {1000, 2000, 4000, 8000, 16000, 30000};
        uint32_t delay_ms;
        if (st == PET_NET_ONLINE) {
            /* 在线掉线：重新给 30 s 窗口，从 1 s 退避开始 */
            s_fail_count = 1;
            s_sta_deadline_ms = (int32_t)(now_ms32() + PET_NET_STA_TIMEOUT_MS);
            delay_ms = backoff_ms[0];
        } else {
            if (s_fail_count < 100) {
                s_fail_count++;
            }
            const size_t idx = (s_fail_count >= 1 && s_fail_count <= 6)
                                   ? (size_t)(s_fail_count - 1)
                                   : (sizeof backoff_ms / sizeof backoff_ms[0]) - 1;
            delay_ms = backoff_ms[idx];
        }
        net_ip_set("");
        net_rssi_set(0);
        net_state_set(PET_NET_CONNECTING);
        s_retry_at_ms = (int32_t)(now_ms32() + delay_ms);
        s_retry_pending = true;
        ESP_LOGW(TAG, "sta: disconnected reason=%d, retry in %u ms", reason,
                 (unsigned)delay_ms);
        if (s_net_task != NULL) {
            (void)xTaskNotify(s_net_task, NET_NOTIFY_EVT, eSetBits);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const pet_net_state_t st = net_state_get();
        if (st != PET_NET_CONNECTING && st != PET_NET_ONLINE) {
            return; /* 已切 AP/停机：迟到的 GOT_IP 忽略 */
        }
        const ip_event_got_ip_t *got = (const ip_event_got_ip_t *)data;
        if (got == NULL) {
            return;
        }
        char ip[16];
        (void)snprintf(ip, sizeof ip, IPSTR, IP2STR(&got->ip_info.ip));
        s_fail_count = 0;
        s_sta_deadline_ms = 0;
        s_retry_pending = false;
        s_ap_fallback = false;
        s_ap_deadline_ms = 0;
        net_ip_set(ip);
        net_refresh_rssi();
        s_rssi_refresh_ms = now_ms_signed();
        net_state_set(PET_NET_ONLINE);
        s_tcp_go = true;
        ESP_LOGI(TAG, "sta: connected ip=%s", ip);
        if (s_net_task != NULL) {
            (void)xTaskNotify(s_net_task, NET_NOTIFY_EVT, eSetBits);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "ap: started");
        s_tcp_go = true;
    }
}

/* ---- 底座（一次性）：netif + 默认事件循环 + WiFi 驱动 + 事件注册 + AP SSID ---- */

static esp_err_t net_base_init(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "netif init failed (%s)", esp_err_to_name(err));
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop init failed (%s)", esp_err_to_name(err));
        return err;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGE(TAG, "netif sta create failed");
        return ESP_FAIL;
    }
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        ESP_LOGE(TAG, "netif ap create failed");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi init failed (%s)", esp_err_to_name(err));
        return err;
    }
    (void)esp_wifi_set_storage(WIFI_STORAGE_RAM); /* 凭据只存本项目 NVS net 命名空间，不写 WiFi 内部存储 */

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              net_event_handler, NULL, &s_ev_wifi);
    if (err == ESP_OK) {
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  net_event_handler, NULL, &s_ev_ip);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "event handler register failed (%s)", esp_err_to_name(err));
        return err;
    }

    uint8_t mac[6] = {0};
    err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read mac failed (%s), AP ssid uses 0000", esp_err_to_name(err));
    }
    (void)snprintf(s_ap_ssid, sizeof s_ap_ssid, "%s%02X%02X", PET_NET_AP_SSID_PREFIX,
                   (unsigned)mac[4], (unsigned)mac[5]);
    return ESP_OK;
}

/* ---- TCP :8766 事件通道：单客户端（新连接顶替旧连接），字节流 → 行 → 去重 → 回调 ---- */

static bool tcp_wait_errno(int e)
{
    /* SO_RCVTIMEO / select 超时的瞬态错误 */
    return e == EAGAIN || e == EWOULDBLOCK || e == ETIMEDOUT || e == EINTR;
}

static void tcp_deliver_line(const char *line, size_t len)
{
    pet_net_line_cb_t cb = s_line_cb;
    if (cb == NULL) {
        ESP_LOGD(TAG, "tcp: no line callback, dropped len=%u", (unsigned)len);
        return;
    }
    cb(line, len); /* main 侧自行解析 + 入队；本组件不碰 pet_state */
}

static void tcp_task(void *arg)
{
    (void)arg;
    int lfd = -1;
    int cfd = -1;
    bool listening = false;
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);

    for (;;) {
        if (!s_tcp_go) {
            vTaskDelay(pdMS_TO_TICKS(NET_TCP_ACCEPT_TICK_MS));
            continue;
        }
        if (!listening) {
            lfd = lwip_socket(AF_INET, SOCK_STREAM, 0);
            if (lfd < 0) {
                ESP_LOGE(TAG, "tcp: socket failed errno=%d", errno);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            const int on = 1;
            (void)lwip_setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof addr);
            addr.sin_family = AF_INET;
            addr.sin_port = lwip_htons((uint16_t)PET_NET_TCP_PORT);
            addr.sin_addr.s_addr = lwip_htonl(INADDR_ANY);
            if (lwip_bind(lfd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
                lwip_listen(lfd, 1) != 0) {
                ESP_LOGE(TAG, "tcp: bind/listen failed errno=%d", errno);
                (void)lwip_close(lfd);
                lfd = -1;
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            listening = true;
            ESP_LOGI(TAG, "tcp: event channel listening on :%d", PET_NET_TCP_PORT);
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(lfd, &rfds);
        int maxfd = lfd;
        if (cfd >= 0) {
            FD_SET(cfd, &rfds);
            if (cfd > maxfd) {
                maxfd = cfd;
            }
        }
        struct timeval tv = {.tv_sec = 0, .tv_usec = NET_TCP_ACCEPT_TICK_MS * 1000};
        const int rc = lwip_select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (!tcp_wait_errno(errno)) {
                ESP_LOGW(TAG, "tcp: select failed errno=%d", errno);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            continue;
        }
        if (rc == 0) {
            continue;
        }

        if (FD_ISSET(lfd, &rfds)) {
            const int nfd = lwip_accept(lfd, NULL, NULL);
            if (nfd >= 0) {
                if (cfd >= 0) {
                    (void)lwip_close(cfd);
                    ESP_LOGI(TAG, "tcp: previous client replaced");
                }
                cfd = nfd;
                const int rcv_ms = 2000;
                (void)lwip_setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &rcv_ms,
                                      sizeof rcv_ms);
                pet_linebuf_init(&lb);
                ESP_LOGI(TAG, "tcp: client connected");
            }
            /* 本轮不处理客户端：避免 accept 复用同一 fd 号导致 fd_set 误判 */
            continue;
        }

        if (cfd >= 0 && FD_ISSET(cfd, &rfds)) {
            uint8_t chunk[NET_TCP_CHUNK];
            const int n = lwip_recv(cfd, chunk, sizeof chunk, 0);
            if (n == 0) {
                (void)lwip_close(cfd);
                cfd = -1;
                ESP_LOGI(TAG, "tcp: client disconnected");
            } else if (n < 0) {
                if (!tcp_wait_errno(errno)) {
                    ESP_LOGW(TAG, "tcp: recv failed errno=%d", errno);
                    (void)lwip_close(cfd);
                    cfd = -1;
                }
            } else {
                for (int i = 0; i < n; i++) {
                    const pet_line_feed_t r = pet_linebuf_feed(&lb, (char)chunk[i]);
                    if (r == PET_LINE_READY) {
                        if (lb.len == 0) {
                            continue; /* 空行无意义（C05 只处理 JSON 行） */
                        }
                        if (pet_net_line_accept(now_ms32(), lb.buf, lb.len)) {
                            tcp_deliver_line(lb.buf, lb.len);
                        }
                    } else if (r == PET_LINE_DROPPED) {
                        ESP_LOGW(TAG, "tcp: line too long (dropped)");
                    }
                }
            }
        }
    }
}

/* ---- net 任务：退避重连 / 30 s 超时回落 AP / AP 120 s 后回 STA / SNTP / RSSI ---- */

static void net_task(void *arg)
{
    (void)arg;
    for (;;) {
        const uint32_t bits = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(NET_TICK_MS));
        if ((bits & NET_NOTIFY_PROV) != 0u && net_state_get() != PET_NET_AP) {
            ESP_LOGI(TAG, "provisioning requested");
            (void)net_ap_start(false);
        }

        const pet_net_state_t st = net_state_get();
        const int32_t now = now_ms_signed();
        if (st == PET_NET_ONLINE) {
            sntp_ensure_started();
            if ((int32_t)(now - s_rssi_refresh_ms) >= (int32_t)NET_RSSI_REFRESH_MS) {
                net_refresh_rssi();
                s_rssi_refresh_ms = now;
            }
        } else if (st == PET_NET_CONNECTING) {
            if (s_sta_deadline_ms != 0 && (int32_t)(now - s_sta_deadline_ms) >= 0) {
                ESP_LOGW(TAG, "sta: no ip within %u ms: fallback to AP",
                         (unsigned)PET_NET_STA_TIMEOUT_MS);
                (void)net_ap_start(true);
            } else if (s_retry_pending && (int32_t)(now - s_retry_at_ms) >= 0) {
                s_retry_pending = false;
                ESP_LOGI(TAG, "sta: retry connect");
                if (esp_wifi_connect() != ESP_OK) {
                    ESP_LOGW(TAG, "sta: retry failed, schedule again");
                    s_retry_at_ms = (int32_t)(now + 1000);
                    s_retry_pending = true;
                }
            }
        } else if (st == PET_NET_AP) {
            if (s_ap_fallback && s_ap_deadline_ms != 0 &&
                (int32_t)(now - s_ap_deadline_ms) >= 0) {
                ESP_LOGI(TAG, "ap: fallback window over: retry STA");
                (void)net_sta_start();
            }
        }
    }
}

/* ---- 公开 API（C08 §4） ---- */

const char *pet_net_state_name(pet_net_state_t state)
{
    switch (state) {
    case PET_NET_AP:
        return "ap";
    case PET_NET_CONNECTING:
        return "connecting";
    case PET_NET_ONLINE:
        return "online";
    case PET_NET_OFF:
    default:
        return "off";
    }
}

void pet_net_set_line_cb(pet_net_line_cb_t cb)
{
    s_line_cb = cb;
}

bool pet_net_line_accept(uint32_t now_ms, const char *line, size_t len)
{
    if (line == NULL) {
        return false;
    }
    if (s_dedupe_lock == NULL) {
        return true; /* 未启动/启动失败：不去重，避免丢事件 */
    }
    (void)xSemaphoreTake(s_dedupe_lock, portMAX_DELAY);
    const pet_net_dedupe_result_t r = pet_net_dedupe_feed(&s_dedupe, now_ms, line, len);
    (void)xSemaphoreGive(s_dedupe_lock);
    if (r == PET_NET_DEDUPE_DROP) {
        /* C08 §1.1：丢弃并记日志（USB/WiFi 同一门；内容不入日志） */
        ESP_LOGI(TAG, "dedupe: duplicate line dropped (len=%u)", (unsigned)len);
        return false;
    }
    return true;
}

void pet_net_get_status(pet_net_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    out->state = PET_NET_OFF;
    out->rssi = 0;
    net_lock();
    out->state = s_status.state;
    net_copy_str(out->ssid, sizeof out->ssid, s_status.ssid);
    net_copy_str(out->ip, sizeof out->ip, s_status.ip);
    out->rssi = s_status.rssi;
    out->time_valid = s_status.time_valid;
    net_unlock();
}

void pet_net_enter_provisioning(void)
{
    if (s_net_task == NULL) {
        ESP_LOGW(TAG, "provisioning ignored: net task not running");
        return;
    }
    (void)xTaskNotify(s_net_task, NET_NOTIFY_PROV, eSetBits);
}

esp_err_t pet_net_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_started = true;

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    if (s_dedupe_lock == NULL) {
        s_dedupe_lock = xSemaphoreCreateMutex();
    }
    if (s_lock == NULL || s_dedupe_lock == NULL) {
        ESP_LOGE(TAG, "mutex alloc failed");
        net_state_set(PET_NET_OFF);
        s_started = false;
        return ESP_ERR_NO_MEM;
    }
    pet_net_dedupe_init(&s_dedupe);

    /* NVS：main 已初始化时幂等；失败 → 记日志、状态 OFF、返回错误（C08 §4） */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs init retry after erase (%s)", esp_err_to_name(err));
        (void)nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed (%s): net OFF", esp_err_to_name(err));
        net_state_set(PET_NET_OFF);
        s_started = false;
        return err;
    }

    err = net_base_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi base init failed: net OFF");
        net_state_set(PET_NET_OFF);
        s_started = false;
        return err;
    }

    char ssid[NET_SSID_MAX] = {0};
    char pass[NET_PASS_MAX] = {0};
    const bool have_creds = net_cred_load(ssid, sizeof ssid, pass, sizeof pass);
    if (have_creds) {
        net_copy_str(s_ssid, sizeof s_ssid, ssid);
        net_copy_str(s_pass, sizeof s_pass, pass);
        ESP_LOGI(TAG, "credentials found: ssid=%s", s_ssid);
    } else {
        ESP_LOGW(TAG, "no wifi credentials: provisioning AP at boot");
    }

    /* TCP 事件通道任务先建（等首次 IP 后监听；建失败只影响 WiFi 事件入口） */
    if (xTaskCreate(tcp_task, "pet-tcp", NET_TCP_TASK_STACK, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "tcp: task create failed (wifi event channel degraded)");
    }

    err = have_creds ? net_sta_start() : net_ap_start(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "initial wifi start failed: net OFF");
        s_started = false;
        return err;
    }

    if (xTaskCreate(net_task, "pet-net", NET_TASK_STACK, NULL, 3, &s_net_task) != pdPASS) {
        ESP_LOGE(TAG, "net: task create failed");
        s_net_task = NULL;
        s_started = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "net started: state=%s", pet_net_state_name(net_state_get()));
    return ESP_OK;
}