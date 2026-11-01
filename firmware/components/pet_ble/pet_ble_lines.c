/* M5b-2：BLE GATT write 字节流 → C05 NDJSON 行的重装纯逻辑（契约 C09 §1.1）。
 *
 * 复用 pet_ui 的 pet_linebuf（未改动）：行长 ≤512 B、剥 `\r`、超长整行丢弃。
 * 本文件不碰 NimBLE/硬件/去重，可在 host 上单测（tests/test_m5b_ble.c）。
 */
#include "pet_ble_lines.h"

void pet_ble_lines_init(pet_ble_lines_t *ls)
{
    if (ls == NULL) {
        return;
    }
    pet_linebuf_init(&ls->lb);
    ls->lines_total = 0;
    ls->lines_dropped = 0;
}

uint32_t pet_ble_lines_feed(pet_ble_lines_t *ls, const uint8_t *data, size_t len,
                            pet_ble_line_deliver_cb_t cb, void *user)
{
    if (ls == NULL || (data == NULL && len != 0)) {
        return 0;
    }

    uint32_t delivered = 0;
    for (size_t i = 0; i < len; i++) {
        const pet_line_feed_t r = pet_linebuf_feed(&ls->lb, (char)data[i]);
        if (r == PET_LINE_READY) {
            if (ls->lb.len == 0) {
                continue; /* 空行/纯 CRLF：C05 只处理 JSON 行，不交付不计数 */
            }
            ls->lines_total++;
            delivered++;
            if (cb != NULL) {
                cb(ls->lb.buf, ls->lb.len, user);
            }
        } else if (r == PET_LINE_DROPPED) {
            ls->lines_dropped++;
        }
    }
    return delivered;
}
