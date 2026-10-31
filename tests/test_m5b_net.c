/* M5b-1 host 单测（T 类，新逻辑 RED→GREEN）：C08 §1.1 跨通道去重 + C08 §1 行切分复用。
 *
 * 被测（真实现）：
 *   - firmware/components/pet_net/pet_net_dedupe.c（纯逻辑：16 条指纹环 + 1000 ms 窗口）；
 *   - firmware/components/pet_ui/pet_linebuf.c（**只链接复用，未改动**；C08 §1 行长/CRLF 语义）。
 * 契约：docs/contracts/C08-wifi-channel.md（2026-10-31 冻结）。
 * 口径：只用公开头文件 API；无 stub、无 sleep、不碰串口/网络/硬件/真实时钟；
 *       now_ms 由测试注入（含 32 位回绕边界）。只写 tests/。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pet_linebuf.h"
#include "pet_net_dedupe.h"

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

#define CHECK_U64(actual, expected)                                          \
    do {                                                                     \
        unsigned long long a_ = (unsigned long long)(actual);                \
        unsigned long long e_ = (unsigned long long)(expected);              \
        g_asserts++;                                                         \
        if (a_ != e_) {                                                      \
            printf("  ASSERT FAIL %s:%d: %s (got 0x%016llX, want 0x%016llX)\n", \
                   __FILE__, __LINE__, #actual, a_, e_);                     \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

/* 夹具：按 C 字符串喂一条原始行。 */
static pet_net_dedupe_result_t feed_str(pet_net_dedupe_t *d, uint32_t now_ms,
                                        const char *s)
{
    return pet_net_dedupe_feed(d, now_ms, s, strlen(s));
}

/* 整串喂给行缓冲；把每个 READY 行拷进 out（最多 max 行），返回行数；
 * dropped 统计 PET_LINE_DROPPED 次数。 */
static int split_lines(const char *s, char out[][PET_LINE_MAX + 1], int max,
                       int *dropped)
{
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);
    int n = 0;
    int drop = 0;
    for (const char *p = s; *p != '\0'; p++) {
        const pet_line_feed_t r = pet_linebuf_feed(&lb, *p);
        if (r == PET_LINE_READY) {
            if (n < max) {
                memcpy(out[n], lb.buf, lb.len + 1);
                n++;
            }
        } else if (r == PET_LINE_DROPPED) {
            drop++;
        }
    }
    if (dropped != NULL) {
        *dropped = drop;
    }
    return n;
}

/* ---- 1. DED：指纹（FNV-1a 64 + 长度） ---- */

static void case_ded01_fingerprint_vectors(void)
{
    /* FNV-1a 64 标准向量（独立参考实现计算） */
    CHECK_U64(pet_net_dedupe_fingerprint("", 0), 0xCBF29CE484222325ULL);
    CHECK_U64(pet_net_dedupe_fingerprint("a", 1), 0xAF63DC4C8601EC8CULL);
    CHECK_U64(pet_net_dedupe_fingerprint("foobar", 6), 0x85944171F73967E8ULL);
    CHECK_U64(pet_net_dedupe_fingerprint("task.start", 10), 0x8CEFFAB9E9210006ULL);
    CHECK_U64(pet_net_dedupe_fingerprint("device.heartbeat", 16),
              0x01D12DAD64C438BDULL);
    const char *json = "{\"version\":1,\"event\":\"task.start\"}";
    CHECK_U64(pet_net_dedupe_fingerprint(json, strlen(json)),
              0xB5BE9E500FEE8B5DULL);

    /* 确定性：同内容多次调用一致 */
    CHECK_U64(pet_net_dedupe_fingerprint("line-A", 6),
              pet_net_dedupe_fingerprint("line-A", 6));
    /* 显式 len：只看前 len 字节 */
    CHECK_U64(pet_net_dedupe_fingerprint("abcdef", 3),
              pet_net_dedupe_fingerprint("abc", 3));
    CHECK_U64(pet_net_dedupe_fingerprint("abc", 2),
              pet_net_dedupe_fingerprint("ab", 2));
    /* NULL 视为空串（防御，不崩） */
    CHECK_U64(pet_net_dedupe_fingerprint(NULL, 0), 0xCBF29CE484222325ULL);
    CHECK_U64(pet_net_dedupe_fingerprint(NULL, 7), 0xCBF29CE484222325ULL);
}

