/* M3 host 单测：按键 / 键表 / 菜单 / 番茄钟纯逻辑（BTN-01..03、KEY-03、
 * KEYMAP-01..08、MENU-01..05、POMO-01..08）。
 * 契约：docs/contracts/C07-keymap.md §2/§3/§4；docs/10-m3-development.md §2/§7；
 *       pet_button.h / pet_keymap.h / pet_menu.h / pet_pomo.h 头文件注释。
 * 时间全部由构造的 now_ms 驱动，不 sleep、不读真实时钟。
 * 只写 tests/；只依赖公开头文件 API（不链接 pet_pomo_store.c，NVS 属设备层）。 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pet_button.h"
#include "pet_keymap.h"
#include "pet_menu.h"
#include "pet_pomo.h"

/* 头文件默认值（不覆盖编译期可改宏）。 */
#ifndef KEY_LONGPRESS_MS
#define KEY_LONGPRESS_MS 600
#endif

static int g_asserts;
static int g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_asserts++;                                                         \
        if (!(cond)) {                                                       \
            printf("  ASSERT FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_EQ(actual, expected)                                           \
    do {                                                                     \
        long long a_ = (long long)(actual);                                  \
        long long e_ = (long long)(expected);                                \
        g_asserts++;                                                         \
        if (a_ != e_) {                                                      \
            printf("  ASSERT FAIL %s:%d: %s == %s (got %lld, want %lld)\n",  \
                   __FILE__, __LINE__, #actual, #expected, a_, e_);          \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

/* ---- 按键夹具 ----
 * 设备侧 20 ms 采样一次；测试用同节拍喂 now_ms，边界用例按毫秒精确喂。 */

/* 从 from_ms 到 to_ms（含）每 step 毫秒喂一次「按住」，期间必须无事件；
 * 返回是否全程 NONE。 */
static bool hold_quiet(pet_button_t *b, uint32_t from_ms, uint32_t to_ms,
                       uint32_t step)
{
    bool quiet = true;
    for (uint32_t t = from_ms; t <= to_ms; t += step) {
        if (pet_button_update(b, true, t) != PET_KEY_EV_NONE) {
            quiet = false;
        }
    }
    return quiet;
}

static void case_btn01_press_short_release(void)
{
    pet_button_t b;
    pet_button_init(&b);
    CHECK_EQ(b.q_len, 0);

    /* 按下沿 → PRESS（队列空时立即返回） */
    CHECK_EQ(pet_button_update(&b, true, 1000), PET_KEY_EV_PRESS);
    /* 按住 590 ms（20 ms 采样）期间无事件 */
    CHECK(hold_quiet(&b, 1020, 1580, KEY_DEBOUNCE_MS));
    /* 松键：先 SHORT，再取出排队的 RELEASE（顺序固定） */
    CHECK_EQ(pet_button_update(&b, false, 1590), PET_KEY_EV_SHORT);
    CHECK_EQ(pet_button_update(&b, false, 1610), PET_KEY_EV_RELEASE);
    CHECK_EQ(pet_button_update(&b, false, 1630), PET_KEY_EV_NONE);

    /* 0 ms 极短按：PRESS → SHORT → RELEASE */
    pet_button_init(&b);
    CHECK_EQ(pet_button_update(&b, true, 2000), PET_KEY_EV_PRESS);
    CHECK_EQ(pet_button_update(&b, false, 2000), PET_KEY_EV_SHORT);
    CHECK_EQ(pet_button_update(&b, false, 2020), PET_KEY_EV_RELEASE);
}

static void case_btn02_long_once_then_release(void)
{
    pet_button_t b;
    pet_button_init(&b);
    CHECK_EQ(pet_button_update(&b, true, 0), PET_KEY_EV_PRESS);
    /* 0..580 ms 未到阈值：无 LONG */
    CHECK(hold_quiet(&b, 20, 580, KEY_DEBOUNCE_MS));
    /* 恰达 600 ms 阈值：触发一次 LONG */
    CHECK_EQ(pet_button_update(&b, true, KEY_LONGPRESS_MS), PET_KEY_EV_LONG);
    /* 继续按住 2.4 s：不得重复 LONG / 不得产生其它事件 */
    bool repeated = false;
    for (uint32_t t = 620; t <= 3000; t += KEY_DEBOUNCE_MS) {
        if (pet_button_update(&b, true, t) != PET_KEY_EV_NONE) {
            repeated = true;
        }
    }
    CHECK(!repeated);
    /* 长按后松键：只 RELEASE，不再判 SHORT */
    CHECK_EQ(pet_button_update(&b, false, 3000), PET_KEY_EV_RELEASE);
    CHECK_EQ(pet_button_update(&b, false, 3020), PET_KEY_EV_NONE);
}

/* 连续点按 + 松键沿 SHORT+RELEASE 使队列积压到队深 3（PET_KEY_EVQ_LEN）；
 * 之后逐拍取出的顺序必须与产生顺序一致（FIFO，不丢不乱序）。 */
static void case_btn03_queue_fifo(void)
{
    pet_button_t b;
    pet_button_init(&b);

    pet_key_event_t got[10];
    got[0] = pet_button_update(&b, true, 0);   /* PRESS 立即返回 */
    got[1] = pet_button_update(&b, false, 20); /* SHORT 立即返回；RELEASE 入队 */
    got[2] = pet_button_update(&b, true, 40);  /* 取 RELEASE；PRESS 入队 */
    got[3] = pet_button_update(&b, false, 60); /* 取 PRESS；SHORT+RELEASE 入队 */
    got[4] = pet_button_update(&b, true, 80);  /* 取 SHORT；PRESS 入队 */
    got[5] = pet_button_update(&b, false, 100); /* 取 RELEASE；SHORT+RELEASE 入队 */
    CHECK_EQ(b.q_len, PET_KEY_EVQ_LEN); /* 此刻积压：PRESS, SHORT, RELEASE */
    got[6] = pet_button_update(&b, false, 120); /* 取 PRESS */
    got[7] = pet_button_update(&b, false, 140); /* 取 SHORT */
    got[8] = pet_button_update(&b, false, 160); /* 取 RELEASE */
    got[9] = pet_button_update(&b, false, 180); /* 空 */

    const pet_key_event_t want[10] = {
        PET_KEY_EV_PRESS,   PET_KEY_EV_SHORT,   PET_KEY_EV_RELEASE,
        PET_KEY_EV_PRESS,   PET_KEY_EV_SHORT,   PET_KEY_EV_RELEASE,
        PET_KEY_EV_PRESS,   PET_KEY_EV_SHORT,   PET_KEY_EV_RELEASE,
        PET_KEY_EV_NONE,
    };
    for (int i = 0; i < 10; i++) {
        CHECK_EQ(got[i], want[i]);
    }
}

/* KEY-03 / BTN 边界：595 ms 判短、605 ms 判长；C07 §2 的 590/610 一并锁定。 */
static void case_btn04_boundary(void)
{
    /* 595 ms：按下 0，末采样 580，595 松键 → SHORT + RELEASE */
    pet_button_t b;
    pet_button_init(&b);
    CHECK_EQ(pet_button_update(&b, true, 0), PET_KEY_EV_PRESS);
    CHECK(hold_quiet(&b, 20, 580, KEY_DEBOUNCE_MS));
    CHECK_EQ(pet_button_update(&b, false, 595), PET_KEY_EV_SHORT);
    CHECK_EQ(pet_button_update(&b, false, 615), PET_KEY_EV_RELEASE);

    /* 605 ms：600 ms 采样点触发 LONG 一次；605 松键只 RELEASE（不再 SHORT） */
    pet_button_init(&b);
    CHECK_EQ(pet_button_update(&b, true, 1000), PET_KEY_EV_PRESS);
    CHECK(hold_quiet(&b, 1020, 1580, KEY_DEBOUNCE_MS));
    CHECK_EQ(pet_button_update(&b, true, 1600), PET_KEY_EV_LONG);
    CHECK_EQ(pet_button_update(&b, false, 1605), PET_KEY_EV_RELEASE);
    CHECK_EQ(pet_button_update(&b, false, 1625), PET_KEY_EV_NONE);

    /* 590 ms（C07 KEY-03）判短 */
    pet_button_init(&b);
    CHECK_EQ(pet_button_update(&b, true, 2000), PET_KEY_EV_PRESS);
    CHECK(hold_quiet(&b, 2020, 2580, KEY_DEBOUNCE_MS));
    CHECK_EQ(pet_button_update(&b, false, 2590), PET_KEY_EV_SHORT);
    CHECK_EQ(pet_button_update(&b, false, 2610), PET_KEY_EV_RELEASE);

    /* 610 ms（C07 KEY-03）判长 */
    pet_button_init(&b);
    CHECK_EQ(pet_button_update(&b, true, 3000), PET_KEY_EV_PRESS);
    CHECK(hold_quiet(&b, 3020, 3580, KEY_DEBOUNCE_MS));
    CHECK_EQ(pet_button_update(&b, true, 3600), PET_KEY_EV_LONG);
    CHECK_EQ(pet_button_update(&b, false, 3610), PET_KEY_EV_RELEASE);
    CHECK_EQ(pet_button_update(&b, false, 3630), PET_KEY_EV_NONE);
}

/* ---- KEYMAP-01..08：C07 §3 冻结映射表 ---- */
static void case_keymap_all8(void)
{
    struct {
        pet_mode_t      mode;
        pet_key_t       key;
        pet_key_event_t ev;
        pet_action_t    want;
    } const table[] = {
        {PET_MODE_DUTY, PET_KEY1, PET_KEY_EV_SHORT, PET_ACT_SEND},
        {PET_MODE_DUTY, PET_KEY1, PET_KEY_EV_LONG, PET_ACT_MUTE_TOGGLE},
        {PET_MODE_DUTY, PET_KEY2, PET_KEY_EV_SHORT, PET_ACT_MODE_POMO},
        {PET_MODE_DUTY, PET_KEY2, PET_KEY_EV_LONG, PET_ACT_MENU},
        {PET_MODE_POMO, PET_KEY1, PET_KEY_EV_SHORT, PET_ACT_POMO_SHORT},
        {PET_MODE_POMO, PET_KEY1, PET_KEY_EV_LONG, PET_ACT_POMO_LONG},
        {PET_MODE_POMO, PET_KEY2, PET_KEY_EV_SHORT, PET_ACT_MODE_DUTY},
        {PET_MODE_POMO, PET_KEY2, PET_KEY_EV_LONG, PET_ACT_MENU},
    };
    const int n = (int)(sizeof table / sizeof table[0]);
    for (int i = 0; i < n; i++) {
        CHECK_EQ(pet_keymap_action(table[i].mode, table[i].key, table[i].ev),
                 table[i].want);
    }

    /* PRESS/RELEASE/NONE 不进键表（M4 PTT 直通）：两模式×两键全部 NONE */
    const pet_key_event_t passthru[] = {PET_KEY_EV_PRESS, PET_KEY_EV_RELEASE,
                                        PET_KEY_EV_NONE};
    for (int m = 0; m <= 1; m++) {
        for (int k = 0; k < PET_KEY_COUNT; k++) {
            for (int e = 0; e < 3; e++) {
                CHECK_EQ(pet_keymap_action((pet_mode_t)m, (pet_key_t)k,
                                           passthru[e]),
                         PET_ACT_NONE);
            }
        }
    }
}

/* ---- MENU-01..05：C07 §4 五项菜单 ---- */
static void case_menu01_cycle(void)
{
    pet_menu_t m;
    pet_menu_init(&m);
    CHECK_EQ(m.selected, 0); /* 打开菜单默认选中 MUTE */

    /* KEY1 短按：+1 mod 5，返回 NONE */
    for (int i = 1; i <= 6; i++) {
        CHECK_EQ(pet_menu_key(&m, PET_KEY1, PET_KEY_EV_SHORT),
                 PET_MENU_CMD_NONE);
        CHECK_EQ(m.selected, i % PET_MENU_ITEMS);
    }
}

static void case_menu02_exec_selected(void)
{
    const pet_menu_cmd_t want[PET_MENU_ITEMS] = {
        PET_MENU_CMD_TOGGLE_MUTE,  /* 0 MUTE：翻转静音，留在菜单 */
        PET_MENU_CMD_CLOSE,        /* 1 STATUS：只读，关闭 */
        PET_MENU_CMD_PROVISION,    /* 2 WIFI (M5) */
        PET_MENU_CMD_BT_PAIR,      /* 3 BT (M5) */
        PET_MENU_CMD_BACK_WINDOW,  /* 4 BACK */
    };
    pet_menu_t m;
    pet_menu_init(&m);
    for (int sel = 0; sel < PET_MENU_ITEMS; sel++) {
        m.selected = sel;
        CHECK_EQ(pet_menu_key(&m, PET_KEY2, PET_KEY_EV_SHORT), want[sel]);
        CHECK_EQ(m.selected, sel); /* KEY2 执行不移动选中 */
    }
}

static void case_menu03_close_and_ignore(void)
{
    pet_menu_t m;
    pet_menu_init(&m);
    m.selected = 2;

    /* KEY2 长按：关闭菜单 */
    CHECK_EQ(pet_menu_key(&m, PET_KEY2, PET_KEY_EV_LONG), PET_MENU_CMD_CLOSE);
    /* 菜单内 KEY1 长按无动作（主循环只记日志，不触发 MUTE） */
    CHECK_EQ(pet_menu_key(&m, PET_KEY1, PET_KEY_EV_LONG), PET_MENU_CMD_NONE);
    /* PRESS/RELEASE 不消费（不进菜单） */
    CHECK_EQ(pet_menu_key(&m, PET_KEY1, PET_KEY_EV_PRESS), PET_MENU_CMD_NONE);
    CHECK_EQ(pet_menu_key(&m, PET_KEY2, PET_KEY_EV_PRESS), PET_MENU_CMD_NONE);
    CHECK_EQ(pet_menu_key(&m, PET_KEY1, PET_KEY_EV_RELEASE),
             PET_MENU_CMD_NONE);
    CHECK_EQ(pet_menu_key(&m, PET_KEY2, PET_KEY_EV_RELEASE),
             PET_MENU_CMD_NONE);
    /* 菜单内主界面动作被屏蔽：键事件不改变选中项 */
    CHECK_EQ(m.selected, 2);
}

/* ---- POMO：状态机 / 统计 / 编解码 / 跨日 ---- */

static pet_pomo_t pomo_at(uint32_t now_ms)
{
    pet_pomo_t p;
    pet_pomo_init(&p, now_ms);
    return p;
}

/* POMO-01：IDLE→FOCUS→FOCUS_PAUSED→FOCUS→FOCUS_DONE→BREAK→BREAK_PAUSED
 * →BREAK→BREAK_DONE→IDLE 全转移（默认 25/5 时长）。 */
static void case_pomo01_full_transition(void)
{
    pet_pomo_t p = pomo_at(0);
    CHECK_EQ(p.phase, PET_POMO_IDLE);
    CHECK_EQ(p.focus_ms, POMO_FOCUS_MS);
    CHECK_EQ(p.break_ms, POMO_BREAK_MS);
    CHECK_EQ(p.total_ms, POMO_FOCUS_MS);
    CHECK_EQ(p.done_count, 0);
    CHECK_EQ(p.focus_seconds, 0);
    CHECK_EQ(p.alarm, 0);
    CHECK_EQ(p.has_day, 0);

    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    CHECK_EQ(p.phase, PET_POMO_FOCUS);
    CHECK_EQ(p.total_ms, POMO_FOCUS_MS);
    CHECK_EQ(p.elapsed_ms, 0);
    CHECK_EQ(p.since_ms, 0);

    pet_pomo_tick(&p, 1000);
    CHECK_EQ(p.elapsed_ms, 1000);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 1000);
    CHECK_EQ(p.phase, PET_POMO_FOCUS_PAUSED);

    /* 暂停：tick 长时间不推进 */
    pet_pomo_tick(&p, 600000);
    CHECK_EQ(p.phase, PET_POMO_FOCUS_PAUSED);
    CHECK_EQ(p.elapsed_ms, 1000);

    /* 继续：保留累计，只换计时起点 */
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 600000);
    CHECK_EQ(p.phase, PET_POMO_FOCUS);
    CHECK_EQ(p.since_ms, 600000);
    CHECK_EQ(p.elapsed_ms, 1000);
    pet_pomo_tick(&p, 601000);
    CHECK_EQ(p.elapsed_ms, 2000);

    /* 到点：elapsed >= total → FOCUS_DONE，计完成、累计专注秒、置 alarm
     * （601000 那一拍起 since_ms=601000，故 2000 + (2099000-601000) = 1500000） */
    pet_pomo_tick(&p, 2099000);
    CHECK_EQ(p.phase, PET_POMO_FOCUS_DONE);
    CHECK_EQ(p.elapsed_ms, 1500000);
    CHECK_EQ(p.done_count, 1);
    CHECK_EQ(p.focus_seconds, POMO_FOCUS_MS / 1000u);
    CHECK_EQ(p.alarm, 1);

    /* DONE 不再推进/再加统计 */
    pet_pomo_tick(&p, 2200000);
    CHECK_EQ(p.done_count, 1);
    CHECK_EQ(p.focus_seconds, POMO_FOCUS_MS / 1000u);

    /* 视图：到点后剩余 0（下限钳制） */
    pet_pomo_view_t view;
    memset(&view, 0, sizeof view);
    pet_pomo_view_get(&p, &view);
    CHECK_EQ(view.phase, PET_POMO_FOCUS_DONE);
    CHECK_EQ(view.remaining_ms, 0);
    CHECK_EQ(view.total_ms, POMO_FOCUS_MS);
    CHECK_EQ(view.done_count, 1);
    CHECK_EQ(view.focus_seconds, POMO_FOCUS_MS / 1000u);

    /* FOCUS_DONE → 下一段 BREAK（不自动开始，等按键） */
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 2200000);
    CHECK_EQ(p.phase, PET_POMO_BREAK);
    CHECK_EQ(p.total_ms, POMO_BREAK_MS);
    CHECK_EQ(p.elapsed_ms, 0);

    pet_pomo_tick(&p, 2300000);
    CHECK_EQ(p.elapsed_ms, 100000);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 2300000);
    CHECK_EQ(p.phase, PET_POMO_BREAK_PAUSED);
    pet_pomo_tick(&p, 2800000); /* 暂停不推进 */
    CHECK_EQ(p.elapsed_ms, 100000);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 2800000);
    CHECK_EQ(p.phase, PET_POMO_BREAK);
    CHECK_EQ(p.since_ms, 2800000);

    pet_pomo_tick(&p, 3100000); /* elapsed 100000+300000 = 400000 >= 300000 */
    CHECK_EQ(p.phase, PET_POMO_BREAK_DONE);
    CHECK_EQ(p.alarm, 1);       /* 休息完成只响铃 */
    CHECK_EQ(p.done_count, 1);  /* 不计专注统计 */
    CHECK_EQ(p.focus_seconds, POMO_FOCUS_MS / 1000u);
    CHECK_EQ(pet_pomo_take_alarm(&p), true);
    CHECK_EQ(pet_pomo_take_alarm(&p), false); /* 取走即清 */

    /* BREAK_DONE → IDLE */
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 3100000);
    CHECK_EQ(p.phase, PET_POMO_IDLE);
    CHECK_EQ(p.elapsed_ms, 0);
}

