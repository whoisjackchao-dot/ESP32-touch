#pragma once

/* M5b-1：WiFi 事件通道（TCP :8766）+ AP 配网 + SNTP 校时 + 跨通道去重。
 *
 * 契约：docs/contracts/C08-wifi-channel.md（2026-10-31 冻结）。
 * 事件协议不变：C05（NDJSON 一行一个 JSON 对象，≤512 B，`\r` 忽略）；本组件只新增传输。
 *
 * 职责边界：
 *   - 本组件**不解析** C05、不碰 pet_state；TCP 收到的行经跨通道去重后通过
 *     pet_net_line_cb_t 回调交给 main，由 main 走与 USB 相同的 pet_link_parse → 事件队列路径。
 *   - 回调在内部 TCP 任务上下文执行（不是 app_main、不是中断），**不要阻塞**：只做解析/入队。
 *   - 跨通道去重（C08 §1.1）由本组件的单一去重器实现：WiFi 路径内部已调用
 *     pet_net_line_accept()；**main 的 USB 路径也应在解析前调用同一函数**，否则 USB/WiFi
 *     1 s 内的重复行会被处理两次。
 *
 * main 接线建议（本组件不改 main.c）：
 *   1. 开机（NVS init 之后）：pet_net_set_line_cb(net_line_cb); pet_net_start();
 *      net_line_cb 内：pet_link_msg_t m; if (pet_link_parse(line, len, &m) == PET_LINK_OK)
 *      xQueueSend(s_evt_q, &m, 0);（现有 main 的 s_evt_q 路径）
 *   2. console_task 收到 `{` 行、pet_linebuf READY 后：
 *      if (pet_net_line_accept(now_ms_u32(), lb.buf, lb.len)) { 解析并入队 }
 *   3. 菜单 PET_MENU_CMD_PROVISION 与串口命令 `W` → pet_net_enter_provisioning()
 *   4. 设备状态显示/自检可用 pet_net_get_status()（state/ssid/ip/rssi/time_valid）。
 *
 * 失败降级（C08 §4）：NVS / WiFi 驱动失败 → 记日志、状态 OFF、pet_net_start() 返回错误；
 * HTTP / TCP / SNTP 失败只记日志，不阻塞显示与按键。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define PET_NET_TCP_PORT 8766
#define PET_NET_AP_SSID_PREFIX "ESP32-Touch-"
#define PET_NET_AP_PASSWORD "touch154" /* v1 固定口令（README 明示；局域网可信） */
#define PET_NET_AP_IP "192.168.4.1"
#define PET_NET_STA_TIMEOUT_MS 30000u   /* 有凭据但 30 s 未拿到 IP → 回落 AP */
#define PET_NET_AP_FALLBACK_MS 120000u  /* 回落 AP 120 s 后重试 STA */

typedef enum {
    PET_NET_OFF = 0,   /* WiFi 未运行（初始化失败/瞬态） */
    PET_NET_AP,        /* 配网热点运行中 */
    PET_NET_CONNECTING,/* STA 连接中（含断线重连退避） */
    PET_NET_ONLINE,    /* STA 已获得 IP */
} pet_net_state_t;

typedef struct {
    pet_net_state_t state;
    char ssid[33]; /* STA=目标 SSID；AP=热点 SSID */
    char ip[16];   /* STA=DHCP 地址；AP=192.168.4.1；OFF/CONNECTING 为空串 */
    int rssi;      /* dBm；无连接为 0 */
    bool time_valid; /* SNTP 同步成功过 */
} pet_net_status_t;

/* 启动网络（读 NVS `net/ssid`,`net/pass`；无凭据→AP；有→STA；创建 TCP/HTTP/SNTP）。
 * 非阻塞式：返回时初始模式已开始（STA 连接中/AP 热点已起），不等待联网完成。
 * 幂等：已启动时直接返回 ESP_OK。 */
esp_err_t pet_net_start(void);

/* 进入 AP 配网（菜单 WIFI / 串口命令 W；在线时断开 STA 转热点）。 */
void pet_net_enter_provisioning(void);

/* 读取状态快照（线程安全；任何任务/上下文可调用）。 */
void pet_net_get_status(pet_net_status_t *out);

/* 已去重的 C05 行回调；在内部 TCP 任务上下文调用。 */
typedef void (*pet_net_line_cb_t)(const char *line, size_t len);
void pet_net_set_line_cb(pet_net_line_cb_t cb);

/* 跨通道去重门（C08 §1.1）：USB 路径与 WiFi 路径共用一个去重器。
 * now_ms 用与设备一致的单调毫秒（main 的 now_ms_u32()）；返回 true=应处理，false=1 s 内重复。
 * 重复时内部记日志。未启动/启动失败时返回 true（不去重，避免丢事件）。 */
bool pet_net_line_accept(uint32_t now_ms, const char *line, size_t len);

/* 状态名（日志/自检用）："off"/"ap"/"connecting"/"online"。 */
const char *pet_net_state_name(pet_net_state_t state);
