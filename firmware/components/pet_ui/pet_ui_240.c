/* M2：240×240 版式渲染（Waveshare ESP32-S3-Touch-LCD-1.54）。
 * 契约：docs/contracts/C03-layout-240x240.md（含 §7 修订记录）；
 *       docs/08-m2-development.md §3（逐项落地取值）。
 * M4.1：状态条右端静音图标（C03 §7 R9，2026-10-31 用户要求）。
 * 上游 135×240 的 pet_render_scene/pomo/menu/skit 坐标一律不改；本文件是本项目
 * 唯一允许出现 240×240 坐标的地方。 */

#include <stdio.h>
#include <string.h>

#include "pet_render.h"
#include "pet_text.h"
#include "pet_ui_240.h"

/* ---- C03 §1 网格 ---- */
#define DOT_X 4
#define DOT_Y 4
#define DOT_W 4
#define DOT_H 4
#define DOT_ONLINE  0x07E0u /* 绿（C03 未定义在线色，C03 §7 R1 登记） */
#define DOT_OFFLINE 0xF800u /* 红（C03 §3.5 强制） */

#define PET_X 72
#define PET_Y 20
#define PET_W (48 * 2) /* 96 */
#define PET_H (64 * 2) /* 128 */
#define PET_CX (PET_X + PET_W / 2) /* 120：标记横向居中基准 */

/* ---- C03 §7 R9（M4.1）：静音图标（状态条右端，仅 muted 时绘制） ----
 * 10×10：喇叭后腔 3×4 + 向右扩音锥 + 斜杠；橙色，与在线绿/离线红区分。
 * 矩形 x=226..235, y=1..10；不进入标记区（x≈111..129）与卡区。 */
#define MUTE_X 226
#define MUTE_Y 1
#define MUTE_W 10
#define MUTE_H 10
#define MUTE_COLOR 0xFD20u

#define MARK_Y 2
#define MARK_ASCALE 2
#define MARK_COLOR_WORKING 0xFFFFu
#define MARK_COLOR_INPUT   0xFD20u

#define CARD_X 4
#define CARD_Y0 152
#define CARD_W 232
#define CARD_H 28
#define CARD_PITCH 28
#define CARD_MAX 3

/* ---- C03 §2 颜色 ---- */
#define C_BG        0x0000u
#define C_CARD_BG   0x18E3u
#define C_TEXT      0xFFFFu
#define C_META      0x8410u
#define C_ACCENT    0x05FFu /* 最新卡左侧 2px */

/* ---- 文本度量（C03 §2：8×8 ASCII scale1 / CJK 16×16） ---- */
#define ASCII_W 8
#define CJK_W   16
#define CARD_TEXT_X_OFF 4
#define LINE1_MAX_W 224
#define LINE2_MAX_W 180
#define CARD_RIGHT_PAD 4 /* 右边界 = x + CARD_W - 4 = x + 228 */

/* ---- 基础绘制 ---- */

static void put_px(pet_surface_t *s, int x, int y, uint16_t c)
{
    if (s == NULL || s->pixels == NULL || x < 0 || y < 0 || x >= s->width ||
        y >= s->height) {
        return;
    }
    uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
    p[0] = (uint8_t)(c & 0xFF); /* RGB565 little-endian */
    p[1] = (uint8_t)(c >> 8);
}

static void fill_rect(pet_surface_t *s, int x, int y, int w, int h, uint16_t c)
{
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            put_px(s, x + dx, y + dy, c);
        }
    }
}

