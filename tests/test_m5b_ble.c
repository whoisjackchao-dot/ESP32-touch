/* M5b-2 host 单测（T 类，新逻辑 RED→GREEN）：C09 §1.1 BLE 行重装（用例 BLE-01）。
 *
 * 被测（真实现）：
 *   - firmware/components/pet_ble/pet_ble_lines.c（纯逻辑：每连接行缓冲 + 交付/丢弃计数）；
 *   - firmware/components/pet_ui/pet_linebuf.c（**只链接复用，未改动**；≤512 B、剥 \r、
 *     超长整行丢弃的语义来自它）。
 * 契约：docs/contracts/C09-ble-channel.md（2026-11-01 冻结）§1 / §1.1、C05 行长/CRLF 语义。
 * 口径：只用公开头文件 API；无 stub（该文件本身即被测纯逻辑）、无 sleep、不碰串口/
 *       网络/BLE 硬件/NimBLE/真实时钟。只写 tests/。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pet_ble_lines.h"

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

#define CAP_MAX 16

typedef struct {
    int n; /* 交付行数（可超过 CAP_MAX，用于验证"每次 write 0..N 行"） */
    char lines[CAP_MAX][PET_LINE_MAX + 1];
    size_t lens[CAP_MAX];
} collect_t;

static void collect_cb(const char *line, size_t len, void *user)
{
    collect_t *c = (collect_t *)user;
    if (c == NULL || line == NULL) {
        return;
    }
    if (c->n < CAP_MAX) {
        size_t n = len;
        if (n > PET_LINE_MAX) {
            n = PET_LINE_MAX;
        }
        memcpy(c->lines[c->n], line, n);
        c->lines[c->n][n] = '\0';
        c->lens[c->n] = len;
    }
    c->n++;
}

/* 夹具：按 C 字符串/内存块喂一段 write。 */
static uint32_t feed_str(pet_ble_lines_t *ls, collect_t *c, const char *s)
{
    return pet_ble_lines_feed(ls, (const uint8_t *)s, strlen(s), collect_cb, c);
}

static uint32_t feed_mem(pet_ble_lines_t *ls, collect_t *c, const void *p, size_t n)
{
    return pet_ble_lines_feed(ls, (const uint8_t *)p, n, collect_cb, c);
}

/* ---- 1. 单 write 多行（一次 write 可含 0..N 行） ---- */

static void case_ble01a_single_write_multi_line(void)
{
    pet_ble_lines_t ls;
    collect_t c;
    memset(&c, 0, sizeof c);
    pet_ble_lines_init(&ls);

    const uint32_t n = feed_str(
        &ls, &c,
        "{\"version\":1,\"event\":\"task.start\"}\n"
        "{\"event\":\"task.done\"}\n"
        "{\"event\":\"device.heartbeat\"}\n");
    CHECK_EQ(n, 3);
    CHECK_EQ(c.n, 3);
    CHECK_STR(c.lines[0], "{\"version\":1,\"event\":\"task.start\"}");
    CHECK_STR(c.lines[1], "{\"event\":\"task.done\"}");
    CHECK_STR(c.lines[2], "{\"event\":\"device.heartbeat\"}");
    /* 交付长度不含 '\n'；行缓冲 NUL 结尾（pet_linebuf 语义） */
    CHECK_EQ(c.lens[0], strlen(c.lines[0]));
    CHECK_EQ(c.lens[2], strlen(c.lines[2]));
    CHECK_EQ(ls.lines_total, 3);
    CHECK_EQ(ls.lines_dropped, 0);

    /* 单 write 只有半行 → 0 行交付，下一 write 补齐 */
    CHECK_EQ(feed_str(&ls, &c, "{\"event\":"), 0);
    CHECK_EQ(c.n, 3);
    CHECK_EQ(feed_str(&ls, &c, "\"event.ack\"}\n"), 1);
    CHECK_STR(c.lines[3], "{\"event\":\"event.ack\"}");
    CHECK_EQ(ls.lines_total, 4);
}

