#pragma once
#include "pet_types.h"

/* M2 状态机（派生自上游 ESP32-buddy MIT 的 pet_state，按本项目 C03 修订卡栈规则）。
 * 纯逻辑：不碰硬件、不画图；host 可测。
 * 契约：docs/08-m2-development.md §2.2；docs/contracts/C03-layout-240x240.md §3。 */

/* 事件：沿用上游事件模型（pet_state_apply 消费，不碰硬件） */
typedef enum {
    PET_EV_AGENT_IDLE = 0,
    PET_EV_TASK_START,
    PET_EV_AGENT_INPUT_REQUIRED,
    PET_EV_TASK_DONE,
    PET_EV_TASK_ERROR,
    PET_EV_AGENT_BLOCKED,
    PET_EV_LINK_DOWN,
    PET_EV_LINK_UP,
    PET_EV_CARD,
    PET_EV_ANNOUNCEMENT
} pet_event_kind_t;

typedef struct {
    pet_event_kind_t kind;
    uint32_t         now_ms;
    const char      *title;     /* 可空 */
    const char      *agent;     /* 可空：本项目增量，C03 §7 R2 */
    const char      *project;   /* 可空 */
    pet_state_t      card_state;
    int32_t          elapsed_s;
} pet_event_t;

/* 不透明句柄：`pet_state_t` 已被 pet_types.h 用作 6 态枚举，故只用结构体标签。 */
struct pet_state;

struct pet_state *pet_state_create(void);
void pet_state_destroy(struct pet_state *s);

/* 收事件；不碰硬件，不画图。 */
void pet_state_apply(struct pet_state *s, const pet_event_t *ev);

/* 每帧推进（done 边沿/驻留/失联/动画相位）。 */
void pet_state_tick(struct pet_state *s, uint32_t now_ms);

/* 取当前视图快照（frame/hair_right/gray/cards 均在此合成）。 */
void pet_state_view(const struct pet_state *s, pet_view_t *out);

/* 取一次性播报，取走即清。 */
pet_sound_t pet_state_take_sound(struct pet_state *s);

/* 入栈/更新：同名（title 且 project 相同）更新并置顶；否则入栈顶；
 * >3 丢最旧（底部）；card==NULL 清栈。空 title 的卡仍入栈（C03 §3.4 要求占位渲染）。 */
void pet_state_set_card(struct pet_state *s, const pet_card_t *card);

/* 整体替换栈（top-first，≤3 截断；count<=0 不动栈）。 */
void pet_state_set_cards(struct pet_state *s, const pet_card_t *cards, int count);
