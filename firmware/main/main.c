/* M2：6 态动画 + 3 卡栈（240×240）。
 * 契约：docs/08-m2-development.md（§2 接口、§3 版式、§4 注入协议）、
 *       docs/contracts/C01（板级接口）、C02（面板）、C03（版式修订）；
 *       05-plan.md M2。
 *
 * 组成：
 *   1. 电源锁存 GPIO2（最先）→ board_lcd_init()（背光/SPI/面板）
 *   2. PSRAM 帧缓冲 + robot.look 解析
 *   3. pet_state（6 态/动画相位/卡栈）+ pet_ui_240（整屏渲染）
 *   4. 8 fps 主循环；复用同一帧缓冲（present 等最后一 chunk 完成）
 *   5. 调试注入通道：USB-Serial-JTAG 单字符命令（M2 专用，M5 由事件协议取代）
 *
 * 明确不含（后续节点）：番茄钟/菜单/按键（M3）、语音（M4）、网络/宿主（M5）。
 */
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "board_lcd.h"
#include "pet_render.h"
#include "pet_state.h"
#include "pet_ui_240.h"

static const char *TAG = "m2";

/* 电池供电锁存（C01 §3：init 首步，最优先） */
#define POWER_LATCH_GPIO GPIO_NUM_2

/* 立绘包（pet_ui EMBED_FILES，48×64 4bpp，12 帧） */
extern const uint8_t robot_look_start[] asm("_binary_robot_look_start");
extern const uint8_t robot_look_end[] asm("_binary_robot_look_end");

#define FRAME_MS 125u /* 8 fps（M2 值班节拍，契约 M2 §9.2 提案值） */

/* 命令队列（console task → 主循环） */
static QueueHandle_t s_cmd_q;

/* ---- 调试注入预置卡（docs/08 §4；第 9 次起循环） ---- */
static const pet_card_t PRESETS[] = {
    {.title = "GAMMA", .agent = "OC", .project = "openai/chat",
     .state = PET_STATE_WORKING, .elapsed_s = 75},
    {.title = "BETA", .agent = "CC", .project = "acme/web",
     .state = PET_STATE_INPUT_REQUIRED, .elapsed_s = 900},
    {.title = "ALPHA", .agent = "CX", .project = "acme/api",
     .state = PET_STATE_DONE, .elapsed_s = 5},
    {.title = "DELTA", .agent = "OC", .project = "",
     .state = PET_STATE_FAILED, .elapsed_s = 3725},
    {.title = "BETA", .agent = "CC", .project = "acme/web",
     .state = PET_STATE_INPUT_REQUIRED, .elapsed_s = 1200}, /* 同名更新置顶 */
    {.title = "", .agent = "OC", .project = "acme/empty",
     .state = PET_STATE_WORKING, .elapsed_s = 10}, /* 空 title 占位 */
    {.title = "THIS-IS-A-VERY-LONG-TITLE-0123456789", .agent = "CX",
     .project = "acme/long", .state = PET_STATE_WORKING, .elapsed_s = 59},
    {.title = "中文标题测试", .agent = "OC", .project = "acme/cjk",
     .state = PET_STATE_DONE, .elapsed_s = 3600},
};
#define PRESET_COUNT ((int)(sizeof PRESETS / sizeof PRESETS[0]))

static int  s_preset_idx;
static bool s_dump_req;
static bool s_status_req;
static bool s_log_muted;

static uint32_t now_ms_u32(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void crc32_update(uint32_t *crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        *crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            *crc = (*crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(*crc & 1u)));
        }
    }
}

static const char *state_name(pet_state_t st)
{
    switch (st) {
    case PET_STATE_IDLE: return "IDLE";
    case PET_STATE_WORKING: return "WORKING";
    case PET_STATE_INPUT_REQUIRED: return "INPUT_REQUIRED";
    case PET_STATE_DONE: return "DONE";
    case PET_STATE_FAILED: return "FAILED";
    case PET_STATE_OFFLINE: return "OFFLINE";
    default: return "?";
    }
}

/* 状态行（G1/G3 自动化按此解析；docs/08 §4） */
static void print_status(const struct pet_state *st, uint32_t fps)
{
    pet_view_t v;
    pet_state_view(st, &v);
    printf("STATUS state=%s frame=%d cards=%d fps=%lu heap=%lu\n",
           state_name(v.state), (int)v.frame, v.card_count,
           (unsigned long)fps, (unsigned long)esp_get_free_heap_size());
    if (v.card_count > 0) {
        printf("CARDS ");
        for (int i = 0; i < v.card_count; i++) {
            const char *t = (v.cards[i].title[0] != '\0') ? v.cards[i].title : "-";
            printf("%s%s", (i == 0) ? "" : "|", t);
        }
        printf("\n");
    }
}

/* 帧缓冲导出：FBBIN 头 + 115200 原始字节 + FBEND crc32（docs/08 §4）。
 * 注意：usb_serial_jtag_write_bytes 一次最多写到 tx ring buffer，超长会直接失败，
 * 必须按 ≤2 KB 分块循环写（2026-10-31 真机实测）。 */
static void dump_framebuffer(const uint16_t *fb)
{
    const uint8_t *bytes = (const uint8_t *)fb;
    esp_log_level_set("*", ESP_LOG_NONE);
    s_log_muted = true;
    printf("FBBIN 240 240 %u\n", (unsigned)BOARD_LCD_FB_BYTES);
    size_t off = 0;
    while (off < BOARD_LCD_FB_BYTES) {
        size_t n = BOARD_LCD_FB_BYTES - off;
        if (n > 2048) {
            n = 2048;
        }
        const int w = usb_serial_jtag_write_bytes(bytes + off, n,
                                                 pdMS_TO_TICKS(1000));
        if (w <= 0) {
            break;
        }
        off += (size_t)w;
    }
    (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(2000));
    uint32_t crc = 0xFFFFFFFFu;
    crc32_update(&crc, bytes, BOARD_LCD_FB_BYTES);
    printf("\nFBEND %08lX %u\n", (unsigned long)(crc ^ 0xFFFFFFFFu), (unsigned)off);
    vTaskDelay(pdMS_TO_TICKS(20)); /* 等驱动把尾包送出 */
    s_log_muted = false;
    esp_log_level_set("*", ESP_LOG_INFO);
}