/* ---- 2. 跨 write 半行重装 ---- */

static void case_ble01b_across_write_half_line(void)
{
    pet_ble_lines_t ls;
    collect_t c;
    memset(&c, 0, sizeof c);
    pet_ble_lines_init(&ls);

    CHECK_EQ(feed_str(&ls, &c, "{\"version\":1,\"ev"), 0);
    CHECK_EQ(feed_str(&ls, &c, "ent\":\"task.start\"}"), 0);
    CHECK_EQ(c.n, 0);
    CHECK_EQ(feed_str(&ls, &c, "\n"), 1);
    CHECK_STR(c.lines[0], "{\"version\":1,\"event\":\"task.start\"}");
    CHECK_EQ(ls.lines_total, 1);

    /* 一行的每个字节各占一次 write（等价 MTU 分片极端情况） */
    collect_t c2;
    memset(&c2, 0, sizeof c2);
    pet_ble_lines_init(&ls);
    const char *json = "{\"event\":\"x\"}";
    uint32_t got = 0;
    for (const char *p = json; *p != '\0'; p++) {
        const char ch[1] = {*p};
        got += feed_mem(&ls, &c2, ch, 1);
    }
    CHECK_EQ(got, 0);
    CHECK_EQ(feed_str(&ls, &c2, "\n"), 1);
    CHECK_STR(c2.lines[0], json);
    CHECK_EQ(c2.lens[0], strlen(json));

    /* 上一 write 的尾部半行 + 本 write 两条完整行 */
    collect_t c3;
    memset(&c3, 0, sizeof c3);
    pet_ble_lines_init(&ls);
    CHECK_EQ(feed_str(&ls, &c3, "part1"), 0);
    CHECK_EQ(feed_str(&ls, &c3, "part2\nline2\n"), 2);
    CHECK_STR(c3.lines[0], "part1part2");
    CHECK_STR(c3.lines[1], "line2");
    CHECK_EQ(ls.lines_total, 2);
}

/* ---- 3. CRLF（含 \r 与 \n 被拆到两次 write） ---- */

static void case_ble01c_crlf(void)
{
    pet_ble_lines_t ls;
    collect_t c;
    memset(&c, 0, sizeof c);
    pet_ble_lines_init(&ls);

    CHECK_EQ(feed_str(&ls, &c, "a\r\nb\r\n"), 2);
    CHECK_STR(c.lines[0], "a");
    CHECK_STR(c.lines[1], "b");
    CHECK_EQ(c.lens[0], 1);
    CHECK_EQ(ls.lines_total, 2);

    /* "\r" 在上一个 write 末尾，"\n" 在下一个 write：仍按 CRLF 剥除 */
    collect_t c2;
    memset(&c2, 0, sizeof c2);
    pet_ble_lines_init(&ls);
    CHECK_EQ(feed_str(&ls, &c2, "y\r"), 0);
    CHECK_EQ(feed_str(&ls, &c2, "\n"), 1);
    CHECK_STR(c2.lines[0], "y");
    CHECK_EQ(c2.lens[0], 1);

    /* 裸 \n 与 CRLF 混用 */
    collect_t c3;
    memset(&c3, 0, sizeof c3);
    pet_ble_lines_init(&ls);
    CHECK_EQ(feed_str(&ls, &c3, "m\r\nn\n"), 2);
    CHECK_STR(c3.lines[0], "m");
    CHECK_STR(c3.lines[1], "n");
}

/* ---- 4. 空行（裸 \n / \r\n）不交付、不计数 ---- */

static void case_ble01d_empty_lines_skipped(void)
{
    pet_ble_lines_t ls;
    collect_t c;
    memset(&c, 0, sizeof c);
    pet_ble_lines_init(&ls);

    CHECK_EQ(feed_str(&ls, &c, "\n"), 0);
    CHECK_EQ(feed_str(&ls, &c, "\n\n\n"), 0);
    CHECK_EQ(feed_str(&ls, &c, "\r\n"), 0);
    CHECK_EQ(c.n, 0);
    CHECK_EQ(ls.lines_total, 0);
    CHECK_EQ(feed_str(&ls, &c, "ok\n"), 1);
    CHECK_STR(c.lines[0], "ok");
    CHECK_EQ(ls.lines_total, 1);
}

