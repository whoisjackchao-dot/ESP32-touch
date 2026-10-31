# M3 host 单测 RED→GREEN 记录（T 类：按键/键表/菜单/番茄钟/240 像素）

- 日期：2026-10-31
- 被测（Lead 实现，**已冻结**）：`firmware/components/pet_ui/pet_button.c`、`pet_keymap.c`、
  `pet_menu.c`、`pet_pomo.c`、`pet_ui_pomo_240.c`、`pet_ui_menu_240.c`；
  像素组另链接**真实** `pet_render.c` / `pet_text.c`（不链接 NVS 设备层 `pet_pomo_store.c`）。
- 运行器：`tests/run_m3_tests.ps1`（mingw gcc 16.1.0，无 CMake）。
  **与 M2 的 `tests/run_tests.ps1` 分开**：M2 用例（`test_pet_state` / `test_ui240`）仍由
  `run_tests.ps1` 负责，本脚本只跑 M3 的 `test_m3_logic` / `test_m3_ui`，互不覆盖。
- 用例：
  - `tests/test_m3_logic.c`：15 例（`BTN-01..03`、`KEY-03`、`KEYMAP-01..08`、
    `MENU-01..03`、`POMO-01..07`），261 条断言，链接 4 个逻辑实现。
  - `tests/test_m3_ui.c`：6 例（`UI-20`×3、`UI-21`×3），256 条断言，链接 2 个 240 页
    实现 + `pet_render.c` / `pet_text.c`。
- 统一口径：测试只读公开头文件；时间全部由构造的 `now_ms` 驱动（无 sleep、不读真实时钟）；
  阈值/时长用头文件默认值（`KEY_LONGPRESS_MS=600`、`POMO_FOCUS_MS=1500000`、
  `POMO_BREAK_MS=300000`）；像素测试用哨兵 `0xA5A5` 预填画布，未清屏/未绘制会直接暴露。
- 只写 `tests/`；未改 `firmware/`、`docs/`、`tools/`；未运行 `idf.py`、未刷机。

## 1. RED（实现为桩，断言失败；非编译/环境错误）

