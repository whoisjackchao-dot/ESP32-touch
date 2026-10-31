/* M3：2 应用键 + 5 项菜单 + 番茄钟（240×240 值班/番茄钟/菜单三页）。
 * 契约：docs/10-m3-development.md、docs/contracts/C07-keymap.md（按键）、
 *       C03（值班页）、docs/08-m2-development.md（注入协议基础）；05-plan.md M3。
 *
 * 组成（在 M2 基础上）：
 *   1. 20 ms 按键采样任务（KEY1=GPIO0 / KEY2=GPIO4 / PWR=GPIO5，低有效）
 *   2. pet_button 短/长按状态机 + pet_keymap 映射 + pet_menu 菜单状态机
 *   3. pet_pomo 番茄钟（25/5；运行期可缩短用于 G5 演示）+ NVS 统计/静音持久化
 *   4. 三页渲染：DUTY=pet_ui_scene_240 / POMO=pet_ui_pomo_240 / MENU=pet_ui_menu_240
 *   5. PWR 长按（≥1500 ms）关机流程（落盘 → 背光灭 → GPIO2 低）
 *
 * 明确不含：语音（M4）、网络/宿主事件（M5）、触摸/IMU（后置）。
 */
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "board_audio.h"
#include "board_lcd.h"
#include "pet_audio_policy.h"
#include "pet_button.h"
#include "pet_keymap.h"
#include "pet_linebuf.h"
#include "pet_link.h"
#include "pet_menu.h"
#include "pet_net.h"
#include "pet_pomo.h"
#include "pet_pomo_store.h"
#include "pet_render.h"
#include "pet_state.h"
#include "pet_ui_240.h"
#include "pet_ui_menu_240.h"
#include "pet_ui_pomo_240.h"

static const char *TAG = "m3";

/* 电池供电锁存（C01 §3：init 首步，最优先） */
#define POWER_LATCH_GPIO GPIO_NUM_2

/* 按键（C07 §1；低有效，板载 10K 上拉） */
#define KEY1_GPIO GPIO_NUM_0
#define KEY2_GPIO GPIO_NUM_4
#define PWR_GPIO  GPIO_NUM_5
#define PWR_LONGPRESS_MS 1500u

/* 立绘包（pet_ui EMBED_FILES，48×64 4bpp，12 帧） */
extern const uint8_t robot_look_start[] asm("_binary_robot_look_start");
extern const uint8_t robot_look_end[] asm("_binary_robot_look_end");

#define FRAME_MS 125u /* 8 fps */

/* 命令队列：串口单字符 + 合成键事件码（0x81..0x84，不与 ASCII 冲突） */
#define Q_EVT_KEY1_SHORT 0x81u
#define Q_EVT_KEY1_LONG  0x82u
#define Q_EVT_KEY2_SHORT 0x83u
#define Q_EVT_KEY2_LONG  0x84u

static QueueHandle_t s_cmd_q;
/* M5a：C05 事件行解析结果队列（console 任务 → 主任务；状态只由主任务改写） */
static QueueHandle_t s_evt_q;

/* 全局状态（只在主任务读写；按键任务只投递事件） */
static struct pet_state *s_state;
static pet_pomo_t s_pomo;
static pet_menu_t s_menu;
static pet_mode_t s_mode = PET_MODE_DUTY;
static bool s_menu_open;
static bool s_muted;
static volatile bool s_pwr_off;
static pet_look_t *s_look;
static pet_surface_t s_surface;
static uint16_t *s_fb;

/* 请求位 */
static bool s_dump_req;
static bool s_status_req;
static bool s_mode_req;

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

static int s_preset_idx;
static bool s_log_muted;