/* POMO-02：暂停期间 tick 不推进（FOCUS / BREAK 两相位）。 */
static void case_pomo02_pause_no_advance(void)
{
    pet_pomo_t p = pomo_at(0);
    pet_pomo_set_durations(&p, 10000, 5000);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    pet_pomo_tick(&p, 3000);
    CHECK_EQ(p.elapsed_ms, 3000);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 3000);
    CHECK_EQ(p.phase, PET_POMO_FOCUS_PAUSED);
    pet_pomo_tick(&p, 1000000);
    CHECK_EQ(p.elapsed_ms, 3000);
    pet_pomo_view_t view;
    pet_pomo_view_get(&p, &view);
    CHECK_EQ(view.remaining_ms, 7000);
    /* 继续后从新起点累计 */
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 1000000);
    pet_pomo_tick(&p, 1004000);
    CHECK_EQ(p.elapsed_ms, 7000);

    /* BREAK 相位暂停同样不推进 */
    pet_pomo_tick(&p, 1010000); /* 10000 到点 → FOCUS_DONE */
    CHECK_EQ(p.phase, PET_POMO_FOCUS_DONE);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 1010000);
    CHECK_EQ(p.phase, PET_POMO_BREAK);
    pet_pomo_tick(&p, 1011000);
    CHECK_EQ(p.elapsed_ms, 1000);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 1011000);
    CHECK_EQ(p.phase, PET_POMO_BREAK_PAUSED);
    pet_pomo_tick(&p, 2000000);
    CHECK_EQ(p.elapsed_ms, 1000);
}