/* ---- 5. 超长丢弃（>512 B）并可恢复；恰好 512 B 合法 ---- */

static void case_ble01e_oversize_drop_and_recover(void)
{
    pet_ble_lines_t ls;
    collect_t c;
    char big[PET_LINE_MAX + 2]; /* 513 字节超长行（不含 \n） */
    memset(big, 'A', PET_LINE_MAX + 1);
    big[PET_LINE_MAX + 1] = '\0';

    memset(&c, 0, sizeof c);
    pet_ble_lines_init(&ls);
    CHECK_EQ(feed_str(&ls, &c, big), 0);
    CHECK_EQ(feed_str(&ls, &c, "\n"), 0);
    CHECK_EQ(c.n, 0);
    CHECK_EQ(ls.lines_total, 0);
    CHECK_EQ(ls.lines_dropped, 1);
    /* 超长行丢弃后缓冲复位，下一行正常 */
    CHECK_EQ(feed_str(&ls, &c, "next\n"), 1);
    CHECK_STR(c.lines[0], "next");
    CHECK_EQ(ls.lines_total, 1);
    CHECK_EQ(ls.lines_dropped, 1);

    /* 超长行分多次 write 累积，同样整行丢弃（只计 1 次） */
    collect_t c2;
    memset(&c2, 0, sizeof c2);
    pet_ble_lines_init(&ls);
    CHECK_EQ(feed_mem(&ls, &c2, big, 250), 0);
    CHECK_EQ(feed_mem(&ls, &c2, big, 250), 0);
    CHECK_EQ(feed_mem(&ls, &c2, big, 13), 0);
    CHECK_EQ(feed_str(&ls, &c2, "\n"), 0);
    CHECK_EQ(ls.lines_dropped, 1);
    CHECK_EQ(c2.n, 0);
    CHECK_EQ(feed_str(&ls, &c2, "ok\n"), 1);
    CHECK_STR(c2.lines[0], "ok");

    /* 恰好 512 B 是合法行（C05 上限），不丢弃 */
    char exact[PET_LINE_MAX + 1];
    memset(exact, 'B', PET_LINE_MAX);
    exact[PET_LINE_MAX] = '\0';
    collect_t c3;
    memset(&c3, 0, sizeof c3);
    pet_ble_lines_init(&ls);
    CHECK_EQ(feed_str(&ls, &c3, exact), 0);
    CHECK_EQ(feed_str(&ls, &c3, "\n"), 1);
    CHECK_EQ(c3.n, 1);
    CHECK_EQ(c3.lens[0], PET_LINE_MAX);
    CHECK_EQ(c3.lines[0][0], 'B');
    CHECK_EQ(c3.lines[0][PET_LINE_MAX - 1], 'B');
    CHECK_EQ(ls.lines_total, 1);
    CHECK_EQ(ls.lines_dropped, 0);
}

/* ---- 6. 按 BLE MTU 净荷分片（20 B 一片）重装 JSON 行 ---- */

