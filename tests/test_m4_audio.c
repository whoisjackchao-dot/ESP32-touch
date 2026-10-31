/* M4 host 测试：语音播报纯逻辑（T）+ 资产格式（T/D）。
 * 契约：docs/contracts/C04-audio-output.md §2/§3/§6。
 * 用法（仓库根运行）：pwsh -NoProfile -File tests/run_m4_tests.ps1
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "pet_audio_policy.h"

static int g_pass;
static int g_fail;

static void check(const char *name, int cond, const char *detail)
{
    if (cond) {
        g_pass++;
        printf("PASS %s\n", name);
    } else {
        g_fail++;
        printf("FAIL %s | %s\n", name, detail ? detail : "");
    }
}

static void test_should_play(void)
{
    check("AUD-01-input-plays", pet_audio_should_play(PET_SOUND_INPUT, false), "");
    check("AUD-01-done-plays", pet_audio_should_play(PET_SOUND_DONE, false), "");
    check("AUD-01-failed-plays", pet_audio_should_play(PET_SOUND_FAILED, false), "");
    check("AUD-01-none-silent", !pet_audio_should_play(PET_SOUND_NONE, false), "");
    check("AUD-02-muted-input", !pet_audio_should_play(PET_SOUND_INPUT, true), "");
    check("AUD-02-muted-done", !pet_audio_should_play(PET_SOUND_DONE, true), "");
    check("AUD-02-muted-failed", !pet_audio_should_play(PET_SOUND_FAILED, true), "");
    check("AUD-02-muted-none", !pet_audio_should_play(PET_SOUND_NONE, true), "");
}

static void test_asset_mapping(void)
{
    check("AUD-03-map-input", pet_audio_asset_for(PET_SOUND_INPUT) == PET_AUDIO_ASSET_INPUT, "");
    check("AUD-03-map-done", pet_audio_asset_for(PET_SOUND_DONE) == PET_AUDIO_ASSET_DONE, "");
    check("AUD-03-map-failed", pet_audio_asset_for(PET_SOUND_FAILED) == PET_AUDIO_ASSET_FAILED, "");
    check("AUD-03-map-none", pet_audio_asset_for(PET_SOUND_NONE) == PET_AUDIO_ASSET_NONE, "");
    check("AUD-03-name-input", strcmp(pet_audio_asset_name(PET_AUDIO_ASSET_INPUT), "input") == 0, "");
    check("AUD-03-name-done", strcmp(pet_audio_asset_name(PET_AUDIO_ASSET_DONE), "done") == 0, "");
    check("AUD-03-name-failed", strcmp(pet_audio_asset_name(PET_AUDIO_ASSET_FAILED), "failed") == 0, "");
    check("AUD-03-name-none", strcmp(pet_audio_asset_name(PET_AUDIO_ASSET_NONE), "none") == 0, "");
}

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fclose(f);
    return n;
}

static void test_asset_file(const char *name, long min_samples, long max_samples)
{
    char path[128];
    snprintf(path, sizeof path, "firmware/main/assets/voice/%s.pcm", name);
    const long bytes = file_size(path);
    char detail[128];

    snprintf(detail, sizeof detail, "path=%s size=%ld", path, bytes);
    check("AUD-05-asset-exists", bytes > 0, detail);
    if (bytes <= 0) {
        return;
    }
    check("AUD-05-asset-even", (bytes % 2) == 0, detail);
    const long samples = bytes / 2;
    snprintf(detail, sizeof detail, "samples=%ld", samples);
    check("AUD-05-asset-duration", samples >= min_samples && samples <= max_samples, detail);

    /* 不能是 WAV（有 44 字节头）——C04 §2 要求 raw PCM */
    FILE *f = fopen(path, "rb");
    unsigned char hdr[4] = {0};
    if (f != NULL) {
        (void)fread(hdr, 1, 4, f);
        fclose(f);
    }
    check("AUD-05-asset-raw-pcm", !(hdr[0] == 'R' && hdr[1] == 'I' && hdr[2] == 'F' && hdr[3] == 'F'),
          "unexpected RIFF header");
}

int main(void)
{
    test_should_play();
    test_asset_mapping();
    /* C04 §2：0.1–3.0 s @16 kHz */
    test_asset_file("input", 1600, 48000);
    test_asset_file("done", 1600, 48000);
    test_asset_file("failed", 1600, 48000);

    printf("SUMMARY m4_audio: cases=%d passed=%d failed=%d\n", g_pass + g_fail, g_pass, g_fail);
    if (g_fail != 0) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS (all M4 audio host tests green)\n");
    return 0;
}