/* POMO-03：到点统计（done_count++ / focus_seconds += focus_ms/1000）、
 * alarm 取走即清、tick 不重复统计。 */
static void case_pomo03_stats_and_alarm(void)
{
    pet_pomo_t p = pomo_at(0);
    pet_pomo_set_durations(&p, 2000, 1000);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    CHECK_EQ(p.total_ms, 2000);
    pet_pomo_tick(&p, 1999);
    CHECK_EQ(p.phase, PET_POMO_FOCUS);
    CHECK_EQ(p.done_count, 0);
    CHECK_EQ(p.alarm, 0);
    pet_pomo_tick(&p, 2000); /* elapsed >= total */
    CHECK_EQ(p.phase, PET_POMO_FOCUS_DONE);
    CHECK_EQ(p.done_count, 1);
    CHECK_EQ(p.focus_seconds, 2); /* 2000 ms / 1000 */

    /* alarm 取走即清（第二次 false，视图同步清零） */
    pet_pomo_view_t view;
    pet_pomo_view_get(&p, &view);
    CHECK_EQ(view.alarm, 1);
    CHECK_EQ(pet_pomo_take_alarm(&p), true);
    CHECK_EQ(p.alarm, 0);
    CHECK_EQ(pet_pomo_take_alarm(&p), false);
    pet_pomo_view_get(&p, &view);
    CHECK_EQ(view.alarm, 0);

    /* DONE 相位再 tick 不重复统计 */
    pet_pomo_tick(&p, 500000);
    CHECK_EQ(p.done_count, 1);
    CHECK_EQ(p.focus_seconds, 2);
    CHECK_EQ(p.alarm, 0);

    /* 休息到点：alarm 置位、不计完成 */
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 500000);
    CHECK_EQ(p.phase, PET_POMO_BREAK);
    pet_pomo_tick(&p, 501000);
    CHECK_EQ(p.phase, PET_POMO_BREAK_DONE);
    CHECK_EQ(p.done_count, 1);
    CHECK_EQ(p.alarm, 1);
    CHECK_EQ(pet_pomo_take_alarm(&p), true);
}

