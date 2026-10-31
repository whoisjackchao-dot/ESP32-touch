/* M5a host 单测（C 类契约，非 TDD 新逻辑）：C05 设备事件协议 ——
 *   pet_linebuf（字节流 → 一行，含超长丢弃）
 *   pet_link    （一行 NDJSON → pet_event_kind_t + 顶层单卡 + tasks 多卡栈）
 * 契约：docs/contracts/C05-event-protocol.md（2026-10-31 冻结）。
 * 被测（派生自上游 ESP32-buddy，MIT；Lead 已实现，本测试不改实现）：
 *   firmware/components/pet_ui/pet_linebuf.c、pet_link.c 及其公开头文件
 *   include/pet_linebuf.h、include/pet_link.h、include/pet_types.h、include/pet_state.h。
 * 口径：只用公开头文件 API；无 stubs、无 sleep、不碰串口/硬件；断言以**真实实现**为准，
 *       文档与实现冲突处在 tests/RED-GREEN-M5A.md 记录（不改 firmware/、docs/）。
 * 只写 tests/。 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pet_linebuf.h"
#include "pet_link.h"

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

/* ---- 夹具 ---- */

/* 按 C 字符串解析（不含结尾 NUL）。 */
static pet_link_status_t parse_str(const char *s, pet_link_msg_t *out)
{
    return pet_link_parse(s, strlen(s), out);
}

/* 整串喂给行缓冲；返回最后一次非 NONE 的结果，*events 统计非 NONE 次数。 */
static pet_line_feed_t feed_whole(pet_linebuf_t *lb, const char *s, int *events)
{
    pet_line_feed_t last = PET_LINE_NONE;
    int n = 0;
    for (const char *p = s; *p != '\0'; p++) {
        pet_line_feed_t r = pet_linebuf_feed(lb, *p);
        if (r != PET_LINE_NONE) {
            last = r;
            n++;
        }
    }
    if (events != NULL) {
        *events = n;
    }
    return last;
}

/* ---- 1. pet_linebuf ---- */

/* LB-01：逐字节喂入单行；NUL 结尾、不含 '\n'。 */
static void case_lb01_single_line_bytewise(void)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);
    CHECK_EQ(lb.len, 0);
    CHECK_EQ(lb.overflow, 0);
    CHECK_EQ(lb.deliver_pending, 0);
    CHECK_STR(lb.buf, "");

    const char *src = "hello";
    bool all_none = true;
    for (const char *p = src; *p != '\0'; p++) {
        if (pet_linebuf_feed(&lb, *p) != PET_LINE_NONE) {
            all_none = false;
        }
    }
    CHECK(all_none); /* 未遇 '\n' 前一律 NONE */
    CHECK_EQ(lb.len, 5);
    CHECK_EQ(lb.overflow, 0);
    CHECK_EQ(lb.deliver_pending, 0);

    CHECK_EQ(pet_linebuf_feed(&lb, '\n'), PET_LINE_READY);
    CHECK_EQ(lb.len, 5);
    CHECK_EQ(lb.buf[5], '\0');
    CHECK_STR(lb.buf, "hello");
    CHECK_EQ(lb.deliver_pending, 1);
}

/* LB-02：CRLF 剥离——只剥行尾一个 '\r'；裸 '\n' 亦可。 */
static void case_lb02_crlf_strip(void)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);
    int ev = 0;
    CHECK_EQ(feed_whole(&lb, "ab\r\n", &ev), PET_LINE_READY);
    CHECK_EQ(ev, 1);
    CHECK_EQ(lb.len, 2);
    CHECK_STR(lb.buf, "ab");

    /* 空行 + CRLF */
    pet_linebuf_init(&lb);
    CHECK_EQ(feed_whole(&lb, "\r\n", NULL), PET_LINE_READY);
    CHECK_EQ(lb.len, 0);
    CHECK_STR(lb.buf, "");

    /* 只有 '\n'（契约：行内含花括号，不含换行；裸 \n 也按一行交付） */
    pet_linebuf_init(&lb);
    CHECK_EQ(feed_whole(&lb, "\n", NULL), PET_LINE_READY);
    CHECK_EQ(lb.len, 0);

    /* 行内 '\r' 不是行尾，不应被剥 */
    pet_linebuf_init(&lb);
    CHECK_EQ(feed_whole(&lb, "a\rb\n", NULL), PET_LINE_READY);
    CHECK_EQ(lb.len, 3);
    CHECK_STR(lb.buf, "a\rb");
}

/* LB-03：空行/连续空行交付语义。 */
static void case_lb03_empty_lines(void)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);
    int ev = 0;
    CHECK_EQ(feed_whole(&lb, "\n\n", &ev), PET_LINE_READY);
    CHECK_EQ(ev, 2); /* 两个空行各交付一次 */
    CHECK_EQ(lb.len, 0);
    CHECK_STR(lb.buf, "");

    /* 上一行 READY 后未复位：下一次 feed 先复位再攒新行 */
    pet_linebuf_init(&lb);
    CHECK_EQ(feed_whole(&lb, "ab\n", NULL), PET_LINE_READY);
    CHECK_EQ(feed_whole(&lb, "cd\n", NULL), PET_LINE_READY);
    CHECK_EQ(lb.len, 2);
    CHECK_STR(lb.buf, "cd");
}

/* LB-04：恰好 PET_LINE_MAX 字节的行 → READY（含缓冲 NUL）。 */
static void case_lb04_exact_max_line(void)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);
    bool all_none = true;
    for (int i = 0; i < PET_LINE_MAX; i++) {
        if (pet_linebuf_feed(&lb, 'A') != PET_LINE_NONE) {
            all_none = false;
        }
    }
    CHECK(all_none);
    CHECK_EQ(lb.len, PET_LINE_MAX);
    CHECK_EQ(lb.overflow, 0);
    CHECK_EQ(pet_linebuf_feed(&lb, '\n'), PET_LINE_READY);
    CHECK_EQ(lb.len, PET_LINE_MAX);
    CHECK_EQ(lb.buf[PET_LINE_MAX], '\0');
    CHECK_EQ(lb.buf[0], 'A');
    CHECK_EQ(lb.buf[PET_LINE_MAX - 1], 'A');
    CHECK_EQ(lb.deliver_pending, 1);
}

