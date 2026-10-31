# M3 开发：2 应用键 + 菜单 + 番茄钟（实施契约与证据计划）

> 日期：2026-10-31；阶段 3（开发）
> 依据：`05-plan.md` M3、`contracts/C07-keymap.md`（2026-10-31 冻结）、`02-requirements.md` `R13/R15/R17/R26`。
> 上游参考：`ESP32-buddy`（MIT）`pet_button.c` / `pet_keymap.c` / `pet_menu.c` / `pet_pomo.c` / `pet_pomo_store.c`
> 的行为规格（代码按本项目重写/派生，来源见 `NOTICE.md`）。
> 起点事实：M2 真机通过并由用户目视确认（2026-10-31，"真机和设计一致"）；`C07` 已冻结。

## 1. 本轮交付（= M3 定义）

1. **按键**：`pet_button`（20 ms 采样 / 600 ms 长按 / 松键判短）+ GPIO 采样（KEY1=GPIO0、KEY2=GPIO4、PWR=GPIO5）；`pet_keymap` 映射；`PWR` 长按关机流程。
2. **菜单**：5 项（MUTE/STATUS/WIFI(M5)/BT(M5)/BACK），两键导航；MUTE 持久化。
3. **番茄钟**：`pet_pomo` 状态机（25/5，开始/暂停/继续/放弃/到点/推进）；240×240 圆环视图；当日统计 NVS 持久化（重启不丢）；跨日清零逻辑（`set_day` 输入驱动）。
4. 主循环模式：DUTY（M2 值班页）/ POMO（番茄钟页）/ MENU（菜单页），8 fps；键事件与模式在**主任务**串行处理。
5. 串口注入扩展（M3 调试用）：合成键事件、模式/番茄钟状态、模拟跨日、重启。
6. host 单测（T）：按键/键表/菜单/番茄钟状态机/统计编解码/240 视图像素断言；RED→GREEN 留档。
7. 证据：`E22`（构建/刷机）、`E23`（KEY T）、`E24`（G6 合成键 + 真机按键）、`E25`（G5 番茄钟 + 重启持久化）、`E26`（跨日清零）、`E27`（D-06…，如有）。

**明确不做**：触摸/IMU/语音输入（后置）；语音播报（M4，本节点只记 alarm 日志）；WiFi/BLE/配网（M5，菜单项显式标注）；OTA。

## 2. 接口

### 2.1 复用上游头（已在本仓库 `include/`，逐字节）

`pet_button.h`、`pet_menu.h`、`pet_pomo.h`。其中：

- `pet_button_update(b, raw_pressed, now_ms)` → 每拍一个事件；`PRESS/RELEASE` 保留。
- `pet_menu_key(m, key, ev)` → 菜单命令；`PET_MENU_ITEMS=5`。
- `pet_pomo_*` → 状态机/视图/统计编解码。

### 2.2 本节点新增/修订

```c
/* include/pet_keymap.h（新增；不依赖 pet_engine，保持与上游行为一致） */
typedef enum { PET_MODE_DUTY = 0, PET_MODE_POMO } pet_mode_t;

typedef enum {
    PET_ACT_NONE = 0,
    PET_ACT_POMO_SHORT,   /* 番茄钟 KEY1 短：开始/暂停/继续/推进 */
    PET_ACT_POMO_LONG,    /* 番茄钟 KEY1 长：放弃 */
    PET_ACT_MODE_POMO,    /* 值班 KEY2 短：切到番茄钟 */
    PET_ACT_MODE_DUTY,    /* 番茄钟 KEY2 短：切回值班 */
    PET_ACT_MUTE_TOGGLE,  /* 值班 KEY1 长：静音翻转（持久化） */
    PET_ACT_SEND,         /* 值班 KEY1 短：发送（M5 接入，M3 只记日志） */
    PET_ACT_MENU          /* 任意模式 KEY2 长：打开菜单 */
} pet_action_t;

pet_action_t pet_keymap_action(pet_mode_t mode, pet_key_t key, pet_key_event_t ev);
```

```c
/* include/pet_pomo_store.h（新增，设备侧 NVS） */
void pet_pomo_nvs_load(pet_pomo_t *p);
void pet_pomo_nvs_save(const pet_pomo_t *p);
/* 静音持久化（M3 用，M4 audio 复用） */
void pet_mute_nvs_load(bool *muted);
void pet_mute_nvs_save(bool muted);
```

