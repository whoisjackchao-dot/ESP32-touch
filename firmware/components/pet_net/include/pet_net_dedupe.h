#pragma once

/* M5b-1：跨通道去重的**纯逻辑**（C08 §1.1；host 可测，不碰 WiFi/时钟/日志）。
 *
 * 语义（契约 docs/contracts/C08-wifi-channel.md §1.1，2026-10-31 冻结）：
 *   - 设备维护最近 **16 条**不同原始行（长度 + FNV-1a 64 指纹）及**最近一次接收时间**；
 *   - 同一行（长度与指纹都相同）在 **1000 ms 内**再次出现 → DROP（并记日志，由调用方）；
 *   - 距上次记录 **>= 1000 ms** → 不再视为重复，ACCEPT 并刷新该槽时间；
 *   - 不同内容互不影响；DROP **不刷新**时间戳（重复风暴只压 1 个窗口，不会无限压制）；
 *   - 指纹环满 16 条时按轮转覆盖最旧一条（刚被覆盖的旧行即使仍在窗口内也不再命中）。
 *
 * 时间基准：`now_ms` 由调用方注入（设备侧 = esp_timer 毫秒低 32 位，单调环形回绕安全；
 * 比较用无符号差 `(uint32_t)(now - rx)`）。host 测试注入任意值。
 *
 * API：
 *   pet_net_dedupe_init(d);                                 初始化/清空
 *   pet_net_dedupe_feed(d, now_ms, line, len);              喂一条原始行 → ACCEPT / DROP
 *   pet_net_dedupe_fingerprint(line, len);                  FNV-1a 64（NULL 视为空串）
 *
 * 说明：长度不混入指纹（契约写的是"长度 + FNV-1a 64"），命中要求 len 与 fp 同时相等。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PET_NET_DEDUPE_SLOTS 16
#define PET_NET_DEDUPE_WINDOW_MS 1000u

typedef enum {
    PET_NET_DEDUPE_ACCEPT = 0,
    PET_NET_DEDUPE_DROP = 1,
} pet_net_dedupe_result_t;

typedef struct {
    uint64_t fp[PET_NET_DEDUPE_SLOTS];   /* FNV-1a 64 指纹 */
    uint16_t len[PET_NET_DEDUPE_SLOTS];  /* 原始行长度（≤ PET_LINE_MAX，这里放宽到 65535） */
    uint32_t rx_ms[PET_NET_DEDUPE_SLOTS];/* 最近一次接收的 now_ms（仅 ACCEPT 刷新） */
    bool used[PET_NET_DEDUPE_SLOTS];     /* 槽已用（now_ms 可为 0，故不能拿时间当哨兵） */
    uint8_t next;                        /* 下一个待覆盖槽（轮转） */
    uint32_t accepted;                   /* 统计：ACCEPT 次数（含窗口外重复） */
    uint32_t dropped;                    /* 统计：DROP 次数 */
} pet_net_dedupe_t;

void pet_net_dedupe_init(pet_net_dedupe_t *d);

/* FNV-1a 64：offset basis 14695981039346656037，prime 1099511628211。
 * `line == NULL` 时按空串计算（防御，不崩）。 */
uint64_t pet_net_dedupe_fingerprint(const char *line, size_t len);

/* 喂一条原始行（解析前）。`d == NULL` / `line == NULL` → 安全返回 DROP。 */
pet_net_dedupe_result_t pet_net_dedupe_feed(pet_net_dedupe_t *d, uint32_t now_ms,
                                           const char *line, size_t len);
