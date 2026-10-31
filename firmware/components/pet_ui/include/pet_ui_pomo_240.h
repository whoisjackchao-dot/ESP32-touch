#pragma once

#include "pet_pomo.h"
#include "pet_render.h"

/* M3：240×240 番茄钟页（契约 docs/10-m3-development.md §5.1）。
 * 与上游 135×240 的 pet_render_pomo 无关；不改其坐标。 */
void pet_ui_pomo_240(pet_surface_t *s, const pet_pomo_view_t *v);