/* LB-05：>PET_LINE_MAX 整行丢弃（DROPPED），复位后可继续解析下一行。 */
static void case_lb05_overflow_dropped_and_recover(void)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);

    bool all_none = true;
    for (int i = 0; i < PET_LINE_MAX; i++) {
        if (pet_linebuf_feed(&lb, 'B') != PET_LINE_NONE) {
            all_none = false;
        }
    }
    CHECK(all_none);
    /* 第 PET_LINE_MAX+1 个字节：标记溢出，但仍属本行 → NONE */
    CHECK_EQ(pet_linebuf_feed(&lb, 'X'), PET_LINE_NONE);
    CHECK_EQ(lb.overflow, 1);
    CHECK_EQ(lb.len, PET_LINE_MAX); /* 不再增长 */
    /* 溢出后继续灌字节也保持 NONE */
    for (int i = 0; i < 32; i++) {
        CHECK_EQ(pet_linebuf_feed(&lb, 'Y'), PET_LINE_NONE);
    }
    CHECK_EQ(pet_linebuf_feed(&lb, '\n'), PET_LINE_DROPPED);
    CHECK_EQ(lb.len, 0);
    CHECK_EQ(lb.overflow, 0);
    CHECK_EQ(lb.deliver_pending, 0);

    /* 丢弃后下一行正常交付（不允许残留上一行内容） */
    int ev = 0;
    CHECK_EQ(feed_whole(&lb, "ok\n", &ev), PET_LINE_READY);
    CHECK_EQ(ev, 1);
    CHECK_EQ(lb.len, 2);
    CHECK_STR(lb.buf, "ok");

    /* 恰好 512 后紧跟 CRLF：'\r' 是第 513 字节 → 仍算超长丢弃 */
    pet_linebuf_init(&lb);
    for (int i = 0; i < PET_LINE_MAX; i++) {
        (void)pet_linebuf_feed(&lb, 'C');
    }
    CHECK_EQ(pet_linebuf_feed(&lb, '\r'), PET_LINE_NONE);
    CHECK_EQ(lb.overflow, 1);
    CHECK_EQ(pet_linebuf_feed(&lb, '\n'), PET_LINE_DROPPED);
}

/* LB-06：READY 后（deliver_pending）不串行；DROPPED 不置 pending。 */
static void case_lb06_deliver_pending_reset(void)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);
    CHECK_EQ(feed_whole(&lb, "abc\n", NULL), PET_LINE_READY);
    CHECK_EQ(lb.deliver_pending, 1);
    /* 上一行已交付：紧接着的 '\n' 必须是新的空行，而不是重复交付 "abc" */
    CHECK_EQ(pet_linebuf_feed(&lb, '\n'), PET_LINE_READY);
    CHECK_EQ(lb.len, 0);
    CHECK_STR(lb.buf, "");
    CHECK_EQ(lb.deliver_pending, 1);

    /* READY 后新行覆盖旧内容 */
    CHECK_EQ(feed_whole(&lb, "z\n", NULL), PET_LINE_READY);
    CHECK_EQ(lb.len, 1);
    CHECK_STR(lb.buf, "z");
}
/* ---- 2. pet_link：正常路径 ---- */

typedef struct {
    const char      *event;
    pet_event_kind_t kind;
    pet_state_t      state;
} ev_map_t;

/* 契约 §3 的 6 类事件（7 个名字，其中 agent.blocked 并入 TASK_ERROR）。 */
static const ev_map_t EVENTS[] = {
    {"task.start", PET_EV_TASK_START, PET_STATE_WORKING},
    {"agent.input_required", PET_EV_AGENT_INPUT_REQUIRED, PET_STATE_INPUT_REQUIRED},
    {"task.done", PET_EV_TASK_DONE, PET_STATE_DONE},
    {"task.error", PET_EV_TASK_ERROR, PET_STATE_FAILED},
    {"agent.blocked", PET_EV_TASK_ERROR, PET_STATE_FAILED},
    {"agent.idle", PET_EV_AGENT_IDLE, PET_STATE_IDLE},
    {"device.heartbeat", PET_EV_ANNOUNCEMENT, PET_STATE_IDLE},
};

/* LK-01：6 类 event → kind 映射；无单卡字段时 has_card=false、card_count=0；
 *        带 title 时单卡 state = 该事件映射态。 */
static void case_lk01_event_kind_map(void)
{
    const int n = (int)(sizeof EVENTS / sizeof EVENTS[0]);
    for (int i = 0; i < n; i++) {
        char line[256];
        pet_link_msg_t msg;

        snprintf(line, sizeof line, "{\"version\":1,\"event\":\"%s\"}",
                 EVENTS[i].event);
        CHECK_EQ(parse_str(line, &msg), PET_LINK_OK);
        CHECK_EQ(msg.kind, EVENTS[i].kind);
        CHECK_EQ(msg.has_card, 0);
        CHECK_EQ(msg.card_count, 0);

        snprintf(line, sizeof line,
                 "{\"version\":1,\"event\":\"%s\",\"title\":\"t\"}",
                 EVENTS[i].event);
        CHECK_EQ(parse_str(line, &msg), PET_LINK_OK);
        CHECK_EQ(msg.kind, EVENTS[i].kind);
        CHECK_EQ(msg.has_card, 1);
        CHECK_EQ(msg.card.state, EVENTS[i].state);
        CHECK_STR(msg.card.title, "t");
    }
}

/* LK-02：agent.blocked 的 kind 显式断言（契约 §3：与 task.error 同 → TASK_ERROR）。 */
static void case_lk02_blocked_is_task_error(void)
{
    pet_link_msg_t msg;
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"agent.blocked\"}", &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.kind, PET_EV_TASK_ERROR);
    CHECK(msg.kind != PET_EV_AGENT_BLOCKED);
}