static uint32_t now_ms_u32(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
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

static const char *pomo_phase_name(pet_pomo_phase_t ph)
{
    switch (ph) {
    case PET_POMO_IDLE: return "IDLE";
    case PET_POMO_FOCUS: return "FOCUS";
    case PET_POMO_FOCUS_PAUSED: return "FOCUS_PAUSED";
    case PET_POMO_BREAK: return "BREAK";
    case PET_POMO_BREAK_PAUSED: return "BREAK_PAUSED";
    case PET_POMO_FOCUS_DONE: return "FOCUS_DONE";
    case PET_POMO_BREAK_DONE: return "BREAK_DONE";
    default: return "?";
    }
}

/* ---- 状态打印（G1/G3/G5/G6 自动化按这些行解析） ---- */

static void print_status(uint32_t fps)
{
    pet_view_t v;
    pet_state_view(s_state, &v);
    printf("STATUS state=%s frame=%d cards=%d fps=%lu heap=%lu\n",
           state_name(v.state), (int)v.frame, v.card_count,
           (unsigned long)fps, (unsigned long)esp_get_free_heap_size());
    /* M5b-1：网络自检行（TCP/NTP/G13 判读用；C08 §4） */
    pet_net_status_t ns;
    pet_net_get_status(&ns);
    printf("NET state=%s ssid=%s ip=%s rssi=%d time_valid=%d\n",
           pet_net_state_name(ns.state), ns.ssid, ns.ip, ns.rssi,
           ns.time_valid ? 1 : 0);
    if (v.card_count > 0) {
        printf("CARDS ");
        for (int i = 0; i < v.card_count; i++) {
            const char *t = (v.cards[i].title[0] != '\0') ? v.cards[i].title : "-";
            printf("%s%s", (i == 0) ? "" : "|", t);
        }
        printf("\n");
    }
}

static void print_mode_status(void)
{
    pet_pomo_view_t pv;
    pet_pomo_view_get(&s_pomo, &pv);
    printf("MODE mode=%s menu=%d sel=%d phase=%s remain_ms=%lu total_ms=%lu "
           "done=%lu focus_s=%lu muted=%d day=%ld has_day=%d "
           "key1=%d key2=%d pwr=%d\n",
           (s_mode == PET_MODE_DUTY) ? "DUTY" : "POMO", s_menu_open ? 1 : 0,
           s_menu.selected, pomo_phase_name(pv.phase),
           (unsigned long)pv.remaining_ms, (unsigned long)pv.total_ms,
           (unsigned long)pv.done_count, (unsigned long)pv.focus_seconds,
           s_muted ? 1 : 0, (long)(s_pomo.has_day ? s_pomo.day : -1),
           s_pomo.has_day ? 1 : 0,
           (gpio_get_level(KEY1_GPIO) == 0) ? 1 : 0,
           (gpio_get_level(KEY2_GPIO) == 0) ? 1 : 0,
           (gpio_get_level(PWR_GPIO) == 0) ? 1 : 0);
}

/* 帧缓冲导出：FBBIN 头 + 115200 原始字节 + FBEND crc32 */
static void crc32_update(uint32_t *crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        *crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            *crc = (*crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(*crc & 1u)));
        }
    }
}

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
    vTaskDelay(pdMS_TO_TICKS(20));
    s_log_muted = false;
    esp_log_level_set("*", ESP_LOG_INFO);
}

/* ---- 键/命令处理（主任务内串行） ---- */

static void save_stats(void)
{
    pet_pomo_nvs_save(&s_pomo);
}

static void set_mode(pet_mode_t mode)
{
    if (s_mode == mode) {
        return;
    }
    save_stats(); /* 离开番茄钟/切回前落盘 */
    s_mode = mode;
    ESP_LOGI(TAG, "mode=%s", (mode == PET_MODE_DUTY) ? "DUTY" : "POMO");
}

static void toggle_mute(void)
{
    s_muted = !s_muted;
    pet_mute_nvs_save(s_muted);
    ESP_LOGI(TAG, "mute=%d", s_muted ? 1 : 0);
}