/* POMO-04：LONG 放弃不计完成（FOCUS / BREAK 阶段、IDLE 无变化）。 */
static void case_pomo04_long_abort(void)
{
    /* FOCUS 中放弃 */
    pet_pomo_t p = pomo_at(0);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    pet_pomo_tick(&p, 500000);
    CHECK_EQ(p.phase, PET_POMO_FOCUS);
    pet_pomo_command(&p, PET_POMO_CMD_LONG, 500000);
    CHECK_EQ(p.phase, PET_POMO_IDLE);
    CHECK_EQ(p.done_count, 0);
    CHECK_EQ(p.focus_seconds, 0);
    CHECK_EQ(p.alarm, 0);
    CHECK_EQ(p.elapsed_ms, 0);
    pet_pomo_tick(&p, 600000); /* IDLE 不推进 */
    CHECK_EQ(p.elapsed_ms, 0);

    /* IDLE 下 LONG 无变化 */
    pet_pomo_command(&p, PET_POMO_CMD_LONG, 600000);
    CHECK_EQ(p.phase, PET_POMO_IDLE);
    CHECK_EQ(p.done_count, 0);

    /* BREAK 中放弃：不改变统计 */
    p = pomo_at(0);
    pet_pomo_set_durations(&p, 1000, 1000);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    pet_pomo_tick(&p, 1000);
    CHECK_EQ(p.phase, PET_POMO_FOCUS_DONE);
    CHECK_EQ(p.done_count, 1);
    CHECK_EQ(p.focus_seconds, 1);
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 1000);
    CHECK_EQ(p.phase, PET_POMO_BREAK);
    pet_pomo_command(&p, PET_POMO_CMD_LONG, 1000);
    CHECK_EQ(p.phase, PET_POMO_IDLE);
    CHECK_EQ(p.done_count, 1);
    CHECK_EQ(p.focus_seconds, 1);
}

