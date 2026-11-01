/* M5b-2：BLE GATT 事件通道（契约 docs/contracts/C09-ble-channel.md，2026-11-01 冻结）。
 *
 * 组成（NimBLE peripheral）：
 *   1. GATT server：Nordic UART Service
 *        Service 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 *        RX      6E400002-...  Write / WriteNoRsp（host → device，C05 NDJSON）
 *        TX      6E400003-...  Notify（device → host；v1 只注册，不主动发数据）
 *      v1 无配对/无加密（C09 §1）。
 *   2. 广播：名 `ESP32-Touch-<MAC 后 2 字节 hex>`（C09 §1），scan response 带 128 bit NUS
 *      Service UUID 便于手机/PC 识别；单连接，断开后立即恢复广播。
 *   3. 每连接一个 pet_linebuf（pet_ble_lines 纯逻辑，C09 §1.1）：GATT write 字节流
 *      （可跨多次 write、单次 write 可含 0..N 行、CRLF、≤512 B、超长整行丢弃）→ 完整行
 *      → 跨通道去重门 pet_net_line_accept（C08 §1.1，USB/WiFi/BLE 共用 1 s 指纹环）
 *      → pet_ble_line_cb_t（在 NimBLE host 任务上下文，**不可阻塞**）。
 *   4. 状态：pet_ble_get_status()（state/addr/mtu/lines_total）；日志 TAG=pet-ble。
 *
 * 失败降级（C09 §2）：NVS / NimBLE 初始化 / GATT 注册失败 → 记日志、状态 OFF、
 * pet_ble_start() 返回错误；WiFi/显示/按键不受影响（不阻塞其它功能）。
 *
 * main 接线：见 pet_ble.h 顶部（set_line_cb → start；USB 路径解析前调用同一去重门）。
 *
 * 参考 IDF 5.5.4 例程：examples/bluetooth/ble_get_started/nimble/NimBLE_GATT_Server
 * （gap.c / gatt_svc.c 的 NimBLE API 用法）与 examples/bluetooth/nimble/bleprph；
 * 按 C09 重写为本项目组件，未逐字节复制。
 */
#include "pet_ble.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "pet_ble_lines.h"
#include "pet_net.h"

static const char *TAG = "pet-ble";

#define BLE_NAME_MAX 32      /* "ESP32-Touch-XXXX" = 16 B + NUL，留余量 */
#define BLE_RX_CHUNK 128u    /* GATT write 净荷按块拷贝（避免 host 任务大栈帧） */

/* NimBLE store/config 初始化（例程同为手动声明；bt 组件内实现，v1 无绑定仅建 RAM store）。 */
void ble_store_config_init(void);

/* ---- NUS UUID（128 bit；BLE_UUID128_INIT 参数为 UUID 字符串的小端字节序） ---- */
static ble_uuid128_t s_nus_svc_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e); /* 6E400001-B5A3-F393-E0A9-E50E24DCCA9E */
static ble_uuid128_t s_nus_rx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e); /* 6E400002-... */
static ble_uuid128_t s_nus_tx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e); /* 6E400003-... */

static uint16_t s_rx_handle;
static uint16_t s_tx_handle;

/* ---- 共享状态（get_status 可从任意任务读；锁保护） ---- */
static SemaphoreHandle_t s_lock;
static pet_ble_status_t s_status = {
    .state = PET_BLE_OFF,
    .addr = {0},
    .mtu = 0,
    .lines_total = 0,
};
static pet_ble_line_cb_t s_line_cb;

/* ---- 运行状态 ---- */
static bool s_started;            /* pet_ble_start 成功过（未 stop） */
static uint8_t s_own_addr_type;   /* 广播用本机地址类型（host sync 后确定） */
static char s_name[BLE_NAME_MAX]; /* 广播名 */
/* 每连接行缓冲：只在 NimBLE host 任务（GAP/GATT 回调）里访问，无需加锁 */
static pet_ble_lines_t s_lines;

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg);
static void ble_on_reset(int reason);
static void ble_on_sync(void);
static void ble_host_task(void *param);
static void ble_advertise(void);

/* ---- 时间 / 小工具 ---- */

