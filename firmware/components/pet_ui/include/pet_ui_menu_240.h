#pragma once

#include "pet_menu.h"
#include "pet_render.h"

/* M3：240×240 菜单页（契约 docs/10-m3-development.md §5.2；C07 §4 五项）。
 * 5 行 232×28，选中行反色；底部统计。 */
void pet_ui_menu_240(pet_surface_t *s, const pet_menu_view_t *v);
