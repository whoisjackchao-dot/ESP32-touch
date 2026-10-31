#pragma once

#include <stdbool.h>
#include <stddef.h>

/* M4 增量 2b：把"字节流"攒成"一行"的纯逻辑（可 host 单测）。
 * 契约：contracts/M4-事件入口与OpenCode接入契约清单.md §9.2/§9.1。
 * 不碰硬件 —— USB-Serial-JTAG 的读取在 pet_rx.c。 */

#ifndef PET_LINE_MAX
#define PET_LINE_MAX 512
#endif

typedef enum {
    PET_LINE_NONE = 0, /* 仍在攒行 */
    PET_LINE_READY,    /* 凑齐一行（lb->buf 有效，NUL 结尾，不含 '\n'） */
    PET_LINE_DROPPED   /* 凑齐但超长，整行已丢弃 */
} pet_line_feed_t;

typedef struct {
    char   buf[PET_LINE_MAX + 1];
    size_t len;
    bool   overflow;        /* 本行已超长 → 到 '\n' 时丢弃 */
    bool   deliver_pending; /* 上一行已交付，下次 feed 前复位 */
} pet_linebuf_t;

void pet_linebuf_init(pet_linebuf_t *lb);

/* 喂一个字节。返回 READY 时，调用方**须在下次 feed 前**读取 lb->buf（内容在下一次 feed 时被复位/覆盖）。 */
pet_line_feed_t pet_linebuf_feed(pet_linebuf_t *lb, char c);
