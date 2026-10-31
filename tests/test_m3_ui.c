/* M3 host 单测：240×240 番茄钟页 / 菜单页像素断言（UI-20 / UI-21）。
 * 契约：docs/10-m3-development.md §5.1/§5.2（版式与配色）、§7（UI-20/UI-21）；
 *       C07 §4（菜单 5 项文案）。直接构造 view 调纯渲染函数，无真实时钟。
 * 只写 tests/；链接真实 pet_ui_pomo_240.c / pet_ui_menu_240.c /
 * pet_render.c / pet_text.c（不加载 assets，两页均为纯绘制）。 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pet_render.h"
#include "pet_ui_menu_240.h"
#include "pet_ui_pomo_240.h"

#define UI_W 240
#define UI_H 240
#define UI_STRIDE (UI_W * 2)

/* docs/10 §5.1/§5.2 颜色常量 */
#define C_BG       0x0000u
#define C_WHITE    0xFFFFu
#define C_GRAY     0x8410u
#define C_FOCUS    0xFD20u /* 专注：橙 */
#define C_BREAK    0x07E0u /* 休息：绿 */
#define C_TRACK    0x4208u /* 圆环轨道 */
#define C_CARD_BG  0x18E3u /* 菜单行卡底 */
#define C_INV_FG   0x0000u /* 选中行反色后的字色（黑） */

/* docs/10 §5.1：环心 / 外半径 / 环厚 */
#define RING_CX 120
#define RING_CY 104
#define RING_R  76
#define RING_T  10
/* 环上半/下/左/右中径采样点（d=71，位于 [R-T, R] 内） */
#define RING_P12X 120
#define RING_P12Y (RING_CY - 71) /* 33 */
#define RING_P3X  (RING_CX + 71) /* 191 */
#define RING_P3Y  RING_CY
#define RING_P6X  RING_CX
#define RING_P6Y  (RING_CY + 71) /* 175 */
#define RING_P9X  (RING_CX - 71) /* 49 */
#define RING_P9Y  RING_CY

/* docs/10 §5.2：菜单 5 行 (4, 48 + i*32, 232, 28)，底部统计 y=212 */
#define MENU_ROW_X 4
#define MENU_ROW_W 232
#define MENU_ROW_H 28
#define MENU_ROW_Y0 48
#define MENU_ROW_STEP 32
#define MENU_ROWS 5
#define MENU_STAT_Y 212

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

static pet_surface_t surface_of(uint8_t *pixels)
{
    pet_surface_t s;
    s.pixels = pixels;
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

/* 哨兵填充：渲染函数必须自己清屏/覆盖，否则断言会看到 0xA5A5。 */
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

/* 既不是 color_a 也不是 color_b 的像素数（用于“选中行只剩反色二色”）。 */
static int count_other(const pet_surface_t *s, int x0, int y0, int x1, int y1,
                       uint16_t color_a, uint16_t color_b)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            const uint16_t c = get_px(s, x, y);
            if (c != color_a && c != color_b) {
                n++;
            }
        }
    }
    return n;
}

/* 指定颜色的像素包围盒中心（无像素时返回 -100000）。 */
static void color_bbox(const pet_surface_t *s, int x0, int y0, int x1, int y1,
                       uint16_t color, int *out_cx, int *out_cy, int *out_n)
{
    int minx = 1000000;
    int maxx = -1000000;
    int miny = 1000000;
    int maxy = -1000000;
    int n = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (get_px(s, x, y) != color) {
                continue;
            }
            n++;
            if (x < minx) {
                minx = x;
            }
            if (x > maxx) {
                maxx = x;
            }
            if (y < miny) {
                miny = y;
            }
            if (y > maxy) {
                maxy = y;
            }
        }
    }
    if (n == 0) {
        *out_cx = -100000;
        *out_cy = -100000;
    } else {
        *out_cx = (minx + maxx) / 2;
        *out_cy = (miny + maxy) / 2;
    }
    *out_n = n;
}

