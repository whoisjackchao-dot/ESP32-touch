#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pet_types.h"
#include "pet_state.h" /* pet_event_kind_t / pet_event_t 定义在此 */

/* M5a：device 侧"一行事件 → pet_event_t + 卡片"的解析（纯逻辑，host 可测）。
 * 契约：docs/contracts/C05-event-protocol.md（2026-10-31 冻结）。
 * 来源：派生自上游 ESP32-buddy（MIT）pet_link；本项目增补 `agent` 字段。 */

#ifndef PET_LINE_MAX
#define PET_LINE_MAX 512
#endif

typedef enum {
    PET_LINK_OK = 0,
    PET_LINK_ERR_TOO_LONG,      /* 超过 PET_LINE_MAX */
    PET_LINK_ERR_JSON,          /* 不是合法 JSON 对象 */
    PET_LINK_ERR_VERSION,       /* version 缺失或 != 1 */
    PET_LINK_ERR_EVENT,         /* event 缺失 */
    PET_LINK_ERR_TYPE,          /* 字段类型不符（如 title 是数字、elapsed_s 为负） */
    PET_LINK_ERR_UNKNOWN_EVENT  /* event 名不认识 */
} pet_link_status_t;

typedef struct {
    pet_event_kind_t kind;
    bool             has_card;
    pet_card_t       card;       /* has_card 时有效（顶层单卡字段） */
    bool             has_tasks;  /* 行内出现 `tasks` 数组（含空数组）→ 整栈替换/清空 */
    int              card_count; /* tasks[] 解析出的卡数 0..3（top-first） */
    pet_card_t       cards[3];   /* cards[0] = tasks 首项 */
} pet_link_msg_t;

/* 解析一行 NDJSON；line 可带可不带结尾 '\n'。成功时填 out。 */
pet_link_status_t pet_link_parse(const char *line, size_t len, pet_link_msg_t *out);
