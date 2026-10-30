/* pet_render：立绘解析 + 绘制 + 状态标记。
 * 契约：contracts/M1-渲染分层契约冻结.md（接口/格式）、
 *       contracts/M2-状态机与动画契约清单.md §8（gray / mirror / 头顶标记）、
 *       contracts/M7-番茄钟契约清单.md §7.1/§7.2（进度环 / 番茄钟整屏）。 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pet_render.h"
#include "pet_text.h"

/* 立绘包格式（与上游 look.rs 规格一致，净室实现） */
#define LOOK_MAGIC      "LOOK"
#define LOOK_W          48
#define LOOK_H          64
#define LOOK_FRAMES     12 /* 6 态 × 2 毛发相位 */
#define LOOK_COLORS     16
#define LOOK_FRAME_BYTES (LOOK_W * LOOK_H / 2)
#define LOOK_HEADER     (8 + LOOK_COLORS * 2)
#define LOOK_BYTES      (LOOK_HEADER + LOOK_FRAMES * LOOK_FRAME_BYTES)

/* 立绘在屏幕上按 2× 绘制（对应上游 look::SCALE） */
#define PET_LOOK_SCALE  2

/* 头顶标记（契约 §8.2，提案值） */
#ifndef PET_MARK_OX
#define PET_MARK_OX 56
#endif
#ifndef PET_MARK_OY
#define PET_MARK_OY (-18)
#endif
#define MARK_COLOR_WORKING 0xFFFFu /* 白 */
#define MARK_COLOR_INPUT   0xFD20u /* 橙 */

struct pet_look {
    uint16_t palette[LOOK_COLORS];
    uint8_t  frames[LOOK_FRAMES][LOOK_FRAME_BYTES];
};

/* ---- 解析 ---- */

pet_look_t *pet_look_parse(const uint8_t *bytes, size_t len)
{
    if (bytes == NULL || len != LOOK_BYTES)
        return NULL;
    if (memcmp(bytes, LOOK_MAGIC, 4) != 0)
        return NULL;
    if (bytes[4] != LOOK_W || bytes[5] != LOOK_H || bytes[6] != LOOK_FRAMES)
        return NULL;

    pet_look_t *look = malloc(sizeof *look);
    if (look == NULL)
        return NULL;

    for (int i = 0; i < LOOK_COLORS; i++)
        look->palette[i] = (uint16_t)(bytes[8 + 2 * i] | (bytes[9 + 2 * i] << 8));

    memcpy(look->frames, bytes + LOOK_HEADER, LOOK_FRAMES * LOOK_FRAME_BYTES);
    return look;
}

void pet_look_free(pet_look_t *look)
{
    free(look);
}

/* ---- 绘制工具 ---- */

static void put_px(pet_surface_t *s, int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= s->width || y >= s->height)
        return;
    uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
    p[0] = (uint8_t)(c & 0xFF); /* RGB565 little-endian */
    p[1] = (uint8_t)(c >> 8);
}

static void fill_block(pet_surface_t *s, int x, int y, uint16_t c)
{
    for (int dy = 0; dy < PET_LOOK_SCALE; dy++)
        for (int dx = 0; dx < PET_LOOK_SCALE; dx++)
            put_px(s, x + dx, y + dy, c);
}

/* 灰化：按亮度权重换算后写回三通道相等（上游 look::gray 同法） */
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

