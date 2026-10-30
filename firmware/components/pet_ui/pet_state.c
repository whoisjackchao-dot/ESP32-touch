/* M2：pet_state 状态机 + done 边沿合成 + 失联 + 3 卡栈。
 * 来源：派生自上游 ESP32-buddy（MIT）components/pet/pet_state.c 的行为规格，
 *       按本项目 docs/contracts/C03 §3 修订卡栈规则（同名=title 且 project；
 *       空 title 卡仍入栈，由渲染层显示占位符）。
 * 契约：docs/08-m2-development.md §2.2；常量提案值可用 -D 覆盖。 */
#include <stdlib.h>
#include <string.h>

#include "pet_state.h"

#ifndef PET_DONE_DWELL_MS
#define PET_DONE_DWELL_MS 5000u
#endif
#ifndef PET_DONE_EDGE_DEBOUNCE_MS
#define PET_DONE_EDGE_DEBOUNCE_MS 1500u
#endif
#ifndef PET_LINK_TIMEOUT_MS
#define PET_LINK_TIMEOUT_MS 15000u
#endif
/* idle 动画节拍（契约 M2 §9.2，提案值、可配置） */
#ifndef PET_BLINK_PERIOD_MS
#define PET_BLINK_PERIOD_MS 1000u
#endif
#ifndef PET_BLINK_MS
#define PET_BLINK_MS 125u
#endif
#ifndef PET_IDLE_MOOD_PERIOD_MS
#define PET_IDLE_MOOD_PERIOD_MS 20000u
#endif
#ifndef PET_IDLE_MOOD_MS
#define PET_IDLE_MOOD_MS 2000u
#endif
/* 头顶毛发相位（12 帧 = 6 关键帧 × 2 相位） */
#ifndef PET_HAIR_PHASE_MS
#define PET_HAIR_PHASE_MS 500u
#endif

struct pet_state {
    pet_state_t  state;
    pet_state_t  prev_state;      /* 进 OFFLINE 前的状态，供 LINK_UP 恢复 */
    uint32_t     since_ms;        /* 进入当前状态的时刻 */
    uint32_t     last_valid_ms;   /* 最近一次有效事件时刻 */
    uint32_t     now_ms;          /* 最近一次 apply/tick 的时刻（动画相位用） */
    uint32_t     pending_done_at; /* 0 = 无待定完成 */
    bool         started;
    pet_sound_t  pending_sound;
    pet_card_t   cards[3];        /* C03 §3：cards[0] = 最新（顶部） */
    int          card_count;      /* 0..3 */
};

/* 环绕安全的时间差比较：now - then >= ms */
static bool elapsed(uint32_t now, uint32_t then, uint32_t ms)
{
    return (int32_t)(now - then) >= (int32_t)ms;
}

static void set_state(struct pet_state *s, pet_state_t st, uint32_t now)
{
    s->state = st;
    s->since_ms = now;
}

struct pet_state *pet_state_create(void)
{
    struct pet_state *s = calloc(1, sizeof *s);
    if (s != NULL) {
        s->state = PET_STATE_IDLE;
        s->prev_state = PET_STATE_IDLE;
    }
    return s;
}

void pet_state_destroy(struct pet_state *s)
{
    free(s);
}

void pet_state_apply(struct pet_state *s, const pet_event_t *ev)
{
    if (s == NULL || ev == NULL) {
        return;
    }
    const uint32_t now = ev->now_ms;
    s->last_valid_ms = now;
    s->now_ms = now;
    s->started = true;

    switch (ev->kind) {
    case PET_EV_TASK_START:
        s->pending_done_at = 0; /* 抖动：取消待定完成 */
        set_state(s, PET_STATE_WORKING, now);
        break;

    case PET_EV_AGENT_INPUT_REQUIRED:
        set_state(s, PET_STATE_INPUT_REQUIRED, now);
        s->pending_sound = PET_SOUND_INPUT;
        break;

    case PET_EV_TASK_DONE:
        s->pending_done_at = 0;
        set_state(s, PET_STATE_DONE, now);
        s->pending_sound = PET_SOUND_DONE;
        break;

    case PET_EV_TASK_ERROR:
    case PET_EV_AGENT_BLOCKED:
        s->pending_done_at = 0;
        set_state(s, PET_STATE_FAILED, now);
        s->pending_sound = PET_SOUND_FAILED;
        break;

    case PET_EV_AGENT_IDLE:
        if (s->state == PET_STATE_WORKING || s->state == PET_STATE_INPUT_REQUIRED) {
            /* 边沿：进入待定完成，等去抖窗口 */
            uint32_t at = now + PET_DONE_EDGE_DEBOUNCE_MS;
            s->pending_done_at = (at == 0) ? 1u : at; /* 0 是哨兵 */
        } else {
            s->pending_done_at = 0;
            set_state(s, PET_STATE_IDLE, now);
        }
        break;

    case PET_EV_LINK_DOWN:
        if (s->state != PET_STATE_OFFLINE) {
            s->prev_state = s->state;
        }
        set_state(s, PET_STATE_OFFLINE, now);
        break;

    case PET_EV_LINK_UP:
        set_state(s, (s->prev_state == PET_STATE_OFFLINE) ? PET_STATE_IDLE
                                                          : s->prev_state,
                  now);
        break;

    case PET_EV_ANNOUNCEMENT:
        /* 心跳只刷新链路；若已 OFFLINE 按恢复语义回到失联前状态，
         * 否则"只发心跳"的系统会永久灰屏。 */
        if (s->state == PET_STATE_OFFLINE) {
            set_state(s, (s->prev_state == PET_STATE_OFFLINE) ? PET_STATE_IDLE
                                                              : s->prev_state,
                      now);
        }
        break;

    case PET_EV_CARD:
    default:
        break; /* 卡由 pet_state_set_card/set_cards 显式维护（M2） */
    }
}

