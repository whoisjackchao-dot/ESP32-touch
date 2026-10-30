/* M2 host 单测：pet_state 状态机 / done 边沿 / 驻留 / 失联 / 卡栈 / 动画（ST-01..07）
 * 契约：docs/08-m2-development.md §2.2/§3/§5；
 *       docs/contracts/C03-layout-240x240.md §3（栈规则）。
 * 时间全部由构造的 now_ms 驱动，不 sleep、不依赖真实时钟。
 * 只写 tests/，不引用实现内部结构（仅公开头文件 API）。 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pet_state.h"

/* 契约固定值。若 pet_state.h 暴露同名宏则以其为准；否则用契约默认值兜底。 */
#ifndef PET_DONE_EDGE_DEBOUNCE_MS
#define PET_DONE_EDGE_DEBOUNCE_MS 1500u
#endif
#ifndef PET_DONE_DWELL_MS
#define PET_DONE_DWELL_MS 5000u
#endif
#ifndef PET_LINK_TIMEOUT_MS
#define PET_LINK_TIMEOUT_MS 15000u
#endif

static int g_asserts;
static int g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_asserts++;                                                         \
        if (!(cond)) {                                                       \
            printf("  ASSERT FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_EQ(actual, expected)                                           \
    do {                                                                     \
        long long a_ = (long long)(actual);                                  \
        long long e_ = (long long)(expected);                                \
        g_asserts++;                                                         \
        if (a_ != e_) {                                                      \
            printf("  ASSERT FAIL %s:%d: %s == %s (got %lld, want %lld)\n",  \
                   __FILE__, __LINE__, #actual, #expected, a_, e_);          \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_STR(actual, expected)                                          \
    do {                                                                     \
        const char *a_ = (actual);                                           \
        const char *e_ = (expected);                                         \
        g_asserts++;                                                         \
        if (a_ == NULL || strcmp(a_, e_) != 0) {                             \
            printf("  ASSERT FAIL %s:%d: %s == \"%s\" (got \"%s\")\n",       \
                   __FILE__, __LINE__, #actual, e_, (a_ ? a_ : "(null)"));   \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

/* ---- 测试夹具 ---- */

static pet_event_t event_at(pet_event_kind_t kind, uint32_t now_ms)
{
    pet_event_t ev;
    memset(&ev, 0, sizeof ev);
    ev.kind = kind;
    ev.now_ms = now_ms;
    ev.card_state = PET_STATE_IDLE;
    return ev;
}

static pet_card_t card_of(const char *title, const char *project,
                          const char *agent, pet_state_t state,
                          int32_t elapsed_s)
{
    pet_card_t c;
    memset(&c, 0, sizeof c);
    if (title != NULL) {
        snprintf(c.title, sizeof c.title, "%s", title);
    }
    if (project != NULL) {
        snprintf(c.project, sizeof c.project, "%s", project);
    }
    if (agent != NULL) {
        snprintf(c.agent, sizeof c.agent, "%s", agent);
    }
    c.state = state;
    c.elapsed_s = elapsed_s;
    return c;
}

static pet_view_t snapshot(struct pet_state *s)
{
    pet_view_t v;
    memset(&v, 0x5A, sizeof v); /* 脏值：检测 view 是否完整填充 */
    pet_state_view(s, &v);
    return v;
}

/* ---- ST-01：事件 → 状态 ---- */
static void case_st01_event_to_state(void)
{
    struct pet_state *s = pet_state_create();
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }

    pet_state_tick(s, 0);
    pet_view_t v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_IDLE);
    CHECK_EQ(v.gray, 0);

    pet_event_t ev = event_at(PET_EV_TASK_START, 100);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_WORKING);
    CHECK_EQ(v.gray, 0);

    ev = event_at(PET_EV_AGENT_INPUT_REQUIRED, 200);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_INPUT_REQUIRED);

    ev = event_at(PET_EV_TASK_DONE, 300);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_DONE);

    ev = event_at(PET_EV_TASK_ERROR, 400);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_FAILED);

    ev = event_at(PET_EV_LINK_DOWN, 500);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_OFFLINE);
    CHECK_EQ(v.gray, 1);
    pet_state_destroy(s);

    /* AGENT_BLOCKED 也必须映射到 FAILED */
    s = pet_state_create();
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }
    pet_state_tick(s, 0);
    ev = event_at(PET_EV_TASK_START, 10);
    pet_state_apply(s, &ev);
    ev = event_at(PET_EV_AGENT_BLOCKED, 20);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_FAILED);
    pet_state_destroy(s);
}

