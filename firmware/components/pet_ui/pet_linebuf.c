/* M4 增量 2b：行缓冲纯逻辑（契约 §9.2）。
 * 只做"字节 → 一行"的切分与超长丢弃；解析与状态变更在 pet_rx.c。 */
#include "pet_linebuf.h"

void pet_linebuf_init(pet_linebuf_t *lb)
{
    lb->len = 0;
    lb->overflow = false;
    lb->deliver_pending = false;
    lb->buf[0] = '\0';
}

pet_line_feed_t pet_linebuf_feed(pet_linebuf_t *lb, char c)
{
    /* 上一行已交付 → 先复位，开始新一行 */
    if (lb->deliver_pending) {
        lb->len = 0;
        lb->overflow = false;
        lb->deliver_pending = false;
    }

    if (c == '\n') {
        if (lb->overflow) {
            /* 超长：丢弃整行 */
            lb->len = 0;
            lb->overflow = false;
            return PET_LINE_DROPPED;
        }
        /* 兼容 CRLF：剥掉尾部 '\r' */
        if (lb->len > 0 && lb->buf[lb->len - 1] == '\r') {
            lb->len--;
        }
        lb->buf[lb->len] = '\0';
        lb->deliver_pending = true;
        return PET_LINE_READY;
    }

    if (lb->len < PET_LINE_MAX) {
        lb->buf[lb->len++] = c;
    } else {
        lb->overflow = true; /* 已满，丢弃到下一个 '\n' */
    }
    return PET_LINE_NONE;
}
