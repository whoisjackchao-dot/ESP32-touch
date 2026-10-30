/* M2 host 单测：pet_ui_240 像素级版式断言（UI-13 / GB-01 / CARD-13）
 * 契约：docs/contracts/C03-layout-240x240.md（§1 网格/§2 卡版式/§3 栈/§4 边界）；
 *       docs/08-m2-development.md §3（落地取值）与 §6 冲突修订（C2/C3）。
 * 直接构造 pet_view_t，按固定 now 语义调 pet_ui_scene_240（纯函数，无真实时钟）。
 * 只写 tests/，链接真实 pet_render.c / pet_text.c。 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pet_render.h"
#include "pet_ui_240.h"

#define UI_W 240
#define UI_H 240
#define UI_STRIDE (UI_W * 2)

/* C03 §2/§3/§7 颜色常量 */
#define C_BG        0x0000u
#define C_CARD_BG   0x18E3u
#define C_ACCENT    0x05FFu
#define C_DOT_ON    0x07E0u
#define C_DOT_OFF   0xF800u
#define C_MARK_WORK 0xFFFFu
#define C_MARK_IN   0xFD20u

#define CARD_Y0   152
#define CARD_X0   4
#define CARD_W    232
#define CARD_H    28
#define CARD_ROW  28

static uint8_t g_pixels[UI_STRIDE * UI_H];
static uint8_t g_ref_pixels[UI_STRIDE * UI_H];

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

/* ---- 画布 ---- */

static pet_surface_t surface(void)
{
    pet_surface_t s;
    s.pixels = g_pixels;
    s.width = UI_W;
    s.height = UI_H;
    s.stride = UI_STRIDE;
    return s;
}

static uint16_t get_px(const pet_surface_t *s, int x, int y)
{
    const uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
    return (uint16_t)(p[0] | (uint16_t)(p[1] << 8));
}

/* 用哨兵填充，检测整屏是否被清屏/覆盖 */
static void fill_sentinel(pet_surface_t *s)
{
    memset(s->pixels, 0xA5, (size_t)s->stride * (size_t)s->height);
}

static int count_eq(const pet_surface_t *s, int x0, int y0, int x1, int y1,
                    uint16_t color)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (get_px(s, x, y) == color) {
                n++;
            }
        }
    }
    return n;
}

static int count_ne(const pet_surface_t *s, int x0, int y0, int x1, int y1,
                    uint16_t color)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (get_px(s, x, y) != color) {
                n++;
            }
        }
    }
    return n;
}

static bool has_color(const pet_surface_t *s, int x0, int y0, int x1, int y1,
                      uint16_t color)
{
    return count_eq(s, x0, y0, x1, y1, color) > 0;
}

/* ---- 夹具 ---- */

static pet_card_t ui_card(const char *title, const char *project,
                          const char *agent, int32_t elapsed_s)
{
    pet_card_t c;
    memset(&c, 0, sizeof c);
    snprintf(c.title, sizeof c.title, "%s", title != NULL ? title : "");
    snprintf(c.project, sizeof c.project, "%s", project != NULL ? project : "");
    snprintf(c.agent, sizeof c.agent, "%s", agent != NULL ? agent : "");
    c.state = PET_STATE_WORKING;
    c.elapsed_s = elapsed_s;
    return c;
}

static void view_reset(pet_view_t *v, pet_state_t state)
{
    memset(v, 0, sizeof *v);
    v->state = state;
    v->frame = PET_FRAME_NORMAL;
}

static void view_set_stack(pet_view_t *v, const pet_card_t *cards, int n)
{
    v->card_count = n;
    v->has_card = (n > 0);
    for (int i = 0; i < n; i++) {
        v->cards[i] = cards[i];
    }
    if (n > 0) {
        v->card = cards[0];
    }
}

static pet_look_t *g_look;
static bool g_look_tried;