/* ---- ST-02：WORKING 时 AGENT_IDLE 不立刻 IDLE，1500ms 后 DONE 且播报一次 ---- */
static void case_st02_done_edge_debounce(void)
{
    struct pet_state *s = pet_state_create();
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }
    pet_state_tick(s, 0);
    pet_event_t ev = event_at(PET_EV_TASK_START, 0);
    pet_state_apply(s, &ev);
    pet_state_tick(s, 1000);

    ev = event_at(PET_EV_AGENT_IDLE, 1000);
    pet_state_apply(s, &ev);
    pet_view_t v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_WORKING); /* 不得立即 IDLE */
    CHECK_EQ(pet_state_take_sound(s), PET_SOUND_NONE);

    pet_state_tick(s, 1000 + PET_DONE_EDGE_DEBOUNCE_MS - 1);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_WORKING); /* 去抖窗口内仍 WORKING */
    CHECK_EQ(pet_state_take_sound(s), PET_SOUND_NONE);

    pet_state_tick(s, 1000 + PET_DONE_EDGE_DEBOUNCE_MS);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_DONE);
    CHECK_EQ(v.frame, PET_FRAME_HAPPY);
    CHECK_EQ(pet_state_take_sound(s), PET_SOUND_DONE); /* 只播一次 */
    CHECK_EQ(pet_state_take_sound(s), PET_SOUND_NONE);
    pet_state_destroy(s);
}

/* ---- ST-03：done 边沿期间 TASK_START 取消，保持 WORKING 且不播报 ---- */
static void case_st03_done_edge_cancelled(void)
{
    struct pet_state *s = pet_state_create();
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }
    pet_state_tick(s, 0);
    pet_event_t ev = event_at(PET_EV_TASK_START, 0);
    pet_state_apply(s, &ev);
    pet_state_tick(s, 1000);

    ev = event_at(PET_EV_AGENT_IDLE, 1000);
    pet_state_apply(s, &ev);
    pet_state_tick(s, 2000); /* 去抖已进行 1000ms，仍未到 1500ms */
    CHECK_EQ(snapshot(s).state, PET_STATE_WORKING);

    ev = event_at(PET_EV_TASK_START, 2000); /* 取消挂起的 done 边沿 */
    pet_state_apply(s, &ev);
    pet_state_tick(s, 2600); /* 若未取消，1000+1500=2500 已到 */
    CHECK_EQ(snapshot(s).state, PET_STATE_WORKING);
    CHECK_EQ(pet_state_take_sound(s), PET_SOUND_NONE);

    pet_state_tick(s, 4000); /* 也不得延迟补发 DONE */
    CHECK_EQ(snapshot(s).state, PET_STATE_WORKING);
    CHECK_EQ(pet_state_take_sound(s), PET_SOUND_NONE);
    pet_state_destroy(s);
}

/* ---- ST-04：IDLE 时 AGENT_IDLE 保持 IDLE 且不播报 ---- */
static void case_st04_idle_agent_idle(void)
{
    struct pet_state *s = pet_state_create();
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }
    pet_state_tick(s, 0);
    pet_event_t ev = event_at(PET_EV_AGENT_IDLE, 0);
    pet_state_apply(s, &ev);
    CHECK_EQ(snapshot(s).state, PET_STATE_IDLE);

    pet_state_tick(s, PET_DONE_EDGE_DEBOUNCE_MS - 1);
    CHECK_EQ(snapshot(s).state, PET_STATE_IDLE);
    pet_state_tick(s, PET_DONE_EDGE_DEBOUNCE_MS + 500);
    CHECK_EQ(snapshot(s).state, PET_STATE_IDLE);
    CHECK_EQ(pet_state_take_sound(s), PET_SOUND_NONE);
    pet_state_destroy(s);
}

/* ---- ST-05：DONE 驻留 5000ms 后回 IDLE ---- */
static void case_st05_done_dwell(void)
{
    struct pet_state *s = pet_state_create();
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }
    pet_state_tick(s, 0);
    pet_event_t ev = event_at(PET_EV_TASK_DONE, 0);
    pet_state_apply(s, &ev);
    CHECK_EQ(snapshot(s).state, PET_STATE_DONE);

    pet_state_tick(s, PET_DONE_DWELL_MS - 1);
    CHECK_EQ(snapshot(s).state, PET_STATE_DONE);
    pet_state_tick(s, PET_DONE_DWELL_MS);
    CHECK_EQ(snapshot(s).state, PET_STATE_IDLE);
    pet_state_destroy(s);
}