static void handle_menu_key(pet_key_t key, pet_key_event_t ev)
{
    const pet_menu_cmd_t cmd = pet_menu_key(&s_menu, key, ev);
    switch (cmd) {
    case PET_MENU_CMD_NONE:
        break;
    case PET_MENU_CMD_TOGGLE_MUTE:
        toggle_mute();
        break;
    case PET_MENU_CMD_CLOSE:
        s_menu_open = false;
        ESP_LOGI(TAG, "menu closed");
        break;
    case PET_MENU_CMD_PROVISION:
        s_menu_open = false;
        ESP_LOGI(TAG, "menu: WIFI -> provisioning (AP mode)");
        pet_net_enter_provisioning();
        break;
    case PET_MENU_CMD_BT_PAIR:
        s_menu_open = false;
        ESP_LOGI(TAG, "menu: BT -> M5 not implemented");
        break;
    case PET_MENU_CMD_BACK_WINDOW:
        s_menu_open = false;
        ESP_LOGI(TAG, "menu: BACK -> host window (M5)");
        break;
    }
}

static void handle_key(pet_key_t key, pet_key_event_t ev, uint32_t now)
{
    if (ev != PET_KEY_EV_SHORT && ev != PET_KEY_EV_LONG) {
        return; /* PRESS/RELEASE 留给 M4 PTT */
    }
    if (s_menu_open) {
        handle_menu_key(key, ev); /* 菜单内屏蔽主界面动作 */
        return;
    }
    switch (pet_keymap_action(s_mode, key, ev)) {
    case PET_ACT_POMO_SHORT:
        pet_pomo_command(&s_pomo, PET_POMO_CMD_SHORT, now);
        ESP_LOGI(TAG, "pomo short -> %s", pomo_phase_name(s_pomo.phase));
        break;
    case PET_ACT_POMO_LONG:
        pet_pomo_command(&s_pomo, PET_POMO_CMD_LONG, now);
        save_stats();
        ESP_LOGI(TAG, "pomo long -> abandon (%s)", pomo_phase_name(s_pomo.phase));
        break;
    case PET_ACT_MODE_POMO:
        set_mode(PET_MODE_POMO);
        break;
    case PET_ACT_MODE_DUTY:
        set_mode(PET_MODE_DUTY);
        break;
    case PET_ACT_MUTE_TOGGLE:
        toggle_mute();
        break;
    case PET_ACT_SEND:
        ESP_LOGI(TAG, "SEND -> host (M5 not implemented)");
        break;
    case PET_ACT_MENU:
        pet_menu_init(&s_menu);
        s_menu_open = true;
        ESP_LOGI(TAG, "menu open");
        break;
    case PET_ACT_NONE:
    default:
        break;
    }
}