static int count_diff(const pet_surface_t *a, const pet_surface_t *b, int x0,
                      int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (get_px(a, x, y) != get_px(b, x, y)) {
                n++;
            }
        }
    }
    return n;
}

/* ---- 视图夹具 ---- */

static pet_pomo_view_t pomo_view(pet_pomo_phase_t phase, uint32_t total_ms,
                                 uint32_t remaining_ms)
{
    pet_pomo_view_t v;
    memset(&v, 0, sizeof v);
    v.phase = phase;
    v.total_ms = total_ms;
    v.remaining_ms = remaining_ms;
    v.ip[0] = '\0';
    return v;
}

static pet_menu_view_t menu_view(int selected, int mode, uint32_t done_count,
                                 uint32_t focus_seconds, bool muted)
{
    pet_menu_view_t v;
    memset(&v, 0, sizeof v);
    v.selected = selected;
    v.mode = mode;
    v.phase = PET_POMO_IDLE;
    v.done_count = done_count;
    v.focus_seconds = focus_seconds;
    v.muted = muted;
    return v;
}

static void render_pomo(pet_surface_t *s, const pet_pomo_view_t *v)
{
    fill_sentinel(s);
    pet_ui_pomo_240(s, v);
}

static void render_menu(pet_surface_t *s, const pet_menu_view_t *v)
{
    fill_sentinel(s);
    pet_ui_menu_240(s, v);
}

/* ---- UI-20：番茄钟页圆环几何（环心/半径/环厚）与底色 ---- */
static void case_ui20_pomo_ring_geometry(void)
{
    pet_surface_t s = surface_of(g_pixels);
    pet_pomo_view_t v = pomo_view(PET_POMO_FOCUS, POMO_FOCUS_MS,
                                  POMO_FOCUS_MS); /* 进度 0 */
    render_pomo(&s, &v);

    /* 整屏底色 0x0000（含四角） */
    CHECK_EQ(get_px(&s, 0, 0), C_BG);
    CHECK_EQ(get_px(&s, UI_W - 1, 0), C_BG);
    CHECK_EQ(get_px(&s, 0, UI_H - 1), C_BG);
    CHECK_EQ(get_px(&s, UI_W - 1, UI_H - 1), C_BG);

    /* 环上中径 d=71：12/3/6/9 点都是轨道色（进度 0 → 全环轨道） */
    CHECK_EQ(get_px(&s, RING_P12X, RING_P12Y), C_TRACK);
    CHECK_EQ(get_px(&s, RING_P3X, RING_P3Y), C_TRACK);
    CHECK_EQ(get_px(&s, RING_P6X, RING_P6Y), C_TRACK);
    CHECK_EQ(get_px(&s, RING_P9X, RING_P9Y), C_TRACK);

    /* 环内洞（d=60 < 66）为底色 */
    CHECK_EQ(get_px(&s, RING_CX, RING_CY - 60), C_BG);
    CHECK_EQ(get_px(&s, RING_CX + 60, RING_CY), C_BG);
    /* 环外（d=80 / 77 > 76）为底色 */
    CHECK_EQ(get_px(&s, RING_CX, RING_CY - 80), C_BG);
    CHECK_EQ(get_px(&s, RING_CX + 77, RING_CY), C_BG);

    /* 环厚 10：12 点径向（x=120）连续 11 px（外 76、内 66 均含边界） */
    CHECK_EQ(count_eq(&s, RING_CX, RING_CY - RING_R, RING_CX,
                      RING_CY - (RING_R - RING_T), C_TRACK),
             11);

    /* 进度 0：环区无进度色；轨道像素数量级正确（约 π(R²-(R-T)²) ≈ 4460） */
    CHECK_EQ(count_eq(&s, RING_CX - RING_R, RING_CY - RING_R,
                      RING_CX + RING_R, RING_CY + RING_R, C_FOCUS),
             0);
    CHECK(count_eq(&s, RING_CX - RING_R, RING_CY - RING_R,
                   RING_CX + RING_R, RING_CY + RING_R, C_TRACK) > 1500);
}