static void case_ble01f_mtu_sized_fragments(void)
{
    pet_ble_lines_t ls;
    collect_t c;
    const char *line =
        "{\"version\":1,\"event\":\"task.start\",\"task\":\"m5b-2 ble\"}";
    const size_t total = strlen(line);
    memset(&c, 0, sizeof c);
    pet_ble_lines_init(&ls);

    uint32_t got = 0;
    for (size_t off = 0; off < total; off += 20) {
        size_t n = total - off;
        if (n > 20) {
            n = 20;
        }
        got += feed_mem(&ls, &c, line + off, n);
    }
    CHECK_EQ(got, 0);
    CHECK_EQ(c.n, 0);
    CHECK_EQ(feed_str(&ls, &c, "\n"), 1);
    CHECK_EQ(c.n, 1);
    CHECK_STR(c.lines[0], line);
    CHECK_EQ(c.lens[0], total);
    CHECK_EQ(ls.lines_total, 1);
    CHECK_EQ(ls.lines_dropped, 0);

    /* 两个 JSON 行按 7 B 分片交错跨 write 交付 */
    const char *l1 = "{\"event\":\"a\"}";
    const char *l2 = "{\"event\":\"b\"}";
    char stream[64];
    const int stream_len =
        snprintf(stream, sizeof stream, "%s\n%s\n", l1, l2);
    CHECK(stream_len > 0);
    collect_t c2;
    memset(&c2, 0, sizeof c2);
    pet_ble_lines_init(&ls);
    for (int off = 0; off < stream_len; off += 7) {
        int n = stream_len - off;
        if (n > 7) {
            n = 7;
        }
        (void)feed_mem(&ls, &c2, stream + off, (size_t)n);
    }
    CHECK_EQ(c2.n, 2);
    CHECK_STR(c2.lines[0], l1);
    CHECK_STR(c2.lines[1], l2);
    CHECK_EQ(ls.lines_total, 2);
}

/* ---- 7. 顺序与每次 feed 的返回值统计 ---- */

static void case_ble01g_order_and_counts(void)
{
    pet_ble_lines_t ls;
    collect_t c;
    memset(&c, 0, sizeof c);
    pet_ble_lines_init(&ls);

    const uint32_t a = feed_str(&ls, &c, "l1\nl2\nl3\npart");
    CHECK_EQ(a, 3);
    const uint32_t b = feed_str(&ls, &c, "4\nl5\n");
    CHECK_EQ(b, 2);
    CHECK_EQ(c.n, 5);
    CHECK_STR(c.lines[0], "l1");
    CHECK_STR(c.lines[1], "l2");
    CHECK_STR(c.lines[2], "l3");
    CHECK_STR(c.lines[3], "part4");
    CHECK_STR(c.lines[4], "l5");
    CHECK_EQ(ls.lines_total, 5);
    CHECK_EQ(ls.lines_dropped, 0);
    CHECK_EQ(a + b, ls.lines_total);
}

/* ---- 8. 防御：NULL 缓冲/数据、0 长度 write、NULL 回调 ---- */

static void case_ble01h_defensive_args(void)
{
    pet_ble_lines_init(NULL); /* 安全空操作，不崩溃 */

    pet_ble_lines_t ls;
    collect_t c;
    memset(&c, 0, sizeof c);
    pet_ble_lines_init(&ls);

    CHECK_EQ(pet_ble_lines_feed(NULL, (const uint8_t *)"x\n", 2, collect_cb, &c), 0);
    CHECK_EQ(pet_ble_lines_feed(&ls, NULL, 0, collect_cb, &c), 0);
    CHECK_EQ(pet_ble_lines_feed(&ls, NULL, 5, collect_cb, &c), 0);
    CHECK_EQ(c.n, 0);

    /* 半行后 data==NULL 不改变缓冲；随后 write 补齐 */
    CHECK_EQ(feed_str(&ls, &c, "half"), 0);
    CHECK_EQ(pet_ble_lines_feed(&ls, NULL, 3, collect_cb, &c), 0);
    CHECK_EQ(feed_str(&ls, &c, "rest\n"), 1);
    CHECK_STR(c.lines[0], "halfrest");

    /* cb == NULL：只计数，不交付（去重门/回调未接线的安全路径） */
    collect_t c2;
    memset(&c2, 0, sizeof c2);
    pet_ble_lines_init(&ls);
    CHECK_EQ(pet_ble_lines_feed(&ls, (const uint8_t *)"only\n", 5, NULL, NULL), 1);
    CHECK_EQ(c2.n, 0);
    CHECK_EQ(ls.lines_total, 1);
    /* 空行在 cb==NULL 时也不计数 */
    CHECK_EQ(pet_ble_lines_feed(&ls, (const uint8_t *)"\n", 1, NULL, NULL), 0);
    CHECK_EQ(ls.lines_total, 1);
}