/* ---- 2. DED：初始化 / 首次接受 / 字段落位 ---- */

static void case_ded02_init_and_first_accept(void)
{
    pet_net_dedupe_t d;
    memset(&d, 0xA5, sizeof d); /* 脏内存 → init 必须清零 */
    pet_net_dedupe_init(&d);
    CHECK_EQ(d.next, 0);
    CHECK_EQ(d.accepted, 0);
    CHECK_EQ(d.dropped, 0);
    for (int i = 0; i < PET_NET_DEDUPE_SLOTS; i++) {
        CHECK_EQ(d.used[i], 0);
        CHECK_U64(d.fp[i], 0);
        CHECK_EQ(d.len[i], 0);
        CHECK_EQ(d.rx_ms[i], 0);
    }

    CHECK_EQ(feed_str(&d, 42, "line-A"), PET_NET_DEDUPE_ACCEPT);
    CHECK_EQ(d.accepted, 1);
    CHECK_EQ(d.dropped, 0);
    CHECK_EQ(d.next, 1);
    CHECK_EQ(d.used[0], 1);
    CHECK_U64(d.fp[0], pet_net_dedupe_fingerprint("line-A", 6));
    CHECK_EQ(d.len[0], 6);
    CHECK_EQ(d.rx_ms[0], 42);
}

/* ---- 3. DED：1000 ms 窗口边界（DROP 不刷新时间戳） ---- */

static void case_ded03_window_boundary(void)
{
    pet_net_dedupe_t d;
    pet_net_dedupe_init(&d);
    const char *a = "{\"version\":1,\"event\":\"task.start\"}";

    CHECK_EQ(feed_str(&d, 0, a), PET_NET_DEDUPE_ACCEPT);
    CHECK_EQ(feed_str(&d, 0, a), PET_NET_DEDUPE_DROP);     /* 0 ms：窗口内 */
    CHECK_EQ(feed_str(&d, 1, a), PET_NET_DEDUPE_DROP);     /* 1 ms */
    CHECK_EQ(feed_str(&d, 999, a), PET_NET_DEDUPE_DROP);   /* 999 ms */
    CHECK_EQ(feed_str(&d, 1000, a), PET_NET_DEDUPE_ACCEPT);/* 恰好 1 s：窗口外 */
    CHECK_EQ(feed_str(&d, 1500, a), PET_NET_DEDUPE_DROP);  /* 距 1000 只 500 ms */
    CHECK_EQ(feed_str(&d, 2000, a), PET_NET_DEDUPE_ACCEPT);
    CHECK_EQ(d.accepted, 3);
    CHECK_EQ(d.dropped, 4);
}

/* ---- 4. DED：不同内容互不影响 ---- */

static void case_ded04_different_lines(void)
{
    pet_net_dedupe_t d;
    pet_net_dedupe_init(&d);
    CHECK_EQ(feed_str(&d, 0, "line-A"), PET_NET_DEDUPE_ACCEPT);
    CHECK_EQ(feed_str(&d, 0, "line-B"), PET_NET_DEDUPE_ACCEPT);
    CHECK_EQ(feed_str(&d, 10, "line-A"), PET_NET_DEDUPE_DROP);
    CHECK_EQ(feed_str(&d, 20, "line-B"), PET_NET_DEDUPE_DROP);
    CHECK_EQ(feed_str(&d, 30, "line-C"), PET_NET_DEDUPE_ACCEPT);
    CHECK_EQ(feed_str(&d, 40, "line-A"), PET_NET_DEDUPE_DROP);
    CHECK_EQ(feed_str(&d, 1000, "line-B"), PET_NET_DEDUPE_ACCEPT);
    CHECK_EQ(feed_str(&d, 1001, "line-A"), PET_NET_DEDUPE_ACCEPT);
    CHECK_EQ(d.accepted, 5);
    CHECK_EQ(d.dropped, 3);
}