/* 读取真实 assets/robot.look 并解析；兼容从仓库根或 tests/ 目录运行。 */
static pet_look_t *get_look(void)
{
    if (g_look_tried) {
        return g_look;
    }
    g_look_tried = true;
    static const char *paths[] = {
        "firmware/components/pet_ui/assets/robot.look",
        "../firmware/components/pet_ui/assets/robot.look",
        "D:/DSH/ESP32-touch/firmware/components/pet_ui/assets/robot.look",
        NULL,
    };
    for (int i = 0; paths[i] != NULL; i++) {
        FILE *f = fopen(paths[i], "rb");
        if (f == NULL) {
            continue;
        }
        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            continue;
        }
        long len = ftell(f);
        if (len <= 0 || fseek(f, 0, SEEK_SET) != 0) {
            fclose(f);
            continue;
        }
        uint8_t *buf = malloc((size_t)len);
        if (buf == NULL) {
            fclose(f);
            continue;
        }
        size_t got = fread(buf, 1, (size_t)len, f);
        fclose(f);
        if (got == (size_t)len) {
            g_look = pet_look_parse(buf, got);
        }
        free(buf);
        if (g_look != NULL) {
            return g_look;
        }
    }
    printf("  NOTE: cannot load/parse firmware/components/pet_ui/assets/robot.look\n");
    return NULL;
}

/* 卡矩形：多数像素为卡底、空白采样点为卡底、最新卡左 2px 强调条 */
static void check_card_rect(const pet_surface_t *s, int index, bool newest,
                            const char *tag)
{
    const int y = CARD_Y0 + index * CARD_ROW;
    const int bg = count_eq(s, CARD_X0, y, CARD_X0 + CARD_W - 1, y + CARD_H - 1,
                            C_CARD_BG);
    if (bg <= 500) {
        printf("  card[%d] %s: CARD_BG pixels=%d (want >500)\n", index, tag, bg);
    }
    CHECK(bg > 500);
    CHECK_EQ(get_px(s, 230, y + 26), C_CARD_BG);
    CHECK_EQ(get_px(s, 50, y + 26), C_CARD_BG);
    CHECK_EQ(get_px(s, 8, y + 26), C_CARD_BG);
    CHECK_EQ(get_px(s, 230, y + 1), C_CARD_BG);
    if (newest) {
        CHECK_EQ(count_eq(s, CARD_X0, y, CARD_X0 + 1, y + CARD_H - 1, C_ACCENT),
                 56);
    } else {
        CHECK_EQ(count_eq(s, CARD_X0, y, CARD_X0 + 1, y + CARD_H - 1, C_ACCENT),
                 0);
        CHECK_EQ(count_eq(s, CARD_X0, y, CARD_X0 + 1, y + CARD_H - 1,
                          C_CARD_BG),
                 56);
    }
}

static void render_stack(const pet_card_t *cards, int n, const pet_look_t *look,
                         pet_state_t state, pet_surface_t *out)
{
    pet_view_t v;
    view_reset(&v, state);
    view_set_stack(&v, cards, n);
    fill_sentinel(out);
    pet_ui_scene_240(&v, look, out);
}

/* ---- UI-13：卡区固定版式 / 0..3 卡 / 最新卡强调条 ---- */
static void case_ui13_card_stack(void)
{
    pet_surface_t s = surface();
    pet_card_t cards[3];
    cards[0] = ui_card("GAMMA", "openai/chat", "OC", 75);
    cards[1] = ui_card("BETA", "acme/web", "CC", 900);
    cards[2] = ui_card("ALPHA", "acme/api", "CX", 5);

    for (int n = 0; n <= 3; n++) {
        render_stack(cards, n, NULL, PET_STATE_IDLE, &s);

        for (int i = 0; i < n; i++) {
            check_card_rect(&s, i, i == 0, "stack");
        }
        /* 未使用的卡行必须留黑（C03 §3.4：不画空框） */
        const int unused_y = CARD_Y0 + n * CARD_ROW;
        if (n < 3) {
            CHECK_EQ(count_eq(&s, CARD_X0, unused_y, CARD_X0 + CARD_W - 1, 235,
                              C_BG),
                     (236 - unused_y) * CARD_W);
        }
        /* 卡区左右外侧留黑 */
        if (n > 0) {
            CHECK_EQ(count_eq(&s, 0, CARD_Y0, 3, CARD_Y0 + n * CARD_ROW - 1,
                              C_BG),
                     4 * n * CARD_ROW);
            CHECK_EQ(count_eq(&s, 236, CARD_Y0, 239,
                              CARD_Y0 + n * CARD_ROW - 1, C_BG),
                     4 * n * CARD_ROW);
        }
        /* 宠物底部与卡区之间 4px 背景（版式矩形不重叠） */
        CHECK_EQ(count_eq(&s, 0, 148, 239, 151, C_BG), 240 * 4);
        /* 页脚 y=236..239 预留、不绘制 */
        CHECK_EQ(count_eq(&s, 0, 236, 239, 239, C_BG), 240 * 4);

        if (n == 0) {
            /* 无卡：除网络点外整屏黑 */
            CHECK_EQ(count_eq(&s, 0, 0, 239, 239, C_BG), 240 * 240 - 16);
        }
        /* 在线网络点 (4,4,4,4) = 0x07E0 */
        CHECK_EQ(count_eq(&s, 4, 4, 7, 7, C_DOT_ON), 16);
    }
}