RED 使用 Lead 提供的 6 个只读桩副本：**`D:\DSH\esp-build\m3-stubs\`**
（只满足链接、行为未实现；不修改 `firmware/`，通过 `-UseStubs` 替换被测源文件路径）。
可复现性：

| 桩文件 | 大小 | SHA256 前 16 位 |
| --- | --- | --- |
| `pet_button.c` | 387 B | `630B114701EB0E70` |
| `pet_keymap.c` | 188 B | `C6ED817C406E01B7` |
| `pet_menu.c` | 235 B | `F159FAFF33C567BC` |
| `pet_pomo.c` | 1122 B | `8248599A8905AD68` |
| `pet_ui_pomo_240.c` | 140 B | `A77F5FBDA86DE089` |
| `pet_ui_menu_240.c` | 140 B | `CDC2FFBAF2A47FD6` |

命令（仓库根 `D:\DSH\ESP32-touch`）：

```powershell
pwsh -NoProfile -File tests/run_m3_tests.ps1 -UseStubs
```

原始输出摘要（完整原文见 `tests/red-m3-run.log`，两个测试程序均**编译成功并运行**，
失败全部是断言失败；RED 运行器退出码 1）：

```text
[MODE] RED: stubs from D:\DSH\esp-build\m3-stubs
[CC] test_m3_logic.c + button/keymap/menu/pomo
[CC] test_m3_ui.c + ui_pomo_240/ui_menu_240 + pet_render.c + pet_text.c
=== RUN test_m3_logic.exe ===
FAIL BTN-01-press-short-release (6/9 asserts failed)
FAIL BTN-02-long-once (3/6 asserts failed)
FAIL BTN-03-queue-fifo (10/11 asserts failed)
FAIL KEY-03-boundary-595-605 (12/18 asserts failed)
FAIL KEYMAP-01..08 (8/20 asserts failed)
FAIL MENU-01-cycle (5/13 asserts failed)
FAIL MENU-02-exec (5/10 asserts failed)
FAIL MENU-03-close-ignore (1/7 asserts failed)
FAIL POMO-01-full-transition (32/48 asserts failed)
FAIL POMO-02-pause-no-advance (10/10 asserts failed)
FAIL POMO-03-stats-alarm (14/20 asserts failed)
FAIL POMO-04-long-abort (7/16 asserts failed)
FAIL POMO-05-set-durations (15/15 asserts failed)
FAIL POMO-06-encode-decode (24/43 asserts failed)
FAIL POMO-07-set-day (12/15 asserts failed)
SUMMARY m3_logic: cases=15 passed=0 failed=15
=== EXIT 1 (test_m3_logic.exe) ===
=== RUN test_m3_ui.exe ===
FAIL UI-20-pomo-ring-geometry (14/15 asserts failed)
FAIL UI-20-pomo-progress-colors (18/21 asserts failed)
FAIL UI-20-pomo-title-center-stats (12/19 asserts failed)
FAIL UI-21-menu-title (7/7 asserts failed)
FAIL UI-21-menu-rows (170/190 asserts failed)
FAIL UI-21-menu-content-follows-view (4/4 asserts failed)
SUMMARY m3_ui: cases=6 passed=0 failed=6
=== EXIT 1 (test_m3_ui.exe) ===
RESULT: FAIL (2 test binary/binaries failed)
```

失败断言原文（代表性，按组；全量见 `tests/red-m3-run.log`）：

```text
[button]  ASSERT FAIL ...\test_m3_logic.c:70: pet_button_update(&b, true, 1000) == PET_KEY_EV_PRESS (got 0, want 1)
[button]  ASSERT FAIL ...\test_m3_logic.c:74: pet_button_update(&b, false, 1590) == PET_KEY_EV_SHORT (got 0, want 3)
[button]  ASSERT FAIL ...\test_m3_logic.c:93: pet_button_update(&b, true, KEY_LONGPRESS_MS) == PET_KEY_EV_LONG (got 0, want 4)
[button]  ASSERT FAIL ...\test_m3_logic.c:121: b.q_len == PET_KEY_EVQ_LEN (got 0, want 3)
[keymap]  ASSERT FAIL ...\test_m3_logic.c:193: pet_keymap_action(table[i].mode, table[i].key, table[i].ev) == table[i].want (got 0, want 6)
[menu]    ASSERT FAIL ...\test_m3_logic.c:222: m.selected == i % PET_MENU_ITEMS (got 0, want 1)
[menu]    ASSERT FAIL ...\test_m3_logic.c:239: pet_menu_key(&m, PET_KEY2, PET_KEY_EV_SHORT) == want[sel] (got 0, want 1)
[pomo]    ASSERT FAIL ...\test_m3_logic.c:289: p.phase == PET_POMO_FOCUS (got 0, want 1)
[pomo]    ASSERT FAIL ...\test_m3_logic.c:316: p.done_count == 1 (got 0, want 1)
[pomo]    ASSERT FAIL ...\test_m3_logic.c:529: pet_pomo_encode_stats(buf, sizeof buf, &p) == PET_POMO_STATS_LEN (got 0, want 16)
[pomo]    ASSERT FAIL ...\test_m3_logic.c:630: p.day == 20261031 (got 0, want 20261031)
[ui-20]   ASSERT FAIL ...\test_m3_ui.c:250: get_px(&s, 0, 0) == C_BG (got 42405, want 0)
[ui-20]   ASSERT FAIL ...\test_m3_ui.c:256: get_px(&s, RING_P12X, RING_P12Y) == C_TRACK (got 42405, want 16904)
[ui-21]   ASSERT FAIL ...\test_m3_ui.c:428: count_other(&s, MENU_ROW_X, y, ..., C_WHITE, C_INV_FG) == 0 (got 6496, want 0)
[ui-21]   ASSERT FAIL ...\test_m3_ui.c:500: count_diff(&s, &ref, MENU_ROW_X, MENU_ROW_Y0, ...) > 0
```

RED 判读（与桩行为一一对应，不是环境问题）：

| 组 | 桩行为 | 失败表现 |
| --- | --- | --- |
| `pet_button` | `pet_button_update` 恒返回 `PET_KEY_EV_NONE` | PRESS/SHORT/LONG/RELEASE 全部 `got 0`；队列恒空 `q_len=0` |
| `pet_keymap` | 恒返回 `PET_ACT_NONE` | 8 组映射 `got 0 want 1..7`；PRESS/RELEASE 组恰好通过 |
| `pet_menu` | 恒返回 `PET_MENU_CMD_NONE`、不改选中 | 选中循环恒 0；执行命令 `got 0` |
| `pet_pomo` | 空实现（结构字段保持 `pet_pomo_init` 默认值） | 相位/累计/统计/编解码/set_day 全不动作 |
| `pet_ui_*_240` | 空实现，不清屏 | 画布仍是哨兵 `0xA5A5`（十进制 42405），所有像素断言失败 |

逐用例统计：

| 测试程序 | 用例 | PASS | FAIL | 断言数 | 失败断言 |
| --- | --- | --- | --- | --- | --- |
| `test_m3_logic.exe` | 15 | 0 | 15 | 261 | 164 |
| `test_m3_ui.exe` | 6 | 0 | 6 | 256 | 225 |
| 合计 | 21 | 0 | 21 | 517 | 389 |

> 本记录在桩上先跑（Lead 并行实现期间用 `D:\DSH\esp-build\m3-stubs\` 固定桩副本，
> 避免与 Lead 替换真实现的时序竞争）；首版 RED 用同一命令得到同样的 21/21 失败。

## 2. GREEN（真实现，全部通过）

Lead 真实现（2026-10-31，**冻结版**：3 处 240 页 UI 修复 + day 未知哨兵）文件指纹：

| 文件 | 大小 | SHA256 前 16 位 |
| --- | --- | --- |
| `pet_button.c` | 2513 B | `45995A11F8408289` |
| `pet_keymap.c` | 927 B | `B3D3ACF4D0139DB0` |
| `pet_menu.c` | 1310 B | `A960684EC13916D1` |
| `pet_pomo.c` | 6398 B | `B9E612A672DE87D3` |
| `pet_ui_pomo_240.c` | 4306 B | `65D049714550F59F` |
| `pet_ui_menu_240.c` | 2948 B | `8E7BCF12E4DE60CF` |
| `pet_render.c`（链接） | 22672 B | `74181B8D7A92E55E` |
| `pet_text.c`（链接） | 8208 B | `901D0105B367D5B6` |

命令（仓库根 `D:\DSH\ESP32-touch`）：

```powershell
pwsh -NoProfile -File tests/run_m3_tests.ps1
```

完整原始输出（`tests/green-m3-run.log` 同文，31 行）：

```text
[MODE] GREEN: firmware/components/pet_ui
[CC] test_m3_logic.c + button/keymap/menu/pomo
[CC] test_m3_ui.c + ui_pomo_240/ui_menu_240 + pet_render.c + pet_text.c
=== RUN test_m3_logic.exe ===
PASS BTN-01-press-short-release (9 asserts)
PASS BTN-02-long-once (6 asserts)
PASS BTN-03-queue-fifo (11 asserts)
PASS KEY-03-boundary-595-605 (18 asserts)
PASS KEYMAP-01..08 (20 asserts)
PASS MENU-01-cycle (13 asserts)
PASS MENU-02-exec (10 asserts)
PASS MENU-03-close-ignore (7 asserts)
PASS POMO-01-full-transition (48 asserts)
PASS POMO-02-pause-no-advance (10 asserts)
PASS POMO-03-stats-alarm (20 asserts)
PASS POMO-04-long-abort (16 asserts)
PASS POMO-05-set-durations (15 asserts)
PASS POMO-06-encode-decode (43 asserts)
PASS POMO-07-set-day (15 asserts)
SUMMARY m3_logic: cases=15 passed=15 failed=0
=== EXIT 0 (test_m3_logic.exe) ===
=== RUN test_m3_ui.exe ===
PASS UI-20-pomo-ring-geometry (15 asserts)
PASS UI-20-pomo-progress-colors (21 asserts)
PASS UI-20-pomo-title-center-stats (19 asserts)
PASS UI-21-menu-title (7 asserts)
PASS UI-21-menu-rows (190 asserts)
PASS UI-21-menu-content-follows-view (4 asserts)
SUMMARY m3_ui: cases=6 passed=6 failed=0
=== EXIT 0 (test_m3_ui.exe) ===
RESULT: PASS (all M3 host tests green)
```

逐用例统计：

| 测试程序 | 用例 | PASS | FAIL | 断言数 |
| --- | --- | --- | --- | --- |
| `test_m3_logic.exe` | 15 | 15 | 0 | 261 |
| `test_m3_ui.exe` | 6 | 6 | 0 | 256 |
| 合计 | 21 | 21 | 0 | 517 |

退出码：`run_m3_tests.ps1` = 0；两个测试程序各 0。

### 2.1 GREEN 首跑暴露的差异及处理（3 实现缺陷 + 2 测试期望 + 1 新增语义）

首跑 GREEN（真实现，尚未含 day 哨兵）为 `test_m3_logic` 13/15、`test_m3_ui` 3/6：

```text
FAIL POMO-01-full-transition (5/48 asserts failed)
FAIL POMO-05-set-durations (1/15 asserts failed)
SUMMARY m3_logic: cases=15 passed=13 failed=2
FAIL UI-20-pomo-ring-geometry (7/15 asserts failed)
FAIL UI-20-pomo-progress-colors (3/21 asserts failed)
FAIL UI-20-pomo-title-center-stats (6/19 asserts failed)
PASS UI-21-menu-title / UI-21-menu-rows / UI-21-menu-content-follows-view
SUMMARY m3_ui: cases=6 passed=3 failed=3
```

| # | 责任 | 差异 | 根因 | 处理 |
| --- | --- | --- | --- | --- |
| 1 | Lead（实现） | 进度 0 被算成进度 1000 | `remaining_ms == total_ms` 时误取 `done = total_ms` | 改为 `remaining < total ? total-remaining : 0`；进度 0 整环轨道（0x4208） |
| 2 | Lead（实现） | 进度 0 时 12 点仍被点亮 | 填充判定用 `<=`，0° 满足 `0 <= 0` | 改严格 `<`；进度 0 的 12/3/6/9 点均轨道色 |
| 3 | Lead（实现） | 标题配色不符 §5.1 | `phase_color()` 把 `FOCUS_PAUSED/FOCUS_DONE` 映射橙、`BREAK_*` 映射绿 | 改为专注橙、休息绿、IDLE/PAUSED/DONE 灰 `0x8410`（契约口径经 Lead 确认保持原文，不继承上游 135×240） |
| 4 | 测试 | `POMO-01` 到点时刻算错 | 忽略 `tick(601000)` 已把 `since_ms` 推到 601000 | 只改期望时刻：`tick(2098000)`→`tick(2099000)`（`2000 + (2099000-601000) = 1500000`） |
| 5 | 测试 | `POMO-05` 期望 `since_ms==0` 错 | `set_durations` 本就不重置 tick 维护的当前段起点 | 只改期望值：`since_ms` 应为 2000（`elapsed_ms=2000` 不变） |
| 6 | Lead（实现，新增语义） | day 未知哨兵 | M3 无可信时间源，`has_day=false` 需与真实 day=0 区分；否则重启会把“未知”误读成 day=0 | 编码 day 字段写 `0x80000000`；解码该值 → `has_day=false/day=0`，done/focus 照常保存。测试新增 `POMO-06` 断言（POMO-06 由 36→43 断言，`POMO-06` RED 24/43 失败），属新增覆盖，非缺陷修复 |

> 第 4/5 项仅修正测试自身算错的数值，未删除/放宽任何断言；修复后逻辑组
> RED 阶段被覆盖的断言全部保留并通过。第 1–3 项属实现缺陷，由 Lead 在其写作用域
> `firmware/` 内修复；第 6 项为 Lead 在代码冻结前新增的语义，测试按 Lead 确认的
> 行为补充断言（`has_day=false` 编码→哨兵、解码→`has_day==0 && day==0`）。
> 本文件只记录，不越界修改 `firmware/`。

## 3. 契约落读与测试口径（与 docs/10 §5 / C07 对照）

1. **环厚 10 的像素口径**：外半径 76、内半径 `76-10=66`，按与 `pet_render_ring`
   相同的双闭区间取 `66 <= d <= 76`，故 12 点径向连续 11 个像素（76..66）。
   测试断言 11 而非 10，是“半径差=10、整数边界双闭”的实现落读；若后续改为半开区间
   需契约先明确并同步改断言。
2. **进度 0/1000 语义**：`remaining_ms == total_ms` → `elapsed=0` → 进度 0（整环轨道）；
   `remaining_ms == 0` → 进度 1000（整环相位色）。测试同时断言 `>1500` 环像素数量级，
   避免只采样点通过但整环未画。
3. **12 点顺时针方向**：避免用 0°/90°/180° 精确边界点判定（受 `<`/`<=` 口径影响），
   改用 45°/135°/150°/210° 采样：进度 300 时 45°/90° 为相位色、135° 为轨道；
   进度 500 时 3 点/150° 为相位色、9 点/210° 为轨道。
4. **相位标题颜色**：按 docs/10 §5.1 原文（专注橙/休息绿/暂停与其它灰）断言；
   IDLE=`READY` 灰、FOCUS 橙、PAUSED 灰、BREAK 绿、DONE 灰。本页为新契约，
   不继承上游 135×240 `pet_render_pomo` 的“PAUSED 用相位色”行为（Lead 已确认）。
5. **中心倒计时/底部统计**：中心 `M:SS` 白字 scale 2，白像素包围盒中心 ≈ 环心
   `(120,104)`；底部统计 y=200 灰字；菜单标题 y=12 scale 2 白、底部统计 y=212 灰。
   菜单行文本用“行区间存在非卡底色像素”判定，未逐字符比对字库。
6. **菜单 `selected` 取值 0..4**：C07 §4 与 docs/10 §5.2 为 5 项；`pet_menu.h` 中
   `pet_menu_view_t.selected` 注释仍写 `/* 0..2 */`（上游 3 项遗留），属注释陈述
   与 C07 不一致，不影响编译/行为。本测试按 5 项断言，**建议 Lead 后续勘误该注释**
   （本次未改 `firmware/`/`docs/`）。
7. **逻辑组不链接 `pet_pomo_store.c`**：NVS 属设备层（docs/10 §3 明示 host 单测不链接）；
   统计编解码与跨日逻辑在 `pet_pomo.c` 内以 `POMO-06/07` 覆盖。
8. **菜单底部统计字段被消费**：以 `muted`（MUTE 行 `ON/OFF`）、`done_count`、
   `focus_seconds`、`mode`（`DUTY|POMO`）四个视图字段的渲染差异断言，避免只画固定字符串。
9. 所有时序均由构造 `now_ms` 驱动；无 sleep、无真实时钟、无 NVS/GPIO/串口依赖。
10. **day 未知哨兵**：`has_day=false` 时 encode 写 `0x80000000`（i32 最小值的 LE 字节
    `00 00 00 80`），decode 该哨兵 → `has_day=false/day=0`；不与真实 `day=0` 混淆。
    该哨兵是 M5 接 NTP 前的“未知”约定（docs/10 §3：无网络不猜测时间）。
    已有 `day=20261031` 与负 day（-5）往返断言不受影响。

## 4. 结论

- T 类 host 单测 RED→GREEN 完成：
  - RED：桩副本上 21/21 用例失败（517 条断言中 389 条失败，两个程序编译成功、退出码 1）；
  - GREEN：真实现（冻结版）上 21/21 用例通过（517 条断言全过，两个程序与运行器退出码 0）。
- GREEN 首跑暴露 5 处差异 + 冻结前新增 1 项语义：3 处 firmware 缺陷（进度 0、12 点边界、
  相位配色，Lead 已修）、2 处测试期望算错（仅改数值，未放宽断言）、1 处 day 未知哨兵
  （Lead 新增，测试补断言），均在本文件 §2.1 说明。
- 无契约修改：docs/10 §5.1 保持原文（Lead 确认），§3 的“不猜测时间”由 day 哨兵落地；
  未改 `firmware/`、`docs/`、`tools/`，M3 交付均为 `tests/` 新增文件。
- 回归：M2 `pwsh -NoProfile -File tests/run_tests.ps1` 仍 15/15 用例 PASS
  （`pet_state` 9/9 + `pet_ui_240` 6/6），M3 测试未影响既有 host 测试。
- 未运行 `idf.py`、未刷机；像素断言只覆盖纯渲染函数（不读 assets）。
- 交付文件：`tests/test_m3_logic.c`、`tests/test_m3_ui.c`、`tests/run_m3_tests.ps1`、
  `tests/red-m3-run.log`、`tests/green-m3-run.log`、本文件。