/* ---- 5. DED：16 槽指纹环的轮转覆盖 ---- */

static void case_ded05_ring_capacity(void)
{
    pet_net_dedupe_t d;
    pet_net_dedupe_init(&d);
    CHECK_EQ(feed_str(&d, 0, "line-A"), PET_NET_DEDUPE_ACCEPT);

    char line[16];
    for (int i = 0; i < 15; i++) {
        snprintf(line, sizeof line, "line-%02d", i);
        CHECK_EQ(feed_str(&d, 10, line), PET_NET_DEDUPE_ACCEPT);
    }
    /* 环内 16 条：A + line-00..line-14；A 仍在窗口内 → DROP */
    CHECK_EQ(feed_str(&d, 20, "line-A"), PET_NET_DEDUPE_DROP);

    /* 第 17 条不同行插入 → 覆盖最旧槽（A，slot0） */
    CHECK_EQ(feed_str(&d, 30, "line-15"), PET_NET_DEDUPE_ACCEPT);
    /* A 已被覆盖：即使距上次仅 40 ms 也 ACCEPT（环容量语义） */
    CHECK_EQ(feed_str(&d, 40, "line-A"), PET_NET_DEDUPE_ACCEPT);
    /* 仍在环内的最近一条仍按窗口判重 */
    CHECK_EQ(feed_str(&d, 50, "line-14"), PET_NET_DEDUPE_DROP);
    CHECK_EQ(d.accepted, 18);
    CHECK_EQ(d.dropped, 2);
}

/* ---- 6. DED：32 位时间回绕 ---- */

static void case_ded06_time_wrap(void)
{
    pet_net_dedupe_t d;
    pet_net_dedupe_init(&d);
    const uint32_t t0 = 0xFFFFFF00u;
    CHECK_EQ(feed_str(&d, t0, "line-A"), PET_NET_DEDUPE_ACCEPT);
    CHECK_EQ(feed_str(&d, 0x00000100u, "line-A"), PET_NET_DEDUPE_DROP);  /* +512 */
    CHECK_EQ(feed_str(&d, 0x00000400u, "line-A"), PET_NET_DEDUPE_ACCEPT);/* +1536 */
    CHECK_EQ(feed_str(&d, 0x00000500u, "line-A"), PET_NET_DEDUPE_DROP);  /* +256 */
    CHECK_EQ(d.accepted, 2);
    CHECK_EQ(d.dropped, 2);
}

/* ---- 7. DED：DROP 不刷新窗口（重复风暴压 1 个窗口） ---- */

static void case_ded07_drop_does_not_refresh(void)
{
    pet_net_dedupe_t d;
    pet_net_dedupe_init(&d);
    CHECK_EQ(feed_str(&d, 0, "line-A"), PET_NET_DEDUPE_ACCEPT);
    CHECK_EQ(feed_str(&d, 900, "line-A"), PET_NET_DEDUPE_DROP);
    /* 若 DROP 刷新了时间戳，此处距 900 仅 100 ms → DROP；实现规定 ACCEPT */
    CHECK_EQ(feed_str(&d, 1000, "line-A"), PET_NET_DEDUPE_ACCEPT);
    CHECK_EQ(d.accepted, 2);
    CHECK_EQ(d.dropped, 1);
}

/* ---- 8. DED：NULL 防御（不崩、不计统计） ---- */

static void case_ded08_null_guards(void)
{
    pet_net_dedupe_t d;
    pet_net_dedupe_init(&d);
    pet_net_dedupe_init(NULL); /* 不崩 */
    CHECK_EQ(pet_net_dedupe_feed(NULL, 0, "line-A", 6), PET_NET_DEDUPE_DROP);
    CHECK_EQ(pet_net_dedupe_feed(&d, 0, NULL, 0), PET_NET_DEDUPE_DROP);
    CHECK_EQ(pet_net_dedupe_feed(&d, 0, NULL, 5), PET_NET_DEDUPE_DROP);
    CHECK_EQ(d.accepted, 0);
    CHECK_EQ(d.dropped, 0);
    CHECK_EQ(d.next, 0);

    /* 空行（len=0）是合法原始行：首次 ACCEPT，窗口内重复 DROP */
    CHECK_EQ(pet_net_dedupe_feed(&d, 5, "", 0), PET_NET_DEDUPE_ACCEPT);
    CHECK_EQ(pet_net_dedupe_feed(&d, 6, "", 0), PET_NET_DEDUPE_DROP);
    CHECK_EQ(d.accepted, 1);
    CHECK_EQ(d.dropped, 1);
}

