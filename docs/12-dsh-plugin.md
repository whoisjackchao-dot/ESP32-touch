# DSH 插件交付（M5a）：`dsh-esp32-touch-bridge`

> 日期：2026-10-31。**用户新增关键要求（2026-10-31）**：本项目的接入对象是 **DeepSeek Harness (DSH)**，
> 最终必须打包为**符合 DSH 插件标准**的格式。本文是该交付物的实施契约与验证计划。
> 协议：`contracts/C05-event-protocol.md`（已冻结）。
> 参考标准：DSH 内置 `cordis-plugin-development` skill（`references/host-plugin.md`）；
> 本机已装的同类插件 `~/.dsh/profiles/node_modules/dsh-sticks3-bridge`、本地包 `D:/DSH/myCFO`。

## 1. 交付物定义

一个**DSH bundle（host 插件包）**：`host-plugin/` 目录，可被

```powershell
dsh plugin --profile desktop add <本仓库>/host-plugin
```

安装进 profile（pnpm 安装 + 自动加入 `dsh.profile.bundles`），重启 DSH 后生效；也可在
Web 侧栏「插件 → 添加插件」用本地绝对路径安装。**不需要构建步骤、无第三方 npm 依赖。**

## 2. 包结构（冻结）

```text
host-plugin/
├── package.json            # name=dsh-esp32-touch-bridge；type=module；dsh.bundle.patch
├── cordis.patch.yml        # - insert: {id: esp32-touch-bridge, name: dsh-esp32-touch-bridge}
├── lib/
│   ├── index.js            # Cordis 插件入口：export const name / export function apply(ctx, config)
│   ├── state.js            # 纯状态归约：DSH 事件 → { mode, sessions, cards }
│   ├── protocol.js         # 纯编码：状态 → C05 NDJSON 行（含转义/截断）
│   └── serial-helper.js    # 子进程生命周期 + 行协议（可注入 fake，测试不碰串口）
├── helper/
│   └── serial-bridge.ps1   # Windows PowerShell 5.1 串口辅助进程（内部使用，不单独发布）
├── locale/
│   ├── zh.json / en.json   # 插件管理器展示元信息（title/description）
├── icon.svg                # 可选展示图标
├── test/                   # node --test（无第三方依赖）
├── README.md               # 安装/配置/卸载/内部辅助进程声明/已知限制
└── LICENSE                 # MIT（与仓库一致）
```

`package.json` 必需字段：`name`、`version`、`type:"module"`、`exports`（含 `./package.json`、`./locale/*.json`）、
`dsh.bundle.patch`、`keywords`（含 `dsh-plugin`）、`license`、`engines.node`。

## 3. DSH 事件订阅（宿主事实，2026-10-31 由已安装包的类型声明确认）

| DSH 事件 | 载荷（关键字段） | 插件用途 |
| --- | --- | --- |
| `session/created` / `session/disposed` | `session`（`id`、`header.cwd`、`header.createdAt`） | 会话集合；卡片的 project/时长来源 |
| `session/event` | `(session, event)`，`event.type`/`event.data` | 标题（`session/title`→`data.title`）、审批（`approval/asked`/`approval/decided`） |
| `agent/status` | `{agent:{id,session}, status:'idle'\|'running'}` | WORKING / 运行→空闲边沿（发 `agent.idle`） |
| `agent/error` | `{agent, turn, step, error}` | FAILED（`task.error`） |
| `user-questions/request`（waterfall） | `{agent?, questions, signal?}` | INPUT_REQUIRED（观察后必须 `next()` 委派，不改变审批结果） |

> 审批本身用**会话日志事件**（`approval/asked`/`approval/decided`）观察，不注册 `approval/request`
> waterfall，避免影响审批链。

### 3.1 归约与事件发射（插件侧）

- 维护 `sessions`：`{id, cwd, createdAt, title, running, pendingApproval, pendingQuestion, errored}`。
- 目标态：任一 pending（审批/提问）→ `INPUT_REQUIRED`；否则任一 running → `WORKING`；否则 `IDLE`。
- 边沿：
  - 进入 WORKING → `task.start`
  - 出现 pending → `agent.input_required`
  - pending 全部解除 → `task.start`（恢复工作）
  - 最后一次 running→idle → `agent.idle`（设备去抖 1.5 s 出 DONE）
  - `agent/error` → `task.error`
- 每次发射都附带顶层 `tasks`（≤3，按“有 pending > running > 最近创建”排序）与顶层单卡字段（`title/project/agent/elapsed_s`）。
- 心跳：每 **10 000 ms** 一条 `device.heartbeat`（无变化也发）。
- 发送串行化（单写者）；所有值经 UTF-8 按 C05 截断；非法/NaN 值不发（记日志）。

### 3.2 字段来源

| 卡片字段 | 来源 |
| --- | --- |
| `title` | 最近一次 `session/title` 的 `data.title`；无则 `cwd` 目录名；再退化为 `session <id 前 6 位>` |
| `project` | `path.basename(session.header.cwd)` |
| `agent` | 固定 `"DSH"`（C03 §7 R2 前缀） |
| `elapsed_s` | `(Date.now() - session.header.createdAt)/1000`，非负整数 |
| `status` | pending→`input_required`；errored→`failed`；running→`working`；否则 `done` |

## 4. 串口子进程（内部辅助进程，必须声明）