/* ---- CARD-13：空字段不得留空白卡（project→"-"，title→"(untitled)"） ---- */
static void case_card13_empty_fields(void)
{
    pet_surface_t s = surface();
    pet_card_t cards[1];

    /* 行 1：project 空 → "-"（agent 也空，确保不是前缀撑起来的非空） */
    cards[0] = ui_card("T", "", "", 0);
    render_stack(cards, 1, NULL, PET_STATE_IDLE, &s);
    CHECK(count_ne(&s, 8, 155, 231, 162, C_CARD_BG) > 0);

    /* 行 2：title 空 → "(untitled)"（只查左侧标题带，避开右侧时长） */
    cards[0] = ui_card("", "acme/empty", "", 0);
    render_stack(cards, 1, NULL, PET_STATE_IDLE, &s);
    CHECK(count_ne(&s, 8, 164, 184, 179, C_CARD_BG) > 0);

    /* 两者皆空仍显示占位符 */
    cards[0] = ui_card("", "", "", 0);
    render_stack(cards, 1, NULL, PET_STATE_IDLE, &s);
    CHECK(count_ne(&s, 8, 155, 231, 162, C_CARD_BG) > 0);
    CHECK(count_ne(&s, 8, 164, 184, 179, C_CARD_BG) > 0);

    /* 卡体仍为卡底色（占位符不是整卡丢失） */
    CHECK_EQ(get_px(&s, 230, 177), C_CARD_BG);
}

/* ---- UI-11/UI-13：头顶标记只在对应态出现，且与宠物内部标记不重复 ---- */
static void case_ui13_markers(void)
{
    pet_surface_t s = surface();

    /* WORKING：`>` 居中于 x=120，宽 10 → x=115..124，y=2..15，白色 */
    render_stack(NULL, 0, NULL, PET_STATE_WORKING, &s);
    CHECK(has_color(&s, 115, 2, 124, 15, C_MARK_WORK));
    CHECK_EQ(count_eq(&s, 115, 2, 124, 15, C_MARK_IN), 0);

    /* INPUT_REQUIRED：`!?` 居中，宽 18 → x=111..128，橙色 */
    render_stack(NULL, 0, NULL, PET_STATE_INPUT_REQUIRED, &s);
    CHECK(has_color(&s, 111, 2, 128, 15, C_MARK_IN));
    CHECK_EQ(count_eq(&s, 111, 2, 128, 15, C_MARK_WORK), 0);

    /* IDLE / DONE：标记区无这两种颜色 */
    render_stack(NULL, 0, NULL, PET_STATE_IDLE, &s);
    CHECK_EQ(count_eq(&s, 111, 2, 128, 15, C_MARK_WORK), 0);
    CHECK_EQ(count_eq(&s, 111, 2, 128, 15, C_MARK_IN), 0);
    render_stack(NULL, 0, NULL, PET_STATE_DONE, &s);
    CHECK_EQ(count_eq(&s, 111, 2, 128, 15, C_MARK_WORK), 0);
    CHECK_EQ(count_eq(&s, 111, 2, 128, 15, C_MARK_IN), 0);

    /* 有真实立绘时：标记仍必须由 pet_ui_240 画在 C03 位置
     * （若把 v 直接交给 pet_render_pet，其内置标记会画在 x=128..，本断言失败） */
    pet_look_t *look = get_look();
    CHECK(look != NULL);
    if (look != NULL) {
        render_stack(NULL, 0, look, PET_STATE_WORKING, &s);
        CHECK(has_color(&s, 115, 2, 124, 15, C_MARK_WORK));
        render_stack(NULL, 0, look, PET_STATE_INPUT_REQUIRED, &s);
        CHECK(has_color(&s, 111, 2, 128, 15, C_MARK_IN));
    }
}

