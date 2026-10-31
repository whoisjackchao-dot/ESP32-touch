#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "pet_types.h"

/* M4 板级音频输出（ES8311 + I2S + NS4150B 功放）。
 * 契约：docs/contracts/C04-audio-output.md。
 * 只在主循环任务调用 play；播放由独立 audio 任务异步执行，不阻塞 8 fps 主循环。 */

esp_err_t board_audio_init(void);

/* 入队一次播报；未初始化/队列满 → 返回错误（调用方只记日志，不重试）。 */
esp_err_t board_audio_play(pet_sound_t sound);

/* 初始化是否成功（失败时 play 为 no-op）。 */
bool board_audio_ready(void);

/* 是否正在播（含排队）。 */
bool board_audio_busy(void);
