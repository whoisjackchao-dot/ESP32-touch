#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 6 态，对应上游 display::State —— 见 contracts/M1-渲染分层契约冻结.md */
typedef enum {
    PET_STATE_IDLE = 0,
    PET_STATE_WORKING,
    PET_STATE_INPUT_REQUIRED,
    PET_STATE_DONE,
    PET_STATE_FAILED,
    PET_STATE_OFFLINE,
    PET_STATE_COUNT
} pet_state_t;

/* 立绘 6 关键帧（2026-10-17 由 4 帧扩展，0–3 编号不变；见契约 M1 §10） */
typedef enum {
    PET_FRAME_NORMAL = 0,
    PET_FRAME_EYES_CLOSED,
    PET_FRAME_HAPPY,
    PET_FRAME_SAD,
    PET_FRAME_WORKING,
    PET_FRAME_INPUT,
    PET_FRAME_COUNT
} pet_frame_t;

/* 一次性播报 */
typedef enum {
    PET_SOUND_NONE = 0,
    PET_SOUND_INPUT,
    PET_SOUND_DONE,
    PET_SOUND_FAILED
} pet_sound_t;

#define PET_TITLE_MAX   64
#define PET_PROJECT_MAX 64

typedef struct {
    char        title[PET_TITLE_MAX];
    char        project[PET_PROJECT_MAX];
    pet_state_t state;
    int32_t     elapsed_s;
} pet_card_t;

typedef struct {
    pet_state_t state;
    pet_frame_t frame;
    bool        mirror;
    bool        gray;
    bool        hair_right; /* 头顶毛发相位：false=左 / true=右（~1s 与眨眼同频） */
    bool        has_card;
    pet_card_t  card;      /* 顶部卡（M8 起与 cards[0] 同源，兼容旧代码） */
    int         card_count; /* M8 §8.3：栈深 0..3 */
    pet_card_t  cards[3];   /* M8 §8.3：多卡栈，cards[0] = 最新（顶部） */
} pet_view_t;