/* 灰化：按亮度权重换算后写回三通道相等（与 pet_render.c 的 gray565 同法） */
static uint16_t gray565(uint16_t c)
{
    const uint32_t r8 = ((c >> 11) & 0x1F) * 255 / 31;
    const uint32_t g8 = ((c >> 5) & 0x3F) * 255 / 63;
    const uint32_t b8 = (c & 0x1F) * 255 / 31;
    const uint32_t level = (r8 * 77 + g8 * 150 + b8 * 29) >> 8; /* 0..255 */
    const uint32_t g6 = (level * 63 + 127) / 255;
    const uint32_t r5 = g6 >> 1;
    const uint32_t b5 = g6 >> 1;
    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

static void gray_screen(pet_surface_t *s)
{
    for (int y = 0; y < s->height; y++) {
        for (int x = 0; x < s->width; x++) {
            uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
            const uint16_t c = (uint16_t)(p[0] | (p[1] << 8));
            const uint16_t g = gray565(c);
            p[0] = (uint8_t)(g & 0xFF);
            p[1] = (uint8_t)(g >> 8);
        }
    }
}

/* ---- 头顶标记（C03 §1：2×、7 行、bit0=最左；居中于 x=120） ----
 * 缺陷 D-05（2026-10-31 真机发现）：上游 135×240 的标记点阵表按 MSB=最左书写，
 * 却按 bit0=最左绘制，导致 '>' 被镜像成 '<'、'?' 的弯钩左右颠倒。本项目按 C03 的
 * "bit0=最左" 口径把两张表镜像到正确朝向（'!' 垂直对称，不受影响）。 */

static const uint8_t GLYPH_GT[7]    = {0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01};
static const uint8_t GLYPH_BANG[7]  = {0x02, 0x02, 0x02, 0x02, 0x02, 0x00, 0x02};
static const uint8_t GLYPH_QUEST[7] = {0x0E, 0x11, 0x10, 0x08, 0x04, 0x00, 0x04};

static void draw_glyph(pet_surface_t *s, int x, int y, const uint8_t *rows,
                       int cols, uint16_t color)
{
    for (int r = 0; r < 7; r++) {
        for (int c = 0; c < cols; c++) {
            if ((rows[r] & (uint8_t)(1u << c)) != 0) {
                fill_rect(s, x + c * MARK_ASCALE, y + r * MARK_ASCALE,
                          MARK_ASCALE, MARK_ASCALE, color);
            }
        }
    }
}

static void draw_status_marker(pet_surface_t *s, pet_state_t state)
{
    switch (state) {
    case PET_STATE_WORKING: {
        const int w = 5 * MARK_ASCALE; /* 10 */
        draw_glyph(s, PET_CX - w / 2, MARK_Y, GLYPH_GT, 5, MARK_COLOR_WORKING);
        break;
    }
    case PET_STATE_INPUT_REQUIRED: {
        /* "!?"：3 列 + 2 px 间隙 + 5 列 = 18 px，整体居中 */
        const int w = 3 * MARK_ASCALE + 2 + 5 * MARK_ASCALE; /* 18 */
        const int x = PET_CX - w / 2;
        draw_glyph(s, x, MARK_Y, GLYPH_BANG, 3, MARK_COLOR_INPUT);
        draw_glyph(s, x + 3 * MARK_ASCALE + 2, MARK_Y, GLYPH_QUEST, 5,
                   MARK_COLOR_INPUT);
        break;
    }
    default:
        break;
    }
}

/* ---- 静音图标（C03 §7 R9）：仅在 view.muted 时调用 ---- */

static void draw_mute_icon(pet_surface_t *s)
{
    /* 喇叭后腔 */
    fill_rect(s, MUTE_X, MUTE_Y + 3, 3, 4, MUTE_COLOR);
    /* 向右扩音锥：4 列，高度 4/6/8/10，垂直居中 */
    for (int i = 0; i < 4; i++) {
        const int h = 4 + i * 2;
        fill_rect(s, MUTE_X + 3 + i, MUTE_Y + (MUTE_H - h) / 2, 1, h, MUTE_COLOR);
    }
    /* 斜杠（静音） */
    for (int i = 0; i < 3; i++) {
        fill_rect(s, MUTE_X + 7 + i, MUTE_Y + 1 + i * 3, 1, 3, MUTE_COLOR);
    }
}

/* ---- 文本工具 ---- */

/* 卡用时长：45S / 12M / 1H02M（≤5 字符，C03 §7 R4）。 */
static void format_elapsed_compact(char *out, size_t cap, int32_t seconds)
{
    if (out == NULL || cap == 0) {
        return;
    }
    if (seconds <= 0) {
        snprintf(out, cap, "0S");
    } else if (seconds < 60) {
        snprintf(out, cap, "%ldS", (long)seconds);
    } else if (seconds < 3600) {
        snprintf(out, cap, "%ldM", (long)(seconds / 60));
    } else {
        const long h = seconds / 3600;
        const long m = (seconds % 3600) / 60;
        if (h < 10) {
            snprintf(out, cap, "%ldH%02ldM", h, m); /* 1H02M：5 字符 */
        } else {
            snprintf(out, cap, "%ldH", h); /* 10H+ 只到小时，保证 ≤5 字符 */
        }
    }
}

/* 把 UTF-8 文本规约为"可画 ASCII"：非 ASCII 每字符 → '?'（8px）。 */
static size_t sanitize_ascii(const char *src, char *dst, size_t cap)
{
    size_t n = 0;
    for (const char *p = src; *p != '\0';) {
        const unsigned char b = (unsigned char)*p;
        char ch = '?';
        if (b < 0x80u) {
            ch = (char)b;
            p++;
        } else {
            int len = 1;
            if (b >= 0xC2u && b <= 0xDFu) {
                len = 2;
            } else if (b >= 0xE0u && b <= 0xEFu) {
                len = 3;
            } else if (b >= 0xF0u && b <= 0xF4u) {
                len = 4;
            }
            p += len;
        }
        if (n + 1 < cap) {
            dst[n++] = ch;
        }
    }
    if (cap > 0) {
        dst[n] = '\0';
    }
    return n;
}

/* 行 1：`<agent>: <project>`（ASCII，8×8），宽 ≤224，溢出尾 ".." */
static void draw_card_line1(pet_surface_t *s, int x, int y, const pet_card_t *c)
{
    char raw[PET_AGENT_MAX + 2 + PET_PROJECT_MAX + 1];
    size_t len = 0;
    raw[0] = '\0';
    if (c->agent[0] != '\0') {
        const size_t alen = strlen(c->agent);
        if (alen < sizeof raw) {
            memcpy(raw, c->agent, alen);
            len = alen;
        }
        if (len > 0 && raw[len - 1] != ':' && len + 1 < sizeof raw) {
            raw[len++] = ':';
        }
        if (len + 1 < sizeof raw) {
            raw[len++] = ' ';
        }
        raw[len] = '\0';
    }
    const char *proj = (c->project[0] != '\0') ? c->project : "-";
    snprintf(raw + len, sizeof raw - len, "%s", proj);

    char clean[sizeof raw];
    size_t n = sanitize_ascii(raw, clean, sizeof clean);
    const int max_chars = LINE1_MAX_W / ASCII_W; /* 28 */
    char out[sizeof raw + 3];
    if ((int)n > max_chars) {
        memcpy(out, clean, (size_t)(max_chars - 2));
        memcpy(out + max_chars - 2, "..", 2);
        out[max_chars] = '\0';
        n = (size_t)max_chars;
    } else {
        memcpy(out, clean, n + 1);
    }
    (void)pet_text_draw(s, x, y, out, (int)n, C_META, 1);
}

/* UTF-8 解码：返回字节数；*cp 输出码点（非法 → '?'） */
static int utf8_decode(const char *p, uint32_t *cp)
{
    const unsigned char b0 = (unsigned char)p[0];
    if (b0 < 0x80u) {
        *cp = b0;
        return 1;
    }
    int need;
    uint32_t v;
    if (b0 >= 0xC2u && b0 <= 0xDFu) {
        need = 1;
        v = b0 & 0x1Fu;
    } else if (b0 >= 0xE0u && b0 <= 0xEFu) {
        need = 2;
        v = b0 & 0x0Fu;
    } else if (b0 >= 0xF0u && b0 <= 0xF4u) {
        need = 3;
        v = b0 & 0x07u;
    } else {
        *cp = '?';
        return 1;
    }
    for (int i = 1; i <= need; i++) {
        const unsigned char bi = (unsigned char)p[i];
        if ((bi & 0xC0u) != 0x80u) {
            *cp = '?';
            return i;
        }
        v = (v << 6) | (bi & 0x3Fu);
    }
    *cp = v;
    return need + 1;
}

/* 行 2：标题；CJK 16×16 顶边 y+12、ASCII 8×8 顶边 y+16（同一 16px 行），
 * 宽 ≤180，溢出尾 ".."（ASCII）。空 title → "(untitled)"。 */
static void draw_card_line2(pet_surface_t *s, int x, int y, const pet_card_t *c)
{
    const char *title = (c->title[0] != '\0') ? c->title : "(untitled)";
    const int res_w = 2 * ASCII_W; /* ".." 预留 */
    int max_w = LINE2_MAX_W;

    int total = 0;
    for (const char *p = title; *p != '\0';) {
        uint32_t cp;
        const int n = utf8_decode(p, &cp);
        total += (cp < 0x80u) ? ASCII_W : CJK_W;
        p += n;
    }
    const bool trunc = (total > LINE2_MAX_W);
    if (trunc) {
        max_w -= res_w;
    }

    int cx = x;
    for (const char *p = title; *p != '\0';) {
        uint32_t cp;
        const int n = utf8_decode(p, &cp);
        const bool ascii = (cp < 0x80u);
        const int w = ascii ? ASCII_W : CJK_W;
        if (trunc && cx + w > x + max_w) {
            break;
        }
        if (ascii) {
            const char one[2] = {(char)cp, '\0'};
            (void)pet_text_draw(s, cx, y + 4, one, 1, C_TEXT, 1);
        } else {
            const uint8_t *g = pet_font16_glyph(cp);
            if (g != NULL) {
                (void)pet_text_draw16(s, cx, y, p, 1, C_TEXT, 1);
            } else {
                (void)pet_text_draw(s, cx, y + 4, "?", 1, C_TEXT, 1);
            }
        }
        cx += w;
        p += n;
    }
    if (trunc) {
        (void)pet_text_draw(s, cx, y + 4, "..", 2, C_TEXT, 1);
    }
}

/* ---- 单卡（232×28） ---- */

void pet_ui_card_240(const pet_card_t *c, pet_surface_t *s, int x, int y,
                     bool newest)
{
    if (c == NULL || s == NULL || s->pixels == NULL) {
        return;
    }
    fill_rect(s, x, y, CARD_W, CARD_H, C_CARD_BG);
    if (newest) {
        fill_rect(s, x, y, 2, CARD_H, C_ACCENT); /* C03 §2：左侧 2 px 强调条 */
    }

    const int tx = x + CARD_TEXT_X_OFF;
    draw_card_line1(s, tx, y + 3, c);

    char elapsed[8];
    format_elapsed_compact(elapsed, sizeof elapsed, c->elapsed_s);
    const int ew = (int)strlen(elapsed) * ASCII_W;
    const int ex = x + CARD_W - CARD_RIGHT_PAD - ew;
    /* 时长右对齐；与标题行 ASCII 基线一致（C03 §7 R3） */
    (void)pet_text_draw(s, ex, y + 16, elapsed, (int)sizeof elapsed - 1, C_META, 1);

    draw_card_line2(s, tx, y + 12, c);
}

/* ---- 整屏场景 ---- */

void pet_ui_scene_240(const pet_view_t *v, const pet_look_t *look,
                      pet_surface_t *s)
{
    if (s == NULL || s->pixels == NULL || s->width <= 0 || s->height <= 0) {
        return;
    }
    /* 底色（C03 §1 页脚/背景 0x0000） */
    fill_rect(s, 0, 0, s->width, s->height, C_BG);

    /* 状态条：左上 4×4 网络点（右侧电池/时间 M3 起）；右端静音图标（C03 §7 R9） */
    fill_rect(s, DOT_X, DOT_Y, DOT_W, DOT_H, DOT_ONLINE);
    if (v != NULL && v->muted) {
        draw_mute_icon(s);
    }

    /* 宠物（无标记副本，标记由本文件按 C03 居中绘制） */
    if (v != NULL && look != NULL) {
        pet_view_t copy = *v;
        copy.state = PET_STATE_IDLE; /* pet_render_pet 的 default 分支不画标记 */
        copy.gray = false;           /* OFFLINE 由整帧灰度统一处理 */
        pet_render_pet(&copy, look, s, PET_X, PET_Y);
    }

    /* 头顶标记（C03 §1：仅 WORKING / INPUT_REQUIRED） */
    if (v != NULL) {
        draw_status_marker(s, v->state);
    }

    /* 任务卡栈（C03 §3：cards[0] 最新，渲染在 y=152） */
    if (v != NULL) {
        const int n = (v->card_count > CARD_MAX) ? CARD_MAX : v->card_count;
        for (int i = 0; i < n; i++) {
            pet_ui_card_240(&v->cards[i], s, CARD_X, CARD_Y0 + i * CARD_PITCH,
                            i == 0);
        }
    }

    /* OFFLINE：整帧灰度后网络点红（C03 §3.5 + §7 R1） */
    if (v != NULL && v->gray) {
        gray_screen(s);
        fill_rect(s, DOT_X, DOT_Y, DOT_W, DOT_H, DOT_OFFLINE);
    }
}