/* ---- UI-20：进度 0/500/1000 的颜色、12 点顺时针方向、FOCUS/BREAK 配色 ---- */
static void case_ui20_pomo_progress_colors(void)
{
    pet_surface_t s = surface_of(g_pixels);

    /* FOCUS 进度 500：右半（0°..180° 顺时针）橙、左半轨道 */
    pet_pomo_view_t v = pomo_view(PET_POMO_FOCUS, POMO_FOCUS_MS, 750000);
    render_pomo(&s, &v);
    CHECK_EQ(get_px(&s, RING_P12X, RING_P12Y), C_FOCUS);
    CHECK_EQ(get_px(&s, RING_P3X, RING_P3Y), C_FOCUS);      /* 90° */
    CHECK_EQ(get_px(&s, 156, 166), C_FOCUS);                /* ~150° */
    CHECK_EQ(get_px(&s, RING_P9X, RING_P9Y), C_TRACK);      /* 270° */
    CHECK_EQ(get_px(&s, 84, 166), C_TRACK);                 /* ~210° */

    /* FOCUS 进度 1000：整环橙，无轨道残留 */
    v = pomo_view(PET_POMO_FOCUS, POMO_FOCUS_MS, 0);
    render_pomo(&s, &v);
    CHECK_EQ(get_px(&s, RING_P12X, RING_P12Y), C_FOCUS);
    CHECK_EQ(get_px(&s, RING_P3X, RING_P3Y), C_FOCUS);
    CHECK_EQ(get_px(&s, RING_P6X, RING_P6Y), C_FOCUS);
    CHECK_EQ(get_px(&s, RING_P9X, RING_P9Y), C_FOCUS);
    CHECK_EQ(count_eq(&s, RING_CX - RING_R, RING_CY - RING_R,
                      RING_CX + RING_R, RING_CY + RING_R, C_TRACK),
             0);
    CHECK(count_eq(&s, RING_CX - RING_R, RING_CY - RING_R,
                   RING_CX + RING_R, RING_CY + RING_R, C_FOCUS) > 1500);

    /* FOCUS 进度 300（108°）：45°/90° 橙、135° 仍轨道（12 点顺时针） */
    v = pomo_view(PET_POMO_FOCUS, POMO_FOCUS_MS, 1050000);
    render_pomo(&s, &v);
    CHECK_EQ(get_px(&s, RING_P3X, RING_P3Y), C_FOCUS); /* 90° */
    CHECK_EQ(get_px(&s, 170, 54), C_FOCUS);            /* ~45° */
    CHECK_EQ(get_px(&s, 170, 154), C_TRACK);           /* ~135° */

    /* BREAK 进度 0：全环轨道、无绿色进度弧 */
    v = pomo_view(PET_POMO_BREAK, POMO_BREAK_MS, POMO_BREAK_MS);
    render_pomo(&s, &v);
    CHECK_EQ(get_px(&s, RING_P12X, RING_P12Y), C_TRACK);
    CHECK_EQ(get_px(&s, RING_P3X, RING_P3Y), C_TRACK);
    CHECK_EQ(count_eq(&s, RING_CX - RING_R, RING_CY - RING_R,
                      RING_CX + RING_R, RING_CY + RING_R, C_BREAK),
             0);

    /* BREAK 进度 500：绿弧、无橙色（休息页整体不出现 0xFD20） */
    v = pomo_view(PET_POMO_BREAK, POMO_BREAK_MS, 150000);
    render_pomo(&s, &v);
    CHECK_EQ(get_px(&s, RING_P12X, RING_P12Y), C_BREAK);
    CHECK_EQ(get_px(&s, RING_P3X, RING_P3Y), C_BREAK);
    CHECK_EQ(get_px(&s, RING_P9X, RING_P9Y), C_TRACK);
    CHECK_EQ(count_eq(&s, 0, 0, UI_W - 1, UI_H - 1, C_FOCUS), 0);
}

