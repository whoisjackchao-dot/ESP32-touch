/* M5b-1：跨通道去重纯逻辑（C08 §1.1）——16 槽指纹环 + 1000 ms 窗口。
 *
 * 与契约的对应：
 *   - 指纹 = 长度 + FNV-1a 64（这里 fp 与 len 分开存，命中要求两者相等）；
 *   - 同指纹在 1000 ms 内重复 → DROP；>=1000 ms → ACCEPT 并刷新该槽时间；
 *   - DROP 不刷新时间戳（重复风暴只压一个窗口）；
 *   - 16 槽轮转覆盖（最旧一条被覆盖后即使仍在窗口内也不再命中）。
 *
 * 本文件不依赖 ESP-IDF（host 可测）：now_ms 由调用方注入，时间比较用无符号差
 * (uint32_t)(now - rx_ms) 以安全处理 32 位回绕。 */
#include "pet_net_dedupe.h"

#include <string.h>

#define FNV_OFFSET_BASIS 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL

void pet_net_dedupe_init(pet_net_dedupe_t *d)
{
    if (d == NULL) {
        return;
    }
    memset(d, 0, sizeof *d);
}

uint64_t pet_net_dedupe_fingerprint(const char *line, size_t len)
{
    uint64_t h = FNV_OFFSET_BASIS;
    if (line == NULL) {
        return h; /* 防御：NULL 视为空串 */
    }
    for (size_t i = 0; i < len; i++) {
        h ^= (uint64_t)(uint8_t)line[i];
        h *= FNV_PRIME;
    }
    return h;
}

pet_net_dedupe_result_t pet_net_dedupe_feed(pet_net_dedupe_t *d, uint32_t now_ms,
                                           const char *line, size_t len)
{
    if (d == NULL || line == NULL) {
        return PET_NET_DEDUPE_DROP; /* 防御：不崩、不计统计 */
    }

    const uint64_t fp = pet_net_dedupe_fingerprint(line, len);
    const uint16_t n = (uint16_t)(len > 0xFFFFu ? 0xFFFFu : len);

    /* 1) 先查环：命中且仍在窗口 → DROP；命中但已出窗口 → 刷新时间后 ACCEPT。 */
    for (size_t i = 0; i < PET_NET_DEDUPE_SLOTS; i++) {
        if (!d->used[i] || d->fp[i] != fp || d->len[i] != n) {
            continue;
        }
        if ((uint32_t)(now_ms - d->rx_ms[i]) < PET_NET_DEDUPE_WINDOW_MS) {
            d->dropped++;
            return PET_NET_DEDUPE_DROP;
        }
        d->rx_ms[i] = now_ms;
        d->accepted++;
        return PET_NET_DEDUPE_ACCEPT;
    }

    /* 2) 未命中：轮转覆盖最旧槽（环满时自然淘汰最旧一条）。 */
    const size_t slot = (size_t)d->next % PET_NET_DEDUPE_SLOTS;
    d->fp[slot] = fp;
    d->len[slot] = n;
    d->rx_ms[slot] = now_ms;
    d->used[slot] = true;
    d->next = (uint8_t)((slot + 1u) % PET_NET_DEDUPE_SLOTS);
    d->accepted++;
    return PET_NET_DEDUPE_ACCEPT;
}