/* POMO-05：set_durations 后下一段用新时长；不重置当前段累计。 */
static void case_pomo05_set_durations(void)
{
    pet_pomo_t p = pomo_at(0);
    pet_pomo_set_durations(&p, 6000, 3000);
    CHECK_EQ(p.focus_ms, 6000);
    CHECK_EQ(p.break_ms, 3000);

    /* 下一段 FOCUS 用新 focus 时长，到点判定同步 */
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 0);
    CHECK_EQ(p.phase, PET_POMO_FOCUS);
    CHECK_EQ(p.total_ms, 6000);
    pet_pomo_tick(&p, 6000);
    CHECK_EQ(p.phase, PET_POMO_FOCUS_DONE);
    CHECK_EQ(p.done_count, 1);
    CHECK_EQ(p.focus_seconds, 6);

    /* 下一段 BREAK 用新 break 时长 */
    pet_pomo_command(&p, PET_POMO_CMD_SHORT, 6000);
    CHECK_EQ(p.phase, PET_POMO_BREAK);
    CHECK_EQ(p.total_ms, 3000);
    pet_pomo_tick(&p, 9000);
    CHECK_EQ(p.phase, PET_POMO_BREAK_DONE);

    /* 运行中改时长：不重置当前段累计 */
    pet_pomo_t q = pomo_at(0);
    pet_pomo_set_durations(&q, 6000, 3000);
    pet_pomo_command(&q, PET_POMO_CMD_SHORT, 0);
    pet_pomo_tick(&q, 2000);
    CHECK_EQ(q.elapsed_ms, 2000);
    pet_pomo_set_durations(&q, 7000, 4000);
    CHECK_EQ(q.elapsed_ms, 2000);
    /* set_durations 不动 tick 维护的起点/累计（2000 那一拍已把 since_ms 推到 2000） */
    CHECK_EQ(q.since_ms, 2000);
    CHECK_EQ(q.focus_ms, 7000);
    CHECK_EQ(q.break_ms, 4000);
}