static void apply_event(struct pet_state *st, pet_event_kind_t kind, uint32_t now)
{
    const pet_event_t ev = {
        .kind = kind,
        .now_ms = now,
    };
    pet_state_apply(st, &ev);
}

static void handle_cmd(struct pet_state *st, uint8_t cmd, uint32_t now)
{
    switch (cmd) {
    case '1': apply_event(st, PET_EV_TASK_START, now); break;
    case '2': apply_event(st, PET_EV_AGENT_INPUT_REQUIRED, now); break;
    case '3': apply_event(st, PET_EV_TASK_DONE, now); break;
    case '4': apply_event(st, PET_EV_TASK_ERROR, now); break;
    case '0': apply_event(st, PET_EV_AGENT_IDLE, now); break;
    case '5': apply_event(st, PET_EV_LINK_DOWN, now); break;
    case 'u': apply_event(st, PET_EV_LINK_UP, now); break;
    case 'n': apply_event(st, PET_EV_ANNOUNCEMENT, now); break;
    case 'c':
        pet_state_set_card(st, &PRESETS[s_preset_idx]);
        s_preset_idx = (s_preset_idx + 1) % PRESET_COUNT;
        break;
    case 'x':
        pet_state_set_card(st, NULL);
        s_preset_idx = 0;
        break;
    case 'p': s_status_req = true; break;
    case 'd': s_dump_req = true; break;
    default: break; /* \r \n 等忽略 */
    }
}

static void console_task(void *arg)
{
    (void)arg;
    const usb_serial_jtag_driver_config_t cfg = {
        .rx_buffer_size = 1024,
        .tx_buffer_size = 4096,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install failed: %s",
                 esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    usb_serial_jtag_vfs_use_driver(); /* stdout 也走 driver，避免与二进制 dump 竞争 */    ESP_LOGI(TAG, "inject ready: 1=start 2=input 3=done 4=error 0=idle 5=linkdown u=linkup c=card x=clear p=status d=dump");

    uint8_t buf[64];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof buf, pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\r' || buf[i] == '\n') {
                continue;
            }
            (void)xQueueSend(s_cmd_q, &buf[i], 0);
        }
    }
}

void app_main(void)
{
    /* ---- 1. 电池供电锁存：最先拉高 GPIO2 ---- */
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

    /* ---- 2. 背光灭 → SPI/panel → 背光 100% ---- */
    err = board_lcd_init();
    if (err != ESP_OK) {
        return; /* C02 §3.4：失败只记录，不挂死 */
    }

    /* ---- 3. 帧缓冲：PSRAM 优先，退化内部 RAM ---- */
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
    memset(fb, 0, BOARD_LCD_FB_BYTES);

    pet_surface_t surface = {
        .pixels = (uint8_t *)fb,
        .width = BOARD_LCD_WIDTH,
        .height = BOARD_LCD_HEIGHT,
        .stride = BOARD_LCD_WIDTH * 2,
    };

    /* ---- 4. 立绘 + 状态机 ---- */
    pet_look_t *look = pet_look_parse(robot_look_start,
                                      (size_t)(robot_look_end - robot_look_start));
    if (look == NULL) {
        ESP_LOGE(TAG, "pet_look_parse failed");
        return;
    }
    struct pet_state *st = pet_state_create();
    if (st == NULL) {
        ESP_LOGE(TAG, "pet_state_create failed");
        return;
    }

    /* ---- 5. 注入通道（失败不影响显示） ---- */
    s_cmd_q = xQueueCreate(32, sizeof(uint8_t));
    if (s_cmd_q == NULL) {
        ESP_LOGE(TAG, "cmd queue alloc failed");
    } else if (xTaskCreate(console_task, "inject", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "inject task create failed");
    }

    ESP_LOGI(TAG, "m2: READY (240x240, 12 frames, %u ms/frame)", (unsigned)FRAME_MS);

    /* ---- 6. 主循环：8 fps ---- */
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t fps_win_start = now_ms_u32();
    uint32_t fps_frames = 0;
    uint32_t fps = 0;

    for (;;) {
        const uint32_t now = now_ms_u32();

        uint8_t cmd;
        while (s_cmd_q != NULL && xQueueReceive(s_cmd_q, &cmd, 0) == pdTRUE) {
            handle_cmd(st, cmd, now);
        }

        pet_state_tick(st, now);
        const pet_sound_t snd = pet_state_take_sound(st);
        if (snd != PET_SOUND_NONE && !s_log_muted) {
            ESP_LOGI(TAG, "sound queued: %d (M2 不播放，M4 接入)", (int)snd);
        }

        pet_view_t view;
        pet_state_view(st, &view);
        pet_ui_scene_240(&view, look, &surface);
        err = board_lcd_present(fb);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "present failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (s_status_req) {
            s_status_req = false;
            print_status(st, fps);
        }
        if (s_dump_req) {
            s_dump_req = false;
            dump_framebuffer(fb);
        }

        /* fps 统计（1 s 窗口） */
        fps_frames++;
        if (now - fps_win_start >= 1000u) {
            fps = fps_frames * 1000u / (now - fps_win_start);
            fps_win_start = now;
            fps_frames = 0;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FRAME_MS));
    }
}
