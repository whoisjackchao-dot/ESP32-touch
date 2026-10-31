/* M3：按键短/长按状态机（纯逻辑）。
 * 契约：docs/contracts/C07-keymap.md §2。来源：上游 ESP32-buddy（MIT）pet_button.c，
 * 行为一致（20 ms 采样由设备侧保证；本层只认边沿）。
 * 边沿驱动：按下沿发 PRESS；短按在松键时判定；长按达到阈值触发一次；
 * 松键沿若同时判出 SHORT，RELEASE 排队在其后按序发出。 */
#include "pet_button.h"

static pet_key_event_t evq_pop(pet_button_t *b)
{
    if (b->q_len == 0) {
        return PET_KEY_EV_NONE;
    }
    const pet_key_event_t ev = b->q[b->q_head];
    b->q_head = (uint8_t)((b->q_head + 1u) % PET_KEY_EVQ_LEN);
    b->q_len--;
    return ev;
}

static void evq_push(pet_button_t *b, pet_key_event_t ev)
{
    if (b->q_len >= PET_KEY_EVQ_LEN) {
        /* 仅病态持续抖动可积满：丢最旧，保留当前物理态对应的事件 */
        (void)evq_pop(b);
    }
    b->q[(b->q_head + b->q_len) % PET_KEY_EVQ_LEN] = ev;
    b->q_len++;
}

void pet_button_init(pet_button_t *b)
{
    b->pressed = false;
    b->long_fired = false;
    b->press_ms = 0;
    b->q_head = 0;
    b->q_len = 0;
}

pet_key_event_t pet_button_update(pet_button_t *b, bool raw_pressed, uint32_t now_ms)
{
    pet_key_event_t out = evq_pop(b);
    pet_key_event_t ev = PET_KEY_EV_NONE;
    pet_key_event_t ev2 = PET_KEY_EV_NONE; /* 松键沿的第二事件（RELEASE） */

    if (raw_pressed && !b->pressed) {
        /* 松→按：记起点，重新武装长按 */
        b->pressed = true;
        b->long_fired = false;
        b->press_ms = now_ms;
        ev = PET_KEY_EV_PRESS;
    } else if (!raw_pressed && b->pressed) {
        /* 按→松：此时才能确定不是长按 → 判短按；RELEASE 紧随 SHORT */
        b->pressed = false;
        if (b->long_fired) {
            ev = PET_KEY_EV_RELEASE;
        } else {
            ev = PET_KEY_EV_SHORT;
            ev2 = PET_KEY_EV_RELEASE;
        }
    } else if (b->pressed && !b->long_fired) {
        /* 持续按住：达到阈值触发一次；now < press_ms 视为未到（防回拨） */
        if (now_ms >= b->press_ms && now_ms - b->press_ms >= KEY_LONGPRESS_MS) {
            b->long_fired = true;
            ev = PET_KEY_EV_LONG;
        }
    }

    if (out == PET_KEY_EV_NONE) {
        out = ev;
        ev = ev2;
        ev2 = PET_KEY_EV_NONE;
    }
    if (ev != PET_KEY_EV_NONE) {
        evq_push(b, ev);
    }
    if (ev2 != PET_KEY_EV_NONE) {
        evq_push(b, ev2);
    }
    return out;
}