/* ---- UI-11b / D-05 回归：标记点阵不得左右镜像（'>' 反向成 '<'、'?' 弯钩反向） ----
 * 真机缺陷 D-05：上游点阵表按 MSB=最左书写、却按 bit0=最左绘制。Lead 已在
 * pet_ui_240.c 把 GLYPH_GT/GLYPH_QUEST 镜像修正。以下断言锁定修正后的朝向：
 *  '>'（5 列 ×2px = x=115..124）：顶/底行最左块 x=115..116，第 4 行尖角 x=121..122；
 *  '?'（x=119..128）：顶横条 x=121..126，右侧竖笔 x=127..128，弯钩 y=8..9 在 x=125..126。
 * 未镜像（MSB 表）时：'>' 顶/底行跑到 x=123..124、尖角 x=117..118；'?' 竖笔/弯钩
 * 跑到左侧 x=119..120、x=121..122 → 本用例将有 10 条断言失败（'>' 6 + '?' 4）。 */
static void case_ui11b_d05_marker_orientation(void)
{
    pet_surface_t s = surface();

    /* ---- WORKING '>'：方向 = 尖角在右（第 4 行），开口朝左（顶/底行在最左） ---- */
    render_stack(NULL, 0, NULL, PET_STATE_WORKING, &s);
    /* 顶行 y=2..3：只有最左块 x=115..116；x=117..124 全黑 */
    CHECK_EQ(count_eq(&s, 115, 2, 116, 3, C_MARK_WORK), 4);
    CHECK_EQ(count_eq(&s, 117, 2, 124, 3, C_MARK_WORK), 0);
    /* 第 4 行 y=8..9：只有尖角块 x=121..122；左侧与最右列均无 */
    CHECK_EQ(count_eq(&s, 121, 8, 122, 9, C_MARK_WORK), 4);
    CHECK_EQ(count_eq(&s, 115, 8, 120, 9, C_MARK_WORK), 0);
    CHECK_EQ(count_eq(&s, 123, 8, 124, 9, C_MARK_WORK), 0);
    /* 底行 y=14..15：回到最左块 x=115..116 */
    CHECK_EQ(count_eq(&s, 115, 14, 116, 15, C_MARK_WORK), 4);
    CHECK_EQ(count_eq(&s, 117, 14, 124, 15, C_MARK_WORK), 0);

    /* ---- INPUT_REQUIRED '?'：右侧竖笔 + 向左下弯钩，不得镜像到左侧 ---- */
    render_stack(NULL, 0, NULL, PET_STATE_INPUT_REQUIRED, &s);
    /* 顶部横条 y=2..3：x=121..126（3 块），左右各留 1 列 */
    CHECK_EQ(count_eq(&s, 121, 2, 126, 3, C_MARK_IN), 12);
    CHECK_EQ(count_eq(&s, 119, 2, 120, 3, C_MARK_IN), 0);
    CHECK_EQ(count_eq(&s, 127, 2, 128, 3, C_MARK_IN), 0);
    /* y=4..5：左起笔 x=119..120 + 右侧竖笔 x=127..128（两段同时存在） */
    CHECK_EQ(count_eq(&s, 119, 4, 120, 5, C_MARK_IN), 4);
    CHECK_EQ(count_eq(&s, 127, 4, 128, 5, C_MARK_IN), 4);
    /* y=6..7：竖笔只在最右列 x=127..128（未镜像表在 x=119..120） */
    CHECK_EQ(count_eq(&s, 127, 6, 128, 7, C_MARK_IN), 4);
    CHECK_EQ(count_eq(&s, 119, 6, 120, 7, C_MARK_IN), 0);
    /* y=8..9：弯钩向左下折到 x=125..126（未镜像表在 x=121..122） */
    CHECK_EQ(count_eq(&s, 125, 8, 126, 9, C_MARK_IN), 4);
    CHECK_EQ(count_eq(&s, 121, 8, 122, 9, C_MARK_IN), 0);
    /* y=14..15：底部点 x=123..124（形状完整性；镜像不改变该行） */
    CHECK_EQ(count_eq(&s, 123, 14, 124, 15, C_MARK_IN), 4);
}