/* POMO-06：encode/decode_stats 往返、magic/长度非法拒绝且不改目标。 */
static void case_pomo06_encode_decode(void)
{
    pet_pomo_t p;
    memset(&p, 0, sizeof p);
    p.phase = PET_POMO_FOCUS;
    p.has_day = true;
    p.day = 20261031;
    p.done_count = 7;
    p.focus_seconds = 1234;

    uint8_t buf[PET_POMO_STATS_LEN];
    memset(buf, 0xEE, sizeof buf);
    CHECK_EQ(pet_pomo_encode_stats(buf, sizeof buf, &p), PET_POMO_STATS_LEN);
    CHECK_EQ(buf[0], 'P');
    CHECK_EQ(buf[1], 'M');
    CHECK_EQ(buf[2], 'C');
    CHECK_EQ(buf[3], '1');
    const uint32_t day_le = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                            ((uint32_t)buf[6] << 16) |
                            ((uint32_t)buf[7] << 24);
    const uint32_t done_le = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) |
                             ((uint32_t)buf[10] << 16) |
                             ((uint32_t)buf[11] << 24);
    const uint32_t focus_le = (uint32_t)buf[12] | ((uint32_t)buf[13] << 8) |
                              ((uint32_t)buf[14] << 16) |
                              ((uint32_t)buf[15] << 24);
    CHECK_EQ(day_le, 20261031u);
    CHECK_EQ(done_le, 7u);
    CHECK_EQ(focus_le, 1234u);

    /* cap < 16：返回 0 且不写 buf */
    uint8_t small[PET_POMO_STATS_LEN];
    memset(small, 0xAA, sizeof small);
    CHECK_EQ(pet_pomo_encode_stats(small, PET_POMO_STATS_LEN - 1, &p), 0);
    bool untouched = true;
    for (size_t i = 0; i < sizeof small; i++) {
        if (small[i] != 0xAA) {
            untouched = false;
        }
    }
    CHECK(untouched);
    CHECK_EQ(pet_pomo_encode_stats(NULL, sizeof buf, &p), 0);

    /* 解码：只置 has_day/day/done_count/focus_seconds，不动其它字段 */
    pet_pomo_t q;
    memset(&q, 0, sizeof q);
    q.phase = PET_POMO_BREAK;
    q.since_ms = 555;
    q.elapsed_ms = 44;
    q.total_ms = 999;
    q.focus_ms = 111;
    q.break_ms = 222;
    q.alarm = true;
    q.has_day = false;
    q.day = -1;
    CHECK(pet_pomo_decode_stats(buf, sizeof buf, &q));
    CHECK_EQ(q.has_day, 1);
    CHECK_EQ(q.day, 20261031);
    CHECK_EQ(q.done_count, 7);
    CHECK_EQ(q.focus_seconds, 1234);
    CHECK_EQ(q.phase, PET_POMO_BREAK);
    CHECK_EQ(q.since_ms, 555);
    CHECK_EQ(q.elapsed_ms, 44);
    CHECK_EQ(q.total_ms, 999);
    CHECK_EQ(q.focus_ms, 111);
    CHECK_EQ(q.break_ms, 222);
    CHECK_EQ(q.alarm, 1);

    /* 非法输入：false 且目标完全不变 */
    const pet_pomo_t before = q;
    uint8_t bad_magic[PET_POMO_STATS_LEN];
    memcpy(bad_magic, buf, sizeof bad_magic);
    bad_magic[0] = (uint8_t)'X';
    pet_pomo_t r = q;
    CHECK(!pet_pomo_decode_stats(bad_magic, sizeof bad_magic, &r));
    CHECK(memcmp(&r, &before, sizeof r) == 0);
    CHECK(!pet_pomo_decode_stats(buf, PET_POMO_STATS_LEN - 1, &r));
    CHECK(memcmp(&r, &before, sizeof r) == 0);
    uint8_t buf17[PET_POMO_STATS_LEN + 1];
    memcpy(buf17, buf, PET_POMO_STATS_LEN);
    buf17[PET_POMO_STATS_LEN] = 0;
    CHECK(!pet_pomo_decode_stats(buf17, sizeof buf17, &r));
    CHECK(memcmp(&r, &before, sizeof r) == 0);
    CHECK(!pet_pomo_decode_stats(NULL, sizeof buf, &r));
    CHECK(!pet_pomo_decode_stats(buf, sizeof buf, NULL));
    CHECK(memcmp(&r, &before, sizeof r) == 0);

    /* 负 day（i32 小端补码）往返 */
    pet_pomo_t neg;
    memset(&neg, 0, sizeof neg);
    neg.has_day = true;
    neg.day = -5;
    uint8_t negbuf[PET_POMO_STATS_LEN];
    CHECK_EQ(pet_pomo_encode_stats(negbuf, sizeof negbuf, &neg),
             PET_POMO_STATS_LEN);
    pet_pomo_t back;
    memset(&back, 0, sizeof back);
    CHECK(pet_pomo_decode_stats(negbuf, sizeof negbuf, &back));
    CHECK_EQ(back.day, -5);
    CHECK_EQ(back.has_day, 1);

    /* day 未知哨兵（M3 无时间源）：has_day=false 编码写 0x80000000，
     * 重启解码回 has_day=false/day=0，不把“未知”误读成真实 day=0；
     * done/focus 统计字段照常保存。 */
    pet_pomo_t unknown;
    memset(&unknown, 0, sizeof unknown);
    unknown.has_day = false;
    unknown.day = 0;
    unknown.done_count = 4;
    unknown.focus_seconds = 99;
    uint8_t ubuf[PET_POMO_STATS_LEN];
    memset(ubuf, 0xEE, sizeof ubuf);
    CHECK_EQ(pet_pomo_encode_stats(ubuf, sizeof ubuf, &unknown),
             PET_POMO_STATS_LEN);
    const uint32_t unknown_day_le = (uint32_t)ubuf[4] |
                                    ((uint32_t)ubuf[5] << 8) |
                                    ((uint32_t)ubuf[6] << 16) |
                                    ((uint32_t)ubuf[7] << 24);
    CHECK_EQ(unknown_day_le, 0x80000000u);
    pet_pomo_t unknown_back;
    memset(&unknown_back, 0, sizeof unknown_back);
    unknown_back.has_day = true;  /* 脏值：解码必须覆盖 */
    unknown_back.day = 12345;
    CHECK(pet_pomo_decode_stats(ubuf, sizeof ubuf, &unknown_back));
    CHECK_EQ(unknown_back.has_day, 0);
    CHECK_EQ(unknown_back.day, 0);
    CHECK_EQ(unknown_back.done_count, 4);
    CHECK_EQ(unknown_back.focus_seconds, 99);
}