/* 头顶标记字符：每行一个位掩码，bit0 = 最左列 */
static const uint8_t GLYPH_GT[7]    = {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10};
static const uint8_t GLYPH_BANG[7]  = {0x02, 0x02, 0x02, 0x02, 0x02, 0x00, 0x02};
static const uint8_t GLYPH_QUEST[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
#define GLYPH_ROWS 7

static void draw_glyph(pet_surface_t *s, int x, int y, const uint8_t *rows,
                       int width, uint16_t color)
{
    for (int r = 0; r < GLYPH_ROWS; r++) {
        for (int c = 0; c < width; c++) {
            if ((rows[r] & (uint8_t)(1u << c)) != 0) {
                fill_block(s, x + c * PET_LOOK_SCALE, y + r * PET_LOOK_SCALE, color);
            }
        }
    }
}

/* ---- 主绘制 ---- */

void pet_render_pet(const pet_view_t *v, const pet_look_t *look,
                    pet_surface_t *surface, int x, int y)
{
    if (look == NULL || surface == NULL || surface->pixels == NULL)
        return;
    if (surface->width <= 0 || surface->height <= 0)
        return;

    int frame = (v != NULL) ? ((int)v->frame * 2 + (v->hair_right ? 1 : 0)) : 0;
    if (frame < 0 || frame >= LOOK_FRAMES)
        frame = 0;
    const bool gray = (v != NULL) && v->gray;
    const bool mirror = (v != NULL) && v->mirror;
    const uint8_t *pixels = look->frames[frame];

    for (int ly = 0; ly < LOOK_H; ly++) {
        for (int lx = 0; lx < LOOK_W; lx++) {
            const int sx = mirror ? (LOOK_W - 1 - lx) : lx; /* 翻转：输出列取镜像输入列 */
            const int at = ly * LOOK_W + sx;
            const uint8_t packed = pixels[at / 2];
            const uint8_t index = (at % 2 == 0) ? (uint8_t)(packed & 0x0F)
                                                : (uint8_t)(packed >> 4);
            if (index == 0)
                continue; /* 调色板 0 = 透明 */

            uint16_t color = look->palette[index];
            if (gray)
                color = gray565(color);
            fill_block(surface, x + lx * PET_LOOK_SCALE, y + ly * PET_LOOK_SCALE,
                       color);
        }
    }

    /* 状态标记：画在头顶上方 */
    if (v != NULL) {
        const int mx = x + PET_MARK_OX;
        const int my = y + PET_MARK_OY;
        switch (v->state) {
        case PET_STATE_WORKING:
            draw_glyph(surface, mx, my, GLYPH_GT, 5, MARK_COLOR_WORKING);
            break;
        case PET_STATE_INPUT_REQUIRED:
            draw_glyph(surface, mx, my, GLYPH_BANG, 3, MARK_COLOR_INPUT);
            draw_glyph(surface, mx + 4 * PET_LOOK_SCALE, my, GLYPH_QUEST, 5,
                       MARK_COLOR_INPUT);
            break;
        default:
            break;
        }
    }
}

/* ---- M3：整屏场景 ---- */

#define CARD_BG      0x0841u
#define CARD_BORDER  0x4208u
#define CARD_TITLE   0xFFFFu
#define CARD_PROJECT 0xC618u /* 次要文字：调浅 */
#define CARD_ELAPSED 0xFFFFu
#ifndef CARD_TEXT_MAX
#define CARD_TEXT_MAX 14
#endif
#define CARD_TITLE_MAX_BIG 7 /* 2× 字体下的最大字符数 */
#define CARD_ACCENT_W 3

static void fill_rect(pet_surface_t *s, int x0, int y0, int x1, int y1, uint16_t c)
{
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            put_px(s, x, y, c);
        }
    }
}

static uint16_t state_accent(pet_state_t st)
{
    switch (st) {
    case PET_STATE_WORKING:
    case PET_STATE_INPUT_REQUIRED:
        return 0xFD20u; /* 橙 */
    case PET_STATE_DONE:
        return 0x07E0u; /* 绿 */
    case PET_STATE_FAILED:
        return 0xF800u; /* 红 */
    default:
        return 0x8410u; /* 灰 */
    }
}

/* 可渲染（ASCII）字符数；UTF-8 多字节按 1 组跳过。 */
static size_t ascii_len(const char *text)
{
    size_t n = 0;
    for (const char *p = text; *p != '\0'; p++) {
        if ((unsigned char)*p < 0x80u) {
            n++;
        } else {
            while (p[1] != '\0' && ((unsigned char)p[1] & 0xC0) == 0x80) {
                p++;
            }
        }
    }
    return n;
}

/* 卡片文本：短行用 2× 字体（更醒目），长行退回 1× 以免截断过多。 */
static void draw_card_line(pet_surface_t *s, int x, int y, const char *text,
                           uint16_t color, int max_scale)
{
    while (*text == ' ') {
        text++; /* 中文被跳过后留下的前导空格：左对齐 */
    }
    const size_t n = ascii_len(text);
    int scale = (n <= CARD_TITLE_MAX_BIG) ? 2 : 1;
    if (scale > max_scale) {
        scale = max_scale;
    }
    const int max_chars = (scale == 2) ? CARD_TITLE_MAX_BIG : CARD_TEXT_MAX;
    (void)pet_text_draw(s, x, y, text, max_chars, color, scale);
}

/* 卡片标题显示文本：空 → "[NO TASK]"；纯非 ASCII（字库画不出）→ "[TASK]" */
static const char *card_title_text(const pet_card_t *card)
{
    const char *title = (card->title[0] != '\0') ? card->title : "[NO TASK]";
    if (ascii_len(title) == 0) {
        title = "[TASK]";
    }
    return title;
}