void pet_state_tick(struct pet_state *s, uint32_t now_ms)
{
    if (s == NULL) {
        return;
    }
    s->now_ms = now_ms;
    if (!s->started) {
        s->last_valid_ms = now_ms;
        s->started = true;
    }

    /* done 边沿到点 → DONE（播报一次） */
    if (s->pending_done_at != 0 && (int32_t)(now_ms - s->pending_done_at) >= 0) {
        s->pending_done_at = 0;
        set_state(s, PET_STATE_DONE, now_ms);
        s->pending_sound = PET_SOUND_DONE;
    }

    /* DONE 驻留结束 → IDLE */
    if (s->state == PET_STATE_DONE && elapsed(now_ms, s->since_ms, PET_DONE_DWELL_MS)) {
        set_state(s, PET_STATE_IDLE, now_ms);
    }

    /* 失联 → OFFLINE（灰） */
    if (s->state != PET_STATE_OFFLINE &&
        elapsed(now_ms, s->last_valid_ms, PET_LINK_TIMEOUT_MS)) {
        s->prev_state = s->state;
        set_state(s, PET_STATE_OFFLINE, now_ms);
    }
}

/* idle 动画帧（契约 M2 §9）：小动作期优先，其次眨眼 */
static pet_frame_t idle_frame(uint32_t phase)
{
    const uint32_t mood_phase = phase % PET_IDLE_MOOD_PERIOD_MS;
    if (mood_phase >= PET_IDLE_MOOD_PERIOD_MS - PET_IDLE_MOOD_MS) {
        const uint32_t mood = (phase / PET_IDLE_MOOD_PERIOD_MS) % 3u;
        return (mood == 0u) ? PET_FRAME_EYES_CLOSED /* Nap */ : PET_FRAME_NORMAL;
    }
    const uint32_t blink_phase = phase % PET_BLINK_PERIOD_MS;
    if (blink_phase >= PET_BLINK_PERIOD_MS - PET_BLINK_MS) {
        return PET_FRAME_EYES_CLOSED;
    }
    return PET_FRAME_NORMAL;
}

void pet_state_view(const struct pet_state *s, pet_view_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (s == NULL) {
        return;
    }
    out->state = s->state;
    out->gray = (s->state == PET_STATE_OFFLINE);
    out->mirror = false;
    out->card_count = s->card_count;
    out->has_card = (s->card_count > 0);
    for (int i = 0; i < s->card_count; i++) {
        out->cards[i] = s->cards[i];
    }
    if (s->card_count > 0) {
        out->card = s->cards[0]; /* 顶部卡（兼容旧字段） */
    }
    switch (s->state) {
    case PET_STATE_DONE:
        out->frame = PET_FRAME_HAPPY;
        break;
    case PET_STATE_FAILED:
        out->frame = PET_FRAME_SAD;
        break;
    case PET_STATE_OFFLINE:
        out->frame = PET_FRAME_EYES_CLOSED;
        break;
    case PET_STATE_WORKING:
        out->frame = PET_FRAME_WORKING;
        break;
    case PET_STATE_INPUT_REQUIRED:
        out->frame = PET_FRAME_INPUT;
        break;
    case PET_STATE_IDLE:
        out->frame = idle_frame(s->now_ms - s->since_ms);
        break;
    default:
        out->frame = PET_FRAME_NORMAL;
        break;
    }

    const uint32_t phase = s->now_ms - s->since_ms;
    out->hair_right = ((phase / PET_HAIR_PHASE_MS) % 2u) != 0u;
}

pet_sound_t pet_state_take_sound(struct pet_state *s)
{
    if (s == NULL) {
        return PET_SOUND_NONE;
    }
    const pet_sound_t sound = s->pending_sound;
    s->pending_sound = PET_SOUND_NONE;
    return sound;
}

/* C03 §3：入栈/更新——同名（title 且 project 相同）更新并移到顶部；
 * 新卡入栈顶；>3 丢最旧（底部）；card==NULL 清栈。
 * 空 title 卡**仍入栈**（C03 §3.4：空字段由渲染层显示占位符，不得整卡丢弃）。 */
void pet_state_set_card(struct pet_state *s, const pet_card_t *card)
{
    if (s == NULL) {
        return;
    }
    if (card == NULL) {
        s->card_count = 0;
        return;
    }
    int found = -1;
    for (int i = 0; i < s->card_count; i++) {
        if (strcmp(s->cards[i].title, card->title) == 0 &&
            strcmp(s->cards[i].project, card->project) == 0) {
            found = i;
            break;
        }
    }
    if (found < 0 && s->card_count < 3) {
        s->card_count++;
    }
    /* found<0：从新栈顶（card_count-1）起整体后移；found>=0：把 found 以上的下移覆盖 */
    for (int i = (found < 0) ? s->card_count - 1 : found; i > 0; i--) {
        s->cards[i] = s->cards[i - 1];
    }
    s->cards[0] = *card;
}

/* 整体替换栈（top-first 原样入栈，≤3 截断；count<=0 不动栈）。 */
void pet_state_set_cards(struct pet_state *s, const pet_card_t *cards, int count)
{
    if (s == NULL || cards == NULL || count <= 0) {
        return;
    }
    s->card_count = 0;
    for (int i = 0; i < count && s->card_count < 3; i++) {
        s->cards[s->card_count++] = cards[i];
    }
}