/* ---- 9. NET：行切分复用（C08 §1：≤512 B、`\r` 忽略、行缓冲复用 pet_linebuf） ---- */

static void case_net01_linebuf_contract_reuse(void)
{
    char lines[4][PET_LINE_MAX + 1];
    int dropped = 0;

    /* 裸 \n、CRLF、空行 */
    int n = split_lines("hello\nab\r\n\n", lines, 4, &dropped);
    CHECK_EQ(n, 3);
    CHECK_EQ(dropped, 0);
    CHECK_STR(lines[0], "hello");
    CHECK_STR(lines[1], "ab");
    CHECK_STR(lines[2], "");
    CHECK_EQ((int)strlen(lines[2]), 0);

    /* 恰好 512 B（不含 \n）→ READY，内容完整 */
    char exact[PET_LINE_MAX + 2];
    memset(exact, 'x', PET_LINE_MAX);
    exact[PET_LINE_MAX] = '\n';
    exact[PET_LINE_MAX + 1] = '\0';
    n = split_lines(exact, lines, 4, &dropped);
    CHECK_EQ(n, 1);
    CHECK_EQ(dropped, 0);
    CHECK_EQ((int)strlen(lines[0]), PET_LINE_MAX);
    CHECK_EQ(lines[0][0], 'x');
    CHECK_EQ(lines[0][PET_LINE_MAX - 1], 'x');

    /* 513 B（不含 \n）→ 整行丢弃，且可恢复 */
    char over[PET_LINE_MAX + 3];
    memset(over, 'y', PET_LINE_MAX + 1);
    over[PET_LINE_MAX + 1] = '\n';
    over[PET_LINE_MAX + 2] = '\0';
    n = split_lines(over, lines, 4, &dropped);
    CHECK_EQ(n, 0);
    CHECK_EQ(dropped, 1);

    /* 超长丢弃后下一行正常交付（行缓冲状态复位） */
    char mixed[PET_LINE_MAX + 16];
    memset(mixed, 'z', PET_LINE_MAX + 1);
    memcpy(&mixed[PET_LINE_MAX + 1], "\nok\n", 5);
    n = split_lines(mixed, lines, 4, &dropped);
    CHECK_EQ(n, 1);
    CHECK_EQ(dropped, 1);
    CHECK_STR(lines[0], "ok");
}

/* ---- 10. NET：行切分 + 去重联动（C08 §1.1 跨通道；USB/WiFi 共用同一去重器） ---- */