static void draw_card(pet_surface_t *s, const pet_card_t *card)
{
    const int x0 = PET_CARD_X;
    const int y0 = PET_CARD_Y;
    const int x1 = PET_CARD_X + PET_CARD_W - 1;
    const int y1 = PET_CARD_Y + PET_CARD_H - 1;

    fill_rect(s, x0, y0, x1, y1, CARD_BG);
    /* 边框 */
    fill_rect(s, x0, y0, x1, y0, CARD_BORDER);
    fill_rect(s, x0, y1, x1, y1, CARD_BORDER);
    fill_rect(s, x0, y0, x0, y1, CARD_BORDER);
    fill_rect(s, x1, y0, x1, y1, CARD_BORDER);
    /* 左侧状态色条 */
    fill_rect(s, x0 + 1, y0 + 1, x0 + CARD_ACCENT_W, y1 - 1, state_accent(card->state));

    const int tx = x0 + CARD_ACCENT_W + 4;
    draw_card_line(s, tx, y0 + 4, card_title_text(card), CARD_TITLE, 2);
    if (card->project[0] != '\0') {
        draw_card_line(s, tx, y0 + 24, card->project, CARD_PROJECT, 2);
    }
    char elapsed[16];
    pet_format_elapsed(elapsed, sizeof elapsed, card->elapsed_s);
    draw_card_line(s, tx, y0 + 44, elapsed, CARD_ELAPSED, 1); /* 时长行固定 1× */
}

/* ---- M8 增量 2：多卡紧凑行（契约 M8 §8.4）---- */

#define STACK_X0        4
#define STACK_X1        131
#define STACK_Y0        150
#define STACK_ROW_H     28
#define STACK_TEXT_MAX  14
#define STACK_TEXT_X    (STACK_X0 + CARD_ACCENT_W + 4)

/* 一行紧凑卡：左侧 3px 状态条 + 标题（1×）+ 第二行 project · elapsed（1× 灰） */
static void draw_card_row(pet_surface_t *s, int y, const pet_card_t *card)
{
    fill_rect(s, STACK_X0, y, STACK_X0 + CARD_ACCENT_W - 1, y + STACK_ROW_H - 1,
              state_accent(card->state));
    (void)pet_text_draw(s, STACK_TEXT_X, y + 3, card_title_text(card),
                        STACK_TEXT_MAX, CARD_TITLE, 1);
    char elapsed[16];
    pet_format_elapsed(elapsed, sizeof elapsed, card->elapsed_s);
    char line[PET_PROJECT_MAX + 24]; /* 63 + " · "(4) + elapsed(15) + NUL，留足不触发 -Wformat-truncation */
    if (card->project[0] != '\0') {
        snprintf(line, sizeof line, "%s \xc2\xb7 %s", card->project, elapsed);
    } else {
        snprintf(line, sizeof line, "%s", elapsed);
    }
    (void)pet_text_draw(s, STACK_TEXT_X, y + 15, line, STACK_TEXT_MAX,
                        CARD_PROJECT, 1);
}

static void draw_card_stack(pet_surface_t *s, const pet_view_t *v)
{
    const int rows = (v->card_count > 3) ? 3 : v->card_count;
    for (int i = 0; i < rows; i++) {
        draw_card_row(s, STACK_Y0 + i * STACK_ROW_H, &v->cards[i]);
    }
}

void pet_render_scene(const pet_view_t *v, const pet_look_t *look,
                      pet_surface_t *surface)
{
    if (surface == NULL || surface->pixels == NULL) {
        return;
    }
    /* 场景底色 */
    for (int y = 0; y < surface->height; y++) {
        for (int x = 0; x < surface->width; x++) {
            put_px(surface, x, y, PET_SCENE_BG);
        }
    }
    /* 宠物（固定在宠物区） */
    pet_render_pet(v, look, surface, PET_SCENE_PET_X, PET_SCENE_PET_Y);
    /* 任务卡：1 张 → 大卡；≥2 张 → 紧凑行（M8 §8.4；card_count==0 但
     * has_card 的旧式视图按 1 张处理，兼容 M3 行为） */
    if (v != NULL && v->card_count >= 2) {
        draw_card_stack(surface, v);
    } else if (v != NULL && v->has_card) {
        draw_card(surface, &v->card);
    }
}

/* ---- M7 增量 2：番茄钟视图（契约 §7.1/§7.2）---- */