static uint32_t ble_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* 有界格式化对端地址 "AA:BB:CC:DD:EE:FF"（addr 可为 NULL → 空串）。 */
static void ble_format_addr(char *out, size_t out_sz, const uint8_t *addr)
{
    if (out == NULL || out_sz == 0) {
        return;
    }
    if (addr == NULL) {
        out[0] = '\0';
        return;
    }
    (void)snprintf(out, out_sz, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1],
                   addr[2], addr[3], addr[4], addr[5]);
}

/* ---- 状态访问（锁保护；只在任务上下文调用，不在中断） ---- */

static void status_lock(void)
{
    if (s_lock != NULL) {
        (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void status_unlock(void)
{
    if (s_lock != NULL) {
        (void)xSemaphoreGive(s_lock);
    }
}

static void status_reset(void)
{
    status_lock();
    memset(&s_status, 0, sizeof s_status);
    s_status.state = PET_BLE_OFF;
    status_unlock();
}

static void status_set_state(pet_ble_state_t state)
{
    status_lock();
    s_status.state = state;
    status_unlock();
}

static void status_set_connected(const char *addr, int mtu)
{
    status_lock();
    s_status.state = PET_BLE_CONNECTED;
    if (addr != NULL) {
        (void)snprintf(s_status.addr, sizeof s_status.addr, "%s", addr);
    } else {
        s_status.addr[0] = '\0';
    }
    s_status.mtu = mtu;
    status_unlock();
}

static void status_clear_connection(void)
{
    status_lock();
    s_status.addr[0] = '\0';
    s_status.mtu = 0;
    status_unlock();
}

static void status_set_mtu(int mtu)
{
    status_lock();
    s_status.mtu = mtu;
    status_unlock();
}

static void status_set_lines(uint32_t total)
{
    status_lock();
    s_status.lines_total = total;
    status_unlock();
}

/* ---- 行交付：跨通道去重（C08 §1.1）→ main 回调（C09 §1.1） ---- */

static void ble_deliver_line(const char *line, size_t len, void *user)
{
    (void)user;
    /* 与 USB/WiFi 共用 1 s 指纹环：重复行在这里丢弃（内部记日志） */
    if (!pet_net_line_accept(ble_now_ms(), line, len)) {
        return;
    }
    const pet_ble_line_cb_t cb = s_line_cb;
    if (cb == NULL) {
        ESP_LOGD(TAG, "rx: no line callback, dropped len=%u", (unsigned)len);
        return;
    }
    cb(line, len); /* main 侧自行解析 + 入队；本组件不碰 pet_state */
}

/* ---- GATT RX 特征写回调（NimBLE host 任务上下文） ---- */

static int ble_rx_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt == NULL || ctxt->om == NULL ||
        ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        ESP_LOGW(TAG, "rx: unsupported access op=%d",
                 (ctxt != NULL) ? (int)ctxt->op : -1);
        return BLE_ATT_ERR_UNLIKELY;
    }

    const uint16_t total = (uint16_t)OS_MBUF_PKTLEN(ctxt->om);
    static uint8_t chunk[BLE_RX_CHUNK]; /* 单连接 + host 任务串行，静态复用避免大栈帧 */
    const uint32_t dropped_before = s_lines.lines_dropped;

    for (uint32_t off = 0; off < total; off += BLE_RX_CHUNK) {
        uint32_t n = (uint32_t)total - off;
        if (n > BLE_RX_CHUNK) {
            n = BLE_RX_CHUNK;
        }
        if (os_mbuf_copydata(ctxt->om, (int)off, (int)n, chunk) != 0) {
            ESP_LOGW(TAG, "rx: mbuf copy failed at off=%u", (unsigned)off);
            break;
        }
        (void)pet_ble_lines_feed(&s_lines, chunk, (size_t)n, ble_deliver_line,
                                 NULL);
    }

    if (s_lines.lines_dropped != dropped_before) {
        ESP_LOGW(TAG, "rx: line too long (>%d B, dropped) total=%u",
                 (int)PET_LINE_MAX, (unsigned)s_lines.lines_dropped);
    }
    status_set_lines(s_lines.lines_total);
    return 0;
}

/* ---- GATT TX 特征（v1 只 Notify，不主动发数据；读写均不支持） ---- */

static int ble_tx_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;
    return BLE_ATT_ERR_UNLIKELY;
}