static void case_net02_line_split_and_across_channel(void)
{
    const char *l1 = "{\"version\":1,\"event\":\"task.start\"}";
    const char *l2 = "{\"version\":1,\"event\":\"agent.idle\"}";
    char lines[4][PET_LINE_MAX + 1];
    int dropped = 0;

    /* 第一次（如 USB）：两行一次到达 */
    char stream[256];
    snprintf(stream, sizeof stream, "%s\n%s\n", l1, l2);
    int n = split_lines(stream, lines, 4, &dropped);
    CHECK_EQ(n, 2);
    CHECK_EQ(dropped, 0);
    CHECK_STR(lines[0], l1);
    CHECK_STR(lines[1], l2);

    pet_net_dedupe_t d;
    pet_net_dedupe_init(&d);
    CHECK_EQ(pet_net_dedupe_feed(&d, 1000, lines[0], strlen(lines[0])),
             PET_NET_DEDUPE_ACCEPT);
    CHECK_EQ(pet_net_dedupe_feed(&d, 1000, lines[1], strlen(lines[1])),
             PET_NET_DEDUPE_ACCEPT);

    /* 另一通道（如 WiFi）1 s 内重发同一行 → DROP（跨通道去重） */
    CHECK_EQ(pet_net_dedupe_feed(&d, 1100, l1, strlen(l1)), PET_NET_DEDUPE_DROP);

    /* WiFi 重连后整段重放（CRLF 变体，行缓冲剥 '\r' 后内容相同）→ 全部 DROP */
    char replay[256];
    snprintf(replay, sizeof replay, "%s\r\n%s\r\n", l1, l2);
    n = split_lines(replay, lines, 4, &dropped);
    CHECK_EQ(n, 2);
    CHECK_EQ(dropped, 0);
    CHECK_STR(lines[0], l1);
    CHECK_STR(lines[1], l2);
    CHECK_EQ(pet_net_dedupe_feed(&d, 1200, lines[0], strlen(lines[0])),
             PET_NET_DEDUPE_DROP);
    CHECK_EQ(pet_net_dedupe_feed(&d, 1200, lines[1], strlen(lines[1])),
             PET_NET_DEDUPE_DROP);

    /* 新内容（与重放不同）→ ACCEPT 并进入环 */
    const char *l3 = "{\"version\":1,\"event\":\"task.done\"}";
    CHECK_EQ(pet_net_dedupe_feed(&d, 1300, l3, strlen(l3)),
             PET_NET_DEDUPE_ACCEPT);
    CHECK_EQ(d.accepted, 3);
    CHECK_EQ(d.dropped, 3);
}

/* ---- 11. NET：同一 chunk 多行顺序与长度 ---- */

static void case_net03_multi_line_order(void)
{
    char lines[4][PET_LINE_MAX + 1];
    int dropped = 0;
    int n = split_lines("b\nc\na\n", lines, 4, &dropped);
    CHECK_EQ(n, 3);
    CHECK_EQ(dropped, 0);
    CHECK_STR(lines[0], "b");
    CHECK_STR(lines[1], "c");
    CHECK_STR(lines[2], "a");

    pet_net_dedupe_t d;
    pet_net_dedupe_init(&d);
    for (int i = 0; i < n; i++) {
        CHECK_EQ(pet_net_dedupe_feed(&d, 500, lines[i], strlen(lines[i])),
                 PET_NET_DEDUPE_ACCEPT);
    }
    /* 顺序回放（同一 1 s 窗口）→ 全部重复 */
    for (int i = 0; i < n; i++) {
        CHECK_EQ(pet_net_dedupe_feed(&d, 900, lines[i], strlen(lines[i])),
                 PET_NET_DEDUPE_DROP);
    }
    CHECK_EQ(d.accepted, 3);
    CHECK_EQ(d.dropped, 3);
}

/* ---- 运行器 ---- */

typedef struct {
    const char *name;
    void (*fn)(void);
} test_case_t;

int main(void)
{
    static const test_case_t cases[] = {
        {"DED-01-fingerprint-vectors", case_ded01_fingerprint_vectors},
        {"DED-02-init-and-first-accept", case_ded02_init_and_first_accept},
        {"DED-03-window-boundary", case_ded03_window_boundary},
        {"DED-04-different-lines", case_ded04_different_lines},
        {"DED-05-ring-capacity", case_ded05_ring_capacity},
        {"DED-06-time-wrap", case_ded06_time_wrap},
        {"DED-07-drop-does-not-refresh", case_ded07_drop_does_not_refresh},
        {"DED-08-null-guards", case_ded08_null_guards},
        {"NET-01-linebuf-contract-reuse", case_net01_linebuf_contract_reuse},
        {"NET-02-line-split-and-across-channel",
         case_net02_line_split_and_across_channel},
        {"NET-03-multi-line-order", case_net03_multi_line_order},
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
    printf("SUMMARY m5b_net: cases=%d passed=%d failed=%d asserts=%d\n", n,
           n - failed_cases, failed_cases, total_asserts);
    return failed_cases == 0 ? 0 : 1;
}
