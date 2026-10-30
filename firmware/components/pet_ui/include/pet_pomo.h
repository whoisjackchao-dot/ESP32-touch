#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* M7 增量 1：番茄钟逻辑（计时状态机 + 当日统计），host 可测。
 * 契约：contracts/M7-番茄钟契约清单.md §6。
 * 圆环绘制 / NVS 落盘 / "今天"来源属增量 2；本模块不读 GPIO。 */

/* §6.5 参数（提案值，待批准；编译期可覆盖） */
#ifndef POMO_FOCUS_MS
#define POMO_FOCUS_MS 1500000u /* 25 × 60 × 1000 */
#endif
#ifndef POMO_BREAK_MS
#define POMO_BREAK_MS 300000u /* 5 × 60 × 1000 */
#endif

/* §6.1 状态机 */
typedef enum {
    PET_POMO_IDLE = 0,
    PET_POMO_FOCUS,
    PET_POMO_FOCUS_PAUSED,
    PET_POMO_BREAK,
    PET_POMO_BREAK_PAUSED,
    PET_POMO_FOCUS_DONE,
    PET_POMO_BREAK_DONE
} pet_pomo_phase_t;

/* §6.1 命令：KEY1 短按（开始/暂停/继续/推进）、长按（放弃） */
typedef enum {
    PET_POMO_CMD_SHORT = 0,
    PET_POMO_CMD_LONG
} pet_pomo_cmd_t;

typedef struct {
    pet_pomo_phase_t phase;
    uint32_t since_ms;      /* 上次推进/相位进入的时刻 */
    uint32_t elapsed_ms;    /* 本段已累计 */
    uint32_t total_ms;      /* 本段总时长 */
    uint32_t done_count;    /* 当日完成专注段数 */
    uint32_t focus_seconds; /* 当日累计专注秒 */
    bool alarm;             /* 待响铃（取走即清） */
    bool has_day;
    int32_t day;
} pet_pomo_t;

typedef struct {
    pet_pomo_phase_t phase;
    uint32_t remaining_ms; /* total_ms - elapsed_ms，下限 0 */
    uint32_t total_ms;
    uint32_t done_count;
    uint32_t focus_seconds;
    bool alarm;
    char     ip[16]; /* M12：WiFi IP（空串=未连接；由引擎从 pet_net 填） */
} pet_pomo_view_t;

void pet_pomo_init(pet_pomo_t *p, uint32_t now_ms);
void pet_pomo_command(pet_pomo_t *p, pet_pomo_cmd_t cmd, uint32_t now_ms);
void pet_pomo_tick(pet_pomo_t *p, uint32_t now_ms);
void pet_pomo_set_day(pet_pomo_t *p, int32_t day);
void pet_pomo_view_get(const pet_pomo_t *p, pet_pomo_view_t *out);
bool pet_pomo_take_alarm(pet_pomo_t *p); /* 返回待响铃并清零 */

/* ---- M7 增量 2：当日统计持久化编解码（契约 §7.3，host 可测）---- */

/* 固定 16 字节小端 blob：magic "PMC1" | day i32 | done_count u32 | focus_seconds u32 */
#define PET_POMO_STATS_LEN 16

/* 编码当日统计；cap < 16 → 0（不写 buf），否则返回 16。 */
size_t pet_pomo_encode_stats(uint8_t *buf, size_t cap, const pet_pomo_t *p);
/* 解码当日统计；len != 16 或 magic 不符 → false 且不修改 *p。
 * 成功只置 has_day/day/done_count/focus_seconds，不动其它字段。 */
bool pet_pomo_decode_stats(const uint8_t *buf, size_t len, pet_pomo_t *p);
