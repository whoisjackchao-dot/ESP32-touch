/* M4 板级音频输出：ES8311（I2C 0x18）+ I2S TX + NS4150B（GPIO7）。
 * 契约：docs/contracts/C04-audio-output.md（参数与序列逐条对应）。
 * 来源：上游 ESP32-buddy（MIT）pet_audio.c 的已验证参数与序列，按本板引脚/采样率改写；
 *       ES8311 驱动为 esp-bsp Apache-2.0（firmware/components/es8311，保留 LICENSE）。
 * 只播放，不含麦克风；失败不阻塞显示（C04 §5）。 */
#include "board_audio.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "es8311.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "pet_audio_policy.h"

static const char *TAG = "board-audio";

/* C04 §1：本板引脚（原理图 §5.1） */
#define AUDIO_I2C_PORT   I2C_NUM_0
#define AUDIO_I2C_SDA    GPIO_NUM_42
#define AUDIO_I2C_SCL    GPIO_NUM_41
#define AUDIO_I2S_MCLK   GPIO_NUM_8
#define AUDIO_I2S_BCLK   GPIO_NUM_9
#define AUDIO_I2S_WS     GPIO_NUM_10
#define AUDIO_I2S_DOUT   GPIO_NUM_12
#define AUDIO_PA_GPIO    GPIO_NUM_7

#define AUDIO_SAMPLE_RATE      16000
#define AUDIO_MCLK_MULTIPLE    256u
#define AUDIO_MCLK_FREQ        (AUDIO_SAMPLE_RATE * (int)AUDIO_MCLK_MULTIPLE)
#define AUDIO_VOLUME           70

#define QUEUE_DEPTH   4
#define CHUNK_FRAMES  480 /* 30 ms @16k */
#define MIN_SAMPLES   (AUDIO_SAMPLE_RATE / 10)   /* 0.1 s */
#define MAX_SAMPLES   (AUDIO_SAMPLE_RATE * 3)    /* 3.0 s（C04 §2） */

/* 内嵌资产（main/CMakeLists.txt EMBED_FILES：assets/voice/ 下三条 pcm） */
extern const uint8_t input_pcm_start[]  asm("_binary_input_pcm_start");
extern const uint8_t input_pcm_end[]    asm("_binary_input_pcm_end");
extern const uint8_t done_pcm_start[]   asm("_binary_done_pcm_start");
extern const uint8_t done_pcm_end[]     asm("_binary_done_pcm_end");
extern const uint8_t failed_pcm_start[] asm("_binary_failed_pcm_start");
extern const uint8_t failed_pcm_end[]   asm("_binary_failed_pcm_end");

typedef struct {
    const uint8_t *data;
    size_t         bytes;
} sample_t;

static i2s_chan_handle_t s_tx;
static es8311_handle_t   s_codec;
static QueueHandle_t     s_q;
static volatile bool     s_playing;
static bool              s_ready;
static bool              s_i2c_installed;

static sample_t sample_of(pet_audio_asset_t asset)
{
    switch (asset) {
    case PET_AUDIO_ASSET_INPUT:
        return (sample_t){input_pcm_start, (size_t)(input_pcm_end - input_pcm_start)};
    case PET_AUDIO_ASSET_DONE:
        return (sample_t){done_pcm_start, (size_t)(done_pcm_end - done_pcm_start)};
    case PET_AUDIO_ASSET_FAILED:
        return (sample_t){failed_pcm_start, (size_t)(failed_pcm_end - failed_pcm_start)};
    default:
        return (sample_t){NULL, 0};
    }
}

static void pa_set(bool on)
{
#if AUDIO_PA_GPIO >= 0
    (void)gpio_set_level(AUDIO_PA_GPIO, on ? 1 : 0);
#else
    (void)on;
#endif
}

