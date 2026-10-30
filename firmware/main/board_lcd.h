#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* board_lcd：240×240 ST7789 板级层（C02 契约）。
 * 引脚号只允许出现在 board_lcd.c（C01 §1：板级文件拥有 ESP-IDF 驱动调用权）。 */

#define BOARD_LCD_WIDTH  240
#define BOARD_LCD_HEIGHT 240
#define BOARD_LCD_FB_BYTES (BOARD_LCD_WIDTH * BOARD_LCD_HEIGHT * 2) /* RGB565 */

/* C02 §2 初始化链路：背光先灭（LEDC duty 0）→ SPI2 总线 → panel IO →
 * ST7789 reset/init/invert/gap/disp_on → 背光 100%。
 * 任一阶段失败：记录失败阶段并返回错误，不挂死（C02 §3.4）。 */
esp_err_t board_lcd_init(void);

/* 背光开关（false 只关背光，不动面板，C02 §3.3）。 */
void board_lcd_backlight_set(bool on);

/* 把一帧 RGB565（little-endian in RAM）送到面板；阻塞至 DMA 完成。
 * 调用方保证 framebuffer 生命周期覆盖本调用（C01 §3）。 */
esp_err_t board_lcd_present(const uint16_t *framebuffer);