/* LK-03：顶层单卡字段 title/project/agent/elapsed_s → has_card + 字段值。 */
static void case_lk03_top_card_fields(void)
{
    pet_link_msg_t msg;
    const char *line =
        "{\"version\":1,\"event\":\"task.start\",\"title\":\"Fix login\","
        "\"project\":\"ESP32-touch\",\"agent\":\"DSH\",\"elapsed_s\":42}";
    CHECK_EQ(parse_str(line, &msg), PET_LINK_OK);
    CHECK_EQ(msg.kind, PET_EV_TASK_START);
    CHECK_EQ(msg.has_card, 1);
    CHECK_EQ(msg.card.state, PET_STATE_WORKING);
    CHECK_STR(msg.card.title, "Fix login");
    CHECK_STR(msg.card.project, "ESP32-touch");
    CHECK_STR(msg.card.agent, "DSH");
    CHECK_EQ(msg.card.elapsed_s, 42);
    CHECK_EQ(msg.card_count, 0);

    /* 键序无关：event 在前、version 在后 */
    CHECK_EQ(parse_str("{\"event\":\"task.done\",\"version\":1,\"title\":\"T\"}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.kind, PET_EV_TASK_DONE);
    CHECK_EQ(msg.card.state, PET_STATE_DONE);
    CHECK_STR(msg.card.title, "T");

    /* 行尾可带 '\n' 或 CRLF（头文件：line 可带可不带结尾 '\n'） */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\"}\n", &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.has_card, 0);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\"}\r\n", &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.has_card, 0);
}

