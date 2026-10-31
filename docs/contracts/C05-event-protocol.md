# C05：设备事件协议 v1（USB-Serial-JTAG，冻结）

> 状态：**已冻结（2026-10-31）**。宿主侧为 **DSH 插件**（`docs/12-dsh-plugin.md`）。
> 依据：`Q04`（2026-10-31 用户确认：接入对象 = DeepSeek Harness，宿主桥=DSH 插件）、`R21/R29`。
> 上游行为参考：`ESP32-buddy/contracts/M4-事件入口与OpenCode接入契约清单.md`（MIT，仅行为）。

## 1. 传输

| 项 | 规定 |
| --- | --- |
| 通道 | **USB-Serial-JTAG**（Windows 侧 `COMx`；设备 CDC，波特率不敏感） |
| 方向 | **host → device**（v1 单向；设备→host 只有人类可读日志，无协议应答） |
| 编码 | UTF-8，**NDJSON**（一行一个 JSON 对象，`\n` 结尾，`\r` 忽略） |
| 行长 | ≤ **512 B**（含花括号，不含换行）；超长整行丢弃 |
| 连接 | 插件打开串口即视为通道建立；断开由插件重连；设备侧只依赖心跳 |

## 2. 信封与字段

必需：

```json
{"version":1,"event":"task.start"}
```

| 键 | 类型 | 规定 |
| --- | --- | --- |
| `version` | int | **必须 `==1`**；否则整行丢弃 |
| `event` | string | 见 §3；未知事件丢弃（不崩） |

可选（顶层单卡字段，兼容 M4）：

| 键 | 类型 | 规定 |
| --- | --- | --- |
| `title` | string | 会话名；>63 字符截断 |
| `project` | string | 项目；>63 字符截断 |
| `agent` | string | Agent 前缀（如 `DSH`）；>7 字符截断 |
| `elapsed_s` | int ≥0 | 驻留秒；负数/非整数 → 整行类型错误丢弃 |
| `tasks` | array | 见 §4 |

- 出现 `title`/`project`/`agent`/`elapsed_s` **任一** → 产生单卡更新（`card.state` = 该事件映射态）。
- 未知键忽略；类型不符 → 丢弃整行（不产生事件、不改状态）。

## 3. 事件 → 设备状态（`pet_state`）

| `event` | 设备动作 |
| --- | --- |
| `task.start` | `PET_EV_TASK_START` → WORKING |
| `agent.input_required` | `PET_EV_AGENT_INPUT_REQUIRED` → INPUT_REQUIRED |
| `task.done` | `PET_EV_TASK_DONE` → DONE（显式完成，不去抖） |
| `task.error` / `agent.blocked` | `PET_EV_TASK_ERROR` → FAILED |
| `agent.idle` | `PET_EV_AGENT_IDLE` → 下降沿：WORKING/INPUT 时 1500 ms 去抖后 DONE；已在 IDLE 保持 |
| `device.heartbeat` | `PET_EV_ANNOUNCEMENT`：只刷新链路（防 OFFLINE），不改状态 |

> 设备失联：15 s 无有效事件（含心跳）→ OFFLINE（M2 契约常量）。插件心跳周期 **10 s**。

## 4. `tasks`（多卡栈，≤3）

- **整体替换**卡栈（top-first）：`cards[0]` 渲染在 y=152。多于 3 项取前 3；`[]` 清空栈。
- 每项：

| 键 | 类型 | 规定 |
| --- | --- | --- |
| `title` | string | 必填（可空串：设备渲染 `(untitled)`）；>63 截断 |
| `project` | string | 可空（设备渲染 `-`）；>63 截断 |
| `elapsed_s` | int ≥0 | 缺省 0 |
| `status` | string | `working` / `input_required` / `done` / `failed`；缺省/未知 → `working` |
| `agent` | string | 可选前缀（`DSH`）；>7 截断 |

- 项内未知键忽略；类型不符的项**忽略该项**（不影响其它项）；`tasks` 非数组 → 忽略该键（单卡字段仍生效）。

## 5. 错误处理（不可违反）

| 情况 | 行为 |
| --- | --- |
| 非法 JSON / 未闭合 / 尾部多余 | 丢弃整行 + `W` 日志；**不崩、不改状态、不重试** |
| `version` 缺失或 ≠1 | 丢弃（`VERSION`） |
| `event` 缺失 / 未知 | 丢弃（`EVENT`/`UNKNOWN`） |
| 字段类型不符 | 丢弃整行（`TYPE`） |
| 过长 / 队列满 | 丢弃 + 限频日志 |
| 重复行 | v1 单通道**不去重**（状态机事件幂等；插件侧保证单写不重复） |

## 6. 设备侧实现边界

- 解析为**纯逻辑**（`pet_linebuf` + `pet_link`，host 可测），不碰 GPIO/时间；`now_ms` 由调用方注入。
- 串口读取在 console 任务；**状态修改只在主任务**（解析结果入队，主任务 apply）。
- 日志行（非 `{` 开头）与既有单字符调试命令保持兼容。

## 7. 明确不做（v1）

- 设备→host 协议应答/ack；加密/鉴权（本机 USB 可信）；多通道（WiFi TCP/BLE 属 **M5b**，届时才引入跨通道去重）。
- 自动审批/自动提交（`R03` 永久排除）。

## 8. 变更方式

字段/事件/语义任一变更 → 修订本文件 + 同步 `docs/12-dsh-plugin.md` 与插件测试；设备侧回归 `tests`（C05 解析用例）。

## 9. 实现对齐记录（2026-10-31，C 类契约测试发现）

> 测试以真实实现为准（`tests/test_m5a_event.c`，28 用例/418 断言）；以下为双方差异与最终口径，
> **不改变 §1–§7 的协议外观**（字段与事件名不变），仅明确边界行为。

| # | 原表述 | 最终口径（实现即契约） |
| --- | --- | --- |
| `D1` | `elapsed_s` 非整数 → TYPE | 负数 → `TYPE`；**含小数/指数**（如 `1.5`）→ `JSON`（解析器拒收该 token）；`version` 同理 |
| `D2` | tasks 项 `title` 必填 | `title`/`project` **可缺省**（等价空串，设备用占位符渲染）；缺少也不影响该项入栈 |
| `D3` | 未提 `\u` 转义 | **不支持 `\uXXXX`**：出现位置不同会得到 `TYPE`/`JSON`（整行丢弃）；只支持 `\" \\ \/ \n \t \r \b \f` |
| `D4` | 未提未知键嵌套 | 未知键（含嵌套对象/数组）最多跳过 **8 层**；更深 → `JSON`（保护 4 KB 解析栈） |
| `D5` | `elapsed_s` int ≥0 | 另有上限 **1e9**；超过 → `TYPE` |
| `D6` | 截断到 63/63/7 **字符** | 实际按 **UTF-8 字节**截断（63/63/7 B）；CJK 可能被切在多字节中间（渲染时按非法序列处理） |
| `D7` | 未提尾部空白 | 对象后允许空白字符；其它尾部内容 → `JSON` |
| `D8` | 心跳"只刷新链路" | `device.heartbeat` **不改状态**；若同行携带顶层卡字段，卡仍会更新（事件本身只刷新链路） |
| `D9` | `agent.blocked` → FAILED | 实现映射为 `PET_EV_TASK_ERROR`（与 `task.error` 同一设备动作），不产出独立 `AGENT_BLOCKED` 种类 |
| `D10` | 未提错误优先级 | 结构（JSON）→ `version` → `event` → 字段类型；首个失败即整行丢弃 |