/* ---- ST-06：失联 15000ms + LINK_UP 恢复 ---- */
static void case_st06_link_timeout_recovery(void)
{
    struct pet_state *s = pet_state_create();
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }
    pet_state_tick(s, 0);
    pet_event_t ev = event_at(PET_EV_TASK_START, 0);
    pet_state_apply(s, &ev);
    CHECK_EQ(snapshot(s).state, PET_STATE_WORKING);

    pet_state_tick(s, PET_LINK_TIMEOUT_MS - 1);
    CHECK_EQ(snapshot(s).state, PET_STATE_WORKING);

    pet_state_tick(s, PET_LINK_TIMEOUT_MS);
    pet_view_t v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_OFFLINE);
    CHECK_EQ(v.gray, 1);

    ev = event_at(PET_EV_LINK_UP, PET_LINK_TIMEOUT_MS);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_WORKING); /* 恢复到失联前状态 */
    CHECK_EQ(v.gray, 0);
    pet_state_destroy(s);

    /* 显式 LINK_DOWN 后 LINK_UP 同样恢复 */
    s = pet_state_create();
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }
    pet_state_tick(s, 0);
    ev = event_at(PET_EV_TASK_START, 0);
    pet_state_apply(s, &ev);
    ev = event_at(PET_EV_LINK_DOWN, 100);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_OFFLINE);
    CHECK_EQ(v.gray, 1);
    ev = event_at(PET_EV_LINK_UP, 200);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_WORKING);
    CHECK_EQ(v.gray, 0);
    pet_state_destroy(s);
}