/* LK-04：任一顶层可选字段出现即产生单卡（含空串/0）。 */
static void case_lk04_top_card_single_field_triggers(void)
{
    pet_link_msg_t msg;

    /* title（空串也算出现） */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"agent.idle\",\"title\":\"\"}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.has_card, 1);
    CHECK_STR(msg.card.title, "");
    CHECK_EQ(msg.card.state, PET_STATE_IDLE);

    /* project 单独出现 */
    CHECK_EQ(parse_str(
                 "{\"version\":1,\"event\":\"task.start\",\"project\":\"P\"}",
                 &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.has_card, 1);
    CHECK_STR(msg.card.project, "P");
    CHECK_STR(msg.card.title, "");
    CHECK_EQ(msg.card.state, PET_STATE_WORKING);

    /* agent 单独出现 */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.error\",\"agent\":\"DSH\"}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.has_card, 1);
    CHECK_STR(msg.card.agent, "DSH");
    CHECK_EQ(msg.card.state, PET_STATE_FAILED);

    /* elapsed_s: 0 单独出现（0 不能因“假值”被当作缺失） */
    CHECK_EQ(parse_str(
                 "{\"version\":1,\"event\":\"task.done\",\"elapsed_s\":0}",
                 &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.has_card, 1);
    CHECK_EQ(msg.card.elapsed_s, 0);
    CHECK_EQ(msg.card.state, PET_STATE_DONE);
}

/* LK-05：tasks 3 项 → card_count=3，cards[0]=首项（top-first），字段/状态正确。 */
static void case_lk05_tasks_three_cards(void)
{
    pet_link_msg_t msg;
    const char *line =
        "{\"version\":1,\"event\":\"task.start\",\"tasks\":["
        "{\"title\":\"A\",\"project\":\"P1\",\"agent\":\"DSH\","
        "\"elapsed_s\":10,\"status\":\"working\"},"
        "{\"title\":\"B\",\"project\":\"P2\",\"status\":\"input_required\"},"
        "{\"title\":\"C\",\"status\":\"done\"}]}";
    CHECK_EQ(parse_str(line, &msg), PET_LINK_OK);
    CHECK_EQ(msg.kind, PET_EV_TASK_START);
    CHECK_EQ(msg.has_card, 0); /* 无顶层单卡字段 */
    CHECK_EQ(msg.card_count, 3);

    CHECK_STR(msg.cards[0].title, "A");
    CHECK_STR(msg.cards[0].project, "P1");
    CHECK_STR(msg.cards[0].agent, "DSH");
    CHECK_EQ(msg.cards[0].elapsed_s, 10);
    CHECK_EQ(msg.cards[0].state, PET_STATE_WORKING);

    CHECK_STR(msg.cards[1].title, "B");
    CHECK_STR(msg.cards[1].project, "P2");
    CHECK_STR(msg.cards[1].agent, "");
    CHECK_EQ(msg.cards[1].elapsed_s, 0); /* 缺省 0 */
    CHECK_EQ(msg.cards[1].state, PET_STATE_INPUT_REQUIRED);

    CHECK_STR(msg.cards[2].title, "C");
    CHECK_STR(msg.cards[2].project, ""); /* 可空 */
    CHECK_EQ(msg.cards[2].state, PET_STATE_DONE);

    /* 空字符串 title 合法（设备渲染 (untitled)） */
    CHECK_EQ(parse_str(
                 "{\"version\":1,\"event\":\"agent.idle\",\"tasks\":[{\"title\":\"\"}]}",
                 &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.card_count, 1);
    CHECK_STR(msg.cards[0].title, "");
    CHECK_EQ(msg.cards[0].state, PET_STATE_WORKING);
}

/* LK-06：tasks [] → card_count=0（清空栈；不产生单卡）。 */
static void case_lk06_tasks_empty(void)
{
    pet_link_msg_t msg;
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.done\",\"tasks\":[]}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.has_card, 0);
    CHECK_EQ(msg.card_count, 0);

    /* 空栈 + 顶层单卡字段：两者互不影响 */
    CHECK_EQ(parse_str(
                 "{\"version\":1,\"event\":\"task.done\","
                 "\"title\":\"Top\",\"tasks\":[]}",
                 &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.has_card, 1);
    CHECK_STR(msg.card.title, "Top");
    CHECK_EQ(msg.card_count, 0);
}

/* LK-07：tasks 多于 3 项 → 只取前 3（top-first，截断）。 */
static void case_lk07_tasks_capped_three(void)
{
    pet_link_msg_t msg;
    const char *line =
        "{\"version\":1,\"event\":\"task.start\",\"tasks\":["
        "{\"title\":\"A\"},{\"title\":\"B\"},{\"title\":\"C\"},"
        "{\"title\":\"D\"},{\"title\":\"E\"}]}";
    CHECK_EQ(parse_str(line, &msg), PET_LINK_OK);
    CHECK_EQ(msg.card_count, 3);
    CHECK_STR(msg.cards[0].title, "A");
    CHECK_STR(msg.cards[1].title, "B");
    CHECK_STR(msg.cards[2].title, "C");
}

/* LK-08：tasks 项 status → 状态映射（working/input_required/done/failed/未知→working）。 */
static void case_lk08_tasks_status_map(void)
{
    pet_link_msg_t msg;
    const char *line =
        "{\"version\":1,\"event\":\"task.start\",\"tasks\":["
        "{\"title\":\"F\",\"status\":\"failed\"},"
        "{\"title\":\"G\",\"status\":\"weird\"},"
        "{\"title\":\"H\"}]}";
    CHECK_EQ(parse_str(line, &msg), PET_LINK_OK);
    CHECK_EQ(msg.card_count, 3);
    CHECK_EQ(msg.cards[0].state, PET_STATE_FAILED);
    CHECK_EQ(msg.cards[1].state, PET_STATE_WORKING); /* 未知 → working */
    CHECK_EQ(msg.cards[2].state, PET_STATE_WORKING); /* 缺省 → working */

    /* 显式 working */
    CHECK_EQ(parse_str(
                 "{\"version\":1,\"event\":\"task.start\","
                 "\"tasks\":[{\"title\":\"W\",\"status\":\"working\"}]}",
                 &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.card_count, 1);
    CHECK_EQ(msg.cards[0].state, PET_STATE_WORKING);
}

/* LK-09：tasks 与顶层单卡字段并存时各自独立填充。 */
static void case_lk09_tasks_plus_top_card(void)
{
    pet_link_msg_t msg;
    const char *line =
        "{\"version\":1,\"event\":\"agent.input_required\","
        "\"title\":\"Top\",\"project\":\"PT\",\"agent\":\"DSH\",\"elapsed_s\":7,"
        "\"tasks\":[{\"title\":\"A\"},{\"title\":\"B\"}]}";
    CHECK_EQ(parse_str(line, &msg), PET_LINK_OK);
    CHECK_EQ(msg.has_card, 1);
    CHECK_STR(msg.card.title, "Top");
    CHECK_STR(msg.card.project, "PT");
    CHECK_STR(msg.card.agent, "DSH");
    CHECK_EQ(msg.card.elapsed_s, 7);
    CHECK_EQ(msg.card.state, PET_STATE_INPUT_REQUIRED);
    CHECK_EQ(msg.card_count, 2);
    CHECK_STR(msg.cards[0].title, "A");
    CHECK_STR(msg.cards[1].title, "B");
}

/* LK-10：字段截断：title/project 63 字节 + NUL，agent 7 字节 + NUL。 */
static void case_lk10_field_truncation(void)
{
    char line[1024];
    char body[200];
    pet_link_msg_t msg;

    /* title 63 字节：边界内，原样保留 */
    memset(body, 'T', 63);
    body[63] = '\0';
    snprintf(line, sizeof line,
             "{\"version\":1,\"event\":\"task.start\",\"title\":\"%s\"}", body);
    CHECK_EQ(parse_str(line, &msg), PET_LINK_OK);
    CHECK_EQ((int)strlen(msg.card.title), 63);
    CHECK_STR(msg.card.title, body);

    /* title 64 字节：截断到 63 字节 */
    memset(body, 'T', 64);
    body[64] = '\0';
    snprintf(line, sizeof line,
             "{\"version\":1,\"event\":\"task.start\",\"title\":\"%s\"}", body);
    CHECK_EQ(parse_str(line, &msg), PET_LINK_OK);
    CHECK_EQ((int)strlen(msg.card.title), 63);

    /* project 70 字节 → 63 */
    memset(body, 'P', 70);
    body[70] = '\0';
    snprintf(line, sizeof line,
             "{\"version\":1,\"event\":\"task.start\",\"project\":\"%s\"}", body);
    CHECK_EQ(parse_str(line, &msg), PET_LINK_OK);
    CHECK_EQ((int)strlen(msg.card.project), 63);

    /* agent 7 字节：边界内 */
    memset(body, 'D', 7);
    body[7] = '\0';
    snprintf(line, sizeof line,
             "{\"version\":1,\"event\":\"task.start\",\"agent\":\"%s\"}", body);
    CHECK_EQ(parse_str(line, &msg), PET_LINK_OK);
    CHECK_STR(msg.card.agent, "DDDDDDD");

    /* agent 8 字节 → 7 */
    memset(body, 'D', 8);
    body[8] = '\0';
    snprintf(line, sizeof line,
             "{\"version\":1,\"event\":\"task.start\",\"agent\":\"%s\"}", body);
    CHECK_EQ(parse_str(line, &msg), PET_LINK_OK);
    CHECK_STR(msg.card.agent, "DDDDDDD");
    CHECK_EQ((int)strlen(msg.card.agent), 7);

    /* tasks 项内同样截断（title 70 → 63） */
    memset(body, 'Q', 70);
    body[70] = '\0';
    snprintf(line, sizeof line,
             "{\"version\":1,\"event\":\"task.start\",\"tasks\":[{\"title\":\"%s\"}]}",
             body);
    CHECK_EQ(parse_str(line, &msg), PET_LINK_OK);
    CHECK_EQ(msg.card_count, 1);
    CHECK_EQ((int)strlen(msg.cards[0].title), 63);

    /* 截断按“字节”而非“字符”（契约 §2 写“>63 字符”；实现为 63 字节）：
     * 61 个 ASCII + 1 个 3 字节 CJK = 64 字节 → 截到 63 字节，CJK 末字节被切掉。 */
    {
        char line2[256];
        char body2[80];
        memset(body2, 'A', 61);
        memcpy(body2 + 61, "\xE5\xAE\xA0", 3); /* U+5BA0（UTF-8 3 字节） */
        body2[64] = '\0';
        snprintf(line2, sizeof line2,
                 "{\"version\":1,\"event\":\"task.start\",\"title\":\"%s\"}",
                 body2);
        CHECK_EQ(parse_str(line2, &msg), PET_LINK_OK);
        CHECK_EQ((int)strlen(msg.card.title), 63);
        CHECK_EQ((unsigned char)msg.card.title[62], 0xAE); /* 只保留前 2 字节 */
    }
}

/* LK-11：linebuf + pet_link 集成（字节流 → READY → 解析）。 */
static void case_lk11_linebuf_link_integration(void)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);
    int ev = 0;
    const char *l1 =
        "{\"version\":1,\"event\":\"task.start\",\"title\":\"Demo\","
        "\"project\":\"P\",\"agent\":\"DSH\",\"elapsed_s\":5}";
    CHECK_EQ(feed_whole(&lb, l1, &ev), PET_LINE_NONE); /* 无 '\n'：仍未交付 */
    CHECK_EQ(ev, 0);
    CHECK_EQ(pet_linebuf_feed(&lb, '\n'), PET_LINE_READY);
    pet_link_msg_t msg;
    CHECK_EQ(pet_link_parse(lb.buf, lb.len, &msg), PET_LINK_OK);
    CHECK_EQ(msg.kind, PET_EV_TASK_START);
    CHECK_EQ(msg.has_card, 1);
    CHECK_STR(msg.card.title, "Demo");
    CHECK_STR(msg.card.agent, "DSH");
    CHECK_EQ(msg.card.elapsed_s, 5);

    /* 连续第二行（CRLF）：linebuf 自动复位，不串上一行 */
    const char *l2 =
        "{\"version\":1,\"event\":\"task.done\",\"tasks\":[{\"title\":\"Done\"}]}";
    CHECK_EQ(feed_whole(&lb, l2, NULL), PET_LINE_NONE);
    CHECK_EQ(pet_linebuf_feed(&lb, '\r'), PET_LINE_NONE);
    CHECK_EQ(pet_linebuf_feed(&lb, '\n'), PET_LINE_READY);
    CHECK_EQ(pet_link_parse(lb.buf, lb.len, &msg), PET_LINK_OK);
    CHECK_EQ(msg.kind, PET_EV_TASK_DONE);
    CHECK_EQ(msg.has_card, 0);
    CHECK_EQ(msg.card_count, 1);
    CHECK_STR(msg.cards[0].title, "Done");

    /* 恰好 PET_LINE_MAX 字节的合法行：linebuf 不丢，pet_link 收（len==512 合法） */
    pet_linebuf_init(&lb);
    char big[PET_LINE_MAX + 1];
    int n = snprintf(big, sizeof big,
                     "{\"version\":1,\"event\":\"task.start\",\"title\":\"");
    memset(big + n, 'A', (size_t)PET_LINE_MAX - (size_t)n - 2);
    big[PET_LINE_MAX - 2] = '"';
    big[PET_LINE_MAX - 1] = '}';
    big[PET_LINE_MAX] = '\0';
    CHECK_EQ((int)strlen(big), PET_LINE_MAX);
    CHECK_EQ(feed_whole(&lb, big, &ev), PET_LINE_NONE);
    CHECK_EQ(pet_linebuf_feed(&lb, '\n'), PET_LINE_READY);
    CHECK_EQ((int)lb.len, PET_LINE_MAX);
    CHECK_EQ(pet_link_parse(lb.buf, lb.len, &msg), PET_LINK_OK);
    CHECK_EQ(msg.has_card, 1);
    CHECK_EQ((int)strlen(msg.card.title), 63);

    /* 513 字节行：linebuf 丢弃（DROPPED），不会交给解析器 */
    pet_linebuf_init(&lb);
    for (int i = 0; i < PET_LINE_MAX; i++) {
        (void)pet_linebuf_feed(&lb, 'A');
    }
    CHECK_EQ(pet_linebuf_feed(&lb, 'A'), PET_LINE_NONE);
    CHECK_EQ(pet_linebuf_feed(&lb, '\n'), PET_LINE_DROPPED);
}
/* LK-12：`has_tasks` 语义（task-7 收口回归，2026-10-31 固件侧新增字段）：
 * 行内出现 `tasks` **数组**（含空数组 `[]`）→ true，供上层整栈替换/清空；
 * 无 `tasks` 键、或 `tasks` 非数组（契约 §4：忽略该键）→ false。 */
static void case_lk12_has_tasks_presence(void)
{
    pet_link_msg_t msg;

    /* 空数组：必须与“无 tasks 键”可区分（上层据此清空旧卡栈，C05 §4） */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"tasks\":[]}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.has_tasks, 1);
    CHECK_EQ(msg.card_count, 0);
    CHECK_EQ(msg.has_card, 0);

    /* 3 项 tasks：整栈替换信号 */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                       "{\"title\":\"A\"},{\"title\":\"B\"},{\"title\":\"C\"}]}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.has_tasks, 1);
    CHECK_EQ(msg.card_count, 3);

    /* 无 tasks 键：false（仅顶层单卡路径） */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"title\":\"T\"}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.has_tasks, 0);
    CHECK_EQ(msg.has_card, 1);

    /* tasks 非数组：忽略该键 → false（契约 §4），顶层单卡仍生效 */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"title\":\"T\","
                       "\"tasks\":5}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.has_tasks, 0);
    CHECK_EQ(msg.card_count, 0);
    CHECK_EQ(msg.has_card, 1);

    /* tasks 与顶层单卡并存：两个信号独立（上层优先整栈替换） */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.done\",\"title\":\"T\","
                       "\"tasks\":[{\"title\":\"A\"}]}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.has_tasks, 1);
    CHECK_EQ(msg.has_card, 1);
    CHECK_EQ(msg.card_count, 1);
}

/* ---- 3. pet_link：错误/边界路径 ---- */

/* ER-01：非法 JSON / 未闭合 / 尾部多余 → PET_LINK_ERR_JSON。 */
static void case_er01_json_syntax(void)
{
    static const char *bad[] = {
        "",
        "   ",
        "x",
        "null",
        "[]",
        "{",
        "{\"version\":1,\"event\":\"task.start\"",
        "{\"version\":1,\"event\":\"task.start\"}x",
        "{\"version\":1,\"event\":\"task.start\"} {}",
        "{\"version\":1,\"event\":\"task.start\"}}",
        "{\"version\":1,\"event\":\"task.start\",}",
        "{\"version\" 1,\"event\":\"task.start\"}",
        "{\"version\":1 \"event\":\"task.start\"}",
        "{version:1}",
        "{\"version\":1,\"event\":\"task.start\"}\nX",
    };
    const int n = (int)(sizeof bad / sizeof bad[0]);
    for (int i = 0; i < n; i++) {
        pet_link_msg_t msg;
        CHECK_EQ(parse_str(bad[i], &msg), PET_LINK_ERR_JSON);
    }
}

/* ER-02：version 缺失/≠1 → PET_LINK_ERR_VERSION；1.5 实现走 JSON 分支。 */
static void case_er02_version(void)
{
    pet_link_msg_t msg;
    CHECK_EQ(parse_str("{\"event\":\"task.start\"}", &msg),
             PET_LINK_ERR_VERSION);
    CHECK_EQ(parse_str("{\"version\":0,\"event\":\"task.start\"}", &msg),
             PET_LINK_ERR_VERSION);
    CHECK_EQ(parse_str("{\"version\":2,\"event\":\"task.start\"}", &msg),
             PET_LINK_ERR_VERSION);
    CHECK_EQ(parse_str("{\"version\":\"1\",\"event\":\"task.start\"}", &msg),
             PET_LINK_ERR_VERSION);
    CHECK_EQ(parse_str("{\"version\":-1,\"event\":\"task.start\"}", &msg),
             PET_LINK_ERR_VERSION);
    CHECK_EQ(parse_str("{\"version\":true,\"event\":\"task.start\"}", &msg),
             PET_LINK_ERR_VERSION);
    CHECK_EQ(parse_str("{\"version\":null,\"event\":\"task.start\"}", &msg),
             PET_LINK_ERR_VERSION);
    /* 非整数（1.5）：parse_uint 只吃 "1"，"." 成为尾部多余 → JSON（实现行为） */
    CHECK_EQ(parse_str("{\"version\":1.5,\"event\":\"task.start\"}", &msg),
             PET_LINK_ERR_JSON);

    /* 校验顺序（实现）：先 version、再 event；字段类型错误在解析期即返回 TYPE */
    CHECK_EQ(parse_str("{}", &msg), PET_LINK_ERR_VERSION);
    CHECK_EQ(parse_str("{\"version\":2,\"event\":\"no.such\"}", &msg),
             PET_LINK_ERR_VERSION);
    CHECK_EQ(parse_str("{\"version\":2,\"event\":\"task.start\",\"title\":42}",
                       &msg),
             PET_LINK_ERR_TYPE);
    CHECK_EQ(parse_str("{\"version\":1,\"title\":42}", &msg), PET_LINK_ERR_TYPE);
}

/* ER-03：event 缺失 → EVENT；非字符串 → TYPE；未知 → UNKNOWN_EVENT。 */
static void case_er03_event(void)
{
    pet_link_msg_t msg;
    CHECK_EQ(parse_str("{\"version\":1}", &msg), PET_LINK_ERR_EVENT);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":42}", &msg), PET_LINK_ERR_TYPE);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":true}", &msg),
             PET_LINK_ERR_TYPE);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start2\"}", &msg),
             PET_LINK_ERR_UNKNOWN_EVENT);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"\"}", &msg),
             PET_LINK_ERR_UNKNOWN_EVENT);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"TASK.START\"}", &msg),
             PET_LINK_ERR_UNKNOWN_EVENT);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"agent.blocked2\"}", &msg),
             PET_LINK_ERR_UNKNOWN_EVENT);
}