- **存在与用途**：`helper/serial-bridge.ps1`（Windows PowerShell 5.1）负责打开 `COMx`、读写字节；
  Node 无内置串口 API，故用子进程。**插件加载后、首次需要写设备时自动拉起；插件卸载/宿主退出时自动回收**
  （父进程关闭 stdin 后 helper 自行退出）。
- **行协议**（JSON lines）：
  - 插件→helper：`{"op":"open","port":"COM7","baud":115200}` / `{"op":"write","data":"<base64 字节>"}` /
    `{"op":"close"}` / `{"op":"ping"}`
  - helper→插件：`{"op":"ready"}` / `{"op":"line","data":"<base64 设备日志行>"}` /
    `{"op":"state","connected":true}` / `{"op":"error","message":"..."}`
- **重连**：helper 内部每 2 s 重试打开；写失败上报 `error` 并置 `connected:false`，插件保留最近状态并在重连后重发当前快照（level-triggered）。
- **不提供**单独启动方式：`package.json` 无 `bin`，README 不给启动命令（与项目既有 BLE helper 交付口径一致）。

## 5. 配置（bundle 行 config）

`cordis.patch.yml` 的 insert 行可带 `config`；插件 `apply(ctx, config)` 读取：

```yaml
- insert:
    - id: esp32-touch-bridge
      name: dsh-esp32-touch-bridge
      config:
        port: COM7          # 必填；缺省读环境变量 ESP32_TOUCH_PORT
        baud: 115200        # 可选，默认 115200（CDC 不敏感）
        heartbeatMs: 10000  # 可选，默认 10000
```

未配置 `port`/环境变量 → 插件**只加载不连设备**（不 spawn helper），日志明示。端口可用 `auto` 时
插件扫描 `COMx` 不可靠，**v1 不做自动发现**（用户显式配置）。

## 6. 安装 / 启用 / 卸载 / 验证

```powershell
# 安装（本地目录；也可 github:whoisjackchao-dot/ESP32-touch#path:host-plugin 形式，待发布后验证）
dsh plugin --profile desktop add D:\DSH\ESP32-touch\host-plugin
# 确认已加入 bundle 列表（不需要启动 GUI）
Get-Content $env:USERPROFILE\.dsh\profiles\desktop\package.json
# 查看合成后的配置树里出现插件行
dsh --profile desktop --dump-config | Select-String esp32-touch-bridge
# 重启 DSH 后生效；卸载：
dsh plugin --profile desktop remove dsh-esp32-touch-bridge
```

- 安装/启用/停用也可以走 Web 侧栏「插件」（官方插件管理器）。
- **不在插件里直接改 profile 文件、不跑 pnpm**（遵循 DSH 插件规范）。

## 7. 测试与验收

| 层 | 内容 | 证据等级 |
| --- | --- | --- |
| 单元 | `state.js` 归约：多会话/审批/提问/标题/错误/边沿/心跳/排序/截断 | T（`node --test`，无设备） |
| 协议 | `protocol.js` 与 C05 逐字段一致（NDJSON、转义、tasks 栈、非法值拒绝） | C（与 C05 对读的契约测试） |
| 装配 | `apply(ctx)` 用 fake ctx/transport：事件→发送行序列；未配置 port 时不 spawn | C |
| 安装 | 临时 profile `touch-test`：`dsh plugin add` → `--dump-config` 出现行；卸载干净 | D/C |
| 设备 H | 插件真实串口 → 设备 6 态/3 卡/心跳/OFFLINE 恢复；`E2E` 类 | H（需设备在线） |
| 真 DSH E2E | 装到 desktop、重启 DSH、真实会话产生事件 → 设备 <1 s 上屏 | H（需用户重启 GUI；本会话不可完成） |

验收映射：`G12`（任意本机事件 1 s 内上屏、三通道中 USB 通道；跨通道去重属 M5b）、`G2`（卡内容 ≤1 s）、
`G4`（心跳/失联 15–17 s）、`E2E` 系列（`E2E-01`：真实 DSH 会话驱动设备状态）。

## 8. 安全与隐私（冻结）

- 插件**只观察** DSH 事件，不注入提示词、不调用工具、不审批、不写会话/工作区文件。
- 只向本机串口写 C05 允许字段；**不发送对话正文/工具输出**；`title` 可能含 DSH 自动生成的会话标题
  （源于用户首条消息摘要）→ 用户可把设备视为“状态显示器”，该风险在 README 明示。
- 设备→host 方向 v1 只读人类日志，插件不解析控制指令。

## 9. 明确不做（M5a）

- WiFi TCP / BLE 通道、配网、NTP（属 **M5b**；`R21` 其余两通道保留，不取消）。
- 设备触摸/语音输入（后置）；自动审批（永久排除）。
- macOS/Linux 串口 helper（v1 仅 Windows DSH 桌面）。

## 10. 交付检查单

- [ ] `package.json` 含 `dsh.bundle.patch`；`cordis.patch.yml` 行 id/name 唯一
- [ ] `apply(ctx, config)` 只用公开 Cordis API；effect/dispose 清理定时器与子进程
- [ ] 无第三方依赖；`node --test` 全绿
- [ ] 未配置端口时零副作用（不 spawn、不写文件）
- [ ] 本地目录安装进临时 profile 成功，`--dump-config` 出现插件行
- [ ] README 声明内部 helper 进程与已知限制
- [ ] 真实设备 H（重连后）与 DSH 重启后 E2E（用户在场）