/* ---- UI-20：相位标题、中心倒计时白字、底部统计 ---- */
static void case_ui20_pomo_title_center_stats(void)
{
    pet_surface_t s = surface_of(g_pixels);

    /* 标题带 y=16..23：FOCUS 橙、无灰；文字水平居中于 x=120 */
    pet_pomo_view_t v = pomo_view(PET_POMO_FOCUS, POMO_FOCUS_MS, 750000);
    render_pomo(&s, &v);
    int cx = 0;
    int cy = 0;
    int n = 0;
    color_bbox(&s, 0, 16, UI_W - 1, 23, C_FOCUS, &cx, &cy, &n);
    CHECK(n > 0);
    CHECK(cx >= 114 && cx <= 126);
    CHECK_EQ(count_eq(&s, 0, 16, UI_W - 1, 23, C_GRAY), 0);

    /* 中心倒计时：白字，scale 2，包围盒中心 ≈ (120,104) */
    color_bbox(&s, 78, 90, 162, 118, C_WHITE, &cx, &cy, &n);
    CHECK(n > 20);
    CHECK(cx >= 112 && cx <= 128);
    CHECK(cy >= 99 && cy <= 109);

    /* 底部统计 y=200..207 灰字（DONE n / mM），水平居中 */
    v.done_count = 3;
    v.focus_seconds = 4500; /* 75M */
    render_pomo(&s, &v);
    color_bbox(&s, 0, 198, UI_W - 1, 210, C_GRAY, &cx, &cy, &n);
    CHECK(n > 5);
    CHECK(cx >= 100 && cx <= 140);

    /* 各相位标题文案存在且颜色符合 §5.1：READY/FOCUS/PAUSED/BREAK/DONE，
     * 专注橙、休息绿、暂停/其它灰 */
    v = pomo_view(PET_POMO_IDLE, POMO_FOCUS_MS, POMO_FOCUS_MS);
    render_pomo(&s, &v);
    CHECK(count_eq(&s, 0, 16, UI_W - 1, 23, C_GRAY) > 0); /* READY 灰 */
    CHECK_EQ(count_eq(&s, 0, 16, UI_W - 1, 23, C_FOCUS), 0);
    CHECK_EQ(count_eq(&s, 0, 16, UI_W - 1, 23, C_BREAK), 0);

    v = pomo_view(PET_POMO_FOCUS_PAUSED, POMO_FOCUS_MS, 750000);
    render_pomo(&s, &v);
    CHECK(count_eq(&s, 0, 16, UI_W - 1, 23, C_GRAY) > 0); /* PAUSED 灰 */
    CHECK_EQ(count_eq(&s, 0, 16, UI_W - 1, 23, C_FOCUS), 0);

    v = pomo_view(PET_POMO_BREAK, POMO_BREAK_MS, 150000);
    render_pomo(&s, &v);
    CHECK(count_eq(&s, 0, 16, UI_W - 1, 23, C_BREAK) > 0); /* BREAK 绿 */
    CHECK_EQ(count_eq(&s, 0, 16, UI_W - 1, 23, C_FOCUS), 0);

    v = pomo_view(PET_POMO_FOCUS_DONE, POMO_FOCUS_MS, 0);
    render_pomo(&s, &v);
    CHECK(count_eq(&s, 0, 16, UI_W - 1, 23, C_GRAY) > 0); /* DONE 灰 */
    CHECK_EQ(count_eq(&s, 0, 16, UI_W - 1, 23, C_FOCUS), 0);

    v = pomo_view(PET_POMO_BREAK_DONE, POMO_BREAK_MS, 0);
    render_pomo(&s, &v);
    CHECK(count_eq(&s, 0, 16, UI_W - 1, 23, C_GRAY) > 0); /* DONE 灰 */
    CHECK_EQ(count_eq(&s, 0, 16, UI_W - 1, 23, C_BREAK), 0);
}

