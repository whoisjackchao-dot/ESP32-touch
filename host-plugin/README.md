# dsh-esp32-touch-bridge

把 **DeepSeek Harness（DSH）** 的会话 / Agent 状态投到 **ESP32-S3 桌面宠物**（240×240 屏）上的
**DSH bundle 插件**（Cordis Host 插件）。宿主侧只观察 DSH 公开事件，按冻结的
**C05 v1** 协议编码成 NDJSON，经本机 USB-Serial-JTAG（`COMx`）写到设备。

- 宿主：DSH（Cordis）profile 的 **bundle 层**；`package.json` 声明 `dsh.bundle.patch`
- 依赖：**零第三方 npm 依赖**，无构建步骤，纯 ESM（`type: module`）
- 协议：`docs/contracts/C05-event-protocol.md`（仓库内，冻结）
- 契约：`docs/12-dsh-plugin.md`

```text
host-plugin/
├── package.json            # name=dsh-esp32-touch-bridge；type=module；exports；dsh.bundle.patch
├── cordis.patch.yml        # insert id=esp32-touch-bridge, name=dsh-esp32-touch-bridge
├── lib/
│   ├── index.js            # Cordis 入口：export name / apply(ctx, config)
│   ├── state.js            # 纯归约：DSH 事件 -> { mode, sessions, cards }
│   ├── protocol.js         # 纯编码：状态 -> C05 NDJSON（转义/63/7/512B/非法值拒绝）
│   └── serial-helper.js    # 内部 helper 子进程生命周期 + JSON-lines 协议
├── helper/
│   └── serial-bridge.ps1   # 内部辅助进程（Windows PowerShell 5.1，见下）
├── locale/{zh,en}.json     # 插件管理器展示元信息
├── icon.svg                # 展示图标
├── test/                   # node --test（无第三方依赖）
├── README.md
└── LICENSE                 # MIT
```

## 安装

在 DSH 安装目录可用的终端里（`dsh` 在 PATH）：

```powershell
# 本地目录安装到某个 profile（本仓库默认说明用 desktop；测试请用临时 profile）
dsh plugin --profile desktop add D:\DSH\ESP32-touch\host-plugin

# 确认已加入 bundle 列表
Get-Content $env:USERPROFILE\.dsh\profiles\desktop\package.json

# 查看合成后的配置树里出现插件行（无需启动 GUI）
dsh --profile desktop --dump-config | Select-String esp32-touch-bridge
```

也可以走 Web 侧栏「插件 → 添加插件」，用本地绝对路径安装。安装后**重启 DSH** 生效。
插件不自己改 profile 文件、不调用 pnpm（遵循 DSH 插件规范）。

## 配置

安装后的 bundle 行在 `cordis.patch.yml` 的 `insert` 中；用 `config` 指定串口：

```yaml
- insert:
    - id: esp32-touch-bridge
      name: dsh-esp32-touch-bridge
      config:
        port: COM7          # 必填（或用环境变量 ESP32_TOUCH_PORT）
        baud: 115200        # 可选，默认 115200（USB CDC 对波特率不敏感）
        heartbeatMs: 10000  # 可选，默认 10000（必须 < 15000，否则设备判 OFFLINE）
```

- **未配置 `port` / `ESP32_TOUCH_PORT` 时：插件只加载、不连设备、不 spawn helper、不写任何文件。**
- v1 **不做 COM 口自动发现**：不支持 `port: auto`，请显式填写（写成 `auto` 会被当作未配置并告警）。
- 环境变量方式：`ESP32_TOUCH_PORT=COM7`（对插件生效；不需要改 profile 文件）。
  注意它必须出现在**启动 DSH 的那个进程环境**里——GUI 直接启动的 DSH 读不到你临时 Shell 里的
  变量；最稳妥的做法仍是写进 bundle 的 `config.port`。

## 事件映射（DSH → C05）

| DSH 事件 | 观察字段 | 发射 |
| --- | --- | --- |
| `session/created` / `session/disposed` | `session.id`、`header.cwd`、`header.createdAt` | 卡片栈刷新（`device.heartbeat`，不改状态） |
| `session/event` → `session/title` | `event.data.title` | 卡片标题 / 卡片栈刷新 |
| `session/event` → `approval/asked` / `approval/decided` | `event.data.id` | `agent.input_required` / 恢复 `task.start` |
| `agent/status` | `{agent, status:'running'\|'idle'}` | 进入 WORKING：`task.start`；最后一次 running→idle：`agent.idle` |
| `agent/error` | `{agent, turn, step, error}` | `task.error` |
| `user-questions/request`（waterfall） | `{agent, questions, signal}` | `agent.input_required`；**始终 `next()` 委派，不改变审批结果** |
| 心跳（10 s） | — | `device.heartbeat`（附当前卡片栈） |

归约目标态：任一 pending（审批/提问）→ `INPUT_REQUIRED`；否则任一 running → `WORKING`；否则 `IDLE`。
卡片栈最多 3 张，按「有 pending > running > 最近创建」排序；每行附带顶层单卡字段
`title/project/agent/elapsed_s`。所有 emission 串行化（单写者），行 ≤ 512 B，
`title/project` 63 字符、`agent` 7 字符截断（按码点），非法值整行丢弃并记日志。

