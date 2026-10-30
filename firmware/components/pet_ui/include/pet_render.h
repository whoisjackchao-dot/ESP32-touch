#pragma once
#include <stddef.h>
#include "pet_menu.h"
#include "pet_pomo.h"
#include "pet_types.h"

/* 一块 RGB565 little-endian、row-major 的画布；几何与 stride 由调用方给。 */
typedef struct {
    uint8_t *pixels;
    int      width;
    int      height;
    int      stride;   /* 每行字节数 */
} pet_surface_t;

/* 立绘包（48×64、4bpp、RGB565 调色板、4 帧）。 */
typedef struct pet_look pet_look_t;

pet_look_t *pet_look_parse(const uint8_t *bytes, size_t len);
void        pet_look_free(pet_look_t *look);

/* 把宠物按 2× 画到 surface 的 (x, y)；越界裁剪。 */
void pet_render_pet(const pet_view_t *v, const pet_look_t *look,
                    pet_surface_t *surface, int x, int y);

/* ---- M3：整屏场景（宠物 + 任务卡）---- */

/* 竖屏 135×240 分区（契约 M3 §5.1，提案值） */
#ifndef PET_SCENE_BG
#define PET_SCENE_BG 0x0000u /* 场景底色（黑） */
#endif
#ifndef PET_SCENE_PET_X
#define PET_SCENE_PET_X 19
#endif
#ifndef PET_SCENE_PET_Y
#define PET_SCENE_PET_Y 24 /* 留出头顶标记（y-18 起、14px 高）的空间 */
#endif
#ifndef PET_CARD_X
#define PET_CARD_X 4
#endif
#ifndef PET_CARD_Y
#define PET_CARD_Y 156
#endif
#ifndef PET_CARD_W
#define PET_CARD_W 127
#endif
#ifndef PET_CARD_H
#define PET_CARD_H 64
#endif

/* 画整屏：宠物固定在宠物区，若有卡则在卡片区画 1 张。 */
void pet_render_scene(const pet_view_t *v, const pet_look_t *look,
                      pet_surface_t *surface);

/* ---- M7 增量 2：番茄钟视图（契约 M7 §7.1/§7.2）---- */

/* 进度环：外半径 r、环厚 t、进度 0..1000（千分比）、12 点顺时针。 */
void pet_render_ring(pet_surface_t *s, int cx, int cy, int r, int t,
                     int progress_permille, uint16_t color, uint16_t track);
/* 番茄钟整屏视图：环 + 中心剩余时间。 */
void pet_render_pomo(pet_surface_t *s, const pet_pomo_view_t *v);

/* ---- M6 增量 3：菜单整屏（契约 M6 §9.3）---- */

/* 菜单整屏：全屏底色 + 标题 + 两行（选中行反色）+ 底部统计。 */
void pet_render_menu(pet_surface_t *s, const pet_menu_view_t *v);

/* ---- M8 增量 1b：小剧渲染（契约 M8 §7.1）---- */

/* 休闲小剧整屏：底色 + 头像按剧目位移/道具绘制；t_ms 为小剧起始后的毫秒，
 * 位移以 2 s 为周期。剧目 0..6；非法 id 退回值班整屏（pet_render_scene）。 */
void pet_render_skit(pet_surface_t *s, const pet_look_t *look,
                     const pet_view_t *v, int skit_id, uint32_t t_ms);