/* ER-04：顶层字段类型不符 → 整行丢弃（TYPE，或实现中的 JSON 分支）。 */
static void case_er04_top_field_types(void)
{
    pet_link_msg_t msg;
    /* title/project/agent 非字符串 → TYPE */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"title\":42}",
                       &msg),
             PET_LINK_ERR_TYPE);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"title\":true}",
                       &msg),
             PET_LINK_ERR_TYPE);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"title\":[]}",
                       &msg),
             PET_LINK_ERR_TYPE);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"title\":{}}",
                       &msg),
             PET_LINK_ERR_TYPE);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"project\":42}",
                       &msg),
             PET_LINK_ERR_TYPE);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"agent\":42}",
                       &msg),
             PET_LINK_ERR_TYPE);

    /* elapsed_s 负数/字符串/bool → TYPE */
    CHECK_EQ(parse_str(
                 "{\"version\":1,\"event\":\"task.start\",\"elapsed_s\":-1}",
                 &msg),
             PET_LINK_ERR_TYPE);
    CHECK_EQ(parse_str(
                 "{\"version\":1,\"event\":\"task.start\",\"elapsed_s\":\"5\"}",
                 &msg),
             PET_LINK_ERR_TYPE);
    CHECK_EQ(parse_str(
                 "{\"version\":1,\"event\":\"task.start\",\"elapsed_s\":true}",
                 &msg),
             PET_LINK_ERR_TYPE);
    /* 实现上限：parse_uint 拒绝 >1e9（契约未写上限） */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\","
                       "\"elapsed_s\":1000000000}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\","
                       "\"elapsed_s\":1000000001}",
                       &msg),
             PET_LINK_ERR_TYPE);
    /* 非整数 1.5：parse_uint 停在 '.' → 尾部多余 → JSON（实现行为，契约写“非整数→类型错误”） */
    CHECK_EQ(parse_str(
                 "{\"version\":1,\"event\":\"task.start\",\"elapsed_s\":1.5}",
                 &msg),
             PET_LINK_ERR_JSON);

    /* 裸控制字符（0x01）在字符串内 → 非法字符串 → TYPE */
    {
        static const char ctrl_line[] =
            "{\"version\":1,\"event\":\"task.start\",\"title\":\"a\001b\"}";
        CHECK_EQ(parse_str(ctrl_line, &msg), PET_LINK_ERR_TYPE);
    }
}