static void handle_cmd(uint8_t cmd, uint32_t now)
{
    switch (cmd) {
    /* M2 注入（保持兼容） */
    case '1': { const pet_event_t ev = {.kind = PET_EV_TASK_START, .now_ms = now}; pet_state_apply(s_state, &ev); } break;
    case '2': { const pet_event_t ev = {.kind = PET_EV_AGENT_INPUT_REQUIRED, .now_ms = now}; pet_state_apply(s_state, &ev); } break;
    case '3': { const pet_event_t ev = {.kind = PET_EV_TASK_DONE, .now_ms = now}; pet_state_apply(s_state, &ev); } break;
    case '4': { const pet_event_t ev = {.kind = PET_EV_TASK_ERROR, .now_ms = now}; pet_state_apply(s_state, &ev); } break;
    case '0': { const pet_event_t ev = {.kind = PET_EV_AGENT_IDLE, .now_ms = now}; pet_state_apply(s_state, &ev); } break;
    case '5': { const pet_event_t ev = {.kind = PET_EV_LINK_DOWN, .now_ms = now}; pet_state_apply(s_state, &ev); } break;
    case 'u': { const pet_event_t ev = {.kind = PET_EV_LINK_UP, .now_ms = now}; pet_state_apply(s_state, &ev); } break;
    case 'n': { const pet_event_t ev = {.kind = PET_EV_ANNOUNCEMENT, .now_ms = now}; pet_state_apply(s_state, &ev); } break;
    case 'c':
        pet_state_set_card(s_state, &PRESETS[s_preset_idx]);
        s_preset_idx = (s_preset_idx + 1) % PRESET_COUNT;
        break;
    case 'x':
        pet_state_set_card(s_state, NULL);
        s_preset_idx = 0;
        break;
    case 'p': s_status_req = true; break;
    case 'd': s_dump_req = true; break;

    /* M4：直接播报（H 测试用；绕过状态机，仍受静音开关约束） */
    case 'a':
        if (pet_audio_should_play(PET_SOUND_INPUT, s_muted)) {
            ESP_LOGI(TAG, "AUDIO test input (%s)", esp_err_to_name(board_audio_play(PET_SOUND_INPUT)));
        } else {
            ESP_LOGI(TAG, "AUDIO test input skipped (muted/off)");
        }
        break;
    case 'b':
        if (pet_audio_should_play(PET_SOUND_DONE, s_muted)) {
            ESP_LOGI(TAG, "AUDIO test done (%s)", esp_err_to_name(board_audio_play(PET_SOUND_DONE)));
        } else {
            ESP_LOGI(TAG, "AUDIO test done skipped (muted/off)");
        }
        break;
    case 'f':
        if (pet_audio_should_play(PET_SOUND_FAILED, s_muted)) {
            ESP_LOGI(TAG, "AUDIO test failed (%s)", esp_err_to_name(board_audio_play(PET_SOUND_FAILED)));
        } else {
            ESP_LOGI(TAG, "AUDIO test failed skipped (muted/off)");
        }
        break;

    /* M3：合成键事件（与真实按键走同一处理路径） */
    case 'K': handle_key(PET_KEY1, PET_KEY_EV_SHORT, now); break;
    case 'L': handle_key(PET_KEY1, PET_KEY_EV_LONG, now); break;
    case 'J': handle_key(PET_KEY2, PET_KEY_EV_SHORT, now); break;
    case 'H': handle_key(PET_KEY2, PET_KEY_EV_LONG, now); break;

    /* M3：状态/演示/重启 */
    case 'm': s_mode_req = true; break;
    case 'y':
        if (s_pomo.has_day) {
            pet_pomo_set_day(&s_pomo, s_pomo.day + 1);
        } else {
            pet_pomo_set_day(&s_pomo, 1);
        }
        save_stats();
        ESP_LOGI(TAG, "day sim -> day=%ld (stats cleared)", (long)s_pomo.day);
        break;
    case 't':
        pet_pomo_set_durations(&s_pomo, 6000u, 3000u);
        ESP_LOGI(TAG, "durations demo: focus=6s break=3s");
        break;
    case 'R':
        save_stats();
        ESP_LOGI(TAG, "restart requested");
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_restart();
        break;

    /* M5b-1：进入 AP 配网（C08 §2；菜单 WIFI 同路径） */
    case 'W':
        ESP_LOGI(TAG, "serial: enter provisioning (AP mode)");
        pet_net_enter_provisioning();
        break;
    default: break; /* \r \n 等忽略 */
    }
}

/* M5a：把一条 C05 事件行应用到状态机 + 卡栈（只在主任务调用）。 */
static void apply_link_msg(const pet_link_msg_t *m, uint32_t now)
{
    pet_event_t ev;
    memset(&ev, 0, sizeof ev);
    ev.kind = m->kind;
    ev.now_ms = now;
    pet_state_apply(s_state, &ev);
    if (m->has_tasks) {
        if (m->card_count > 0) {
            pet_state_set_cards(s_state, m->cards, m->card_count); /* 整栈替换 */
        } else {
            pet_state_set_card(s_state, NULL); /* tasks:[] → 清空卡栈（C05 §4） */
        }
    } else if (m->has_card) {
        pet_state_set_card(s_state, &m->card);
    }
}

/* M5b-1：WiFi TCP 收到的 C05 行（已跨通道去重）。在 pet_net 内部任务上下文调用，
 * 只做解析 + 入队，不阻塞（C08 §4 / pet_net.h 接线说明）。 */
static void net_line_cb(const char *line, size_t len)
{
    pet_link_msg_t m;
    if (pet_link_parse(line, len, &m) == PET_LINK_OK) {
        if (s_evt_q != NULL) {
            (void)xQueueSend(s_evt_q, &m, 0);
        }
    } else {
        ESP_LOGW(TAG, "wifi event line dropped");
    }
}

