# M5a host C 类契约测试记录（C05 设备事件协议：pet_linebuf + pet_link）

- 日期：2026-10-31
- 任务：`task-7`（evt-tests）。性质 = **C 类契约测试**（不是 TDD 新逻辑）：
  Lead 已实现设备侧解析器（派生自上游 ESP32-buddy，MIT），本测试只按
  `docs/contracts/C05-event-protocol.md`（冻结）+ **真实实现** 写契约用例，不改实现。
- 被测（只读，**未改动**）：`firmware/components/pet_ui/pet_linebuf.c`、`pet_link.c`；
  公开头文件 `include/pet_linebuf.h`、`pet_link.h`、`pet_types.h`、`pet_state.h`。
- 用例：`tests/test_m5a_event.c`（1133 行，29 用例，436 条断言；2026-10-31 收口补充 `LK-12` `has_tasks` 回归，见 §9）。
- 运行器：`tests/run_m5a_tests.ps1`（**新建独立脚本**，未并入 `run_tests.ps1` /
  `run_m3_tests.ps1`）。理由：现有两个运行器各自负责 M2/M3 的二进制，M5a 是新的
  `test_m5a_event.exe`，只链接 `pet_linebuf.c` + `pet_link.c`（不链接 `pet_state.c`，
  `pet_link` 不调用其函数），独立脚本可单独跑、单独看退出码且不影响既有回归；
  M2/M3 回归仍由原脚本各自负责（见 §6）。
- 口径：只用公开头文件 API；无 stub、无 sleep、不碰串口/GPIO/真实时钟；gcc 参数与
  既有运行器一致（`-std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter`，mingw gcc 16.1.0）；
  常量用头文件默认值：`PET_LINE_MAX=512`、`PET_TITLE_MAX=64`、`PET_PROJECT_MAX=64`、
  `PET_AGENT_MAX=8`。
- 只写 `tests/`；未改 `firmware/`、`docs/`、`tools/`；未运行 `idf.py`、未刷机。

## 1. 结果（GREEN）

命令（仓库根 `D:\DSH\ESP32-touch`）：

```powershell
pwsh -NoProfile -File tests/run_m5a_tests.ps1
```

- `SUMMARY m5a_event: cases=29 passed=29 failed=0 asserts=436`
- `RESULT: PASS (all M5a host tests green)`，退出码 0；编译 0 warning / 0 error。
- 原始日志：`tests/m5a-run.log`。