/* ---- UI-13：宠物区 (72,20,96,128) 与区域不重叠 ---- */
static void case_ui13_pet_layout(void)
{
    pet_surface_t s = surface();
    pet_view_t v;
    view_reset(&v, PET_STATE_IDLE);

    pet_look_t *look = get_look();
    CHECK(look != NULL);
    if (look != NULL) {
        fill_sentinel(&s);
        pet_ui_scene_240(&v, look, &s);
        const int pet_px = count_ne(&s, 72, 20, 167, 147, C_BG);
        if (pet_px <= 300) {
            printf("  pet pixels=%d (want >300)\n", pet_px);
        }
        CHECK(pet_px > 300);
        /* 宠物矩形外保持背景色 */
        CHECK_EQ(count_eq(&s, 0, 148, 239, 151, C_BG), 240 * 4);
        CHECK_EQ(count_eq(&s, 0, 20, 71, 147, C_BG), 72 * 128);
        CHECK_EQ(count_eq(&s, 168, 20, 239, 147, C_BG), 72 * 128);
    }

    /* look=NULL：宠物区不画，但标记/网络点仍按状态绘制（本视图 IDLE 无标记） */
    fill_sentinel(&s);
    pet_ui_scene_240(&v, NULL, &s);
    CHECK_EQ(count_ne(&s, 72, 20, 167, 147, C_BG), 0);
    CHECK_EQ(count_eq(&s, 0, 148, 239, 151, C_BG), 240 * 4);
}

/* ---- GB-01：OFFLINE 整帧灰度 + 红点；灰度换算与 gray565 同法 ---- */