static void console_task(void *arg)
{
    (void)arg;
    usb_serial_jtag_driver_config_t cfg = {
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
    usb_serial_jtag_vfs_use_driver();
    ESP_LOGI(TAG, "console ready (M5a): C05 NDJSON lines + M3 debug chars K/L/J/H m y R t a/b/f audio; M2 chars 1..5 0 u n c x p d");

    pet_linebuf_t lb;
    bool json_mode = false;
    uint8_t buf[64];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof buf, pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            const char c = (char)buf[i];
            if (!json_mode) {
                if (c == '{') {
                    /* C05 事件行开始：行缓冲接管，直到 '\n' */
                    json_mode = true;
                    pet_linebuf_init(&lb);
                    (void)pet_linebuf_feed(&lb, c);
                } else if (c != '\r' && c != '\n') {
                    if (s_cmd_q != NULL) {
                        (void)xQueueSend(s_cmd_q, &buf[i], 0);
                    }
                }
                continue;
            }
            const pet_line_feed_t r = pet_linebuf_feed(&lb, c);
            if (r == PET_LINE_READY) {
                json_mode = false;
                /* C08 §1.1：USB 与 WiFi 共用一个去重器（1 s 内同指纹只处理一次） */
                if (!pet_net_line_accept(now_ms_u32(), lb.buf, lb.len)) {
                    continue;
                }
                pet_link_msg_t m;
                const pet_link_status_t st = pet_link_parse(lb.buf, lb.len, &m);
                if (st == PET_LINK_OK) {
                    if (s_evt_q != NULL) {
                        (void)xQueueSend(s_evt_q, &m, 0);
                    }
                } else {
                    ESP_LOGW(TAG, "event line dropped (status=%d)", (int)st);
                }
            } else if (r == PET_LINE_DROPPED) {
                json_mode = false;
                ESP_LOGW(TAG, "event line too long (dropped)");
            }
        }
    }
}

/* 20 ms 按键采样任务：只投递合成事件，不改全局状态 */
static void key_task(void *arg)
{
    (void)arg;
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << 0) | (1ULL << 4) | (1ULL << 5),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    pet_button_t b1;
    pet_button_t b2;
    pet_button_init(&b1);
    pet_button_init(&b2);
    uint32_t pwr_start = 0;
    bool pwr_fired = false;

    ESP_LOGI(TAG, "keys init: KEY1=GPIO0 KEY2=GPIO4 PWR=GPIO5 (low active)");

    TickType_t last = xTaskGetTickCount();
    for (;;) {
        const uint32_t now = now_ms_u32();
        const bool k1 = gpio_get_level(KEY1_GPIO) == 0;
        const bool k2 = gpio_get_level(KEY2_GPIO) == 0;
        const bool pwr = gpio_get_level(PWR_GPIO) == 0;

        const pet_key_event_t e1 = pet_button_update(&b1, k1, now);
        const pet_key_event_t e2 = pet_button_update(&b2, k2, now);
        uint8_t code = 0;
        if (e1 == PET_KEY_EV_SHORT) {
            code = Q_EVT_KEY1_SHORT;
        } else if (e1 == PET_KEY_EV_LONG) {
            code = Q_EVT_KEY1_LONG;
        } else if (e2 == PET_KEY_EV_SHORT) {
            code = Q_EVT_KEY2_SHORT;
        } else if (e2 == PET_KEY_EV_LONG) {
            code = Q_EVT_KEY2_LONG;
        }
        if (code != 0 && s_cmd_q != NULL) {
            (void)xQueueSend(s_cmd_q, &code, 0);
        }

        /* PWR：长按 ≥1500 ms 置关机请求；短按无动作 */
        if (pwr) {
            if (pwr_start == 0) {
                pwr_start = now;
            } else if (!pwr_fired && now - pwr_start >= PWR_LONGPRESS_MS) {
                pwr_fired = true;
                s_pwr_off = true;
            }
        } else {
            pwr_start = 0;
            pwr_fired = false;
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(KEY_DEBOUNCE_MS));
    }
}

