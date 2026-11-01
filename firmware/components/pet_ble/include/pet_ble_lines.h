#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pet_linebuf.h"

/* M5b-2：BLE GATT write 字节流 → C05 NDJSON 行的重装纯逻辑（host 可测）。
 *
 * 契约：C09 §1.1（每连接一个 pet_linebuf，≤512 B，剥 `\r`，半行跨 write 缓存，
 * 超长行丢弃并记日志；一次 write 可含 0..N 行）。
 * 复用 firmware/components/pet_ui/pet_linebuf.h（**不改动其实现**）：行长上限、
 * CRLF 剥除、超长整行丢弃的语义全部来自 pet_linebuf。
 *
 * 本模块只做"字节 → 行"与计数，不碰硬件/NimBLE/去重；交付回调由 pet_ble.c 传入
 * （回调内做跨通道去重 + main 回调），host 测试用收集器回调。
 */

typedef void (*pet_ble_line_deliver_cb_t)(const char *line, size_t len, void *user);

typedef struct {
    pet_linebuf_t lb;        /* 当前连接的行缓冲（每次连接/断开时重装） */
    uint32_t lines_total;    /* 已交付的完整非空行数 */
    uint32_t lines_dropped;  /* 超长（>512 B）被整行丢弃的行数 */
} pet_ble_lines_t;

/* 重置为"新连接"初始状态（清零计数、重装行缓冲）。ls 可为 NULL（安全空操作）。 */
void pet_ble_lines_init(pet_ble_lines_t *ls);

/* 喂一段 write 字节；每凑齐一条非空行同步调用 cb（可为 NULL，仅计数/丢弃）。
 * 返回本次交付的行数。cb 在调用者上下文（设备上是 NimBLE host 任务）执行。
 * 防御：ls==NULL 或 (data==NULL && len>0) → 返回 0，不崩溃。 */
uint32_t pet_ble_lines_feed(pet_ble_lines_t *ls, const uint8_t *data, size_t len,
                            pet_ble_line_deliver_cb_t cb, void *user);