| # | 用例 | 覆盖点（契约） | 断言 |
| --- | --- | --- | --- |
| 1 | `LB-01-single-line-bytewise` | §1 逐字节攒行；NUL 结尾、不含 `\n`、deliver 前 NONE | 13 |
| 2 | `LB-02-crlf-strip` | §1 CRLF 剥离（只剥行尾一个 `\r`；行内 `\r` 保留） | 12 |
| 3 | `LB-03-empty-lines` | §1 空行/连续空行各交付一次 | 8 |
| 4 | `LB-04-exact-max-line` | §1 恰好 512 字节 → READY（`buf[512]='\0'`） | 9 |
| 5 | `LB-05-overflow-dropped-recover` | §1 第 513 字节起标记溢出 → `DROPPED`；不增长；丢弃后可恢复 | 47 |
| 6 | `LB-06-deliver-pending-reset` | `deliver_pending` 复位：READY 后新行不串内容；DROPPED 不置 pending | 9 |
| 7 | `LK-01-event-kind-map` | §3 六类事件（7 个名字）→ `kind`；单卡 `state` 映射 | 63 |
| 8 | `LK-02-blocked-is-task-error` | §3 `agent.blocked` → `PET_EV_TASK_ERROR`（≠ legacy `PET_EV_AGENT_BLOCKED`） | 3 |
| 9 | `LK-03-top-card-fields` | §2 顶层 `title/project/agent/elapsed_s` → `has_card` + 字段值；键序无关；行尾 `\n`/CRLF 可带 | 17 |
| 10 | `LK-04-top-card-single-field` | §2 任一可选字段出现即单卡（含空串、`elapsed_s:0`） | 17 |
| 11 | `LK-05-tasks-three-cards` | §4 3 项 → `card_count=3`、`cards[0]`=首项、字段/缺省/空串 title | 21 |
| 12 | `LK-06-tasks-empty` | §4 `tasks:[]` → 0 卡；与顶层单卡互不影响 | 7 |
| 13 | `LK-07-tasks-capped-three` | §4 >3 项取前 3（top-first） | 5 |
| 14 | `LK-08-tasks-status-map` | §4 status：`failed`/未知/缺省/显式 `working` → 状态映射 | 8 |
| 15 | `LK-09-tasks-plus-top-card` | §2+§4 tasks 与顶层单卡并存 | 10 |
| 16 | `LK-10-field-truncation` | §2/§4 截断：title/project 63、agent 7；含**字节截断**边界 | 18 |
| 17 | `LK-11-linebuf-link-integration` | §1+§2 端到端：字节流→READY→解析；512 字节合法行；513 字节 DROPPED | 26 |
| 18 | `LK-12-tasks-presence` | §4 `has_tasks` 信号：`tasks:[]`→true/count0（与无键可区分）；3 项→true/count3；无键→false；非数组→false；与顶层单卡独立 | 18 |
| 19 | `ER-01-json-syntax` | §5 非法 JSON/未闭合/尾部多余 → `JSON` | 15 |
| 20 | `ER-02-version` | §5 version 缺失/0/2/字符串/负数/bool/null → `VERSION`；校验顺序 | 12 |
| 21 | `ER-03-event` | §5 event 缺失 → `EVENT`；非字符串 → `TYPE`；大小写/未知 → `UNKNOWN_EVENT` | 7 |
| 22 | `ER-04-top-field-types` | §2/§5 顶层字段类型不符 → `TYPE`；`elapsed_s` 上限/非整数边界 | 13 |
| 23 | `ER-05-tasks-non-array` | §4 `tasks` 非数组 → 忽略该键，单卡字段仍生效 | 20 |
| 24 | `ER-06-tasks-syntax` | §4 tasks 数组语法坏 → `JSON` | 7 |
| 25 | `ER-07-tasks-bad-items-ignored` | §4 项类型不符 → 忽略该项、其它项保留；非对象项跳过；未知键跳过 | 25 |
| 26 | `ER-08-too-long` | §1/§5 >512 → `ERR_TOO_LONG`（优先于 JSON）；512 合法 | 5 |
| 27 | `ER-09-unknown-keys` | §2 未知键（嵌套对象/数组/数字/bool/null/字符串）跳过；深度上限 8 | 9 |
| 28 | `ER-10-unicode-and-escapes` | `\u` 不支持（顶层 → `TYPE`，tasks 项 → `JSON`）；标准转义还原 | 6 |
| 29 | `ER-11-api-and-len` | 公开 API：NULL 入参；显式 `len` 截断语义 | 6 |

合计：29 用例 / 436 断言，全部 PASS。

## 2. 覆盖点补充说明

- `pet_linebuf`：正常单行、逐字节、CRLF、裸 `\n`、空行、恰好 512、513 起超长丢弃
  （`PET_LINE_DROPPED`）、丢弃后恢复、`deliver_pending` 复位语义、READY 后内容覆盖。
- `pet_link` 正常：§3 全部事件名（6 类 / 7 名字，含 `agent.blocked` 并入 `TASK_ERROR`）；
  顶层单卡四字段各自触发 `has_card`（空串、0 也触发）；tasks 3 卡、空数组、>3 截断、
  项字段/缺省、status 映射（`working`/`input_required`/`done`/`failed`/未知→`working`）、
  项内 `agent`；顶层单卡与 tasks 并存；字段截断边界；linebuf+link 集成。
- `pet_link` 错误/边界：非法 JSON、未闭合、尾部多余、version 缺失/0/2/非数字、
  event 缺失/非字符串/未知、顶层字段类型、`tasks` 非数组与坏项/坏数组、超长、
  未知键嵌套跳过、`\u`/未知转义、显式 len、NULL 入参。
- 设计约束：错误行只断言**返回状态**（不约束 `out` 残留内容），避免过度约束实现细节；
  未知键/错误码优先级仅在与契约相关处显式断言（EN-02 注释标明实现顺序）。
- 明确不在本测试范围（属设备层，纯解析层覆盖不到）：`pet_rx.c` 串口读取/入队、
  §5“队列满”丢弃、`pet_state.c` 的事件应用（M2 已测）、USB-Serial-JTAG 物理传输。

