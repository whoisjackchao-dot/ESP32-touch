/* M3：240×240 菜单页。
 * 契约：docs/10-m3-development.md §5.2；C07 §4 五项。
 * 5 行 232×28（y=48+i*32）；未选中卡底 0x18E3/白字，选中反色（白底黑字）。 */
#include <stdio.h>
#include <string.h>

#include "pet_render.h"
#include "pet_text.h"
#include "pet_ui_menu_240.h"

#define MENU_X      4
#define MENU_W      232
#define MENU_H      28
#define MENU_Y0     48
#define MENU_PITCH  32
#define MENU_BG     0x18E3u
#define C_BG        0x0000u
#define C_TEXT      0xFFFFu
#define C_INVERT    0x0000u
#define C_STATS     0x8410u

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

static void fill_rect(pet_surface_t *s, int x, int y, int w, int h, uint16_t c)
{
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            put_px(s, x + dx, y + dy, c);
        }
    }
}

static void draw_text_centered(pet_surface_t *s, int y, const char *text,
                               uint16_t color, int scale)
{
    const int n = (int)strlen(text);
    if (n == 0) {
        return;
    }
    const int w = n * 8 * scale;
    (void)pet_text_draw(s, (s->width - w) / 2, y, text, n, color, scale);
}

static void draw_row(pet_surface_t *s, int y, const char *text, bool selected)
{
    fill_rect(s, MENU_X, y, MENU_W, MENU_H, selected ? C_TEXT : MENU_BG);
    (void)pet_text_draw(s, MENU_X + 8, y + 10, text, 28,
                        selected ? C_INVERT : C_TEXT, 1);
}

void pet_ui_menu_240(pet_surface_t *s, const pet_menu_view_t *v)
{
    if (s == NULL || s->pixels == NULL) {
        return;
    }
    for (int y = 0; y < s->height; y++) {
        for (int x = 0; x < s->width; x++) {
            put_px(s, x, y, C_BG);
        }
    }
    if (v == NULL) {
        return;
    }

    draw_text_centered(s, 12, "MENU", C_TEXT, 2);

    char item[PET_MENU_ITEMS][32];
    snprintf(item[0], sizeof item[0], "MUTE: %s", v->muted ? "ON" : "OFF");
    snprintf(item[1], sizeof item[1], "STATUS");
    snprintf(item[2], sizeof item[2], "WIFI (M5)");
    snprintf(item[3], sizeof item[3], "BT (M5)");
    snprintf(item[4], sizeof item[4], "BACK");

    int selected = v->selected;
    if (selected < 0) {
        selected = 0;
    } else if (selected >= PET_MENU_ITEMS) {
        selected = PET_MENU_ITEMS - 1;
    }
    for (int i = 0; i < PET_MENU_ITEMS; i++) {
        draw_row(s, MENU_Y0 + i * MENU_PITCH, item[i], i == selected);
    }

    char stats[64];
    snprintf(stats, sizeof stats, "DONE %lu  %luM  %s",
             (unsigned long)v->done_count,
             (unsigned long)(v->focus_seconds / 60u),
             (v->mode == 0) ? "DUTY" : "POMO");
    draw_text_centered(s, 212, stats, C_STATS, 1);
}