```c
/* include/pet_pomo.h 修订（本项目扩展，C07 不覆盖） */
/* 运行期可改本段时长：仅用于 M3 的 G5 演示（25/5 常量仍是发布值），
 * 默认焦点= POMO_FOCUS_MS、休息= POMO_BREAK_MS。 */
typedef struct { ... uint32_t focus_ms; uint32_t break_ms; ... } pet_pomo_t;
void pet_pomo_set_durations(pet_pomo_t *p, uint32_t focus_ms, uint32_t break_ms);
```

```c
/* include/pet_ui_pomo_240.h / pet_ui_menu_240.h（新增渲染） */
void pet_ui_pomo_240(pet_surface_t *s, const pet_pomo_view_t *v);
void pet_ui_menu_240(pet_surface_t *s, const pet_menu_view_t *v);
```

> `pet_pomo.h` 的 `phase` 判定必须使用 `p->focus_ms/break_ms`（默认等于宏），
> 保证 25/5 发布值与 G5 演示可同时成立。

## 3. NVS 布局（M3）

| namespace | key | 类型 | 内容 |
| --- | --- | --- | --- |
| `pomo` | `stats` | blob 16 B | `PMC1` + day(i32) + done(u32) + focus_s(u32)（上游格式，`pet_pomo_encode/decode_stats`） |
| `voice` | `muted` | u8 | 0=有声 / 1=静音（M4 音频复用） |

- `nvs_flash_init()` 失败 → 记录并继续（统计退化为内存态，日志明示）。
- 落盘时机：专注段完成、放弃、模式切换、`PWR` 关机、静音翻转、跨日清零。
- **跨日**：M3 无可信时间源（R23：无网络不猜测时间）→ 不自动调用 `set_day`；
  保留逻辑与 NVS 字段，M5 由 NTP 提供 day 后调用。M3 用调试命令模拟跨日（§4）验证逻辑。

## 4. M3 串口注入协议（在 M2 协议上追加；仍为调试接口）

| 命令 | 含义 |
| --- | --- |
| `K` | 合成 KEY1 短按 |
| `L` | 合成 KEY1 长按 |
| `J` | 合成 KEY2 短按 |
| `H` | 合成 KEY2 长按 |
| `m` | 打印一行模式/番茄钟状态 |
| `y` | 模拟跨日：`has_day ? set_day(day+1) : set_day(1)`，并落盘 |
| `R` | 软重启（`esp_restart()`，用于 G5 重启持久化） |
| `t` | 临时缩短本段时长（G5 演示）：下一段起 focus=6 s / break=3 s |
| `n` | （M2 已有）心跳，避免 OFFLINE |
| `p`/`d`/`c`/`x`/`0..5`/`u` | M2 已有，保持 |

`m` 行格式（单行，UTF-8；字段顺序为真机实现）：

```text
MODE mode=<DUTY|POMO> menu=<0|1> sel=<0..4> phase=<IDLE|FOCUS|FOCUS_PAUSED|BREAK|BREAK_PAUSED|FOCUS_DONE|BREAK_DONE> remain_ms=<n> total_ms=<n> done=<n> focus_s=<n> muted=<0|1> day=<n|-> has_day=<0|1> key1=<0|1> key2=<0|1> pwr=<0|1>
```

`sel` = 菜单当前选中项（菜单打开时从 0 开始，关闭后保留最后值）；`key1/key2/pwr` = **原始按下状态**（1=按下，用于真机按键排查）。

## 5. 240×240 版式（M3 两页；`C03` 只覆盖值班页，本节为 M3 自定）

### 5.1 番茄钟页

| 区域 | 矩形/参数 | 内容 |
| --- | --- | --- |
| 底色 | 全屏 `0x0000` | |
| 相位标题 | 居中，`y=16`，8×8 scale 1 | `READY`（IDLE）/`FOCUS`/`PAUSED`/`BREAK`/`DONE`；颜色：专注 `0xFD20`、休息 `0x07E0`、暂停/其它 `0x8410` |
| 圆环 | 心 `(120,104)`，外半径 **76**，环厚 **10** | 轨道 `0x4208`；进度 = `elapsed/total` 千分比，**12 点顺时针**；颜色随相位 |
| 中心倒计时 | 居中于环心，8×8 **scale 2**，白 | `M:SS`（剩余，如 `25:00`） |
| 底部统计 | `y=200` 居中，8×8 scale 1，灰 `0x8410` | `DONE <n>  <m>M`（当日完成段数/累计专注分钟） |