## 3. 与文档/实现的差异（契约 vs 真实实现）

原则：**以真实实现为准**（任务要求），冲突不通过改 firmware 解决；测试断言实现行为，
差异在此如实记录并回报 Lead。若 Lead 判定应以契约为准，需改实现后本测试再翻转相应断言。

| # | 契约（C05）原文 | 真实实现实测 | 测试处理 |
| --- | --- | --- | --- |
| D1 | §2 `elapsed_s`：负数/非整数 → 整行**类型错误**丢弃 | 负数/字符串/bool → `ERR_TYPE` ✓；但 **`elapsed_s:1.5` → `ERR_JSON`**（`parse_uint` 只吃整数前缀 `1`，`.` 变成“尾部多余”）；`version:1.5` 同理 → `JSON` | ER-02/ER-04 按实现断言 `JSON` 并注释；差异待裁决 |
| D2 | §4 tasks 项 `title` **必填**（可空串） | 缺 `title` 的项 / 空对象 `{}` 项被**接受**（title 缺省空串）并计入 `card_count` | ER-07 按实现断言 1 卡；差异待裁决（是否收紧由 Lead 定） |
| D3 | §2 未知键忽略；§5 字段类型不符 → `TYPE`；`\u` 未在契约定义 | `\u`：顶层 `title`/`event` → `ERR_TYPE`；**未知键的字符串值** → `ERR_JSON`；**tasks 项内字符串** → **整行 `ERR_JSON`**（字段解析失败后回退 `skip_value`，而 `\u` 使 `skip_value` 也失败，不是“忽略该项”） | ER-09/ER-10 按实现断言；建议契约补“不支持 `\u` 转义”及错误码 |
| D4 | §2 “未知键忽略”（未写嵌套深度限制） | 未知键嵌套对象/数组最多 **8 层**（`SKIP_MAX_DEPTH=8`，rx 任务栈仅 4 KB 的防溢出限值）；第 9 层 → `ERR_JSON` | ER-09 断言 8 层 OK、9 层 `JSON`；建议契约补注深度上限 |
| D5 | §2 `elapsed_s` 为 int ≥0（未写上限） | 实现另有上限 **1e9**（`parse_uint` 防 int32 溢出）：`1000000000` OK、`1000000001` → `ERR_TYPE` | ER-04 断言两边界；建议契约补注上限 |
| D6 | §2/§4 “>63 字符截断”“>7 字符截断” | 按**字节**截断：title/project 63 **字节**、agent 7 **字节**（`PET_*_MAX-1`）；CJK 多字节字符会被切在中间，产生不完整 UTF-8 | LK-10 显式断言 63 字节 + 被切剩的中间字节 `0xAE`；建议契约写“字节” |
| D7 | §5 “尾部多余 → 丢弃” | JSON 对象后的空格/Tab/CR/LF（任意数量）被 `skip_ws` 忽略并接受；仅非空白尾部 → `JSON` | ER-01 断言 `}x`/`} {}`/`}}\nX` 为 `JSON`；LK-03 断言 `}\n`、`}\r\n` 合法。属合法 JSON 尾随空白，非冲突，仅记录 |
| D8 | §3 `device.heartbeat`：只刷新链路，不改状态 | 解析层在**带顶层单卡字段**时仍给出 `has_card=1`（`card.state=IDLE`）；是否改状态由 `pet_state` 应用层决定（M2 已测） | LK-01 断言该解析输出；属观察项，非解析缺陷 |
| D9 | §3 `agent.blocked` → `PET_EV_TASK_ERROR` | 实现正确映射 `TASK_ERROR`；`pet_state.h` 中遗留 `PET_EV_AGENT_BLOCKED` 枚举**从不被解析器产出** | LK-02 显式断言两者不等；仅记录 |
| D10 | §5 错误优先级（契约未规定） | 解析期字段类型错误**先于** version/event 校验返回（`version:2` + `title:42` → `TYPE`）；`version` 缺失优先于 `event` 缺失（→ `VERSION`） | ER-02 按实现断言并注释；属未规定行为，仅记录 |

## 4. 测试侧修正记录（如实保留，不掩盖）

本任务为 C 类契约测试，无 RED 桩阶段；但开发过程中出现过 **2 次测试侧缺陷**，均被断言暴露、
修正的是**测试**（实现 `firmware/` 全程零改动），原始失败原文如下，未删除/未放宽断言：