/* 与 pet_render.c gray565 相同的亮度权重换算（契约要求同法）。 */
static uint16_t gray565_ref(uint16_t c)
{
    const uint32_t r8 = ((c >> 11) & 0x1Fu) * 255u / 31u;
    const uint32_t g8 = ((c >> 5) & 0x3Fu) * 255u / 63u;
    const uint32_t b8 = (c & 0x1Fu) * 255u / 31u;
    const uint32_t level = (r8 * 77u + g8 * 150u + b8 * 29u) >> 8;
    const uint32_t g6 = (level * 63u + 127u) / 255u;
    const uint32_t r5 = g6 >> 1;
    const uint32_t b5 = g6 >> 1;
    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

static bool is_gray565(uint16_t c)
{
    const uint32_t r5 = (c >> 11) & 0x1Fu;
    const uint32_t g6 = (c >> 5) & 0x3Fu;
    const uint32_t b5 = c & 0x1Fu;
    return r5 == (g6 >> 1) && b5 == (g6 >> 1);
}

static void case_gb01_offline_gray(void)
{
    pet_surface_t s = surface();
    pet_surface_t ref = surface();
    ref.pixels = g_ref_pixels;

    pet_card_t cards[3];
    cards[0] = ui_card("GAMMA", "openai/chat", "OC", 75);
    cards[1] = ui_card("BETA", "acme/web", "CC", 900);
    cards[2] = ui_card("ALPHA", "acme/api", "CX", 5);

    pet_look_t *look = get_look();
    CHECK(look != NULL);

    /* 参考帧：正常色（同一卡栈；卡区与 OFFLINE 帧同源） */
    pet_view_t refv;
    view_reset(&refv, PET_STATE_IDLE);
    refv.frame = PET_FRAME_EYES_CLOSED;
    view_set_stack(&refv, cards, 3);
    fill_sentinel(&ref);
    pet_ui_scene_240(&refv, look, &ref);
    uint16_t normal_card[CARD_W * CARD_H * 3];
    for (int i = 0; i < 3; i++) {
        for (int y = 0; y < CARD_H; y++) {
            for (int x = 0; x < CARD_W; x++) {
                normal_card[(i * CARD_H + y) * CARD_W + x] =
                    get_px(&ref, CARD_X0 + x, CARD_Y0 + i * CARD_ROW + y);
            }
        }
    }

    /* OFFLINE 帧 */
    pet_view_t v;
    view_reset(&v, PET_STATE_OFFLINE);
    v.gray = true;
    v.frame = PET_FRAME_EYES_CLOSED;
    view_set_stack(&v, cards, 3);
    fill_sentinel(&s);
    pet_ui_scene_240(&v, look, &s);

    /* 网络点变红，且红点区域完整 4×4 */
    CHECK_EQ(count_eq(&s, 4, 4, 7, 7, C_DOT_OFF), 16);

    /* 整帧灰度（红点区域除外）：每像素 r5==(g6>>1) 且 b5==(g6>>1) */
    int nongray = 0;
    for (int y = 0; y < UI_H; y++) {
        for (int x = 0; x < UI_W; x++) {
            if (x >= 4 && x <= 7 && y >= 4 && y <= 7) {
                continue;
            }
            if (!is_gray565(get_px(&s, x, y))) {
                if (nongray < 8) {
                    printf("  non-gray pixel (%d,%d)=0x%04X\n", x, y,
                           get_px(&s, x, y));
                }
                nongray++;
            }
        }
    }
    if (nongray != 0) {
        printf("  non-gray pixels=%d (dot excluded)\n", nongray);
    }
    CHECK_EQ(nongray, 0);

    /* 宠物在灰度前确实画过（不是全黑通过） */
    if (look != NULL) {
        CHECK(count_ne(&s, 72, 20, 167, 147, C_BG) > 300);
    }

    /* 卡区灰度值必须等于参考 normal 像素的 gray565（同法且保序） */
    int map_bad = 0;
    int levels[64];
    int nlevels = 0;
    for (int i = 0; i < 3; i++) {
        for (int y = 0; y < CARD_H; y++) {
            for (int x = 0; x < CARD_W; x++) {
                const uint16_t src =
                    normal_card[(i * CARD_H + y) * CARD_W + x];
                const uint16_t want = gray565_ref(src);
                const uint16_t got =
                    get_px(&s, CARD_X0 + x, CARD_Y0 + i * CARD_ROW + y);
                if (got != want) {
                    if (map_bad < 8) {
                        printf("  gray-map mismatch (%d,%d): src=0x%04X "
                               "got=0x%04X want=0x%04X\n",
                               CARD_X0 + x, CARD_Y0 + i * CARD_ROW + y, src,
                               got, want);
                    }
                    map_bad++;
                }
                bool seen = false;
                for (int k = 0; k < nlevels; k++) {
                    if (levels[k] == (int)got) {
                        seen = true;
                        break;
                    }
                }
                if (!seen && nlevels < 64) {
                    levels[nlevels++] = (int)got;
                }
            }
        }
    }
    CHECK_EQ(map_bad, 0);
    /* 单调/信息保留的弱证据：卡区不是单一灰度 */
    CHECK(nlevels >= 3);
}

/* ---- 运行器 ---- */

typedef struct {
    const char *name;
    void (*fn)(void);
} test_case_t;

int main(void)
{
    static const test_case_t cases[] = {
        {"UI-13-card-stack", case_ui13_card_stack},
        {"CARD-13-empty-fields", case_card13_empty_fields},
        {"UI-11-markers", case_ui13_markers},
        {"UI-11b-d05-marker-orientation", case_ui11b_d05_marker_orientation},
        {"UI-13-pet-layout", case_ui13_pet_layout},
        {"GB-01-offline-gray", case_gb01_offline_gray},
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
    printf("SUMMARY pet_ui_240: cases=%d passed=%d failed=%d\n", n,
           n - failed_cases, failed_cases);
    if (g_look != NULL) {
        pet_look_free(g_look);
    }
    return failed_cases == 0 ? 0 : 1;
}

