# M5a 交付说明：DSH 插件 `dsh-esp32-touch-bridge`（2026-10-31）

> 用户新增关键要求（2026-10-31）：**接入对象是 DeepSeek Harness (DSH)**，最终交付必须是**符合 DSH 插件标准**的包。
> 契约：`contracts/C05-event-protocol.md`（设备事件协议，冻结）、`docs/12-dsh-plugin.md`（插件交付契约）。
> 证据：`E28`（设备侧构建）、`E29`（C05 契约测试）、`host-plugin/test/console/`（插件测试/安装证据）。

## 1. 交付物

| 交付 | 位置 | 说明 |
| --- | --- | --- |
| DSH bundle 插件 | [`host-plugin/`](../host-plugin/) | `dsh-esp32-touch-bridge@0.1.0`；纯 ESM、**零第三方依赖、无构建**；`dsh.bundle.patch` → `cordis.patch.yml` → `id: esp32-touch-bridge` |
| 设备侧事件入口 | `firmware/components/pet_ui/{pet_linebuf,pet_link}.c` + `main.c` console 任务 | C05 NDJSON 解析（纯逻辑）+ 主任务应用；与 M2/M3 调试命令共存 |
| 协议契约 | `docs/contracts/C05-event-protocol.md` | USB-Serial-JTAG 单向 NDJSON；6 事件 + `tasks` 3 卡 + 心跳；§9 实现对齐记录 |
| 插件契约 | `docs/12-dsh-plugin.md` | DSH 标准、事件映射、helper 声明、安装/验证、隐私 |
| 设备 H 脚本 | `tools/host/pet_event.py` | 6 事件/3 卡/心跳/非法输入/OFFLINE 恢复 + PNG 证据（设备在线时跑） |
| 插件测试 | `host-plugin/test/*.test.mjs` | 56/56 PASS；含真实 PowerShell helper 冒烟 |
| 安装证据 | `host-plugin/test/console/install-verify.txt` | 临时 profile `touch-test` 安装 + `--dump-config` 出现插件行 |

## 2. 架构

```text
DSH 事件（session/created|disposed, session/event: title/approval,
          agent/status|error, user-questions/request）
        │  host-plugin/lib/state.js（纯归约：mode/sessions/cards）
        ▼
host-plugin/lib/protocol.js（C05 NDJSON：版本/转义/截断/tasks≤3/512B）
        │  单写者、变更触发 + 10 s 心跳、断线重连后按当前快照重放
        ▼
host-plugin/lib/serial-helper.js ── JSON-lines/base64 ──► helper/serial-bridge.ps1
        │                                              （PowerShell 5.1, System.IO.Ports, 2 s 重连）
        ▼
COMx（USB-Serial-JTAG）── C05 NDJSON ──► 设备 pet_linebuf → pet_link → pet_state/卡栈
```

- 设备**只在主任务**改状态（console 任务解析后入队），单写者，无锁竞争。
- 插件未配置 `port`/`ESP32_TOUCH_PORT` 时**只加载、不 spawn helper、不开串口**（零副作用）。
- 审批/提问只观察：`user-questions/request` waterfall 恒 `next()`；审批用会话日志
  `approval/asked|decided` 观察，不改变任何审批结果。

## 3. 安装与启用（用户操作）

```powershell
# 1) 安装到 desktop profile（本地目录；也可在 Web「插件 → 添加插件」里选本地路径）
dsh plugin --profile desktop add D:\DSH\ESP32-touch\host-plugin
# 2) 在 desktop 的 cordis.patch.yml 里给插件行加 config.port（或设环境变量 ESP32_TOUCH_PORT）
#    例：- id: esp32-touch-bridge
#        name: dsh-esp32-touch-bridge
#        config: { port: COM7 }
# 3) 重启 DSH（首次安装必须重启）→ 设备进入正常状态显示
# 卸载：dsh plugin --profile desktop remove dsh-esp32-touch-bridge
```

- 设备侧：烧录 M5a 固件（`idf.py -B D:\DSH\esp-build\buddy154 -p <COMx> flash`）；启动日志
  `m5a: READY ... C05 USB events`。
- **本机已按此安装**（2026-10-31，`E32`）：desktop profile 已 link 本包 + bundles 追加 +
  `config.port: COM7`；备份在 `D:\DSH\esp-build\profile-backup-desktop-20261031-101527`。
  桌面 profile `patchReload: live` 使插件**即时激活**（helper 已持有 COM7）；重启 DSH 后做真实会话 E2E。
- 端口以 Windows 设备管理器实际枚举为准（本机当前未连接 Waveshare 板；`COM5` 是另一台 StickS3）。

## 4. 已验证（本机，2026-10-31）

| 项 | 结果 | 证据 |
| --- | --- | --- |
| 设备侧构建 | 0 warning，app `0x8cdc0`（576,960 B，82% free） | `E28`/本文件 |
| C05 解析 C 测试 | **29/29 用例、436 断言 PASS**（linebuf + link：事件/agent/tasks/`has_tasks`/错误/超长/非法） | `E29`、`tests/RED-GREEN-M5A.md` |
| C05 设备事件入口 H | **16/16 PASS**：WORKING 延迟 **0.165 s**、3 卡 top-first、`tasks:[]` 清栈、6 态切换、非法输入不崩、15 s OFFLINE/恢复；帧 CRC 一致 | `E30`、`tools/host/evidence/m5a/` |
| 插件→真机路径 H | **PASS**：真实插件代码（fake ctx + 真实串口 helper）驱动真机，CJK 标题/`DSH` 前缀/审批 INPUT_REQUIRED 上屏 | `E31`、`tools/host/evidence/m5a/plugin_device_e2e.png` |
| 插件 `node --test` | **56/56 PASS**（state/protocol/apply/helper/fake + PowerShell helper 冒烟） | `host-plugin/test/console/node-test.txt` |
| 插件安装 | 临时 profile `touch-test`：`dsh plugin add` exit 0；`--dump-config` 出现 `id: esp32-touch-bridge`；卸载/re-add 干净 | `host-plugin/test/console/install-verify.txt` |
| M2/M3 回归 | 15/15 + 21/21 用例 PASS（宿主测试） | `tests/*.log` |

## 5. 未完成（PENDING，需用户/设备）

| 项 | 阻塞条件 | 解除动作 |
| --- | --- | --- |
| `E2E-01` 真实 DSH 会话 | 需安装到 desktop + **重启 DSH GUI**（本会话进程内无法重启） | 用户确认后 `dsh plugin --profile desktop add .../host-plugin` + 配 `config.port` + 重启；真实发消息/触发审批，看设备 ≤1 s 变化 |
| `KEY-01/02/05` 真机按压 | 同设备未连接（M3 PENDING） | 设备重连后跑 `keyprobe.py` + `keyh_capture.py` |

## 6. 已知限制

- v1 仅 **Windows + USB 串口**；WiFi/BLE/配网/NTP 属 M5b（`R21` 其他通道保留，不取消）。
- 不自动发现 COM 口（用户显式配置）。
- 设备→host v1 无协议应答；插件以“写串口成功”为连接判据，断线重连后按当前快照重放。
- 卡片 `elapsed_s` 由会话创建时间推算；DSH 重启/恢复会话时可能从较大值开始。
- `title` 来自 DSH 会话标题（可能由用户首条消息摘要生成）→ 设备视为状态显示器；README 已声明。

## 7. 下一节点

本节点出口（设备 H + 真实 DSH E2E）闭合后：按 `05-plan.md` 顺序进入 **M4 语音播报**（`C04`），
再 M5b（WiFi/BLE/配网/NTP）。



