/* M3：菜单状态机（纯逻辑，host 可测）。
 * 契约：docs/contracts/C07-keymap.md §4；5 项（MUTE/STATUS/WIFI/BT/BACK）。
 * KEY1 短按切选中；KEY2 短按按选中行产出命令；KEY2 长按关闭。
 * 来源：上游 ESP32-buddy（MIT）pet_menu.c（行为一致；WIFI/BT 由主循环按 M5 未实现处理）。 */
#include "pet_menu.h"

void pet_menu_init(pet_menu_t *m)
{
    m->selected = 0; /* 打开菜单默认选中静音 */
}

pet_menu_cmd_t pet_menu_key(pet_menu_t *m, pet_key_t key, pet_key_event_t ev)
{
    if (ev == PET_KEY_EV_SHORT) {
        if (key == PET_KEY1) {
            m->selected = (m->selected + 1) % PET_MENU_ITEMS;
            return PET_MENU_CMD_NONE;
        }
        if (key == PET_KEY2) {
            switch (m->selected) {
            case 0:
                return PET_MENU_CMD_TOGGLE_MUTE;
            case 2:
                return PET_MENU_CMD_PROVISION;
            case 3:
                return PET_MENU_CMD_BT_PAIR;
            case 4:
                return PET_MENU_CMD_BACK_WINDOW;
            default:
                return PET_MENU_CMD_CLOSE; /* 1=STATUS：只读，关闭 */
            }
        }
    } else if (ev == PET_KEY_EV_LONG && key == PET_KEY2) {
        return PET_MENU_CMD_CLOSE;
    }
    return PET_MENU_CMD_NONE;
}