/* POMO-07：set_day 首次/变化清零、相同不动。 */
static void case_pomo07_set_day(void)
{
    pet_pomo_t p = pomo_at(0);
    p.done_count = 3;
    p.focus_seconds = 300;
    p.has_day = false;

    /* 首次 set_day：记下并清零 */
    pet_pomo_set_day(&p, 20261031);
    CHECK_EQ(p.has_day, 1);
    CHECK_EQ(p.day, 20261031);
    CHECK_EQ(p.done_count, 0);
    CHECK_EQ(p.focus_seconds, 0);

    /* 同一天：统计数据原样保留 */
    p.done_count = 5;
    p.focus_seconds = 500;
    p.phase = PET_POMO_FOCUS;
    pet_pomo_set_day(&p, 20261031);
    CHECK_EQ(p.done_count, 5);
    CHECK_EQ(p.focus_seconds, 500);
    CHECK_EQ(p.day, 20261031);
    CHECK_EQ(p.phase, PET_POMO_FOCUS); /* 不清状态机 */

    /* 跨日：清零并更新 day */
    pet_pomo_set_day(&p, 20261101);
    CHECK_EQ(p.day, 20261101);
    CHECK_EQ(p.done_count, 0);
    CHECK_EQ(p.focus_seconds, 0);
    CHECK_EQ(p.has_day, 1);

    /* 负 day 同样按“变化”处理 */
    p.done_count = 2;
    p.focus_seconds = 120;
    pet_pomo_set_day(&p, -1);
    CHECK_EQ(p.day, -1);
    CHECK_EQ(p.done_count, 0);
    CHECK_EQ(p.focus_seconds, 0);
}

