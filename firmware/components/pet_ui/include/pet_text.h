#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pet_render.h"

/* 画 8×8 点阵文本；最多 max_chars 个字符（<=0 表示不限，遇 NUL 停止）。
 * 返回实际画出的字符数。越界裁剪。 */
int pet_text_draw(pet_surface_t *surface, int x, int y, const char *text,
                  int max_chars, uint16_t color, int scale);

/* ---- M15：16×16 中文点阵（assets/cjk16.h，Noto Sans SC / OFL 1.1）---- */

/* 码点 → 16×16 字形（32 字节，行主序、MSB=最左列）；二分查表，
 * 无此字形返回 NULL。覆盖 ASCII + GB2312 一级 + 常用中文标点。 */
const uint8_t *pet_font16_glyph(uint32_t codepoint);

/* 画 16×16 点阵 UTF-8 文本：每码点占 1 格（16×scale 像素，ASCII 同占整格）；
 * 4 字节序列跳过（不占格）；缺字/非法序列画 '?'。最多 max_cells 格
 * （<=0 不限）。返回画出的格数（供上层折行）。越界裁剪。 */
int pet_text_draw16(pet_surface_t *surface, int x, int y, const char *utf8,
                    int max_cells, uint16_t color, int scale);

/* 按格折行画 UTF-8 文本：每行 cells_per_line 格、行距 line_h 像素，
 * 最多 max_lines 行；超出容量时末格画 "…"（截断标记）。
 * 返回使用的行数（空文本 → 0）。 */
int pet_text_draw16_wrap(pet_surface_t *surface, int x, int y, const char *utf8,
                         int cells_per_line, int max_lines, int line_h,
                         uint16_t color, int scale);

/* 秒 → 精简时长文本："45S" / "12M" / "3H"；<=0 → "0S" */
void pet_format_elapsed(char *out, size_t cap, int32_t seconds);