## 内部辅助进程（必须声明）

> 本节按交付要求**明示**：插件内部存在一个辅助进程。

- **存在与用途**：`helper/serial-bridge.ps1` 是一个 **Windows PowerShell 5.1** 子进程，用
  `[System.IO.Ports.SerialPort]` 打开 `COMx` 并读写字节。需要它是因为 Node.js 没有内置串口 API
  （见 `docs/12-dsh-plugin.md` §4）。
- **生命周期**：插件加载后，**首次需要写设备时自动拉起**；插件卸载 / 宿主退出时自动回收。
  宿主进程若被强制结束，helper 会因 **stdin 关闭而自行退出**，不留残留进程。
- **重连**：端口打不开或读写失败时，helper 每 **2 s** 重试打开；插件在重连后**重发当前快照**
  （level-triggered），因此短暂掉线后设备状态会自行追平。
- **用户无需、也不应手动运行它**：本包**没有** `bin` 入口、没有启动脚本、本文档也不给出启动命令；
  手动启动既无必要，也会与插件自身实例抢串口。

行协议（stdin/stdout JSON lines，设备字节用 base64）：

```text
插件 -> helper: {"op":"open","port":"COM7","baud":115200}
                {"op":"write","data":"<base64>"}  {"op":"close"}  {"op":"ping"}
helper -> 插件: {"op":"ready"}  {"op":"line","data":"<base64 设备日志>"}
                {"op":"state","connected":true|false}  {"op":"error","message":"..."}
```

## 隐私与安全

- 插件**只观察** DSH 事件：不注入提示词、不调用工具、不审批、不写会话/工作区文件。
- 只向本机串口写 C05 允许字段；**不发送对话正文、工具输出或工具参数**。
- `title` 来自 DSH 的会话标题（可能由用户首条消息自动摘要生成）→ 请把设备视为
  **“状态显示器”**；如果标题内容敏感，可在 DSH 里重命名或让插件不显示标题（后续可配置）。
- 设备 → host 方向 v1 只把人类可读日志转发到 DSH 日志，**不解析控制指令**；本机 USB 通道按可信处理。
- 不连接网络、不访问注册表、不写 profile / 工作区文件。

## 已知限制（诚实声明）

- **仅 Windows**：helper 依赖 Windows PowerShell 5.1 与 `System.IO.Ports`；macOS/Linux 未提供 helper。
- v1 仅 USB 串口通道：WiFi TCP / BLE、配网、NTP 属 M5b，不在本包内。
- v1 **不做 COM 口自动发现**；端口必须显式配置。
- **空卡栈清屏**：C05 §4 规定 `tasks: []` 清空卡栈；M5a 固件已修（`pet_link_msg_t.has_tasks`，
  空数组也触发整栈替换/清空）。设备 H 待验（本包按契约发送 `tasks: []`）。
- **设备 H 尚未完成**：真实 ESP32 板（Waveshare）与真串口上的 6 态/3 卡/心跳/OFFLINE 恢复
  由项目 H 类验收统一执行；本包内 `test/e2e-device.mjs` 已就绪但默认跳过。
- **真 DSH E2E 需重启**：装到 profile 后要重启 DSH 才生效；本会话只验证了安装/配置合成。
- `elapsed_s` 由会话创建时间推算，DSH 进程重启或恢复会话时可能从较大值开始。

## 测试

```powershell
cd D:\DSH\ESP32-touch\host-plugin
node --test        # 单元/协议/装配/helper 行协议 + PowerShell helper 冒烟（无设备）
```

覆盖：`state.js` 归约（多会话/审批/提问/标题/错误/边沿/排序/截断/elapsed）、`protocol.js` 与
C05 逐字段（版本/事件/转义/63/7/512B/tasks≤3/非法值拒绝）、`applyWith` fake ctx/transport 装配
（含未配置 port 零副作用、waterfall 恒 `next()`、断线重放）、`serial-helper.js` fake
child_process 行协议与 2 s 重启，以及真实 PowerShell helper 的 ready/坏端口/关闭 stdin 退出。

### 真机 E2E（H 类，默认跳过）

```powershell
$env:ESP32_TOUCH_PORT = 'COM7'    # 你的设备端口；不要用被其他程序占用的口
$env:ESP32_TOUCH_E2E  = '1'
node test/e2e-device.mjs          # 发 6 个事件 + 3 张卡，打印设备回传日志
```

未设置 `ESP32_TOUCH_E2E=1` 或 `ESP32_TOUCH_PORT` 时脚本立即打印 skip 并退出 0。

## 卸载

```powershell
dsh plugin --profile desktop remove dsh-esp32-touch-bridge
# 重启 DSH 生效；helper 子进程随插件卸载/宿主退出自动回收
```

## 安装验证证据

`test/console/install-verify.txt` 记录了在临时 profile `touch-test` 上执行
`dsh plugin --profile touch-test add D:\DSH\ESP32-touch\host-plugin` 的原始输出，以及
`dsh --profile touch-test --dump-config` 中出现 `esp32-touch-bridge` 行的关键行。
（临时 profile 不影响 desktop / web 既有配置。）

## License

MIT，可见 `LICENSE`。与 `ESP32-touch` 仓库一致。