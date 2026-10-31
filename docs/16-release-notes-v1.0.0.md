# v1.0.0 发布说明（2026-10-31）

> 仓库：https://github.com/whoisjackchao-dot/ESP32-touch
> 提交：`76b7b6a`（发布前收尾完成）；标签：`v1.0.0`
> 许可：MIT（派生自上游 `ESP32-buddy` MIT；`es8311` 组件 Apache-2.0；字体 OFL 1.1；语音资产来源见 NOTICE）

## 1. 这是什么

**ESP32-S3 触摸桌面宠物 × DeepSeek Harness (DSH) 状态显示器**。
DSH 会话/Agent 状态（工作、等待确认、完成、失败）通过 USB 串口实时同步到 240×240 小屏：
6 态宠物动画 + 3 张任务卡；需要确认/完成/失败时中文语音播报；支持静音（待机页图标可见、跨重启保持）。

**宿主集成是 DSH 插件标准包**：`host-plugin/`，可用 `dsh plugin add` 安装，不依赖自研 Python 桥。

## 2. 硬件

| 项 | 值 |
| --- | --- |
| 板卡 | **Waveshare ESP32-S3-Touch-LCD-1.54**（SKU 33869） |
| 屏 | ST7789 240×240（SPI2 40 MHz，RGB565 LE） |
| 音频 | ES8311 @ I2C 0x18 + NS4150B（PA=GPIO7）；16 kHz 单声道 |
| 按键 | BOOT/KEY1=GPIO0，PLUS/KEY2=GPIO4，PWR=GPIO5（低有效）；**无独立 RST 键** |
| 连接 | 原生 USB-Serial-JTAG（Windows 枚举为 COMx） |

## 3. 功能（v1.0.0）

- **值班页**：6 态宠物动画（IDLE/WORKING/INPUT_REQUIRED/DONE/FAILED/OFFLINE）、状态标记 `>`/`!?`、
  网络点（绿/红）、3 卡栈（会话标题/项目/DSH 前缀/时长）、**静音图标**。
- **键与菜单**：KEY1 短按（发送占位）、长按静音；KEY2 短按切换值班/番茄钟、长按菜单；
  PWR 长按关机流程（USB 供电下黑屏，需拔插 USB/串口硬复位恢复）。
- **番茄钟**：25/5，开始/暂停/继续/放弃，NVS 统计（跨日/重启保持）。
- **语音**：需要确认/完成/失败各播一次；静音不播；静音跨重启保持；异步播放不阻塞 UI。
- **DSH 插件 `dsh-esp32-touch-bridge`**：
  - 订阅 `session/created|disposed`、`session/event`（标题/审批）、`agent/status|error`、
    `user-questions/request`（只观察，恒 `next()` 委派，不审批）；
  - 归约为 C05 NDJSON 事件，经 PowerShell 串口 helper 写设备；10 s 心跳；断线重连；
  - 未配置端口时零副作用（不 spawn helper）。

## 4. 安装/升级

### 4.1 固件

```powershell
# 方式 A：ESP-IDF v5.5.4
$env:IDF_TOOLS_PATH='D:\Espressif'; $env:IDF_PATH='D:\Espressif\frameworks\esp-idf-v5.5.4'
. "$env:IDF_PATH\export.ps1"; cd ESP32-touch
idf.py -B D:\DSH\esp-build\buddy154 -DIDF_TARGET=esp32s3 build
idf.py -B D:\DSH\esp-build\buddy154 -p COM7 flash

# 方式 B：Release 附件整包镜像（偏移 0x0；factory app 在 0x20000）
esptool.py --chip esp32s3 -p COM7 -b 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 esp32-touch-v1.0.0-full.bin
```

> 手工刷 app 时注意：factory app 偏移 **0x20000**（不要用 0x10000）。

### 4.2 DSH 插件

```powershell
dsh plugin --profile desktop add <仓库路径>\host-plugin
# 在 desktop/cordis.patch.yml 给该行加 config:
#   - id: esp32-touch-bridge
#     config: { port: COM7 }
# 或设环境变量 ESP32_TOUCH_PORT=COM7；重启 DSH 生效
```

## 5. 验证摘要（本机实测，2026-10-31）

| 项 | 结果 |
| --- | --- |
| 宿主单测 | M2 15/231、M3 21/517、M5a 29/436、M4 28/28 断言 PASS；插件 56/56 |
| G1 显示 | 24/24（用户认可 24 次门槛） |
| G5 番茄钟 | 17/17 |
| **G6 按键** | 13 项×20 = **260/260**，布局 9/9 |
| **G9 语音** | **60/60 有声 + 60/60 静音跳过** |
| 真机 H | 6 态/卡片、语音三声、静音图标、KEY 短按/长按、PWR 长按、FRM-01 无撕裂 — 用户确认 |
| DSH E2E | 真实子代理任务 → 设备 WORKING + 会话标题卡片 + 绿点；重启后自动加载 |
| 构建 | app `0xc46c0`（804,544 B，74% free，0 warning）；**全新 clone 构建 PASS** |

## 6. 已知限制

- 按键 H 为 `Q11` 缩减样本（短按/长按各 3 次）；正式 20 次/项未跑（合成键 G6 已 20/20）。
- 插件仅 **Windows**（PowerShell 5.1 helper + `System.IO.Ports`）；v1 仅 USB 通道。
- **WiFi / BLE / 配网 / NTP 未包含**（M5b，后续版本）。
- 麦克风/AEC/触摸/IMU/OTA/电池显示未包含（后置）。
- 设备 PWR 长按进入关机后需拔插 USB 或串口硬复位（板上无 RST 键）。
- 卡片标题来自 DSH 自动会话标题（首条消息摘要），设备仅接收状态/标题/项目/时长，不收对话正文。

## 7. 下一步（M5b 计划）

WiFi TCP 8766 事件入口 + NTP 校时 + 配网（AP 网页）→ BLE GATT 通道 → 跨通道去重；
语音转写（`Q08`）与 AEC（`Q09`）在 M5b 相关部分前决策。

## 8. 许可与来源

- 派生自 **ESP32-buddy**（MIT，© 2026 whoisjackchao-dot）；派生文件清单见 `firmware/components/pet_ui/NOTICE.md`。
- `firmware/components/es8311/`：Espressif esp-bsp **Apache-2.0**（保留 LICENSE）。
- 字体 `cjk16.h`：OFL 1.1；语音资产由 Edge TTS 合成（再分发条款见 NOTICE）。
- 无 GPL 代码。