/* ---- UI-21：菜单页标题与底色 ---- */
static void case_ui21_menu_title(void)
{
    pet_surface_t s = surface_of(g_pixels);
    pet_menu_view_t v = menu_view(0, 1, 3, 4500, false);
    render_menu(&s, &v);

    CHECK_EQ(get_px(&s, 0, 0), C_BG);
    CHECK_EQ(get_px(&s, UI_W - 1, 0), C_BG);
    CHECK_EQ(get_px(&s, 0, UI_H - 1), C_BG);
    CHECK_EQ(get_px(&s, UI_W - 1, UI_H - 1), C_BG);

    /* 标题 MENU：y=12 起、8×8 scale 2、白、水平居中 */
    int cx = 0;
    int cy = 0;
    int n = 0;
    color_bbox(&s, 0, 12, UI_W - 1, 27, C_WHITE, &cx, &cy, &n);
    CHECK(n > 0);
    CHECK(cx >= 112 && cx <= 128);
    CHECK(cy >= 18 && cy <= 27);
}

/* ---- UI-21：5 行矩形 (4,48+i*32,232,28)、未选中底 0x18E3、
 * 选中行反色（底 0xFFFF/字 0x0000）、底部统计非空 ---- */
static void case_ui21_menu_rows(void)
{
    pet_surface_t s = surface_of(g_pixels);

    for (int sel = 0; sel < MENU_ROWS; sel++) {
        pet_menu_view_t v = menu_view(sel, 1, 3, 4500, false);
        render_menu(&s, &v);

        for (int i = 0; i < MENU_ROWS; i++) {
            const int y = MENU_ROW_Y0 + i * MENU_ROW_STEP;
            if (i == sel) {
                /* 选中行整行反色：只允许 0xFFFF（底）与 0x0000（字） */
                CHECK_EQ(count_other(&s, MENU_ROW_X, y,
                                     MENU_ROW_X + MENU_ROW_W - 1,
                                     y + MENU_ROW_H - 1, C_WHITE, C_INV_FG),
                         0);
                CHECK(count_eq(&s, MENU_ROW_X, y, MENU_ROW_X + MENU_ROW_W - 1,
                               y + MENU_ROW_H - 1, C_WHITE) > 3000);
                CHECK(count_eq(&s, MENU_ROW_X, y, MENU_ROW_X + MENU_ROW_W - 1,
                               y + MENU_ROW_H - 1, C_INV_FG) > 0);
                CHECK_EQ(get_px(&s, MENU_ROW_X, y), C_WHITE);
                CHECK_EQ(get_px(&s, MENU_ROW_X + MENU_ROW_W - 1,
                                y + MENU_ROW_H - 1),
                         C_WHITE);
            } else {
                /* 未选中行：卡底 0x18E3，文字非空，文字下方仍为卡底 */
                CHECK(count_eq(&s, MENU_ROW_X, y, MENU_ROW_X + MENU_ROW_W - 1,
                               y + MENU_ROW_H - 1, C_CARD_BG) > 3000);
                CHECK_EQ(get_px(&s, MENU_ROW_X, y), C_CARD_BG);
                CHECK(count_ne(&s, MENU_ROW_X + 4, y + 10,
                               MENU_ROW_X + MENU_ROW_W - 1, y + 17,
                               C_CARD_BG) > 0);
                CHECK_EQ(count_eq(&s, MENU_ROW_X + 4, y + 20,
                                  MENU_ROW_X + MENU_ROW_W - 1, y + 27,
                                  C_CARD_BG),
                         (MENU_ROW_W - 4) * 8);
            }

            /* 行左右外侧留黑（x=0..3 / x=236..239） */
            CHECK_EQ(count_eq(&s, 0, y, MENU_ROW_X - 1, y + MENU_ROW_H - 1,
                              C_BG),
                     MENU_ROW_X * MENU_ROW_H);
            CHECK_EQ(count_eq(&s, MENU_ROW_X + MENU_ROW_W, y, UI_W - 1,
                              y + MENU_ROW_H - 1, C_BG),
                     (UI_W - MENU_ROW_X - MENU_ROW_W) * MENU_ROW_H);

            /* 行间 4 px 留黑（48+28=76，下一行 80） */
            if (i + 1 < MENU_ROWS) {
                CHECK_EQ(count_eq(&s, MENU_ROW_X, y + MENU_ROW_H,
                                  MENU_ROW_X + MENU_ROW_W - 1,
                                  y + MENU_ROW_STEP - 1, C_BG),
                         MENU_ROW_W * (MENU_ROW_STEP - MENU_ROW_H));
            }
        }

        /* 末行（y=176..203）与底部统计（y=212）之间留黑 */
        const int last_bottom = MENU_ROW_Y0 + (MENU_ROWS - 1) * MENU_ROW_STEP +
                                MENU_ROW_H; /* 204 */
        CHECK_EQ(count_eq(&s, MENU_ROW_X, last_bottom, MENU_ROW_X + MENU_ROW_W - 1,
                          MENU_STAT_Y - 1, C_BG),
                 MENU_ROW_W * (MENU_STAT_Y - last_bottom));

        /* 底部统计非空（灰）且水平居中 */
        int cx = 0;
        int cy = 0;
        int n = 0;
        color_bbox(&s, 0, MENU_STAT_Y, UI_W - 1, MENU_STAT_Y + 7, C_GRAY, &cx,
                   &cy, &n);
        CHECK(n > 5);
        CHECK(cx >= 100 && cx <= 140);
    }
}

