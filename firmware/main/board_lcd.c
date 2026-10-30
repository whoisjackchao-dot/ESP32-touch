/* board_lcd：ST7789 240×240 板级实现。
 * 契约：docs/contracts/C02-panel-init.md（引脚与参数冻结；SPI mode=3 /
 * invert / gap 为候选值，M1 实测后修订契约，B07）。
 * 失败语义（C02 §3.4）：任何阶段失败都记录阶段名并返回错误，不无限重试。 */
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

#include "board_lcd.h"

static const char *TAG = "board_lcd";

/* ---- C02 §1 引脚（冻结，禁止业务层出现这些数字） ---- */
#define LCD_SCLK_GPIO 38
#define LCD_MOSI_GPIO 39 /* 单向写，无 MISO */
#define LCD_CS_GPIO   21
#define LCD_DC_GPIO   45
#define LCD_RST_GPIO  40
#define LCD_BL_GPIO   46 /* 高有效，LEDC PWM */

/* ---- C02 §2 参数（候选值） ---- */
#define LCD_SPI_HOST    SPI2_HOST
#define LCD_PCLK_HZ     (40 * 1000 * 1000)
#define LCD_SPI_MODE    3
#define LCD_QUEUE_DEPTH 10
/* 单次 DMA 上限 240*240*2/4（与官方 factory demo 一致；draw_bitmap 自动分块） */
#define LCD_MAX_TRANSFER (BOARD_LCD_WIDTH * BOARD_LCD_HEIGHT * 2 / 4)

#define BL_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define BL_LEDC_TIMER   LEDC_TIMER_0
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define BL_LEDC_FREQ_HZ 5000
#define BL_LEDC_BIT     LEDC_TIMER_10_BIT
#define BL_DUTY_FULL    ((1u << 10) - 1u) /* 1023 = 100% */

static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_color_done; /* 颜色传输完成（ISR 给出） */

/* post_cb 在 SPI ISR 上下文调用（esp_lcd_panel_io_spi.c lcd_spi_post_trans_color_cb） */
static bool lcd_color_done_cb(esp_lcd_panel_io_handle_t io,
                              esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_color_done, &woken);
    return woken != pdFALSE;
}

/* 失败即记录阶段名再向上返回；成功不做多余输出 */
static esp_err_t check(esp_err_t err, const char *stage)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "stage '%s' failed: %s", stage, esp_err_to_name(err));
    }
    return err;
}

void board_lcd_backlight_set(bool on)
{
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, on ? BL_DUTY_FULL : 0);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}

esp_err_t board_lcd_init(void)
{
    esp_err_t err;

    /* 1) 背光先灭：LEDC 5 kHz / 10-bit / duty 0，避免初始化期间闪屏 */
    s_color_done = xSemaphoreCreateBinary();
    if (s_color_done == NULL) {
        ESP_LOGE(TAG, "stage 'alloc semaphore' failed");
        return ESP_ERR_NO_MEM;
    }
    const ledc_timer_config_t bl_timer = {
        .speed_mode = BL_LEDC_MODE,
        .duty_resolution = BL_LEDC_BIT,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = BL_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    err = check(ledc_timer_config(&bl_timer), "ledc_timer_config");
    if (err != ESP_OK) {
        return err;
    }
    const ledc_channel_config_t bl_chan = {
        .gpio_num = LCD_BL_GPIO,
        .speed_mode = BL_LEDC_MODE,
        .channel = BL_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BL_LEDC_TIMER,
        .duty = 0, /* off */
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags.output_invert = false, /* 高有效，不反相 */
    };
    err = check(ledc_channel_config(&bl_chan), "ledc_channel_config");
    if (err != ESP_OK) {
        return err;
    }

    /* 2) SPI2 总线：SCLK 38 / MOSI 39，无 MISO */
    const spi_bus_config_t bus = {
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_MAX_TRANSFER,
    };
    err = check(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO),
                "spi_bus_initialize");
    if (err != ESP_OK) {
        return err;
    }

    /* 3) panel IO：CS 21 / DC 45 / mode 3 / 40 MHz / 队深 10 */
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS_GPIO,
        .dc_gpio_num = LCD_DC_GPIO,
        .spi_mode = LCD_SPI_MODE,
        .pclk_hz = LCD_PCLK_HZ,
        /* 必须显式给命令/参数字宽：未初始化时为 0，会触发
         * spi_master check_trans_valid "length==0 but tx_buffer set" 而 init 失败
         * （2026-10-30 真机实测，见 07-evidence-log E12） */
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .trans_queue_depth = LCD_QUEUE_DEPTH,
        .on_color_trans_done = lcd_color_done_cb,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    err = check(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                         &io_cfg, &io),
                "esp_lcd_new_panel_io_spi");
    if (err != ESP_OK) {
        return err;
    }

    /* 4) ST7789 面板：RST 40，RGB 序，16bpp。
     * 帧缓冲是 RGB565 **little-endian**（uint16_t 原生序），因此把控制器的
     * RAMCTL 设为 little-endian，避免再做一次全屏字节交换。
     * （M1 若实机颜色错位，改回 BIG 并在 present 前 swap；见 C02 §2 字节序行） */
    const esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
    };
    err = check(esp_lcd_new_panel_st7789(io, &dev_cfg, &s_panel),
                "esp_lcd_new_panel_st7789");
    if (err != ESP_OK) {
        return err;
    }

    /* 5) 复位 → 初始化 → 反色开 → gap(0,0) → 显示开（C02 §2 复位时序由
     * esp_lcd_panel_reset 内置；B07 实测项在此三处，改动须修订契约） */
    err = check(esp_lcd_panel_reset(s_panel), "esp_lcd_panel_reset");
    if (err != ESP_OK) {
        return err;
    }
    err = check(esp_lcd_panel_init(s_panel), "esp_lcd_panel_init");
    if (err != ESP_OK) {
        return err;
    }
    err = check(esp_lcd_panel_invert_color(s_panel, true), "esp_lcd_panel_invert_color");
    if (err != ESP_OK) {
        return err;
    }
    err = check(esp_lcd_panel_set_gap(s_panel, 0, 0), "esp_lcd_panel_set_gap");
    if (err != ESP_OK) {
        return err;
    }
    err = check(esp_lcd_panel_disp_on_off(s_panel, true), "esp_lcd_panel_disp_on_off");
    if (err != ESP_OK) {
        return err;
    }

    /* 6) 背光 100%：init 成功 ⇒ 面板已 init 且 display on（C02 §3.1） */
    board_lcd_backlight_set(true);
    ESP_LOGI(TAG, "panel %dx%d ready, SPI mode %d, backlight on",
             BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT, LCD_SPI_MODE);
    return ESP_OK;
}

esp_err_t board_lcd_present(const uint16_t *framebuffer)
{
    if (s_panel == NULL) {
        ESP_LOGE(TAG, "present called before init");
        return ESP_ERR_INVALID_STATE;
    }
    /* 全屏一次送帧（end 为 exclusive） */
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, 0,
                                              BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT,
                                              framebuffer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(err));
        return err;
    }
    /* 队深 10 + max_transfer=28800 → 一整帧被拆成 4 个 chunk（CS 保持）。
     * esp_lcd_panel_io_spi.c 只在**最后一个** chunk 上置 en_trans_done_cb
     * （IDF v5.5.4 源码 371-378 行），因此这里等到的一次完成 = 整帧完成。
     * M2 起每帧复用同一 fb，依赖此语义；D-02 的"只等首个 chunk"结论已更正
     * （见 docs/08-m2-development.md §6 C5）。 */
    if (xSemaphoreTake(s_color_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "color transfer timeout");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