/* ---- 运行器 ---- */

typedef struct {
    const char *name;
    void (*fn)(void);
} test_case_t;

int main(void)
{
    static const test_case_t cases[] = {
        {"BTN-01-press-short-release", case_btn01_press_short_release},
        {"BTN-02-long-once", case_btn02_long_once_then_release},
        {"BTN-03-queue-fifo", case_btn03_queue_fifo},
        {"KEY-03-boundary-595-605", case_btn04_boundary},
        {"KEYMAP-01..08", case_keymap_all8},
        {"MENU-01-cycle", case_menu01_cycle},
        {"MENU-02-exec", case_menu02_exec_selected},
        {"MENU-03-close-ignore", case_menu03_close_and_ignore},
        {"POMO-01-full-transition", case_pomo01_full_transition},
        {"POMO-02-pause-no-advance", case_pomo02_pause_no_advance},
        {"POMO-03-stats-alarm", case_pomo03_stats_and_alarm},
        {"POMO-04-long-abort", case_pomo04_long_abort},
        {"POMO-05-set-durations", case_pomo05_set_durations},
        {"POMO-06-encode-decode", case_pomo06_encode_decode},
        {"POMO-07-set-day", case_pomo07_set_day},
    };

    int failed_cases = 0;
    const int n = (int)(sizeof cases / sizeof cases[0]);
    for (int i = 0; i < n; i++) {
        const int before = g_failures;
        g_asserts = 0;
        cases[i].fn();
        if (g_failures > before) {
            printf("FAIL %s (%d/%d asserts failed)\n", cases[i].name,
                   g_failures - before, g_asserts);
            failed_cases++;
        } else {
            printf("PASS %s (%d asserts)\n", cases[i].name, g_asserts);
        }
    }
    printf("SUMMARY m3_logic: cases=%d passed=%d failed=%d\n", n,
           n - failed_cases, failed_cases);
    return failed_cases == 0 ? 0 : 1;
}
