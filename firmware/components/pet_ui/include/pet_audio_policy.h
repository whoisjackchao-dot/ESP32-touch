#pragma once

#include <stdbool.h>

#include "pet_types.h"

/* M4：语音播报的纯逻辑判定（可 host 单测，不碰硬件）。
 * 契约：docs/contracts/C04-audio-output.md §3。
 * 设备侧实际播放由 board_audio.c（ESP32）执行；本模块只回答"该不该播、播哪条"。 */

typedef enum {
    PET_AUDIO_ASSET_NONE = 0,
    PET_AUDIO_ASSET_INPUT,
    PET_AUDIO_ASSET_DONE,
    PET_AUDIO_ASSET_FAILED
} pet_audio_asset_t;

/* muted 时一律不播；NONE 不播；其余播一次。 */
bool pet_audio_should_play(pet_sound_t sound, bool muted);

/* 声音 → 资产；PET_SOUND_NONE → NONE。 */
pet_audio_asset_t pet_audio_asset_for(pet_sound_t sound);

/* 资产名（日志/测试用，C04 §2 文件名）。 */
const char *pet_audio_asset_name(pet_audio_asset_t asset);