/* ER-05：tasks 非数组 → 忽略该键（单卡字段仍生效、不算错误）。 */
static void case_er05_tasks_non_array(void)
{
    static const char *lines[] = {
        "{\"version\":1,\"event\":\"task.start\",\"title\":\"T\",\"tasks\":42}",
        "{\"version\":1,\"event\":\"task.start\",\"title\":\"T\",\"tasks\":\"x\"}",
        "{\"version\":1,\"event\":\"task.start\",\"title\":\"T\",\"tasks\":null}",
        "{\"version\":1,\"event\":\"task.start\",\"title\":\"T\",\"tasks\":true}",
        "{\"version\":1,\"event\":\"task.start\",\"title\":\"T\",\"tasks\":{\"a\":1}}",
    };
    const int n = (int)(sizeof lines / sizeof lines[0]);
    for (int i = 0; i < n; i++) {
        pet_link_msg_t msg;
        CHECK_EQ(parse_str(lines[i], &msg), PET_LINK_OK);
        CHECK_EQ(msg.card_count, 0);
        CHECK_EQ(msg.has_card, 1);
        CHECK_STR(msg.card.title, "T");
    }
}

/* ER-06：tasks 数组语法坏 → JSON。 */
static void case_er06_tasks_syntax(void)
{
    static const char *bad[] = {
        "{\"version\":1,\"event\":\"task.start\",\"tasks\":[",
        "{\"version\":1,\"event\":\"task.start\",\"tasks\":[{\"title\"}]}",
        "{\"version\":1,\"event\":\"task.start\",\"tasks\":[{\"title\":\"A\"},]}",
        "{\"version\":1,\"event\":\"task.start\",\"tasks\":[{\"title\":\"A\"}"
        " {\"title\":\"B\"}]}",
        "{\"version\":1,\"event\":\"task.start\",\"tasks\":[{\"title\":\"A\"}",
        "{\"version\":1,\"event\":\"task.start\",\"tasks\":[{\"title\":}]}",
        "{\"version\":1,\"event\":\"task.start\",\"tasks\":[{\"title\":\"A\"}]",
    };
    const int n = (int)(sizeof bad / sizeof bad[0]);
    for (int i = 0; i < n; i++) {
        pet_link_msg_t msg;
        CHECK_EQ(parse_str(bad[i], &msg), PET_LINK_ERR_JSON);
    }
}

