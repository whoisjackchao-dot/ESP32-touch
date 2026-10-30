#pragma once

#include <stdbool.h>

#include "pet_render.h"
#include "pet_types.h"

/* 本项目 240×240（Waveshare ESP32-S3-Touch-LCD-1.54）专属版式。
 * 契约：docs/contracts/C03-layout-240x240.md（含 §7 修订记录）；
 *       docs/08-m2-development.md §3（逐项落地取值）。
 * 本文件是唯一允许出现 240×240 坐标的地方；业务层只调用 scene/card。 */

#define PET_UI_240_WIDTH  240
#define PET_UI_240_HEIGHT 240

/* 整屏场景：清屏 + 状态条（网络点）+ 宠物（96×128 @ (72,20)）+ 头顶标记
 * （WORKING `>` / INPUT_REQUIRED `!?`，居中）+ 3 卡 + 页脚。
 * OFFLINE：整帧灰度后网络点画红。v/look 可空时按黑屏/无宠物降级，不崩溃。 */
void pet_ui_scene_240(const pet_view_t *v, const pet_look_t *look, pet_surface_t *s);

/* 单卡 232×28，(x,y)=卡左上角；newest=true 画左侧 2 px 强调条。
 * 空字段按 C03 §2 渲染占位符（project→"-"，title→"(untitled)"），不留空白卡。 */
void pet_ui_card_240(const pet_card_t *c, pet_surface_t *s, int x, int y, bool newest);