1) 首轮全量运行（416 断言）：27/28 PASS，唯一失败是测试对 `\u` 的错误预期
   （期望 tasks 项 `\u` → `OK`，实测整行 `JSON`；原因见 D3）：

```text
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_m5a_event.c:972: parse_str("{\"version\":1,\"event\":\"task.start\",\"tasks\":[" "{\"title\":\"a\\u0041b\"}]}", &msg) == PET_LINK_OK (got 2, want 0)
FAIL ER-10-unicode-and-escapes (1/7 asserts failed)
SUMMARY m5a_event: cases=28 passed=27 failed=1 asserts=416
=== EXIT 1 (test_m5a_event.exe) ===
```

2) 补 LK-10 字节截断断言后（418 断言）：测试数据笔误（UTF-8 字节写成 `E5 BA A0`，
   应为 `E5 AE A0`），被精确断言捕获：

```text
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_m5a_event.c:569: (unsigned char)msg.card.title[62] == 0xAE (got 186, want 174)
FAIL LK-10-field-truncation (1/18 asserts failed)
SUMMARY m5a_event: cases=28 passed=27 failed=1 asserts=418
```

## 5. 原始输出摘要

`tests/m5a-run.log`（完整原文）：

```text
[MODE] GREEN: firmware/components/pet_ui (pet_linebuf.c + pet_link.c)
[CC] test_m5a_event.c + pet_linebuf.c + pet_link.c
=== RUN test_m5a_event.exe ===
PASS LB-01-single-line-bytewise (13 asserts)
PASS LB-02-crlf-strip (12 asserts)
PASS LB-03-empty-lines (8 asserts)
PASS LB-04-exact-max-line (9 asserts)
PASS LB-05-overflow-dropped-recover (47 asserts)
PASS LB-06-deliver-pending-reset (9 asserts)
PASS LK-01-event-kind-map (63 asserts)
PASS LK-02-blocked-is-task-error (3 asserts)
PASS LK-03-top-card-fields (17 asserts)
PASS LK-04-top-card-single-field (17 asserts)
PASS LK-05-tasks-three-cards (21 asserts)
PASS LK-06-tasks-empty (7 asserts)
PASS LK-07-tasks-capped-three (5 asserts)
PASS LK-08-tasks-status-map (8 asserts)
PASS LK-09-tasks-plus-top-card (10 asserts)
PASS LK-10-field-truncation (18 asserts)
PASS LK-11-linebuf-link-integration (26 asserts)
PASS LK-12-tasks-presence (18 asserts)
PASS ER-01-json-syntax (15 asserts)
PASS ER-02-version (12 asserts)
PASS ER-03-event (7 asserts)
PASS ER-04-top-field-types (13 asserts)
PASS ER-05-tasks-non-array (20 asserts)
PASS ER-06-tasks-syntax (7 asserts)
PASS ER-07-tasks-bad-items-ignored (25 asserts)
PASS ER-08-too-long (5 asserts)
PASS ER-09-unknown-keys (9 asserts)
PASS ER-10-unicode-and-escapes (6 asserts)
PASS ER-11-api-and-len (6 asserts)
SUMMARY m5a_event: cases=29 passed=29 failed=0 asserts=436
=== EXIT 0 (test_m5a_event.exe) ===
RESULT: PASS (all M5a host tests green)
```

## 6. 兼容性回归（M2 / M3 不回归）

```powershell
pwsh -NoProfile -File tests/run_tests.ps1
# SUMMARY pet_state: cases=9 passed=9 failed=0
# SUMMARY pet_ui_240: cases=6 passed=6 failed=0
# RESULT: PASS (all host tests green)   exit 0

pwsh -NoProfile -File tests/run_m3_tests.ps1
# SUMMARY m3_logic: cases=15 passed=15 failed=0
# SUMMARY m3_ui: cases=6 passed=6 failed=0
# RESULT: PASS (all M3 host tests green)   exit 0
```

原始日志：`tests/m5a-regression-m2.log`、`tests/m5a-regression-m3.log`。均为 **PASS、退出码 0**。

## 7. 文件清单（只写 tests/）