/* ER-07：tasks 项类型不符 → 忽略该项（其它项保留）；非对象项跳过。 */
static void case_er07_tasks_bad_items_ignored(void)
{
    pet_link_msg_t msg;

    /* 第 1 项 title 非字符串 → 该项忽略；第 2 项保留 */
    CHECK_EQ(parse_str(
                 "{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                 "{\"title\":42},{\"title\":\"B\"}]}",
                 &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.card_count, 1);
    CHECK_STR(msg.cards[0].title, "B");

    /* 全部项类型不符 → 0 卡，但整行仍然 OK */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                       "{\"title\":42},{\"title\":42}]}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.card_count, 0);

    /* elapsed_s / status / agent 类型不符 → 该项忽略 */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                       "{\"title\":\"A\",\"elapsed_s\":\"x\"}]}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.card_count, 0);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                       "{\"title\":\"A\",\"status\":42}]}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.card_count, 0);
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                       "{\"title\":\"A\",\"agent\":42}]}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.card_count, 0);

    /* 非对象项跳过：1 / true / null / "x" 之后的对象项仍保留 */
    CHECK_EQ(parse_str(
                 "{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                 "1,true,null,\"x\",{\"title\":\"B\"}]}",
                 &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.card_count, 1);
    CHECK_STR(msg.cards[0].title, "B");

    /* 项内未知键（含嵌套对象/数组）跳过 */
    CHECK_EQ(parse_str(
                 "{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                 "{\"title\":\"A\",\"extra\":{\"n\":[1,{\"z\":2}]}}]}",
                 &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.card_count, 1);
    CHECK_STR(msg.cards[0].title, "A");

    /* 缺 title 的项：实现接受该项（契约 §4 写“必填”，见 RED-GREEN-M5A.md 差异） */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                       "{\"project\":\"P\"}]}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.card_count, 1);
    CHECK_STR(msg.cards[0].title, "");
    CHECK_STR(msg.cards[0].project, "P");

    /* 空对象项同样接受（title 缺省空串） */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"tasks\":[{}]}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.card_count, 1);
    CHECK_STR(msg.cards[0].title, "");

    /* 项内 elapsed_s 非整数 1.5 → 整行 JSON（实现行为，同顶层） */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                       "{\"title\":\"A\",\"elapsed_s\":1.5}]}",
                       &msg),
             PET_LINK_ERR_JSON);
}
/* ER-08：超长（> PET_LINE_MAX）→ ERR_TOO_LONG（优先于 JSON 检查）。 */
static void case_er08_too_long(void)
{
    pet_link_msg_t msg;

    /* 600 字节垃圾：长度检查先返回 TOO_LONG，不做 JSON 解析 */
    {
        char junk[601];
        memset(junk, 'x', 600);
        junk[600] = '\0';
        CHECK_EQ(pet_link_parse(junk, 600, &msg), PET_LINK_ERR_TOO_LONG);
    }

    /* 合法行恰好 512 字节 → OK */
    {
        char line[PET_LINE_MAX + 2];
        int n = snprintf(line, sizeof line,
                         "{\"version\":1,\"event\":\"task.start\",\"title\":\"");
        memset(line + n, 'A', (size_t)PET_LINE_MAX - (size_t)n - 2);
        line[PET_LINE_MAX - 2] = '"';
        line[PET_LINE_MAX - 1] = '}';
        line[PET_LINE_MAX] = '\0';
        CHECK_EQ((int)strlen(line), PET_LINE_MAX);
        CHECK_EQ(pet_link_parse(line, PET_LINE_MAX, &msg), PET_LINK_OK);

        /* 同一行再加 1 字节（尾部空格）→ 513 → TOO_LONG */
        line[PET_LINE_MAX] = ' ';
        line[PET_LINE_MAX + 1] = '\0';
        CHECK_EQ((int)strlen(line), PET_LINE_MAX + 1);
        CHECK_EQ(pet_link_parse(line, PET_LINE_MAX + 1, &msg),
                 PET_LINK_ERR_TOO_LONG);
    }
}

/* ER-09：未知键（嵌套对象/数组/字符串/数字/bool/null）跳过；深度上限 8。 */
static void case_er09_unknown_keys(void)
{
    pet_link_msg_t msg;
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"device.heartbeat\","
                       "\"meta\":{\"a\":[1,2,{\"b\":\"c\"}],\"d\":true},"
                       "\"nullk\":null,\"num\":-1.5,\"s\":\"esc\\\\n\","
                       "\"boolk\":false}",
                       &msg),
             PET_LINK_OK);
    CHECK_EQ(msg.kind, PET_EV_ANNOUNCEMENT);
    CHECK_EQ(msg.has_card, 0);
    CHECK_EQ(msg.card_count, 0);

    /* 未知键的嵌套深度：实现上限 SKIP_MAX_DEPTH=8（契约未写深度限制） */
    {
        char buf[512];
        int n = snprintf(buf, sizeof buf,
                         "{\"version\":1,\"event\":\"device.heartbeat\",\"x\":");
        for (int d = 0; d < 8; d++) {
            n += snprintf(buf + n, sizeof buf - (size_t)n, "{\"a\":");
        }
        n += snprintf(buf + n, sizeof buf - (size_t)n, "1");
        for (int d = 0; d < 8; d++) {
            n += snprintf(buf + n, sizeof buf - (size_t)n, "}");
        }
        n += snprintf(buf + n, sizeof buf - (size_t)n, "}");
        CHECK(n < (int)sizeof buf);
        CHECK_EQ(parse_str(buf, &msg), PET_LINK_OK);
    }
    {
        char buf[512];
        int n = snprintf(buf, sizeof buf,
                         "{\"version\":1,\"event\":\"device.heartbeat\",\"x\":");
        for (int d = 0; d < 9; d++) {
            n += snprintf(buf + n, sizeof buf - (size_t)n, "{\"a\":");
        }
        n += snprintf(buf + n, sizeof buf - (size_t)n, "1");
        for (int d = 0; d < 9; d++) {
            n += snprintf(buf + n, sizeof buf - (size_t)n, "}");
        }
        n += snprintf(buf + n, sizeof buf - (size_t)n, "}");
        CHECK(n < (int)sizeof buf);
        CHECK_EQ(parse_str(buf, &msg), PET_LINK_ERR_JSON);
    }

    /* 未知键的字符串值使用 \u 转义：实现解析失败 → JSON（契约：未知键忽略） */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\","
                       "\"junk\":\"\\u0041\"}",
                       &msg),
             PET_LINK_ERR_JSON);
}