/* ---- GATT 服务表（Nordic UART Service，C09 §1） ---- */

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                /* host → device：C05 NDJSON（可跨多次 write 分片） */
                .uuid = &s_nus_rx_uuid.u,
                .access_cb = ble_rx_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &s_rx_handle,
            },
            {
                /* device → host：Notify（v1 可选、默认不主动发） */
                .uuid = &s_nus_tx_uuid.u,
                .access_cb = ble_tx_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_tx_handle,
            },
            {
                0, /* 该服务无更多特征 */
            },
        },
    },
    {
        0, /* 无更多服务 */
    },
};

/* ---- 广播（C09 §1：名 ESP32-Touch-<4hex>；断开后继续广播） ---- */

static void ble_advertise(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp;
    struct ble_gap_adv_params params;
    int rc;

    if (!s_started) {
        return; /* 已 stop：迟到的 sync/adv-complete 事件不再重开广播 */
    }

    memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)s_name;
    fields.name_len = (uint8_t)strlen(s_name);
    fields.name_is_complete = 1;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv: set fields failed rc=%d", rc);
        return;
    }

    /* scan response 带 NUS Service UUID（16 B，主广播包放不下，C09 只要求名字） */
    memset(&rsp, 0, sizeof rsp);
    rsp.uuids128 = &s_nus_svc_uuid;
    rsp.num_uuids128 = 1;
    rsp.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGW(TAG, "adv: scan rsp set failed rc=%d (继续广播)", rc);
    }

    memset(&params, 0, sizeof params);
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params,
                           ble_gap_event_cb, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv: start failed rc=%d", rc);
        return;
    }
    status_set_state(PET_BLE_ADVERTISING);
    ESP_LOGI(TAG, "advertising: name=%s svc=%s", s_name, PET_BLE_NUS_SVC_UUID);
}

/* ---- GAP 事件（NimBLE host 任务上下文） ---- */

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            struct ble_gap_conn_desc desc;
            char addr[PET_BLE_ADDR_STR_LEN] = {0};
            if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                ble_format_addr(addr, sizeof addr, desc.peer_id_addr.val);
            }
            /* 每连接一个行缓冲（C09 §1.1）；单连接：连接期间广播已停 */
            pet_ble_lines_init(&s_lines);
            const int mtu = (int)ble_att_mtu(event->connect.conn_handle);
            status_set_connected(addr, mtu);
            ESP_LOGI(TAG, "connected: peer=%s mtu=%d", addr, mtu);
        } else {
            ESP_LOGW(TAG, "connect failed: status=%d",
                     event->connect.status);
            ble_advertise(); /* 连接失败 → 继续广播 */
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected: reason=%d", event->disconnect.reason);
        pet_ble_lines_init(&s_lines); /* 半行不跨连接 */
        status_clear_connection();
        ble_advertise(); /* C09 §1：断开后继续广播 */
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "advertising complete: reason=%d",
                 event->adv_complete.reason);
        ble_advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "connection updated: status=%d",
                 event->conn_update.status);
        status_set_mtu((int)ble_att_mtu(event->conn_update.conn_handle));
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu updated: conn=%d cid=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.channel_id,
                 event->mtu.value);
        status_set_mtu((int)event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        /* TX 特征 Notify 订阅状态（v1 不主动发数据，仅记录） */
        if (event->subscribe.attr_handle == s_tx_handle) {
            ESP_LOGI(TAG, "tx notify %s",
                     (event->subscribe.cur_notify != 0) ? "subscribed"
                                                        : "unsubscribed");
        }
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        if (event->notify_tx.status != 0 &&
            event->notify_tx.status != BLE_HS_EDONE) {
            ESP_LOGW(TAG, "notify_tx status=%d", event->notify_tx.status);
        }
        return 0;

    default:
        return 0;
    }
}

/* ---- NimBLE host 生命周期 ---- */

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "nimble host reset: reason=%d", reason);
    status_set_state(PET_BLE_OFF);
}