/* 0°=12 点、顺时针（屏幕 y 向下，atan2(dx, -dy) 即给出顺时针角）。
 * 自定义 PI：-std=c11 严格模式下 M_PI 不保证可见。 */
#define POMO_PI 3.14159265358979323846

#define POMO_RING_CX 67
#define POMO_RING_CY 72
#define POMO_RING_R  48
#define POMO_RING_T  6
#define POMO_C_FOCUS 0xFD20u /* 橙 */
#define POMO_C_BREAK 0x07E0u /* 绿 */
#define POMO_C_OTHER 0x8410u /* 灰 */
#define POMO_C_TRACK 0x4208u
#define POMO_C_TEXT  0xFFFFu /* 白 */

void pet_render_ring(pet_surface_t *s, int cx, int cy, int r, int t,
                     int progress_permille, uint16_t color, uint16_t track)
{
    if (s == NULL || s->pixels == NULL || r <= 0 || t < 0) {
        return;
    }
    if (progress_permille < 0) {
        progress_permille = 0;
    } else if (progress_permille > 1000) {
        progress_permille = 1000;
    }

    const int inner = (t < r) ? (r - t) : 0; /* 洞半径；t >= r 时退化为实心圆 */
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            const int d2 = dx * dx + dy * dy;
            if (d2 > r * r || d2 < inner * inner) {
                continue; /* 环外 / 洞内 */
            }
            double ang = atan2((double)dx, (double)(-dy)) * 180.0 / POMO_PI;
            if (ang < 0) {
                ang += 360.0;
            }
            const uint16_t c = (ang * 1000.0 <= (double)progress_permille * 360.0)
                ? color
                : track;
            put_px(s, cx + dx, cy + dy, c);
        }
    }
}

static uint16_t pomo_phase_color(pet_pomo_phase_t phase)
{
    switch (phase) {
    case PET_POMO_FOCUS:
    case PET_POMO_FOCUS_PAUSED:
        return POMO_C_FOCUS;
    case PET_POMO_BREAK:
    case PET_POMO_BREAK_PAUSED:
        return POMO_C_BREAK;
    default:
        return POMO_C_OTHER;
    }
}

void pet_render_pomo(pet_surface_t *s, const pet_pomo_view_t *v)
{
    if (s == NULL || s->pixels == NULL) {
        return;
    }
    for (int y = 0; y < s->height; y++) {
        for (int x = 0; x < s->width; x++) {
            put_px(s, x, y, PET_SCENE_BG);
        }
    }
    if (v == NULL) {
        return;
    }

    /* 进度 = elapsed/total 千分比；total==0 → 0 */
    const uint32_t elapsed = (v->total_ms > 0) ? v->total_ms - v->remaining_ms : 0;
    const int permille = (v->total_ms > 0)
        ? (int)((uint32_t)elapsed * 1000u / v->total_ms)
        : 0;
    pet_render_ring(s, POMO_RING_CX, POMO_RING_CY, POMO_RING_R, POMO_RING_T,
                    permille, pomo_phase_color(v->phase), POMO_C_TRACK);

    /* 中心剩余 M:SS，水平居中于环心 */
    const uint32_t rem_s = v->remaining_ms / 1000u;
    char txt[16];
    snprintf(txt, sizeof txt, "%u:%02u", (unsigned)(rem_s / 60u),
             (unsigned)(rem_s % 60u));
    const int tx = POMO_RING_CX - (int)strlen(txt) * 8 / 2;
    (void)pet_text_draw(s, tx, POMO_RING_CY - 4, txt, (int)sizeof txt,
                        POMO_C_TEXT, 1);

    /* M12：拿到 IP 时在底部显示 WiFi 地址（无 IP 不占位） */
    if (v->ip[0] != '\0') {
        const int n = (int)strlen(v->ip);
        (void)pet_text_draw(s, POMO_RING_CX - (4 * 8) / 2, 186, "WIFI", 4,
                            POMO_C_OTHER, 1);
        (void)pet_text_draw(s, POMO_RING_CX - (n * 8) / 2, 200, v->ip,
                            (int)sizeof v->ip, POMO_C_TEXT, 1);
    }
}

/* ---- M6 增量 3：菜单整屏（契约 §9.3）---- */

#define MENU_TEXT_MAX 15
#define MENU_ROW_X    12
#define MENU_ROW_W    123 /* 反色矩形宽（x=6 起） */
#define MENU_ROW_H    14  /* 反色矩形高（y=row_y-3 起） */
#define MENU_C_TEXT   0xFFFFu /* 白（未选中行文字 / 反色行底） */
#define MENU_C_INVERT 0x0000u /* 黑（反色行文字；与场景底色同值） */

