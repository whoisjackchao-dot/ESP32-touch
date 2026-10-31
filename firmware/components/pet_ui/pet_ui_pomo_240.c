/* M3：240×240 番茄钟页。
 * 契约：docs/10-m3-development.md §5.1。
 * 复用 pet_text_draw（8×8）；圆环按 12 点顺时针、进度 elapsed/total 千分比。 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pet_render.h"
#include "pet_text.h"
#include "pet_ui_pomo_240.h"

#define RING_CX 120
#define RING_CY 104
#define RING_R  76
#define RING_T  10

#define C_BG     0x0000u
#define C_TRACK  0x4208u
#define C_FOCUS  0xFD20u /* 橙 */
#define C_BREAK  0x07E0u /* 绿 */
#define C_OTHER  0x8410u /* 灰 */
#define C_TEXT   0xFFFFu

#define POMO_PI 3.14159265358979323846

static void put_px(pet_surface_t *s, int x, int y, uint16_t c)
{
    if (s == NULL || s->pixels == NULL || x < 0 || y < 0 || x >= s->width ||
        y >= s->height) {
        return;
    }
    uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
    p[0] = (uint8_t)(c & 0xFF);
    p[1] = (uint8_t)(c >> 8);
}

static uint16_t phase_color(pet_pomo_phase_t ph)
{
    /* docs/10 §5.1：专注=橙、休息=绿、暂停/其它（IDLE/DONE）=灰 */
    switch (ph) {
    case PET_POMO_FOCUS:
        return C_FOCUS;
    case PET_POMO_BREAK:
        return C_BREAK;
    default:
        return C_OTHER;
    }
}

static const char *phase_label(pet_pomo_phase_t ph)
{
    switch (ph) {
    case PET_POMO_IDLE: return "READY";
    case PET_POMO_FOCUS: return "FOCUS";
    case PET_POMO_FOCUS_PAUSED: return "PAUSED";
    case PET_POMO_FOCUS_DONE: return "DONE";
    case PET_POMO_BREAK: return "BREAK";
    case PET_POMO_BREAK_PAUSED: return "PAUSED";
    case PET_POMO_BREAK_DONE: return "DONE";
    default: return "?";
    }
}

/* 进度环：0°=12 点、顺时针；0..1000 千分比 */
static void draw_ring(pet_surface_t *s, int cx, int cy, int r, int t,
                      int permille, uint16_t color)
{
    if (permille < 0) {
        permille = 0;
    } else if (permille > 1000) {
        permille = 1000;
    }
    const int inner = (t < r) ? (r - t) : 0;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            const int d2 = dx * dx + dy * dy;
            if (d2 > r * r || d2 < inner * inner) {
                continue;
            }
            double ang = atan2((double)dx, (double)(-dy)) * 180.0 / POMO_PI;
            if (ang < 0) {
                ang += 360.0;
            }
            const bool filled =
                (ang * 1000.0 < (double)permille * 360.0);
            put_px(s, cx + dx, cy + dy, filled ? color : C_TRACK);
        }
    }
}

static void draw_text_centered(pet_surface_t *s, int y, const char *text,
                               uint16_t color, int scale)
{
    const int w = (int)strlen(text) * 8 * scale;
    (void)pet_text_draw(s, (s->width - w) / 2, y, text, (int)strlen(text),
                        color, scale);
}

void pet_ui_pomo_240(pet_surface_t *s, const pet_pomo_view_t *v)
{
    if (s == NULL || s->pixels == NULL) {
        return;
    }
    /* 清屏 */
    for (int y = 0; y < s->height; y++) {
        for (int x = 0; x < s->width; x++) {
            put_px(s, x, y, C_BG);
        }
    }
    if (v == NULL) {
        return;
    }

    const uint16_t color = phase_color(v->phase);

    /* 相位标题 */
    draw_text_centered(s, 16, phase_label(v->phase), color, 1);

    /* 进度环（elapsed/total 千分比；remaining==total 即 elapsed=0） */
    int permille = 0;
    if (v->total_ms > 0 && v->remaining_ms < v->total_ms) {
        const uint32_t done = v->total_ms - v->remaining_ms;
        permille = (int)((uint64_t)done * 1000u / v->total_ms);
    }
    draw_ring(s, RING_CX, RING_CY, RING_R, RING_T, permille, color);

    /* 中心剩余 M:SS（2×） */
    const uint32_t rem_s = v->remaining_ms / 1000u;
    char txt[16];
    snprintf(txt, sizeof txt, "%u:%02u", (unsigned)(rem_s / 60u),
             (unsigned)(rem_s % 60u));
    const int tw = (int)strlen(txt) * 8 * 2;
    (void)pet_text_draw(s, RING_CX - tw / 2, RING_CY - 8, txt, (int)strlen(txt),
                        C_TEXT, 2);

    /* 底部统计：DONE n  mM */
    char stats[48];
    snprintf(stats, sizeof stats, "DONE %lu  %luM",
             (unsigned long)v->done_count,
             (unsigned long)(v->focus_seconds / 60u));
    draw_text_centered(s, 200, stats, C_OTHER, 1);
}