/* ---- ST-07：3 卡栈（top-first / 第 4 卡淘汰 / 同名置顶 / 空 title / 清栈） ---- */
static void case_st07_card_stack(void)
{
    struct pet_state *s = pet_state_create();
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }
    pet_state_tick(s, 0);

    pet_view_t v = snapshot(s);
    CHECK_EQ(v.card_count, 0);
    CHECK_EQ(v.has_card, 0);

    /* 入栈 3 张：cards[0] 为最新（top-first） */
    pet_card_t a = card_of("ALPHA", "acme/api", "CX", PET_STATE_DONE, 5);
    pet_state_set_card(s, &a);
    v = snapshot(s);
    CHECK_EQ(v.card_count, 1);
    CHECK_EQ(v.has_card, 1);
    CHECK_STR(v.cards[0].title, "ALPHA");
    CHECK_STR(v.card.title, "ALPHA"); /* 旧字段与 cards[0] 同源 */

    pet_card_t b = card_of("BETA", "acme/web", "CC", PET_STATE_INPUT_REQUIRED, 900);
    pet_state_set_card(s, &b);
    v = snapshot(s);
    CHECK_EQ(v.card_count, 2);
    CHECK_STR(v.cards[0].title, "BETA");
    CHECK_STR(v.cards[1].title, "ALPHA");

    pet_card_t c = card_of("GAMMA", "openai/chat", "OC", PET_STATE_WORKING, 75);
    pet_state_set_card(s, &c);
    v = snapshot(s);
    CHECK_EQ(v.card_count, 3);
    CHECK_STR(v.cards[0].title, "GAMMA");
    CHECK_STR(v.cards[1].title, "BETA");
    CHECK_STR(v.cards[2].title, "ALPHA");

    /* 第 4 张：淘汰最旧（ALPHA） */
    pet_card_t d = card_of("DELTA", "", "OC", PET_STATE_FAILED, 3725);
    pet_state_set_card(s, &d);
    v = snapshot(s);
    CHECK_EQ(v.card_count, 3);
    CHECK_STR(v.cards[0].title, "DELTA");
    CHECK_STR(v.cards[1].title, "GAMMA");
    CHECK_STR(v.cards[2].title, "BETA");

    /* NULL 清栈 */
    pet_state_set_card(s, NULL);
    v = snapshot(s);
    CHECK_EQ(v.card_count, 0);
    CHECK_EQ(v.has_card, 0);

    /* 同名（title 且 project 相同）→ 更新并置顶，保留其余相对顺序 */
    a = card_of("T", "P", "OC", PET_STATE_WORKING, 10);
    b = card_of("B", "Q", "CC", PET_STATE_WORKING, 20);
    c = card_of("T", "P", "CX", PET_STATE_INPUT_REQUIRED, 30);
    pet_state_set_card(s, &a);
    pet_state_set_card(s, &b);
    pet_state_set_card(s, &c);
    v = snapshot(s);
    CHECK_EQ(v.card_count, 2); /* 同名更新而非新增 */
    CHECK_STR(v.cards[0].title, "T");
    CHECK_STR(v.cards[0].project, "P");
    CHECK_STR(v.cards[0].agent, "CX");
    CHECK_STR(v.cards[1].title, "B");

    /* 仅 title 相同、project 不同 → 不得视为同名 */
    pet_state_set_card(s, NULL);
    a = card_of("T", "P1", "OC", PET_STATE_WORKING, 10);
    b = card_of("B", "Q", "CC", PET_STATE_WORKING, 20);
    c = card_of("T", "P2", "CX", PET_STATE_WORKING, 30);
    pet_state_set_card(s, &a);
    pet_state_set_card(s, &b);
    pet_state_set_card(s, &c);
    v = snapshot(s);
    CHECK_EQ(v.card_count, 3);
    CHECK_STR(v.cards[0].title, "T");
    CHECK_STR(v.cards[0].project, "P2");
    CHECK_STR(v.cards[1].title, "B");
    CHECK_STR(v.cards[2].title, "T");
    CHECK_STR(v.cards[2].project, "P1");

    /* 空 title 的卡仍入栈（C03 §3.4） */
    pet_state_set_card(s, NULL);
    pet_card_t e = card_of("", "acme/empty", "OC", PET_STATE_WORKING, 10);
    pet_state_set_card(s, &e);
    v = snapshot(s);
    CHECK_EQ(v.card_count, 1);
    CHECK_STR(v.cards[0].title, "");
    CHECK_STR(v.cards[0].project, "acme/empty");

    /* set_cards：top-first 整体替换；count<=0 不动栈 */
    pet_state_set_card(s, NULL);
    pet_card_t arr[2];
    arr[0] = card_of("NEW", "n", "OC", PET_STATE_WORKING, 1);
    arr[1] = card_of("OLD", "o", "CC", PET_STATE_DONE, 2);
    pet_state_set_cards(s, arr, 2);
    v = snapshot(s);
    CHECK_EQ(v.card_count, 2);
    CHECK_STR(v.cards[0].title, "NEW");
    CHECK_STR(v.cards[1].title, "OLD");
    pet_state_set_cards(s, arr, 0);
    v = snapshot(s);
    CHECK_EQ(v.card_count, 2);
    pet_state_destroy(s);
}

/* ---- ST-08：动画（IDLE 眨眼 / DONE→HAPPY / FAILED→SAD / OFFLINE→闭眼 / 毛发 500ms） ---- */
static void case_st08_animation(void)
{
    struct pet_state *s = pet_state_create();
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }

    /* IDLE：每 1000ms 周期末 125ms 眨眼（phase 900 属于末 125ms） */
    pet_state_tick(s, 0);
    pet_view_t v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_IDLE);
    CHECK_EQ(v.frame, PET_FRAME_NORMAL);
    const bool hair0 = v.hair_right;

    pet_state_tick(s, 500);
    v = snapshot(s);
    CHECK_EQ(v.frame, PET_FRAME_NORMAL);
    CHECK_EQ(v.hair_right, !hair0); /* 毛发每 500ms 翻转 */

    pet_state_tick(s, 900);
    v = snapshot(s);
    CHECK_EQ(v.frame, PET_FRAME_EYES_CLOSED); /* 眨眼 */

    pet_state_tick(s, 1000);
    v = snapshot(s);
    CHECK_EQ(v.frame, PET_FRAME_NORMAL); /* 下一周期开始，睁眼 */
    CHECK_EQ(v.hair_right, hair0);

    pet_state_tick(s, 1900);
    v = snapshot(s);
    CHECK_EQ(v.frame, PET_FRAME_EYES_CLOSED);
    pet_state_destroy(s);

    /* DONE → HAPPY */
    s = pet_state_create();
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }
    pet_state_tick(s, 0);
    pet_event_t ev = event_at(PET_EV_TASK_DONE, 0);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_DONE);
    CHECK_EQ(v.frame, PET_FRAME_HAPPY);
    pet_state_destroy(s);

    /* FAILED → SAD */
    s = pet_state_create();
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }
    pet_state_tick(s, 0);
    ev = event_at(PET_EV_TASK_ERROR, 0);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_FAILED);
    CHECK_EQ(v.frame, PET_FRAME_SAD);
    pet_state_destroy(s);

    /* OFFLINE → EYES_CLOSED */
    s = pet_state_create();
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }
    pet_state_tick(s, 0);
    ev = event_at(PET_EV_LINK_DOWN, 0);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_OFFLINE);
    CHECK_EQ(v.frame, PET_FRAME_EYES_CLOSED);
    pet_state_destroy(s);
}