/* ---- 9. 脏内存 init / 重连重装（半行丢弃） ---- */

static void case_ble01i_dirty_init_and_reload(void)
{
    pet_ble_lines_t ls;
    memset(&ls, 0xAA, sizeof ls);
    pet_ble_lines_init(&ls);
    CHECK_EQ(ls.lines_total, 0);
    CHECK_EQ(ls.lines_dropped, 0);
    CHECK_EQ(ls.lb.len, 0);
    CHECK_EQ(ls.lb.overflow, false);
    CHECK_EQ(ls.lb.deliver_pending, false);
    CHECK_EQ(ls.lb.buf[0], '\0');

    collect_t c;
    memset(&c, 0, sizeof c);
    CHECK_EQ(feed_str(&ls, &c, "stale-half"), 0);
    /* 断开/重连 → 重装：上一连接的半行不得串进新连接 */
    pet_ble_lines_init(&ls);
    CHECK_EQ(feed_str(&ls, &c, "fresh\n"), 1);
    CHECK_STR(c.lines[0], "fresh");
    CHECK_EQ(ls.lines_total, 1);
}

/* ---- 10. 每连接独立（两个 pet_ble_lines 互不影响） ---- */

static void case_ble01j_per_connection_isolation(void)
{
    pet_ble_lines_t a;
    pet_ble_lines_t b;
    collect_t ca;
    collect_t cb;
    memset(&ca, 0, sizeof ca);
    memset(&cb, 0, sizeof cb);
    pet_ble_lines_init(&a);
    pet_ble_lines_init(&b);

    CHECK_EQ(feed_str(&a, &ca, "a1\npa"), 1);
    CHECK_EQ(feed_str(&b, &cb, "b1\n"), 1);
    CHECK_EQ(ca.n, 1);
    CHECK_EQ(cb.n, 1);
    CHECK_STR(ca.lines[0], "a1");
    CHECK_STR(cb.lines[0], "b1");
    CHECK_EQ(a.lines_total, 1);
    CHECK_EQ(b.lines_total, 1);

    CHECK_EQ(feed_str(&a, &ca, "rt\n"), 1);
    CHECK_EQ(feed_str(&b, &cb, "b2\n"), 1);
    CHECK_STR(ca.lines[1], "part");
    CHECK_STR(cb.lines[1], "b2");
    CHECK_EQ(a.lines_total, 2);
    CHECK_EQ(b.lines_total, 2);
    CHECK_EQ(a.lines_dropped, 0);
    CHECK_EQ(b.lines_dropped, 0);
}

/* ---- 运行器 ---- */

typedef struct {
    const char *name;
    void (*fn)(void);
} test_case_t;

int main(void)
{
    static const test_case_t cases[] = {
        {"BLE-01a-single-write-multi-line", case_ble01a_single_write_multi_line},
        {"BLE-01b-across-write-half-line", case_ble01b_across_write_half_line},
        {"BLE-01c-crlf", case_ble01c_crlf},
        {"BLE-01d-empty-lines-skipped", case_ble01d_empty_lines_skipped},
        {"BLE-01e-oversize-drop-and-recover",
         case_ble01e_oversize_drop_and_recover},
        {"BLE-01f-mtu-sized-fragments", case_ble01f_mtu_sized_fragments},
        {"BLE-01g-order-and-counts", case_ble01g_order_and_counts},
        {"BLE-01h-defensive-args", case_ble01h_defensive_args},
        {"BLE-01i-dirty-init-and-reload", case_ble01i_dirty_init_and_reload},
        {"BLE-01j-per-connection-isolation",
         case_ble01j_per_connection_isolation},
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
    printf("SUMMARY m5b_ble: cases=%d passed=%d failed=%d asserts=%d\n", n,
           n - failed_cases, failed_cases, total_asserts);
    return failed_cases == 0 ? 0 : 1;
}