static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "no available BT address (rc=%d)", rc);
        return;
    }
    /* 无隐私：用公有/身份地址广播 */
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer own address type failed rc=%d", rc);
        return;
    }
    uint8_t addr_val[6] = {0};
    char addr_str[PET_BLE_ADDR_STR_LEN] = {0};
    if (ble_hs_id_copy_addr(s_own_addr_type, addr_val, NULL) == 0) {
        ble_format_addr(addr_str, sizeof addr_str, addr_val);
    }
    ESP_LOGI(TAG, "host synced: own address=%s", addr_str);
    ble_advertise();
}

static void ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "host task started");
    nimble_port_run(); /* 仅 nimble_port_stop() 后返回 */

    nimble_port_freertos_deinit(); /* 删除 host 任务（本函数返回后任务已不存在） */
}

/* ---- GATT 注册（NUS）；失败返回错误供 start 降级 ---- */

static esp_err_t ble_gatt_register(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt count cfg failed rc=%d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt add svcs failed rc=%d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ---- 公开 API（C09 §2） ---- */

const char *pet_ble_state_name(pet_ble_state_t state)
{
    switch (state) {
    case PET_BLE_ADVERTISING:
        return "advertising";
    case PET_BLE_CONNECTED:
        return "connected";
    case PET_BLE_OFF:
    default:
        return "off";
    }
}

void pet_ble_set_line_cb(pet_ble_line_cb_t cb)
{
    s_line_cb = cb;
}

void pet_ble_get_status(pet_ble_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    out->state = PET_BLE_OFF;
    status_lock();
    *out = s_status;
    status_unlock();
}

esp_err_t pet_ble_start(void)
{
    if (s_started) {
        return ESP_OK; /* 幂等（C09 §2） */
    }
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            ESP_LOGE(TAG, "mutex alloc failed: BLE OFF");
            return ESP_ERR_NO_MEM;
        }
    }
    status_reset();
    pet_ble_lines_init(&s_lines);

    /* NimBLE 需要 NVS（PHY 校准/绑定信息；v1 无绑定但协议栈初始化仍会用到）。 */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs init retry after erase (%s)", esp_err_to_name(err));
        (void)nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed (%s): BLE OFF", esp_err_to_name(err));
        return err;
    }

    /* 协议栈 + 控制器（esp_nimble_hci 由 nimble_port_init 内部完成） */
    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble init failed (%s): BLE OFF", esp_err_to_name(err));
        status_set_state(PET_BLE_OFF);
        return err;
    }

    err = ble_gatt_register();
    if (err != ESP_OK) {
        (void)nimble_port_deinit();
        status_set_state(PET_BLE_OFF);
        ESP_LOGE(TAG, "gatt register failed: BLE OFF");
        return err;
    }

    /* host 配置：v1 无配对/无加密（C09 §1），不做 sm_* 设置 */
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();

    /* 广播名 = ESP32-Touch-<MAC 后 2 字节 hex>（C09 §1） */
    uint8_t mac[6] = {0};
    err = esp_read_mac(mac, ESP_MAC_BT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read BT mac failed (%s): name uses 0000",
                 esp_err_to_name(err));
    }
    (void)snprintf(s_name, sizeof s_name, "%s%02X%02X", PET_BLE_NAME_PREFIX,
                   (unsigned)mac[4], (unsigned)mac[5]);
    const int rc = ble_svc_gap_device_name_set(s_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "gap device name set failed rc=%d (继续)", rc);
    }

    s_started = true;
    status_set_state(PET_BLE_ADVERTISING); /* host sync 后真正开播（ms 级） */
    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "started: name=%s", s_name);
    return ESP_OK;
}

void pet_ble_stop(void)
{
    if (!s_started) {
        status_reset();
        return;
    }
    ESP_LOGI(TAG, "stopping");
    s_started = false; /* 先清标志：adv_complete/sync 迟到的回调不再重开广播 */

    const int rc = nimble_port_stop();
    if (rc != 0) {
        ESP_LOGW(TAG, "nimble port stop rc=%d", rc);
    }
    const esp_err_t err = nimble_port_deinit();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nimble port deinit failed (%s)", esp_err_to_name(err));
    }

    pet_ble_lines_init(&s_lines);
    status_reset();
    ESP_LOGI(TAG, "stopped");
}