static void do_power_off(void)
{
    save_stats();
    pet_mute_nvs_save(s_muted);
    ESP_LOGW(TAG, "PWR long: power off (backlight off, GPIO2 low)");
    board_lcd_backlight_set(false);
    gpio_set_level(POWER_LATCH_GPIO, 0);
    /* 不返回应用层；USB 供电时设备不会真正断电，需要复位/重新上电恢复 */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
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

    /* ---- 2. 面板 ---- */
    err = board_lcd_init();
    if (err != ESP_OK) {
        return; /* C02 §3.4：失败只记录，不挂死 */
    }

    /* ---- 3. 帧缓冲 ---- */
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
    s_fb = fb;
    memset(fb, 0, BOARD_LCD_FB_BYTES);
    s_surface.pixels = (uint8_t *)fb;
    s_surface.width = BOARD_LCD_WIDTH;
    s_surface.height = BOARD_LCD_HEIGHT;
    s_surface.stride = BOARD_LCD_WIDTH * 2;

    /* ---- 4. 立绘 + 状态机 ---- */
    s_look = pet_look_parse(robot_look_start,
                            (size_t)(robot_look_end - robot_look_start));
    if (s_look == NULL) {
        ESP_LOGE(TAG, "pet_look_parse failed");
        return;
    }
    s_state = pet_state_create();
    if (s_state == NULL) {
        ESP_LOGE(TAG, "pet_state_create failed");
        return;
    }

    /* ---- 5. NVS + 番茄钟/静音恢复 ---- */
    esp_err_t nerr = nvs_flash_init();
    if (nerr == ESP_ERR_NVS_NO_FREE_PAGES || nerr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        (void)nvs_flash_erase();
        nerr = nvs_flash_init();
    }
    if (nerr != ESP_OK) {
        ESP_LOGW(TAG, "nvs_flash_init failed (%s): stats/mute 退化为内存态",
                 esp_err_to_name(nerr));
    }
    pet_pomo_init(&s_pomo, now_ms_u32());
    pet_pomo_nvs_load(&s_pomo);
    pet_mute_nvs_load(&s_muted);
    pet_menu_init(&s_menu);
    ESP_LOGI(TAG, "restored: done=%lu focus_s=%lu day=%ld has_day=%d muted=%d",
             (unsigned long)s_pomo.done_count,
             (unsigned long)s_pomo.focus_seconds,
             (long)(s_pomo.has_day ? s_pomo.day : -1), s_pomo.has_day ? 1 : 0,
             s_muted ? 1 : 0);

    /* ---- 5b. 音频输出（M4）：失败只降级为无声，不阻塞显示（C04 §5） ---- */
    err = board_audio_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "audio init failed (%s): 播报降级为静音", esp_err_to_name(err));
    }

    /* ---- 6. 队列 + 任务 ---- */
    s_cmd_q = xQueueCreate(32, sizeof(uint8_t));
    s_evt_q = xQueueCreate(8, sizeof(pet_link_msg_t));
    if (s_cmd_q == NULL || s_evt_q == NULL) {
        ESP_LOGE(TAG, "cmd/evt queue alloc failed");
    } else {
        if (xTaskCreate(console_task, "inject", 4096, NULL, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "inject task create failed");
        }
        if (xTaskCreate(key_task, "keys", 3072, NULL, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "key task create failed");
        }
    }

    /* M5b-1：WiFi 事件通道（TCP 8766）+ 配网 + NTP；失败只降级（C08 §4） */
    pet_net_set_line_cb(net_line_cb);
    {
        const esp_err_t nerr2 = pet_net_start();
        if (nerr2 != ESP_OK) {
            ESP_LOGW(TAG, "net start failed (%s): WiFi 通道不可用", esp_err_to_name(nerr2));
        }
    }

    ESP_LOGI(TAG, "m5b: READY (240x240, 8fps, keys=GPIO0/4/5, pomo 25/5, C05 USB+WiFi, audio=%s)",
             board_audio_ready() ? "on" : "off");

    /* ---- 7. 主循环 ---- */
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t fps_win_start = now_ms_u32();
    uint32_t fps_frames = 0;
    uint32_t fps = 0;

    for (;;) {
        const uint32_t now = now_ms_u32();

        uint8_t cmd;
        while (s_cmd_q != NULL && xQueueReceive(s_cmd_q, &cmd, 0) == pdTRUE) {
            /* 合成键事件码 → 与真实按键同一路径 */
            switch (cmd) {
            case Q_EVT_KEY1_SHORT: handle_key(PET_KEY1, PET_KEY_EV_SHORT, now); break;
            case Q_EVT_KEY1_LONG:  handle_key(PET_KEY1, PET_KEY_EV_LONG, now); break;
            case Q_EVT_KEY2_SHORT: handle_key(PET_KEY2, PET_KEY_EV_SHORT, now); break;
            case Q_EVT_KEY2_LONG:  handle_key(PET_KEY2, PET_KEY_EV_LONG, now); break;
            default: handle_cmd(cmd, now); break;
            }
        }

        if (s_pwr_off) {
            do_power_off();
        }

        /* M5a：应用 C05 事件（状态与卡栈只由主任务改写） */
        pet_link_msg_t link_msg;
        while (s_evt_q != NULL && xQueueReceive(s_evt_q, &link_msg, 0) == pdTRUE) {
            apply_link_msg(&link_msg, now);
        }

        pet_state_tick(s_state, now);
        pet_pomo_tick(&s_pomo, now);
        if (pet_pomo_take_alarm(&s_pomo)) {
            save_stats();
            ESP_LOGI(TAG, "ALARM phase=%s done=%lu (audio M4)", pomo_phase_name(s_pomo.phase),
                     (unsigned long)s_pomo.done_count);
        }

        /* M4：一次性播报（C04 §3；去重由 take_sound "取走即清"保证） */
        {
            const pet_sound_t snd = pet_state_take_sound(s_state);
            if (snd != PET_SOUND_NONE) {
                const pet_audio_asset_t asset = pet_audio_asset_for(snd);
                if (pet_audio_should_play(snd, s_muted)) {
                    const esp_err_t perr = board_audio_play(snd);
                    ESP_LOGI(TAG, "AUDIO play %s (%s)", pet_audio_asset_name(asset),
                             perr == ESP_OK ? "queued" : esp_err_to_name(perr));
                } else {
                    ESP_LOGI(TAG, "AUDIO skipped %s (muted)", pet_audio_asset_name(asset));
                }
            }
        }

        /* 渲染：菜单 > 番茄钟 > 值班 */
        if (s_menu_open) {
            pet_pomo_view_t pv;
            pet_pomo_view_get(&s_pomo, &pv);
            pet_menu_view_t mv = {
                .muted = s_muted,
                .selected = s_menu.selected,
                .mode = (s_mode == PET_MODE_POMO) ? 1 : 0,
                .phase = (int)pv.phase,
                .done_count = pv.done_count,
                .focus_seconds = pv.focus_seconds,
            };
            pet_ui_menu_240(&s_surface, &mv);
        } else if (s_mode == PET_MODE_POMO) {
            pet_pomo_view_t pv;
            pet_pomo_view_get(&s_pomo, &pv);
            pet_ui_pomo_240(&s_surface, &pv);
        } else {
            pet_view_t view;
            pet_state_view(s_state, &view);
            view.muted = s_muted; /* C03 §7 R9：待机画面静音图标 */
            pet_ui_scene_240(&view, s_look, &s_surface);
        }

        err = board_lcd_present(fb);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "present failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (s_status_req) {
            s_status_req = false;
            print_status(fps);
        }
        if (s_mode_req) {
            s_mode_req = false;
            print_mode_status();
        }
        if (s_dump_req) {
            s_dump_req = false;
            dump_framebuffer(fb);
        }

        fps_frames++;
        if (now - fps_win_start >= 1000u) {
            fps = fps_frames * 1000u / (now - fps_win_start);
            fps_win_start = now;
            fps_frames = 0;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FRAME_MS));
    }
}
