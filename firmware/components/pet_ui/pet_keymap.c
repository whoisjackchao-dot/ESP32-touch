/* M3：键 → 动作映射（纯查表）。
 * 契约：docs/contracts/C07-keymap.md §3。行为对齐上游 ESP32-buddy（MIT）pet_keymap.c，
 * 但类型独立于 pet_engine（本项目没有引擎层）。 */
#include "pet_keymap.h"

pet_action_t pet_keymap_action(pet_mode_t mode, pet_key_t key, pet_key_event_t ev)
{
    if (ev != PET_KEY_EV_SHORT && ev != PET_KEY_EV_LONG) {
        return PET_ACT_NONE; /* PRESS/RELEASE 不进键表（M4 PTT 直通） */
    }
    if (mode == PET_MODE_DUTY) {
        if (key == PET_KEY1) {
            return (ev == PET_KEY_EV_SHORT) ? PET_ACT_SEND : PET_ACT_MUTE_TOGGLE;
        }
        return (ev == PET_KEY_EV_SHORT) ? PET_ACT_MODE_POMO : PET_ACT_MENU;
    }
    /* 番茄钟模式 */
    if (key == PET_KEY1) {
        return (ev == PET_KEY_EV_SHORT) ? PET_ACT_POMO_SHORT : PET_ACT_POMO_LONG;
    }
    return (ev == PET_KEY_EV_SHORT) ? PET_ACT_MODE_DUTY : PET_ACT_MENU;
}
