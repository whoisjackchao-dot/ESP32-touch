#pragma once

/* M5b-2：BLE GATT 事件通道（NimBLE peripheral + Nordic UART Service）。
 *
 * 契约：docs/contracts/C09-ble-channel.md（2026-11-01 冻结）；事件协议复用 C05
 * （UTF-8 NDJSON，一行一个 JSON 对象，行长 ≤512 B，`\n` 结尾，`\r` 忽略）。
 * 跨通道去重沿用 C08 §1.1：USB / WiFi / BLE 共用一个 1 s 指纹环（pet_net_line_accept）。
 *
 * 职责边界：
 *   - 本组件**不解析** C05、不碰 pet_state；BLE 收到的行按连接重装成行 → 跨通道去重门
 *     → pet_ble_line_cb_t 交给 main，由 main 走与 USB/WiFi 相同的 pet_link_parse → 事件队列路径。
 *   - 回调在 NimBLE host 任务上下文执行（不是 app_main、不是中断），**不可阻塞**：
 *     只做解析/入队（同 C08 WiFi 回调约束）。
 *   - 单连接（C09 §1）：连接期间不广播；断开后立即恢复广播。新连接只能在旧连接断开后建立。
 *   - v1 无配对/无加密（C09 §1）；TX 特征 Notify 仅注册、默认不主动发数据。
 *
 * main 接线建议（本组件不改 main.c）：
 *   1. 开机（NVS init 之后）：pet_ble_set_line_cb(ble_line_cb); pet_ble_start();
 *      ble_line_cb 内：pet_link_msg_t m; if (pet_link_parse(line, len, &m) == PET_LINK_OK)
 *      xQueueSend(s_evt_q, &m, 0);（与 pet_net 的 net_line_cb 同一路径）
 *   2. USB 路径（console_task）在解析前调用 pet_net_line_accept(now_ms_u32(), line, len)，
 *      与 BLE/WiFi 共用去重门（pet_ble 内部已对本通道调用该门）。
 *   3. 自检/调试可用 pet_ble_get_status()（state/addr/mtu/lines_total）。
 *   4. pet_ble_stop() 仅测试用，**不可在 NimBLE 回调（host 任务）内调用**。
 *
 * 失败降级（C09 §2）：NimBLE 初始化/GATT 注册失败 → 记日志、状态 OFF、pet_ble_start()
 * 返回错误；WiFi/显示/按键不受影响（不阻塞其它功能）。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* Nordic UART Service（C09 §1，UUID 字符串形式；实现内用 128 bit 小端字节序） */
#define PET_BLE_NUS_SVC_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define PET_BLE_NUS_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" /* Write / WriteNoRsp */
#define PET_BLE_NUS_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" /* Notify（v1 仅注册） */

#define PET_BLE_NAME_PREFIX "ESP32-Touch-" /* 广播名 = 前缀 + MAC 后 2 字节 hex（C09 §1） */
#define PET_BLE_ADDR_STR_LEN 18            /* "AA:BB:CC:DD:EE:FF" + NUL */
#define PET_BLE_MTU_DEFAULT 23             /* BLE 默认 ATT MTU（协商前） */

typedef enum {
    PET_BLE_OFF = 0,    /* 未启动 / 启动失败 / 已停止 */
    PET_BLE_ADVERTISING,/* 已启动，广播中（未连接） */
    PET_BLE_CONNECTED,  /* 已连接（单连接） */
} pet_ble_state_t;

typedef struct {
    pet_ble_state_t state;
    char addr[PET_BLE_ADDR_STR_LEN]; /* 连接时对端 MAC 字符串；否则空串 */
    int mtu;                         /* 协商后的 ATT MTU；未连接/未启动为 0 */
    uint32_t lines_total;            /* 已接收完整非空行数（去重前；调试） */
} pet_ble_status_t;

/* 启动 BLE：初始化 NimBLE、注册 GATT（NUS）、主机同步后开始广播。
 * 非阻塞式：返回时协议栈已启动，广播在 host sync 后自动开始（毫秒级）。
 * 幂等：已启动时直接返回 ESP_OK。失败：记日志、状态 OFF、返回错误（C09 §2）。 */
esp_err_t pet_ble_start(void);

/* 停止 BLE（停广播/断开；测试用）。不在 NimBLE 回调内调用（会和 host 任务互相等待）。
 * 未启动时安全返回。 */
void pet_ble_stop(void);

/* 读取状态快照（线程安全；任何任务/上下文可调用）。 */
void pet_ble_get_status(pet_ble_status_t *out);

/* 已去重的 C05 行回调；在 NimBLE host 任务上下文调用（不可阻塞）。 */
typedef void (*pet_ble_line_cb_t)(const char *line, size_t len);
void pet_ble_set_line_cb(pet_ble_line_cb_t cb);

/* 状态名（日志/自检用）："off"/"advertising"/"connected"。 */
const char *pet_ble_state_name(pet_ble_state_t state);