/* 数值相位 → 短 ASCII 词；未知 → "?" */
static const char *menu_phase_word(int phase)
{
    switch (phase) {
    case PET_POMO_IDLE:
        return "IDLE";
    case PET_POMO_FOCUS:
        return "FOCUS";
    case PET_POMO_FOCUS_PAUSED:
        return "FPAUSE";
    case PET_POMO_BREAK:
        return "BREAK";
    case PET_POMO_BREAK_PAUSED:
        return "BPAUSE";
    case PET_POMO_FOCUS_DONE:
        return "FDONE";
    case PET_POMO_BREAK_DONE:
        return "BDONE";
    default:
        return "?";
    }
}

/* 画一行：选中行反色（白底矩形 + 黑字），未选中行白字 */
static void menu_draw_row(pet_surface_t *s, int y, const char *text,
                          bool selected)
{
    if (selected) {
        fill_rect(s, 6, y - 3, 6 + MENU_ROW_W - 1, y - 3 + MENU_ROW_H - 1,
                  MENU_C_TEXT);
        (void)pet_text_draw(s, MENU_ROW_X, y, text, MENU_TEXT_MAX,
                            MENU_C_INVERT, 1);
    } else {
        (void)pet_text_draw(s, MENU_ROW_X, y, text, MENU_TEXT_MAX,
                            MENU_C_TEXT, 1);
    }
}

void pet_render_menu(pet_surface_t *s, const pet_menu_view_t *v)
{
    if (s == NULL || s->pixels == NULL) {
        return;
    }
    for (int y = 0; y < s->height; y++) {
        for (int x = 0; x < s->width; x++) {
            put_px(s, x, y, PET_SCENE_BG);
        }
    }
    if (v == NULL) {
        return;
    }

    char row[48];
    (void)pet_text_draw(s, MENU_ROW_X, 16, "MENU", MENU_TEXT_MAX, MENU_C_TEXT, 1);

    snprintf(row, sizeof row, "MUTE: %s", v->muted ? "ON" : "OFF");
    menu_draw_row(s, 48, row, v->selected == 0);

    snprintf(row, sizeof row, "STATUS: %s %s", (v->mode == 0) ? "DUTY" : "POMO",
             menu_phase_word(v->phase));
    menu_draw_row(s, 80, row, v->selected == 1);

    menu_draw_row(s, 112, "WIFI: SETUP", v->selected == 2);
    menu_draw_row(s, 144, "BT: PAIR", v->selected == 3);
    menu_draw_row(s, 176, "BACK: WINDOW", v->selected == 4);

    snprintf(row, sizeof row, "done %lu  %lus", (unsigned long)v->done_count,
             (unsigned long)v->focus_seconds);
    (void)pet_text_draw(s, MENU_ROW_X, 208, row, MENU_TEXT_MAX, MENU_C_TEXT, 1);
}

/* ---- M8 增量 1b：小剧渲染（契约 M8 §7.1）---- */

/* 位移周期（2 s）与道具颜色（硬编码 RGB565，与头顶标记同法） */
#define SKIT_PERIOD_MS 2000u
#define SKIT_C_STAR 0xFFE0u /* 黄：星光 */
#define SKIT_C_BALL 0xFFFFu /* 白：小球 */
#define SKIT_C_BOOK 0x8410u /* 灰：书身 */
#define SKIT_C_PAGE 0xFFFFu /* 白：书页线 */
#define SKIT_C_INK  0x0000u /* 黑：梦话嘴部 */

/* 踢球：8×8 小球（切角圆盘），右下沿弧线（sin 拱）滚动 */
static void draw_skit_ball(pet_surface_t *s, uint32_t phase)
{
    const int bx = 96 + (int)(phase * 24u / SKIT_PERIOD_MS);
    const int by = 168 -
        (int)lround(24.0 * sin(POMO_PI * (double)phase / (double)SKIT_PERIOD_MS));
    for (int dy = 0; dy < 8; dy++) {
        for (int dx = 0; dx < 8; dx++) {
            const int ex = 2 * dx - 7;
            const int ey = 2 * dy - 7;
            if (ex * ex + ey * ey <= 52) {
                put_px(s, bx + dx, by + dy, SKIT_C_BALL);
            }
        }
    }
}