/* ---- UI-21：菜单内容随视图变化（MUTE: ON/OFF、底部 DONE/mM/模式） ---- */
static void case_ui21_menu_content_follows_view(void)
{
    pet_surface_t s = surface_of(g_pixels);
    pet_surface_t ref = surface_of(g_ref_pixels);

    /* 静音标志：MUTE 行文案 ON/OFF 不同 */
    pet_menu_view_t v = menu_view(0, 1, 0, 0, false);
    render_menu(&s, &v);
    v.muted = true;
    render_menu(&ref, &v);
    CHECK(count_diff(&s, &ref, MENU_ROW_X, MENU_ROW_Y0,
                     MENU_ROW_X + MENU_ROW_W - 1,
                     MENU_ROW_Y0 + MENU_ROW_H - 1) > 0);

    /* 底部统计：done_count / focus_seconds / mode 变化必须体现 */
    v = menu_view(0, 0, 0, 0, false);
    render_menu(&s, &v);
    v.done_count = 7;
    render_menu(&ref, &v);
    CHECK(count_diff(&s, &ref, 0, MENU_STAT_Y, UI_W - 1, UI_H - 1) > 0);

    v = menu_view(0, 0, 0, 0, false);
    render_menu(&s, &v);
    v.focus_seconds = 3600; /* 60M */
    render_menu(&ref, &v);
    CHECK(count_diff(&s, &ref, 0, MENU_STAT_Y, UI_W - 1, UI_H - 1) > 0);

    v = menu_view(0, 0, 0, 0, false);
    render_menu(&s, &v);
    v.mode = 1; /* DUTY vs POMO */
    render_menu(&ref, &v);
    CHECK(count_diff(&s, &ref, 0, MENU_STAT_Y, UI_W - 1, UI_H - 1) > 0);
}

/* ---- 运行器 ---- */

typedef struct {
    const char *name;
    void (*fn)(void);
} test_case_t;

int main(void)
{
    static const test_case_t cases[] = {
        {"UI-20-pomo-ring-geometry", case_ui20_pomo_ring_geometry},
        {"UI-20-pomo-progress-colors", case_ui20_pomo_progress_colors},
        {"UI-20-pomo-title-center-stats", case_ui20_pomo_title_center_stats},
        {"UI-21-menu-title", case_ui21_menu_title},
        {"UI-21-menu-rows", case_ui21_menu_rows},
        {"UI-21-menu-content-follows-view", case_ui21_menu_content_follows_view},
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
    printf("SUMMARY m3_ui: cases=%d passed=%d failed=%d\n", n,
           n - failed_cases, failed_cases);
    return failed_cases == 0 ? 0 : 1;
}
