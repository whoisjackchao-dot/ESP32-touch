/* M3：番茄钟统计 + 静音标志的 NVS 落盘（设备侧）。
 * 契约：docs/10-m3-development.md §3。缺失/损坏用默认值，不崩。 */
#include "pet_pomo_store.h"

#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "pet-nvs";
static const char *NVS_POMO = "pomo";
static const char *KEY_STATS = "stats";
static const char *NVS_VOICE = "voice";
static const char *KEY_MUTED = "muted";

void pet_pomo_nvs_load(pet_pomo_t *p)
{
    if (p == NULL) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_POMO, NVS_READONLY, &h) != ESP_OK) {
        return; /* 尚无命名空间：保留 init 默认值 */
    }
    uint8_t buf[PET_POMO_STATS_LEN];
    size_t len = sizeof buf;
    const esp_err_t err = nvs_get_blob(h, KEY_STATS, buf, &len);
    nvs_close(h);
    if (err != ESP_OK) {
        return;
    }
    if (!pet_pomo_decode_stats(buf, len, p)) {
        ESP_LOGW(TAG, "pomo stats blob 非法，忽略");
    }
}

void pet_pomo_nvs_save(const pet_pomo_t *p)
{
    if (p == NULL) {
        return;
    }
    uint8_t buf[PET_POMO_STATS_LEN];
    const size_t n = pet_pomo_encode_stats(buf, sizeof buf, p);
    if (n == 0) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_POMO, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(pomo) 失败，统计未落盘");
        return;
    }
    if (nvs_set_blob(h, KEY_STATS, buf, n) == ESP_OK) {
        (void)nvs_commit(h);
    }
    nvs_close(h);
}

void pet_mute_nvs_load(bool *muted)
{
    if (muted == NULL) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_VOICE, NVS_READONLY, &h) != ESP_OK) {
        return; /* 默认不静音 */
    }
    uint8_t v = 0;
    const esp_err_t err = nvs_get_u8(h, KEY_MUTED, &v);
    nvs_close(h);
    if (err == ESP_OK) {
        *muted = (v != 0);
    }
}

void pet_mute_nvs_save(bool muted)
{
    nvs_handle_t h;
    if (nvs_open(NVS_VOICE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(voice) 失败，静音未落盘");
        return;
    }
    if (nvs_set_u8(h, KEY_MUTED, (uint8_t)(muted ? 1 : 0)) == ESP_OK) {
        (void)nvs_commit(h);
    }
    nvs_close(h);
}