/* ---- ST-09：视图字段（card_count/cards/card 与栈一致；gray 仅 OFFLINE） ---- */
static void case_st09_view_fields(void)
{
    struct pet_state *s = pet_state_create();
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }
    pet_state_tick(s, 0);

    pet_view_t v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_IDLE);
    CHECK_EQ(v.gray, 0);
    CHECK_EQ(v.card_count, 0);
    CHECK_EQ(v.has_card, 0);

    pet_event_t ev = event_at(PET_EV_TASK_START, 10);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_WORKING);
    CHECK_EQ(v.gray, 0);

    ev = event_at(PET_EV_AGENT_INPUT_REQUIRED, 20);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_INPUT_REQUIRED);
    CHECK_EQ(v.gray, 0);

    ev = event_at(PET_EV_TASK_DONE, 30);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_DONE);
    CHECK_EQ(v.gray, 0);

    ev = event_at(PET_EV_TASK_ERROR, 40);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_FAILED);
    CHECK_EQ(v.gray, 0);

    ev = event_at(PET_EV_LINK_DOWN, 50);
    pet_state_apply(s, &ev);
    v = snapshot(s);
    CHECK_EQ(v.state, PET_STATE_OFFLINE);
    CHECK_EQ(v.gray, 1); /* gray 仅 OFFLINE */

    /* 栈与 card 字段同源：后入栈的 BOTTOM 在 cards[0]（C03 §3.1：cards[0]=最新） */
    pet_card_t a = card_of("TOP", "p", "OC", PET_STATE_WORKING, 1);
    pet_card_t b = card_of("BOTTOM", "q", "CC", PET_STATE_DONE, 2);
    pet_state_set_card(s, &a);
    pet_state_set_card(s, &b);
    v = snapshot(s);
    CHECK_EQ(v.card_count, 2);
    CHECK_EQ(v.has_card, 1);
    CHECK_STR(v.cards[0].title, "BOTTOM");
    CHECK_STR(v.cards[1].title, "TOP");
    CHECK_STR(v.card.title, "BOTTOM");
    pet_state_destroy(s);
}

/* ---- 运行器 ---- */

typedef struct {
    const char *name;
    void (*fn)(void);
} test_case_t;

int main(void)
{
    static const test_case_t cases[] = {
        {"ST-01-event-to-state", case_st01_event_to_state},
        {"ST-02-done-edge-debounce", case_st02_done_edge_debounce},
        {"ST-03-done-edge-cancelled", case_st03_done_edge_cancelled},
        {"ST-04-idle-agent-idle", case_st04_idle_agent_idle},
        {"ST-05-done-dwell", case_st05_done_dwell},
        {"ST-06-link-timeout-recovery", case_st06_link_timeout_recovery},
        {"ST-07-card-stack", case_st07_card_stack},
        {"ST-08-animation", case_st08_animation},
        {"ST-09-view-fields", case_st09_view_fields},
    };

    int failed_cases = 0;
    const int n = (int)(sizeof cases / sizeof cases[0]);
    for (int i = 0; i < n; i++) {
        const int before = g_failures;
        g_asserts = 0;
        cases[i].fn();
        if (g_failures > before) {
            printf("FAIL %s (%d/%d asserts failed)\n", cases[i].name,
                   g_failures - before, g_asserts);
            failed_cases++;
        } else {
            printf("PASS %s (%d asserts)\n", cases[i].name, g_asserts);
        }
    }
    printf("SUMMARY pet_state: cases=%d passed=%d failed=%d\n", n,
           n - failed_cases, failed_cases);
    return failed_cases == 0 ? 0 : 1;
}

