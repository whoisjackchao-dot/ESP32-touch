#pragma once

#include <stdbool.h>

#include "pet_pomo.h"

/* M3：番茄钟当日统计 + 静音标志的 NVS 落盘（设备侧；host 单测不链接本文件）。
 * 契约：docs/10-m3-development.md §3。
 * - pomo/stats：16 B blob（格式由 pet_pomo_encode/decode_stats 提供）
 * - voice/muted：u8（0=有声，1=静音；M4 音频复用） */

void pet_pomo_nvs_load(pet_pomo_t *p);
void pet_pomo_nvs_save(const pet_pomo_t *p);
void pet_mute_nvs_load(bool *muted);
void pet_mute_nvs_save(bool muted);