/* 看书：头像（96px 宽）正下方 16×8 小书 + 两条书页线 */
static void draw_skit_book(pet_surface_t *s)
{
    const int x0 = PET_SCENE_PET_X + 40;
    const int y0 = PET_SCENE_PET_Y + 134; /* 头像下沿（低头 +2 后 154）再留 4px */
    fill_rect(s, x0, y0, x0 + 15, y0 + 7, SKIT_C_BOOK);
    fill_rect(s, x0, y0 + 2, x0 + 15, y0 + 2, SKIT_C_PAGE);
    fill_rect(s, x0, y0 + 5, x0 + 15, y0 + 5, SKIT_C_PAGE);
}

/* 数星星：头顶上方 3 颗十字星光，按 250 ms 槽位交错显隐 */
static void draw_skit_sparkles(pet_surface_t *s, uint32_t phase)
{
    static const int sx[3] = {30, 66, 102};
    static const int sy[3] = {12, 6, 14};
    const uint32_t slot = phase / 250u;
    for (int i = 0; i < 3; i++) {
        if ((slot + (uint32_t)i) % 2u != 0u) {
            continue;
        }
        for (int d = -2; d <= 2; d++) {
            put_px(s, sx[i] + d, sy[i], SKIT_C_STAR);
            put_px(s, sx[i], sy[i] + d, SKIT_C_STAR);
        }
    }
}

void pet_render_skit(pet_surface_t *s, const pet_look_t *look,
                     const pet_view_t *v, int skit_id, uint32_t t_ms)
{
    if (s == NULL || s->pixels == NULL) {
        return;
    }
    if (skit_id < 0 || skit_id > 6) {
        pet_render_scene(v, look, s); /* 非法剧目：退回值班整屏 */
        return;
    }
    for (int y = 0; y < s->height; y++) {
        for (int x = 0; x < s->width; x++) {
            put_px(s, x, y, PET_SCENE_BG);
        }
    }
    if (look == NULL) {
        return;
    }

    /* 需要换帧的剧目改写本地副本，不动调用方的 view */
    pet_view_t local;
    const pet_view_t *vp = v;
    if (v != NULL) {
        local = *v;
        vp = &local;
    }

    const uint32_t phase = t_ms % SKIT_PERIOD_MS;
    const double ang = 2.0 * POMO_PI * (double)phase / (double)SKIT_PERIOD_MS;
    int ox = 0;
    int oy = 0;

    switch (skit_id) {
    case 0: /* 巡逻：x = ±6 来回（正弦取余弦相位：0 ms 在 +6、1000 ms 在 −6） */
        ox = (int)lround(6.0 * cos(ang));
        break;
    case 1: /* 踢球：y 弹跳 ±3 */
        oy = (int)lround(3.0 * sin(ang));
        break;
    case 2: /* 看书：略低头 */
        oy = 2;
        break;
    case 3: /* 数星星：上抬 */
        oy = -2;
        break;
    case 4: /* 躲猫猫：y 0→+10→0 三角波；躲下（y>4）换闭眼帧 */
        oy = (phase < 1000u) ? (int)(phase / 100u)
                             : (int)((SKIT_PERIOD_MS - phase) / 100u);
        if (oy > 4 && v != NULL) {
            local.frame = PET_FRAME_EYES_CLOSED;
        }
        break;
    case 5: /* 被吓醒：前 300 ms 急跳 −6→0；睁大眼帧 */
        oy = (phase < 300u) ? -(int)((300u - phase) / 50u) : 0;
        if (v != NULL) {
            local.frame = PET_FRAME_INPUT;
        }
        break;
    case 6: /* 梦话：静止闭眼 */
        if (v != NULL) {
            local.frame = PET_FRAME_EYES_CLOSED;
        }
        break;
    default:
        break;
    }

    pet_render_pet(vp, look, s, PET_SCENE_PET_X + ox, PET_SCENE_PET_Y + oy);

    switch (skit_id) {
    case 1:
        draw_skit_ball(s, phase);
        break;
    case 2:
        draw_skit_book(s);
        break;
    case 3:
        draw_skit_sparkles(s, phase);
        break;
    case 6: /* 嘴部 2×1 开合：立绘嘴部 look(21,41) → 屏幕 (61,106) */
        if ((phase / 250u) % 2u == 0u) {
            fill_rect(s, PET_SCENE_PET_X + 42, PET_SCENE_PET_Y + 82,
                      PET_SCENE_PET_X + 43, PET_SCENE_PET_Y + 82, SKIT_C_INK);
        }
        break;
    default:
        break;
    }
}