| 文件 | 行数/大小 | 说明 |
| --- | --- | --- |
| `tests/test_m5a_event.c` | 1133 行 / 45,553 B | 29 用例 / 436 断言（含 LK-12 `has_tasks`），链接真实现 `pet_linebuf.c` + `pet_link.c` |
| `tests/run_m5a_tests.ps1` | 68 行 / 2,265 B | 独立运行器（gcc 编译 + 运行 + PASS/FAIL + 退出码） |
| `tests/RED-GREEN-M5A.md` | 本文件 | 用例/断言统计、覆盖点、差异、原始输出 |
| `tests/m5a-run.log` | 1,478 B | M5a GREEN 原始输出（含 LK-12） |
| `tests/m5a-regression-m2.log` | 945 B | M2 回归原始输出 |
| `tests/m5a-regression-m3.log` | 1,258 B | M3 回归原始输出 |

## 8. 结论

- M5a 设备事件解析（`pet_linebuf` + `pet_link`）C 类契约测试 **GREEN**：29/29 用例、436/436 断言 PASS（含收口 `has_tasks` 回归 LK-12，§9）。
- M2/M3 既有 host 测试**零回归**（均 PASS，退出码 0）。
- 与契约的 10 处差异（D1–D10）已记录，其中 **D1（非整数 elapsed_s 返回 JSON 而非 TYPE）**、
  **D2（tasks 项 title 非必填）**、**D3（`\u` 转义错误码）**、**D4/D5（实现限值未入契约）**、
  **D6（按字节截断）** 建议 Lead 裁决：改契约文字（推荐，行为合理）或改实现后翻转测试。
- 约束遵守：只写 `tests/`，未改 `firmware/`、`docs/`、`tools/`；未跑 `idf.py`、未刷机。

## 9. 收口补充：`has_tasks` 回归（2026-10-31，仅改 tests/）

**背景（Lead 修复的设备侧差异）**：此前 `pet_link_parse` 只输出 `card_count`，无法区分
“`tasks` 空数组 `[]`”与“行内无 `tasks` 键”，`main.c apply_link_msg` 因此无法在收到 `tasks:[]`
时清空旧卡栈（C05 §4 要求 `[]` 清空栈）。Lead 的固件改动（**非本次测试改动，仅记录**）：

- `include/pet_link.h`：`pet_link_msg_t` 新增 `bool has_tasks`（行内出现 `tasks` **数组**即 true，含空数组）；
- `pet_link.c`：`tasks` 键且值为 `[` 时置 `has_tasks=true`（数组语法坏仍整行 `JSON`）；
- `main.c apply_link_msg`：`has_tasks ? pet_state_set_cards(..., card_count) : has_card ? pet_state_set_card(...)`
  （`set_cards(count=0)` 清空，M2 语义）。

**本次测试补充**：新增 `tests/test_m5a_event.c` 用例 `LK-12-tasks-presence`（18 条断言）：

| 场景 | 断言 |
| --- | --- |
| `tasks:[]` | `OK`、`has_tasks=1`、`card_count=0`、`has_card=0` |
| tasks 3 项 | `OK`、`has_tasks=1`、`card_count=3` |
| 无 `tasks` 键（顶层 title 单卡） | `OK`、`has_tasks=0`、`has_card=1` |
| `tasks` 非数组（`5`） | `OK`、`has_tasks=0`、`card_count=0`、`has_card=1`（契约 §4 忽略该键） |
| `tasks` + 顶层单卡并存 | `OK`、`has_tasks=1`、`has_card=1`、`card_count=1`（两信号独立，上层优先整栈替换） |

**结果（全量重跑）**：

```text
PASS LK-12-tasks-presence (18 asserts)
SUMMARY m5a_event: cases=29 passed=29 failed=0 asserts=436
RESULT: PASS (all M5a host tests green)
=== EXIT 0 (test_m5a_event.exe) ===
```

M2/M3 回归仍 **PASS**（汇总同 §6，退出码 0）；原始日志已同步 `tests/m5a-run.log`、
`tests/m5a-regression-m2.log`、`tests/m5a-regression-m3.log`。

**说明**：此前 LK-06 只断言 `tasks:[] → card_count=0`，无法锁定“行内是否出现 tasks”的信号；
新增 `has_tasks` 断言后，“空数组清栈”在解析层被契约测试锁定。§3 的差异清单 D1–D10 不变。
