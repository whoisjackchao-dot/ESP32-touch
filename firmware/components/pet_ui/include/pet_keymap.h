#pragma once

#include "pet_button.h"

/* M3：键 → 动作映射（纯查表，不执行；契约 docs/contracts/C07-keymap.md §3）。
 * 与上游 ESP32-buddy（MIT）pet_keymap 行为一致，但类型独立于 pet_engine：
 * 本项目没有 pet_engine，模式由主循环持有。 */

typedef enum { PET_MODE_DUTY = 0, PET_MODE_POMO } pet_mode_t;

typedef enum {
    PET_ACT_NONE = 0,
    PET_ACT_POMO_SHORT,  /* 番茄钟 KEY1 短：开始/暂停/继续/推进 */
    PET_ACT_POMO_LONG,   /* 番茄钟 KEY1 长：放弃番茄钟 */
    PET_ACT_MODE_POMO,   /* 值班 KEY2 短：切到番茄钟 */
    PET_ACT_MODE_DUTY,   /* 番茄钟 KEY2 短：切回值班 */
    PET_ACT_MUTE_TOGGLE, /* 值班 KEY1 长：静音翻转（持久化） */
    PET_ACT_SEND,        /* 值班 KEY1 短：发送（M5 接入；M3 只记日志） */
    PET_ACT_MENU         /* 任意模式 KEY2 长：打开菜单 */
} pet_action_t;

/* 只处理 SHORT / LONG；PRESS/RELEASE 返回 PET_ACT_NONE。 */
pet_action_t pet_keymap_action(pet_mode_t mode, pet_key_t key, pet_key_event_t ev);
