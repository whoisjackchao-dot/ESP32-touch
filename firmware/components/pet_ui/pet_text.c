/* M3：8×8 点阵文本绘制 + 时长格式化。
 * 字体 font8x8_basic.h：公有领域（Daniel Hepper / Marcel Sondaar / IBM VGA），
 * 来源已登记于 NOTICE.md。bit0 = 最左列。
 * M15：16×16 中文点阵（assets/cjk16.h，Noto Sans SC / OFL 1.1）+ UTF-8 绘制
 * 与按格折行；查表/渲染/折行均为纯函数，无外部状态。 */
#include "pet_text.h"

#include <stdio.h>

#include "font8x8_basic.h"
#include "assets/cjk16.h"

#define GLYPH_W 8
#define GLYPH_H 8

#define GLYPH16_W 16
#define GLYPH16_H 16
#define GLYPH16_PITCH 2 /* 每行字节数（16 px × 1 bpp） */

/* utf8_next 的「4 字节序列」标记：合法但字库外（BMP 之外），调用方跳过 */
#define CP_SKIP 0xFFFFFFFFu

static void put_px(pet_surface_t *s, int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= s->width || y >= s->height) {
        return;
    }
    uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
    p[0] = (uint8_t)(c & 0xFF); /* RGB565 little-endian */
    p[1] = (uint8_t)(c >> 8);
}

int pet_text_draw(pet_surface_t *surface, int x, int y, const char *text,
                  int max_chars, uint16_t color, int scale)
{
    if (surface == NULL || surface->pixels == NULL || text == NULL || scale <= 0) {
        return 0;
    }
    int drawn = 0;
    for (const char *p = text; *p != '\0'; p++) {
        if (max_chars > 0 && drawn >= max_chars) {
            break;
        }
        const unsigned char ch = (unsigned char)*p;
        if (ch >= 128u) {
            /* 8×8 字库只有 ASCII：跳过 UTF-8 后续字节，且不占用字符格 */
            while (p[1] != '\0' && ((unsigned char)p[1] & 0xC0) == 0x80) {
                p++;
            }
            continue;
        }
        {
            const uint8_t *rows = font8x8_basic[ch];
            for (int r = 0; r < GLYPH_H; r++) {
                for (int c = 0; c < GLYPH_W; c++) {
                    if ((rows[r] & (uint8_t)(1u << c)) == 0) {
                        continue;
                    }
                    const int px = x + (drawn * GLYPH_W + c) * scale;
                    const int py = y + r * scale;
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            put_px(surface, px + dx, py + dy, color);
                        }
                    }
                }
            }
        }
        drawn++;
    }
    return drawn;
}

void pet_format_elapsed(char *out, size_t cap, int32_t seconds)
{
    if (out == NULL || cap == 0) {
        return;
    }
    if (seconds <= 0) {
        snprintf(out, cap, "0S");
    } else if (seconds >= 3600) {
        snprintf(out, cap, "%ldH", (long)(seconds / 3600));
    } else if (seconds >= 60) {
        snprintf(out, cap, "%ldM", (long)(seconds / 60));
    } else {
        snprintf(out, cap, "%ldS", (long)seconds);
    }
}

/* ---- M15：16×16 中文点阵 ---- */

const uint8_t *pet_font16_glyph(uint32_t codepoint)
{
    int lo = 0;
    int hi = (int)CJK16_COUNT - 1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        if (cjk16_index[mid] == codepoint) {
            return cjk16_bitmap[mid];
        }
        if (cjk16_index[mid] < codepoint) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

/* 解析 p 处一个 UTF-8 序列，返回消费的字节数。
 * 合法 1..3 字节 → *cp = 码点；合法 4 字节 → *cp = CP_SKIP（跳过、不占格）；
 * 非法（裸续字节 / 前导后缺续 / C0-C1、F5+ 超界）→ *cp = U+FFFD，
 * 并一并吞掉后随续字节（缺字路径画 '?'）。 */
static int utf8_next(const char *p, uint32_t *cp)
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
        need = 0;
        v = 0xFFFDu;
    }
    for (int i = 1; need > 0 && i <= need; i++) {
        const unsigned char bi = (unsigned char)p[i];
        if ((bi & 0xC0u) != 0x80u) {
            need = -1;
            break;
        }
        v = (v << 6) | (bi & 0x3Fu);
    }
    if (need > 0) {
        *cp = (need == 3) ? CP_SKIP : v;
        return need + 1;
    }
    int n = 1;
    while (p[n] != '\0' && ((unsigned char)p[n] & 0xC0u) == 0x80u && n < 4) {
        n++;
    }
    *cp = 0xFFFDu;
    return n;
}