/* ER-10：\u 转义不支持（顶层 → TYPE；tasks 项 → 整行 JSON）；合法转义保留。 */
static void case_er10_unicode_and_escapes(void)
{
    pet_link_msg_t msg;

    /* 顶层 title 含 \u → parse_string 失败 → TYPE */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\","
                       "\"title\":\"a\\u0041b\"}",
                       &msg),
             PET_LINK_ERR_TYPE);
    /* event 含 \u → TYPE */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.\\u0073tart\"}", &msg),
             PET_LINK_ERR_TYPE);
    /* tasks 项 title 含 \u：此处回退 skip_value 同样失败 → 整行 JSON（实现行为） */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\",\"tasks\":["
                       "{\"title\":\"a\\u0041b\"}]}",
                       &msg),
             PET_LINK_ERR_JSON);

    /* 支持的标准转义：\t \\ \/ \" → 还原为对应字符 */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\","
                       "\"title\":\"a\\tb\\\\c\\/d\\\"e\"}",
                       &msg),
             PET_LINK_OK);
    CHECK_STR(msg.card.title, "a\tb\\c/d\"e");

    /* 未知转义（\x）→ 顶层 TYPE */
    CHECK_EQ(parse_str("{\"version\":1,\"event\":\"task.start\","
                       "\"title\":\"a\\xb\"}",
                       &msg),
             PET_LINK_ERR_TYPE);
}

/* ER-11：公开 API 防御性行为 + 显式 len 语义。 */
static void case_er11_api_and_len(void)
{
    pet_link_msg_t msg;
    CHECK_EQ(pet_link_parse(NULL, 0, &msg), PET_LINK_ERR_JSON);
    CHECK_EQ(pet_link_parse("", 0, &msg), PET_LINK_ERR_JSON);
    CHECK_EQ(pet_link_parse("{\"version\":1}", 13, NULL), PET_LINK_ERR_JSON);

    /* 显式 len：只解析前 len 字节，尾部垃圾不参与 */
    const char *base = "{\"version\":1,\"event\":\"device.heartbeat\"}";
    char line[128];
    snprintf(line, sizeof line, "%sTRAILING", base);
    CHECK_EQ(pet_link_parse(line, strlen(base), &msg), PET_LINK_OK);
    CHECK_EQ(msg.kind, PET_EV_ANNOUNCEMENT);
    CHECK_EQ(pet_link_parse(line, strlen(line), &msg), PET_LINK_ERR_JSON);
}

/* ---- 运行器 ---- */

typedef struct {
    const char *name;
    void (*fn)(void);
} test_case_t;

int main(void)
{
    static const test_case_t cases[] = {
        {"LB-01-single-line-bytewise", case_lb01_single_line_bytewise},
        {"LB-02-crlf-strip", case_lb02_crlf_strip},
        {"LB-03-empty-lines", case_lb03_empty_lines},
        {"LB-04-exact-max-line", case_lb04_exact_max_line},
        {"LB-05-overflow-dropped-recover", case_lb05_overflow_dropped_and_recover},
        {"LB-06-deliver-pending-reset", case_lb06_deliver_pending_reset},
        {"LK-01-event-kind-map", case_lk01_event_kind_map},
        {"LK-02-blocked-is-task-error", case_lk02_blocked_is_task_error},
        {"LK-03-top-card-fields", case_lk03_top_card_fields},
        {"LK-04-top-card-single-field", case_lk04_top_card_single_field_triggers},
        {"LK-05-tasks-three-cards", case_lk05_tasks_three_cards},
        {"LK-06-tasks-empty", case_lk06_tasks_empty},
        {"LK-07-tasks-capped-three", case_lk07_tasks_capped_three},
        {"LK-08-tasks-status-map", case_lk08_tasks_status_map},
        {"LK-09-tasks-plus-top-card", case_lk09_tasks_plus_top_card},
        {"LK-10-field-truncation", case_lk10_field_truncation},
        {"LK-11-linebuf-link-integration", case_lk11_linebuf_link_integration},
        {"LK-12-tasks-presence", case_lk12_has_tasks_presence},
        {"ER-01-json-syntax", case_er01_json_syntax},
        {"ER-02-version", case_er02_version},
        {"ER-03-event", case_er03_event},
        {"ER-04-top-field-types", case_er04_top_field_types},
        {"ER-05-tasks-non-array", case_er05_tasks_non_array},
        {"ER-06-tasks-syntax", case_er06_tasks_syntax},
        {"ER-07-tasks-bad-items-ignored", case_er07_tasks_bad_items_ignored},
        {"ER-08-too-long", case_er08_too_long},
        {"ER-09-unknown-keys", case_er09_unknown_keys},
        {"ER-10-unicode-and-escapes", case_er10_unicode_and_escapes},
        {"ER-11-api-and-len", case_er11_api_and_len},
    };

    int failed_cases = 0;
    int total_asserts = 0;
    const int n = (int)(sizeof cases / sizeof cases[0]);
    for (int i = 0; i < n; i++) {
        const int before = g_failures;
        g_asserts = 0;
        cases[i].fn();
        total_asserts += g_asserts;
        if (g_failures > before) {
            printf("FAIL %s (%d/%d asserts failed)\n", cases[i].name,
                   g_failures - before, g_asserts);
            failed_cases++;
        } else {
            printf("PASS %s (%d asserts)\n", cases[i].name, g_asserts);
        }
    }
    printf("SUMMARY m5a_event: cases=%d passed=%d failed=%d asserts=%d\n", n,
           n - failed_cases, failed_cases, total_asserts);
    return failed_cases == 0 ? 0 : 1;
}
