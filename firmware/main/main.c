/* M1：电源锁存 + 240×240 出一帧（IDLE 立绘 + READY）。
 * 契约：docs/contracts/C01-board-interface.md（init 顺序与失败语义）、
 *       C02-panel-init.md（面板链路，实现在 board_lcd.c）、
 *       docs/05-plan.md M1（唯一交付：一帧，不做动画/卡片/交互）。
 * 渲染代码来自上游 ESP32-buddy（MIT），见 components/pet_ui/NOTICE.md。 */
#include <string.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_lcd.h"
#include "pet_render.h"
#include "pet_text.h"

static const char *TAG = "m1";

/* 电池供电锁存（C01 §3：init 首步，最优先） */
#define POWER_LATCH_GPIO GPIO_NUM_2

/* pet_ui 组件 EMBED_FILES 嵌入的立绘包（48×64 4bpp，12 帧，18472 B） */
extern const uint8_t robot_look_start[] asm("_binary_robot_look_start");
extern const uint8_t robot_look_end[] asm("_binary_robot_look_end");

/* 立绘 2× 尺寸（pet_render.c 固定 PET_LOOK_SCALE=2） */
#define PET_W (48 * 2) /* 96 */
#define PET_H (64 * 2) /* 128 */
#define PET_Y 40       /* 立绘顶部 y；底部 168，给 READY 让位 */

void app_main(void)
{
    /* ---- 1. 电池供电锁存：必须最先拉高 GPIO2，否则脱离供电握手即掉电 ---- */
    esp_err_t err = gpio_set_direction(POWER_LATCH_GPIO, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "power latch gpio_set_direction: %s", esp_err_to_name(err));
        return;
    }
    err = gpio_set_level(POWER_LATCH_GPIO, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "power latch gpio_set_level: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "power latched (GPIO2 high)");

    /* ---- 2-7. 背光灭 → SPI/panel 链路 → 背光 100%（见 board_lcd.c） ---- */
    err = board_lcd_init();
    if (err != ESP_OK) {
        /* C02 §3.4：失败只记录，不挂死、不无限重试 */
        return;
    }

    /* ---- 帧缓冲：优先 8MB octal PSRAM，失败退化内部 RAM ---- */
    uint16_t *fb = heap_caps_malloc(BOARD_LCD_FB_BYTES,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (fb == NULL) {
        ESP_LOGW(TAG, "SPIRAM framebuffer alloc failed, trying internal RAM");
        fb = heap_caps_malloc(BOARD_LCD_FB_BYTES, MALLOC_CAP_8BIT);
    }
    if (fb == NULL) {
        ESP_LOGE(TAG, "framebuffer alloc (%d bytes) failed", BOARD_LCD_FB_BYTES);
        return;
    }
    memset(fb, 0, BOARD_LCD_FB_BYTES); /* 黑底；C02 §3.2 首帧前全屏清屏 */

    /* ---- 渲染：IDLE 一帧（2×=96×128，水平居中，y=40）+ READY ---- */
    pet_look_t *look = pet_look_parse(robot_look_start,
                                      (size_t)(robot_look_end - robot_look_start));
    if (look == NULL) {
        ESP_LOGE(TAG, "pet_look_parse failed (len=%d)",
                 (int)(robot_look_end - robot_look_start));
        return;
    }

    pet_surface_t surface = {
        .pixels = (uint8_t *)fb,
        .width = BOARD_LCD_WIDTH,
        .height = BOARD_LCD_HEIGHT,
        .stride = BOARD_LCD_WIDTH * 2, /* RGB565 little-endian，行主序 */
    };
    pet_view_t view;
    memset(&view, 0, sizeof view);
    view.state = PET_STATE_IDLE;
    view.frame = PET_FRAME_NORMAL;

    pet_render_pet(&view, look, &surface, (BOARD_LCD_WIDTH - PET_W) / 2, PET_Y);
    pet_look_free(look);

    /* READY：8×8 字库 2×，5 字符宽 80px，水平居中，y=200 */
    const char *ready = "READY";
    pet_text_draw(&surface, (BOARD_LCD_WIDTH - 5 * 8 * 2) / 2, 200, ready, 5,
                  0xFFFFu, 2);

    /* ---- 送帧一次；present 阻塞至 DMA 完成 ---- */
    err = board_lcd_present(fb);
    if (err != ESP_OK) {
        return;
    }

    ESP_LOGI(TAG, "DISPLAY READY M1 (240x240)");

    /* M1 无循环刷新：空转即可（C01 §3：主循环仅在主任务） */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