static void draw16_glyph(pet_surface_t *s, int x, int y, const uint8_t *g,
                         uint16_t color, int scale)
{
    for (int r = 0; r < GLYPH16_H; r++) {
        const uint8_t left = g[r * GLYPH16_PITCH];
        const uint8_t right = g[r * GLYPH16_PITCH + 1];
        for (int c = 0; c < GLYPH16_W; c++) {
            const uint8_t byte = (c < 8) ? left : right;
            if ((byte & (uint8_t)(1u << (7 - (c & 7)))) == 0) {
                continue;
            }
            const int px = x + c * scale;
            const int py = y + r * scale;
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    put_px(s, px + dx, py + dy, color);
                }
            }
        }
    }
}

int pet_text_draw16(pet_surface_t *surface, int x, int y, const char *utf8,
                    int max_cells, uint16_t color, int scale)
{
    if (surface == NULL || surface->pixels == NULL || utf8 == NULL || scale <= 0) {
        return 0;
    }
    static const uint8_t blank16[GLYPH16_H * GLYPH16_PITCH]; /* '?' 必在库，兜底 */
    int cells = 0;
    for (const char *p = utf8; *p != '\0';) {
        if (max_cells > 0 && cells >= max_cells) {
            break;
        }
        uint32_t cp;
        p += utf8_next(p, &cp);
        if (cp == CP_SKIP) {
            continue;
        }
        const uint8_t *g = pet_font16_glyph(cp);
        if (g == NULL) {
            g = pet_font16_glyph('?');
        }
        if (g == NULL) {
            g = blank16;
        }
        draw16_glyph(surface, x + cells * GLYPH16_W * scale, y, g, color, scale);
        cells++;
    }
    return cells;
}

int pet_text_draw16_wrap(pet_surface_t *surface, int x, int y, const char *utf8,
                         int cells_per_line, int max_lines, int line_h,
                         uint16_t color, int scale)
{
    if (surface == NULL || surface->pixels == NULL || utf8 == NULL ||
        cells_per_line <= 0 || max_lines <= 0 || scale <= 0) {
        return 0;
    }
    const int cap = cells_per_line * max_lines;
    int total = 0;
    for (const char *p = utf8; *p != '\0';) {
        uint32_t cp;
        p += utf8_next(p, &cp);
        if (cp != CP_SKIP) {
            total++;
        }
    }
    const int limit = (total > cap) ? cap - 1 : total; /* 超长留 1 格给 "…" */

    int cell = 0;
    int line = 0;
    const char *p = utf8;
    while (*p != '\0' && cell < limit) {
        const int room = limit - cell;
        const int take = (room < cells_per_line) ? room : cells_per_line;
        pet_text_draw16(surface, x, y + line * line_h, p, take, color, scale);
        int drawn = 0;
        while (*p != '\0' && drawn < take) {
            uint32_t cp;
            p += utf8_next(p, &cp);
            if (cp != CP_SKIP) {
                drawn++;
                cell++;
            }
        }
        line++;
    }
    if (total > cap) {
        const int idx = limit; /* 顺序布局中的下一空格 */
        pet_text_draw16(surface, x + (idx % cells_per_line) * GLYPH16_W * scale,
                        y + (idx / cells_per_line) * line_h, "\xE2\x80\xA6", 1,
                        color, scale);
        line = (limit / cells_per_line) + 1;
    }
    return line;
}