/* mono → stereo 槽位分块写（C04 §4：MONO 槽位会 2 倍速） */
static esp_err_t write_mono(const int16_t *mono, size_t samples)
{
    if (s_tx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    static int16_t stereo[2 * CHUNK_FRAMES];
    size_t done = 0;
    while (done < samples) {
        size_t n = samples - done;
        if (n > CHUNK_FRAMES) {
            n = CHUNK_FRAMES;
        }
        for (size_t i = 0; i < n; i++) {
            stereo[2 * i] = mono[done + i];
            stereo[2 * i + 1] = mono[done + i];
        }
        size_t written = 0;
        const esp_err_t err = i2s_channel_write(s_tx, stereo,
                                                n * 2 * sizeof(int16_t),
                                                &written, pdMS_TO_TICKS(2000));
        if (err != ESP_OK) {
            return err;
        }
        done += n;
    }
    return ESP_OK;
}

static void play_now(pet_sound_t sound)
{
    const pet_audio_asset_t asset = pet_audio_asset_for(sound);
    const sample_t s = sample_of(asset);
    if (s.data == NULL) {
        return;
    }
    const size_t samples = s.bytes / sizeof(int16_t);
    if (samples < MIN_SAMPLES || samples > MAX_SAMPLES) {
        ESP_LOGE(TAG, "AUDIO bad asset %s (%u samples)", pet_audio_asset_name(asset),
                 (unsigned)samples);
        return;
    }

    pa_set(true);
    vTaskDelay(pdMS_TO_TICKS(10));
    if (s_codec != NULL) {
        es8311_voice_volume_set(s_codec, AUDIO_VOLUME, NULL);
        es8311_voice_mute(s_codec, false);
    }

    const esp_err_t err = write_mono((const int16_t *)s.data, samples);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2s write failed: %s", esp_err_to_name(err));
    }

    /* 静音尾：避免停在波形沿产生持续电平/长鸣（C04 §4） */
    static const int16_t silence[320] = {0};
    (void)write_mono(silence, sizeof(silence) / sizeof(silence[0]));

    if (s_codec != NULL) {
        es8311_voice_mute(s_codec, true);
    }
    pa_set(false);
}

static void audio_task(void *arg)
{
    (void)arg;
    for (;;) {
        pet_sound_t sound;
        if (xQueueReceive(s_q, &sound, portMAX_DELAY) == pdTRUE) {
            s_playing = true;
            play_now(sound);
            s_playing = false;
        }
    }
}

static esp_err_t i2c_init_once(void)
{
    if (s_i2c_installed) {
        return ESP_OK;
    }
    const i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = AUDIO_I2C_SDA,
        .scl_io_num = AUDIO_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    esp_err_t err = i2c_param_config(AUDIO_I2C_PORT, &cfg);
    if (err == ESP_OK) {
        err = i2c_driver_install(AUDIO_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    }
    if (err == ESP_OK) {
        s_i2c_installed = true;
    }
    return err;
}

esp_err_t board_audio_init(void)
{
    /* 功放先关（上电默认低），避免初始化杂音 */
    const gpio_config_t pa_cfg = {
        .pin_bit_mask = (1ULL << AUDIO_PA_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&pa_cfg));
    pa_set(false);

    esp_err_t err = i2c_init_once();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c init: %s", esp_err_to_name(err));
        return err;
    }

    /* I2S TX（master，STEREO 槽位，MCLK×256） */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_I2S_MCLK,
            .bclk = AUDIO_I2S_BCLK,
            .ws = AUDIO_I2S_WS,
            .dout = AUDIO_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {false, false, false},
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s tx init: %s", esp_err_to_name(err));
        return err;
    }
    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s tx enable: %s", esp_err_to_name(err));
        return err;
    }

    /* ES8311（slave；MCLK 来自 ESP32 MCLK 引脚） */
    s_codec = es8311_create(AUDIO_I2C_PORT, ES8311_ADDRESS_0);
    if (s_codec == NULL) {
        ESP_LOGE(TAG, "es8311_create failed");
        return ESP_FAIL;
    }
    const es8311_clock_config_t clk_cfg = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = AUDIO_MCLK_FREQ,
        .sample_frequency = AUDIO_SAMPLE_RATE,
    };
    err = es8311_init(s_codec, &clk_cfg, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "es8311_init: %s", esp_err_to_name(err));
        es8311_delete(s_codec);
        s_codec = NULL;
        return err;
    }
    (void)es8311_voice_volume_set(s_codec, AUDIO_VOLUME, NULL);
    (void)es8311_voice_mute(s_codec, true); /* 空闲静音 */

    s_q = xQueueCreate(QUEUE_DEPTH, sizeof(pet_sound_t));
    if (s_q == NULL) {
        ESP_LOGE(TAG, "audio queue alloc failed");
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(audio_task, "audio", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "audio task create failed");
        return ESP_FAIL;
    }

    s_ready = true;
    ESP_LOGI(TAG, "AUDIO READY (ES8311 0x18, %d Hz, stereo slots, MCLK x%u, PA=GPIO%d)",
             AUDIO_SAMPLE_RATE, AUDIO_MCLK_MULTIPLE, (int)AUDIO_PA_GPIO);
    return ESP_OK;
}

esp_err_t board_audio_play(pet_sound_t sound)
{
    if (!s_ready || s_q == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pet_audio_asset_for(sound) == PET_AUDIO_ASSET_NONE) {
        return ESP_ERR_INVALID_ARG;
    }
    return (xQueueSend(s_q, &sound, 0) == pdTRUE) ? ESP_OK : ESP_ERR_NO_MEM;
}

bool board_audio_ready(void)
{
    return s_ready;
}

bool board_audio_busy(void)
{
    return s_playing || (s_q != NULL && uxQueueMessagesWaiting(s_q) > 0);
}