### 5.2 菜单页

| 区域 | 矩形/参数 | 内容 |
| --- | --- | --- |
| 底色 | 全屏 `0x0000` | |
| 标题 | `MENU` 居中，`y=12`，8×8 scale 2，白 | |
| 行 | `x=4`，`w=232`，`h=28`，`y=48 + i*32`（i=0..4） | 卡底 `0x18E3`；选中行**反色**（底 `0xFFFF`、字 `0x0000`） |
| 行文本 | `(x+8, y+10)`，8×8 scale 1 | `MUTE: ON/OFF`、`STATUS`、`WIFI (M5)`、`BT (M5)`、`BACK` |
| 底部统计 | `y=212` 居中，灰 | `DONE <n>  <m>M   <DUTY|POMO>` |

> 菜单页所有文案为 ASCII（8×8 字库）；不引入 CJK。选中项反色矩形布局 `x=4,y,w=232,h=28`，行间 4 px。

## 6. 主循环集成（串行、无锁）

- 单一 `uint8_t` 命令队列（M2 已有）承载：串口命令 + **合成键事件码**（`0x81..0x84`）。
- 20 ms `key_task`：读 GPIO0/4/5 → `pet_button_update` 两键 → 事件码入队；`PWR` 独立计时，≥1500 ms 执行关机流程（不经过队列）。
- 主任务每帧（125 ms）：先排空队列（键事件→键表/菜单/番茄钟；串口命令），再 `pet_state_tick` + `pet_pomo_tick`，再按模式渲染，再送屏。
- 模式：`DUTY` → `pet_ui_scene_240`；`POMO` → `pet_ui_pomo_240`；`MENU` → `pet_ui_menu_240`（`menu_open` 优先）。
- 番茄钟到点：`pet_pomo_take_alarm()` → M3 只记日志（M4 接语音）。
- 统计落盘：见 §3；失败只记录。

## 7. 用例（T/C/H）

| 用例 | 类别 | 判定 |
| --- | --- | --- |
| `BTN-01..03` | T | 按下/短按/长按/600 ms 边界/松键不重复/队列顺序 |
| `KEY-03` | T | 590 ms 判短、610 ms 判长 |
| `KEYMAP-01..08` | T | 8 组（DUTY/POMO × KEY1/KEY2 × SHORT/LONG）映射 |
| `MENU-01..05` | T | 5 项循环、执行、关闭、菜单内主界面动作屏蔽 |
| `POMO-01..08` | T | 状态机全转移、暂停不推进、到点统计、alarm 取走即清、编解码、set_day 清零 |
| `UI-20` | T | 番茄钟页像素：环心/半径/进度角/颜色/倒计时文本存在 |
| `UI-21` | T | 菜单页像素：5 行矩形、选中反色、底部统计 |
| `KEY-01/02/04/05` | H | 真机按键（短/长/菜单/PWR）；合成键先跑 C 级自动 |
| `G5` | H | 完成/暂停/放弃/继续各 1 组（短时长演示构建/运行期缩短）+ 重启 1 次；统计不丢 |
| `G6` | H | 两键短/长各 20 次 ≥19/20；PWR 长按 5/5、短按 5/5 无动作 |
| `UI-22` | H | 番茄钟圆环/菜单在真机可读（帧缓冲导出 + 用户目视） |

## 8. 分工与写作用域（并行）

| 角色 | 写作用域 | 交付 |
| --- | --- | --- |
| Lead | `firmware/`、`docs/` | 契约、实现、主循环集成、NVS、真机验证、证据 |
| m3-tests | `tests/` | T 用例（逻辑 + 像素），RED→GREEN 留档 |
| m3-tools | `tools/host/` | 合成键/G5/G6 跑测、番茄钟/菜单截图与像素断言、模拟器扩展 |

## 9. 残余风险

- `B09`：GPIO0/4/5 按键未逐键实测（原理图来源）；若某键读不到，停止并回报，不静默改映射。
- `PWR` 在 USB 供电下拉低 GPIO2 不会真断电（物理事实）：H 只能验证"进入关机流程"，真断电需电池。
- 跨日依赖时间源（M5 NTP）；M3 仅验证 `set_day` 逻辑与持久化。
- 600 ms 长按为提案值；菜单/番茄钟页为 M3 自定版式，需用户目视确认。
