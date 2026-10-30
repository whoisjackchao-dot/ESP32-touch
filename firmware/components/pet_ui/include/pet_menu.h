#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pet_button.h"

/* M6 增量 3：菜单纯逻辑（host 可测）。
 * 契约：contracts/M6-按键映射契约清单.md §9（2026-10-17 冻结）。
 * 只认键事件（pet_button 层的产物）；打开菜单属设备侧（KEY2 长按 → MENU 动作）。 */

#define PET_MENU_ITEMS 5 /* 0=静音, 1=状态, 2=配网, 3=BT配对, 4=回窗口 */

typedef struct {
    int selected; /* 0..PET_MENU_ITEMS-1 */
} pet_menu_t;

typedef enum {
    PET_MENU_CMD_NONE = 0,
    PET_MENU_CMD_TOGGLE_MUTE,
    PET_MENU_CMD_CLOSE,
    PET_MENU_CMD_PROVISION,
    PET_MENU_CMD_BACK_WINDOW,
    PET_MENU_CMD_BT_PAIR /* M16：清绑定并重新广播，激活配对 */
} pet_menu_cmd_t;

void           pet_menu_init(pet_menu_t *m);
pet_menu_cmd_t pet_menu_key(pet_menu_t *m, pet_key_t key, pet_key_event_t ev);

/* §9.3 菜单整屏视图（渲染输入；pet_render_menu 消费） */
typedef struct {
    bool     muted;
    int      selected;    /* 0..2 */
    int      mode;        /* 0=DUTY, 1=POMO */
    int      phase;       /* pet_pomo_phase_t */
    uint32_t done_count;
    uint32_t focus_seconds;
} pet_menu_view_t;
