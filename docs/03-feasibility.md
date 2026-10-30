# esp32-buddy-touch154：可行性核查与证据记录（阶段 0）

> 日期：2026-10-30；版本：v0.1
> **结论范围：源码阅读 / 官方原理图 / 本机隔离检查，不是端到端验收。**
> 本轮没有编写固件代码、没有安装 ESP-IDF、没有构建、没有烧录。
> 所有"上游有某功能"均指**源码层面存在**，不等于在本项目目标板上可用。

## 1. 证据分级

| 标记 | 含义 | 本轮实例 |
| --- | --- | --- |
| **实测** | 本次实际运行命令得到的输出 | `host/run-tests.sh` → 77 tests OK（子代理在独立 clone 执行）；`make-manifest.py --check` → 一致；`gh` 不存在、GitHub 匿名 API 限流 |
| **类型/源码** | 读了固定提交的源码，未运行该功能 | 两个上游的引脚、分区表、功能模块、协议 |
| **官方文档** | 厂商官方 wiki / 原理图 / 数据手册 | Waveshare 1.54 原理图与 wiki；ESP32-S3 数据手册 |
| **拟定设计** | 本项目新规则 | 分层方案、里程碑切分、验收口径 |
| **未验证** | 需要实物/账号/构建才能确认 | 触摸芯片变体、面板 SPI 模式、ES7210 通道映射、IMU 行为阈值 |

**上游提交锚点**

| 仓库 | 提交 | 说明 |
| --- | --- | --- |
| `second-state/vibebuddy` | `107bc152520f9f954d13971088c7faee75a7a224` | `main`，2026-10-30 |
| `whoisjackchao-dot/ESP32-buddy` | `f0d182ee52594f9a75a8f1893e2dfddf17ee11dc` | `main`，2026-10-30；本机 `/home/home/ESP32-buddy` 与之同步 |
| Waveshare 1.54 demo | `5157db7c888e476fd57f8a95020800377447478f` | 官方 demo 仓库 |

> **隔离实验（子代理在独立 clone 中执行）是历史特征证据，不得冒充本项目的产品测试。**

---

## 2. 环境基线

| 项 | 值 | 证据 |
| --- | --- | --- |
| 主机 | Linux（WSL 形态），工作目录 `/home/home` | 实测 |
| ESP-IDF | **未安装**：`~/esp` 下无 `esp-idf`，`idf.py` 不在 PATH | 实测 |
| 交叉工具链 | 无 `~/.espressif` | 实测 |
| 本机 C 编译器 | **无** `cc` / `gcc` / `clang` / `zig` | 实测 |
| Git | 2.53.0 | 实测 |
| 磁盘 | 951 GB 可用 | 实测 |
| 串口 | **设备已接在 Windows 侧 `COM7`**（`VID_303A:PID_1001`，原生 USB-Serial/JTAG）；WSL 内**不可见** `/dev/ttyACM*`，需经 `powershell.exe` 调 Windows esptool/pyserial | 实测（2026-10-30） |
| 构建环境 | **ESP-IDF v5.5.4 可用**：`idf.py --version` / xtensa-esp-elf 14.2.0 / cmake 3.30.2 / ninja 1.12.1；用户态 Python 3.13；openocd 用 local libusb | 实测（`07-evidence-log.md` E11） |

> **本基线不能证明**：能构建本项目固件、能烧写、能读触摸/IMU 寄存器。
> **基线不构成性能承诺。**
>
> 现有能力边界（2026-10-30 更新）：
> - **可做**：经 Windows esptool 读芯片/Flash/分区/App 描述；经 Windows pyserial 抓启动日志。→ **已产出 E06–E09 实测证据**。
> - **不可做（尚未）**：在 WSL 内直接构建（IDF 安装中）；直接刷写（未授权 `Q10`）；I2C 主动探测（需烧探针固件）。

---

## 3. 上游 A：`whoisjackchao-dot/ESP32-buddy`（MIT，C/ESP-IDF）

### 3.1 定位

- 给 AI 编码 Agent 的**桌面宠物**：6 态动画、任务卡 ≤3、番茄钟、休闲小剧、语音播报、按住说话 → 智谱 ASR → 文本注入宿主。
- **净室重实现**：借鉴 vibebuddy 的设计/事件模型，**不搬 Rust 代码**；代码 MIT，美术原创 MIT。
- 目标硬件 **M5Stack StickS3**：ESP32-S3，135×240 ST7789（SPI），ES8311，M5PM1 PMIC，2 键。
- 当前状态（以 `ESP32-buddy_文档索引.md` 为准，README 已漂移）：M1–M9 + M11–M17 已做，tag `v1.0.0`–`v1.6.0`。

### 3.2 与目标板的差异（决定性）

| 维度 | ESP32-buddy（StickS3） | 目标板（1.54"） | 影响 |
| --- | --- | --- | --- |
| 屏幕 | 135×240 SPI，gap(52,40)，反色开 | 240×240 SPI | **版式全部重做**，不是平移 |
| 触摸 | **无** | CST816S @0x15 | 全新增 |
| IMU | **无** | QMI8658 @0x6B | 全新增 |
| 按键 | 2 键（GPIO11/12，低有效） | 3 键（GPIO0/4/5，低有效；PWR 参与电源锁存） | 键位与语义重定 |
| 音频输入 | ES8311 内置 ADC（模拟麦） | **ES7210 4 通道 ADC** + ES8311（ES8311 仅 DAC） | **PTT 采集路径必须重写** |
| 音频输出 | ES8311 + M5PM1 功放使能 | ES8311 + NS4150B（GPIO7 使能） | 功放控制改 GPIO |
| 电源/电池 | M5PM1 寄存器 | ADC GPIO1 + CHG_STAT GPIO3 + **BAT_EN GPIO2** | PMIC 层替换 |
| Flash | 8 MB，factory 2 MB，**无 OTA** | 16 MB | 可上双槽 OTA |
| PSRAM | 8 MB octal | 8 MB octal | 一致 |

### 3.3 可直接复用 / 需改 / 需换

| 分类 | 模块 | 依据 |
| --- | --- | --- |
| **原样复用（纯逻辑）** | `pet_state` `pet_link` `pet_linebuf` `pet_button` `pet_keymap` `pet_menu` `pet_pomo` `pet_leisure` `pet_voice` `pet_text`、宿主协议模块 | 无硬件依赖，可 host 单测 |
| **轻改** | `pet_rx` `pet_evt_tcp` `pet_prov` `pet_net` `pet_ble` `pet_ptt` | 协议/网络可移植；改名称、端口、AP 前缀、ASR 端点 |
| **重改** | `pet_render` `pet_engine` `pet_audio` `pet_keys` | 135×240 坐标、2 键、ES8311 单编解码 |
| **替换** | `pet_hal_sticks3.c` `pm1.c/.h`、显示启动 | M5PM1 电源时序与 StickS3 面板 |
| **新增** | CST816S、QMI8658、第三键、ES7210/AEC、240×240 版式、OTA | 上游完全没有 |

### 3.4 关键事实（供阶段 2 使用）

- **HAL 并不真的是唯一硬件层**：`pet_hal_t` 只覆盖 `now_ms/present/set_backlight/play_sound/audio_busy/panel_*`；`pet_keys` 直接 `gpio_get_level`，`pet_engine` 直接调 `pm1_battery()` 并写 USB-Serial-JTAG，`pet_audio` 直接配 I2S/ES8311，`pet_net` 直接配 WiFi。**移植时必须扩展 HAL 或引入 board 接口**。
- **显示缓冲在内部 RAM**：`s_panel_buf = 135×240×2 = 64,800 B`，引擎帧缓冲再 ~64,800 B。目标板 240×240 每个 **115,200 B**，合计 230 KB → 必须决定是否放 PSRAM。
- **`pet_pomo_set_day()` 无调用者**："当日统计"实际从不跨日清零。
- **`BACK: WINDOW` 在默认宿主栈中无人消费**：只有遗留 `pet_daemon.py` 处理，默认 supervisor 跑的是 `file_to_serial.py`。
- **app 分区仅剩 ~301 KB**（1,788,832 / 2,097,152）；加触摸/IMU/ES7210/OTA 必然超。16 MB flash 必须重划分区。
- **无 OTA**：只有 `factory`，无 `ota_0/ota_1/otadata`，无回滚。
- **安全现状**：配网 AP 开放；事件 TCP 8766 与宿主控制 8767 无鉴权；BLE Just Works；NVS 未加密。授权边界必须写进需求。

---

## 4. 上游 B：`second-state/vibebuddy`（GPL-3.0，Rust + 宿主 daemon）

### 4.1 定位

**不是单机固件**，是五件套：ESP32-S3 固件 + 本机 daemon `vibebuddyd` + macOS App + Linux App + Agent hooks。
设备只是**瘦终端**：聚合、LLM、STT/TTS、路由都在宿主。没有 daemon 时设备显示 `NO LINK`。

### 4.2 硬件（与目标板完全不同）

- 板卡：**ALIENTEK ATK-DNESP32S3-BOX V1.1**，N16R8，16 MB flash + 8 MB octal PSRAM。
- 显示：ST7789 **320×240，8-bit i80 并口**，10 MHz，无复位脚，反色，MADCTL 0x60。
- 触摸：**CHSC5432 @0x2E**（可选，32 位寄存器）。
- 音频：ES8311 @0x18，24 kHz，无 MCLK；**麦克风走 ES8311 ADC**，DIN=GPIO47；**无 ES7210、无 AEC**。
- 按键：K0=GPIO0，K1/K2 经 **XL9555 扩展芯片**（P0.4/P0.3）；背光 P0.7、功放 P0.5。
- IMU：**无驱动**；仅 `docs/hardware.md` 记录 0x6A "QMI8658 (unconfirmed)"。
- 电池：**无**；LED：**无**；深度睡眠：**无**。

### 4.3 可借鉴的能力（设计层面，非代码）

| 能力 | 价值 | 许可注意 |
| --- | --- | --- |
| 任务卡分页（3 张/页，8 s 自动翻页）+ Agent 图标 | 240×240 上比 3 行紧凑卡更可读 | 设计可借鉴 |
| 设备菜单（VOLUME/MUTE/COMPUTERS/NETWORK/STATUS/STOP FOCUS） | 比现有 5 项菜单更完整 | |
| 触摸手势（点卡、竖滑翻页、摸头） | 目标板正好有 CST816S | |
| Character/voice pack（VBCP v1/v2，48×64 4bpp look + ADPCM 语音） | 可换角色/语音，16 MB flash 放得下 | **代码 GPL；格式可重新实现** |
| OTA：分块、SHA-256、写非活动槽、回滚确认 | 目标板 16 MB 正好支持 | |
| 分页 + 统计（done/asks/busy seconds） | 比现有只统计番茄钟更完整 | |
| 语音回复 MP3 解码（nanomp3，24 kHz mono） | 8 MB PSRAM 可做 | |
| 配对/Noise/Relay | 远程链路，本期大概率超范围 | |

### 4.4 许可结论（重要）

- vibebuddy **代码 GPL-3.0-or-later**，角色/动画以代码形式存在 → 复制即传染。
- `whoisjackchao-dot/ESP32-buddy` 之所以是 MIT，正是因为它**净室重实现**。
- **本项目若要保持 MIT（推荐），只能借鉴 vibebuddy 的设计与协议思路，不能复制其源码/素材。** 该结论写入 `Q02/Q07`。

---

## 5. 目标板基线（官方原理图级）

> **型号为推断**：用户描述与 **Waveshare ESP32-S3-Touch-LCD-1.54（SKU 33869）** 完全吻合。非触摸兄弟型号 33866/33867 同 PCB。
> 来源：官方 wiki + 原理图 PDF（`files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.54/ESP32-S3-LCD-1.54-Schematic.pdf`）。
> **本基线不替代实物核对**；下单型号/丝印/CST816 芯片 ID 仍需确认（`B01`）。

### 5.1 引脚（原理图自带 GPIO→网络表，与官方 demo 三处代码一致）

| 功能 | 信号 | GPIO | 备注 |
| --- | --- | --- | --- |
| LCD | SCLK / MOSI / CS / DC / RST / BL | **38 / 39 / 21 / 45 / 40 / 46** | 4 线 SPI；BL 为 LEDC 5 kHz 10-bit，高有效 |
| 触摸 | SDA / SCL / INT / RST | **42 / 41 / 48 / 47** | 与 LCD 复位**不共用**；INT/RST 低有效 |
| IMU | QMI8658 | 同 I2C 41/42，**0x6B**；INT=GPIO6 | |
| 音频 | MCLK / BCLK / WS / ESP→ES8311 / ES7210→ESP / PA | **8 / 9 / 10 / 12 / 11 / 7** | 共享 I2S；PA 高有效 |
| 按键 | BOOT(MINUS) / PLUS / PWR | **0 / 4 / 5** | 均低有效，10K 上拉 |
| 电池 | BAT_ADC / BAT_EN / CHG_STAT | **1 / 2 / 3** | ADC 分压 200K 上 / 100K 下 → ×3；**GPIO2 必须拉高否则松手掉电** |
| USB / UART | D−/D+ / TX-RX | 19/20 / 43/44 | 原生 USB，无 CH34x |
| TF 卡 | CLK/CMD/D0–D3 | 16/15/17/18/13/14 | 4-bit SDMMC |

### 5.2 器件

| 项 | 值 |
| --- | --- |
| 模组 | ESP32-S3R8（8 MB **octal** PSRAM，80 MHz max） |
| Flash | 16 MB（原理图 `W25Q128JVSIQ`；**实物 JEDEC `20 4018` = Micron**，厂牌不符、容量一致，见 `B19`） |
| 屏 | 1.54"，**240×240**，ST7789/ST7789V2，RGB565，**需要反色** |
| 触摸 | CST816 家族，I2C **0x15**（S/T/D 变体未定） |
| 音频 | ES8311 @**0x18**（仅 DAC）+ **ES7210 @0x40**（4 通道 ADC，双模拟麦） |
| 功放 | NS4150B，使能 GPIO7 |
| 充电 | ETA6098；充电状态 GPIO3 低有效 |
| RTC | **PCF85063A 存在**：出厂固件日志 `PCF85063A initialized successfully`（2026-10-30 实测）。**与原理图文本"无 PCF85063"冲突**，冲突已登记 `B18`；在拿到地址与备份电池事实前，不把它写进 v1 依赖 |

### 5.3 音频拓扑（决定 AEC 可行性）

- 两颗模拟麦 → **ES7210** 差分输入（MIC1P/N、MIC2P/N）；ES8311 的 MIC 脚接地。
- ES7210 只是 **4 通道 ADC，不含 AEC DSP**。
- 结论：**有"支持软件 AEC 的硬件通路"（双麦），没有"硬件 AEC"**。要做回声消除必须上 **ESP-SR AFE/AEC**，吃 CPU/RAM。
- **主麦/参考麦对应哪个通道：未验证**（`B05`）。
- **实机补充（2026-10-30）**：出厂固件日志显示 ES7210 `Enable ES7210_INPUT_MIC1` 与 `MIC2`、`Unmuted`；I2S 为 **24 kHz / 16-bit / 2 ch**，ES8311 与 ES7210 均为 Slave。→ 双麦通路真实存在，采样率与上游 `ESP32-buddy`（16 kHz）不同，需在契约中定。证据：`07-evidence-log.md` E07。

### 5.4 移植风险清单（物理级）

| 风险 | 说明 |
| --- | --- |
| **GPIO2 电源锁存** | 不在启动早期拉高，电池供电时松开 PWR 即断电。这是最可能的"上电即死" |
| **PWR 键不是普通键** | 长按触发关机（GPIO2 拉低），不能当第三个 UI 键随意用 |
| **GPIO45/46 是 strapping** | 45=LCD_DC（VDD_SPI 选择），46=LCD_BL（启动模式）；复位时不得被外部强上拉 |
| **GPIO0 是 BOOT** | BOOT 键即 MINUS 键；按住复位进下载模式 |
| **JTAG 被占用** | 39/40/41/42 = MTCK/MTDO/MTDI/MTMS，已用于 LCD/I2C；只能用 USB-Serial-JTAG |
| **SPI 模式不一致** | 官方 factory 用 mode 3，LVGL 例程用 mode 0；wiki 说 mode 0。必须实测选定 |
| **06_esp-brookesia 是错的 BSP** | 那是 1.85C-V2 的（ST77916 + TCA9554 + PCF85063），**不得作为 1.54 参考** |
| **RTC 存在但未验** | 实机日志有 `PCF85063A initialized`，但地址/备份电池/走时未验（`B18`）；v1 夜间规则仍只用 NTP/宿主时间，不依赖 RTC |

---

### 5.5 实机核对结果（2026-10-30，只读）

> 方法：Windows `esptool` + `pyserial`；未烧写、未改分区。详见 `07-evidence-log.md` E06–E09。
> **这是"板子存在且这些器件应答"的证据，不是本项目固件的验收证据。**

| 用户声明 | 实测 | 判定 |
| --- | --- | --- |
| ESP32-S3 | `Chip is ESP32-S3 (QFN56) (revision v0.2)` | ✅ |
| 8 MB PSRAM | `Found 8MB PSRAM device`，**octal**，`Speed: 80MHz`，自检 OK | ✅（且为 octal） |
| 16 MB Flash | `Detected flash size: 16MB`，QIO | ✅ |
| ST7789 显示 | 板级组件 `esp32_s3_touch_lcd_1_54`，`SPI BUS init` / `Install panel IO` / `Backlight on` | ✅（SPI 而非 i80） |
| CST816S 触摸 | `CST816S: IC id: 182`，IRQ 模式注册成功 | ✅（家族确认；182=0xB6，变体见 `B06`） |
| ES8311 Codec | `ES8311: Work in Slave mode`，`Adev_Codec: Open codec device OK` | ✅ |
| ES7210 AEC ADC | `ES7210: Work in Slave mode`，`Enable MIC1/MIC2`，`Unmuted` | ✅（双麦；AEC 仍需软件） |
| QMI8658 IMU | `QMI8658: QMI8658 initialized successfully` | ✅ |
| 三键 | 原理图 GPIO0/4/5；启动日志只显示 `button: IoT Button Version 4.1.6` | 🟡 未逐键验证 |
| 8 MB PSRAM 模式 | octal / 80 MHz | ✅（原 `B11` 的内存假设成立） |
| 原生 USB | `USB mode: USB-Serial/JTAG`，`VID_303A:PID_1001`，Windows `COM7` | ✅ |
| RTC | `PCF85063A initialized successfully` | 🟡 与原理图冲突（`B18`） |

**仍未验证**：面板 SPI mode/反色/gap（`B07`）；CST816 变体（`B06`）；按键逐键与 `PWR` 锁存（`B09`）；ES7210 通道映射（`B05`）；RTC 地址/备份电池（`B18`）；Flash 厂牌差异是否影响时序（`B19`）。

### 5.6 出厂固件分区（只读，E08）

```text
nvs      0x009000  0x004000
otadata  0x00d000  0x002000
phy_init 0x00f000  0x001000
factory  0x020000  0x650000  (6.3 MB)
ota_0    0x670000  0x400000  (4 MB)
assets   0xa70000  0x450000  (4.3 MB)
```

出厂就有 `factory + ota_0 + assets`，说明 16 MB 空间足够做双槽 OTA；但**本项目 v1 不用 OTA**，分区仍按 `C06` 冻结。

## 6. 架构候选（拟定，阶段 2 评审）

| 方案 | 做法 | 优点 | 风险/结论 |
| --- | --- | --- | --- |
| **A（推荐）** | 派生 MIT 的 `ESP32-buddy` 固件为基线，新增 1.54 板级层；借鉴 vibebuddy 设计补卡片分页/触摸/OTA | 复用已验收的状态机/协议/宿主链；许可干净；风险最低 | 240×240 版式与音频输入是主要工作量 |
| B | 以 `vibebuddy` Rust 固件为基线移植 | 功能最全（触摸/OTA/分页/角色包） | **GPL 传染**；i80→SPI、CHSC5432→CST816S、无 daemon 即空壳；Rust/esp-hal 工具链本机也没有 |
| C | 全新净室重写 | 最干净 | 成本最高，放弃已验收资产，不建议 |

> 失败时**不得自动降级到 B**（许可不可逆），必须回到用户重新决策。

---

## 7. 隔离实验（子代理执行，独立 clone）

| ID | 操作 | 观察 | 可得结论 |
| --- | --- | --- | --- |
| `E01` | `python3 tools/make-manifest.py --check`（ESP32-buddy） | 208 文件 / 2,926,444 B 一致 | 上游交付清单自洽；**不代表可构建** |
| `E02` | `./host/run-tests.sh` | **77 tests OK** | 宿主 Python 逻辑可测；**不代表设备行为** |
| `E03` | `./tests/run.sh` | 失败：无 `cc/gcc/clang/zig` | 本机无法复现上游 589 checks；**C 单测在本机 UNKNOWN** |
| `E04` | GitHub API / `gh` | `gh` 不存在；匿名 API 限流；改用 clone | 元数据来自 clone + 网页；**星标数非证据** |
| `E05` | 官方原理图 PDF 文本 + demo 代码交叉核对 | 引脚三处一致；`06` BSP 属 1.85C | 1.54 引脚可作为基线，`06` 必须排除 |

---

## 8. 已知缺口与下一阶段证据要求

| ID | 缺口 | 需要的证据（可执行动作） | 当前处理（口径限制） |
| --- | --- | --- | --- |
| `B01` | 板卡型号/批次未确认 | 用户确认 SKU 33869 + 实机 `01_factory`/`esp32_s3_touch_lcd_1_54` 日志 | ✅ **已解决（2026-10-30）**：E06–E09 认定实物为 Waveshare 33869 同源；批次差异不再阻断 |
| `B02` | 无 ESP-IDF / 无编译器 | 安装 IDF v5.5.x + 工具链，`idf.py --version` 成功 | ✅ **已解决（2026-10-30，E11）**：v5.5.4 + xtensa 14.2.0 可用；**尚未构建本项目** |
| `B03` | 无实机通路 | 确认刷写主机（Windows COM7 / usbipd） | ✅ **只读通路已解决**（E06–E09）；**刷写仍未授权**（`Q10`） |
| `B04` | 240×240 版式未设计 | 产出 240×240 版式契约 + host 预览图 | 不承诺"沿用现有坐标" |
| `B05` | ES7210 主麦/参考麦通道未知 | 双通道各录 10 s（说话/拍手），按通道能量判定 | 实机确认 MIC1/MIC2 均启用，但**哪路是参考未定** |
| `B06` | CST816 变体未知 | 读 `0xA7`；不符则换驱动 | 🟡 实测 `IC id: 182`（0xB6，常见映射为 CST816D）；驱动按 CST816S 工作。**先用不依赖变体的最小手势，差异待验** |
| `B07` | 面板 SPI 模式/反色/gap 未实测 | 上电试 mode0/3 + invert on/off，各拍一张 | 不承诺显示正确；M1 必须先验 |
| `B08` | GPIO2 电源锁存未实测 | 电池供电，启动即拉高，松开 PWR 观察是否保持 | 不承诺电池可运行 |
| `B09` | PWR 键语义未定 | 与用户确认"长按关机"是否保留 | 不擅自占用 |
| `B10` | app 分区装不下新功能 | 重划 16 MB 分区并记录余量 | 不承诺"直接沿用 2 MB" |
| `B11` | 240×240 双缓冲内存 | 计算并决定 PSRAM/局部刷新策略 | 不承诺 8 fps |
| `B12` | 无 OTA | 分区表 + `esp_ota` + 回滚用例 | 不承诺可 OTA |
| `B13` | GPL 污染 | 只读 vibebuddy 设计与协议；代码来自 MIT 基线或自写 | 不复制 vibebuddy 源文件 |
| `B14` | BLE+WiFi 共存拖慢 ASR | 上游实测"同连占 50% 射频"；复测 | 不承诺双连同时可用 |
| `B15` | 宿主链路 Windows 专用 | 明确宿主 OS 与端口（COM5 / 8765–8767） | 不承诺 Linux 宿主可用 |
| `B16` | IMU 行为阈值未定 | 实机采样抬腕/摇晃，定阈值 | 不承诺手势识别率；v1 不做 IMU |
| `B17` | 出厂固件备份 | 全片 16 MB + sha256 双端一致 | ✅ **已完成（2026-10-30，E10）**：`6bc50c73…a1721d`；**回退演练未做** |
| `B18` | RTC 冲突：原理图无 PCF85063，实机日志 `PCF85063A initialized successfully` | 读 I2C `0x51` 寄存器 + 断电后走时测试 | 不把 RTC 写进 v1 依赖；只记录冲突 |
| `B19` | Flash 厂牌冲突：原理图 Winbond，实物 JEDEC `20 4018` Micron | 读 SFDP/时序；以实测容量与时序为准 | 不承诺特定厂牌；容量已确认 16 MB |

> "当前处理"是口径限制，不是"待补充"。

---

## 9. 工作审视报告

**原定目标**：读两个上游 + 目标板资料，产出阶段 0/1 文档；不写代码、不构建、不刷机。

**完成情况**：
- [x] 三路调研（ESP32-buddy 源码、vibebuddy 源码、Waveshare 原理图）
- [x] 证据分级与环境基线
- [x] 上游能力清单与复用分类
- [x] 目标板引脚/器件/风险基线
- [x] **实机只读核对**（E06–E09）：芯片、PSRAM、Flash、USB、屏、触摸、ES8311、ES7210、QMI8658、RTC 全部有日志/寄存器证据
- [x] 缺口台账 `B01–B19`
- [ ] 未做：固件构建（IDF 安装中）、刷机（未授权）、I2C 主动探测
- [ ] 未做：240×240 版式与 M1 契约冻结（阶段 2）

**发现的问题与修正**：

| 严重程度 | 具体问题 | 真实原因 | 本次修正 |
| --- | --- | --- | --- |
| 必须改正 | 用户写 `ES7219` | 该型号不存在；板上是 `ES7210`（原理图 U8，0x40） | 全文按 ES7210 记录，并列入 `Q06` 待确认 |
| 必须改正 | 把 vibebuddy 当"可移植固件" | 它是瘦终端 + 宿主 daemon；无 daemon 即 `NO LINK` | 架构候选 A 明确"宿主要么保留要么重写"（`Q04`） |
| 应当改正 | 上游 README 与文档索引矛盾 | README 停在 M1–M8，索引已到 v1.6.0 | 以索引 + 源码为准，README 标为漂移 |
| 应当改正 | 以为 vibebuddy 是 ESP32-buddy 的上游 fork | GitHub `fork:false`，无共享历史，仅借鉴设计 | 关系表按"非 fork、净室"记录 |
| **必须改正（实机后）** | 文档写"目标板无 RTC" | 实机 `PCF85063A initialized successfully`；原理图文本未命中 | 已改为"存在，但与原理图冲突"，登记 `B18`；不进 v1 依赖 |
| **必须改正（实机后）** | 文档写 Flash 为 `W25Q128JVSIQ` | 实物 JEDEC `20 4018` = Micron | 已改为"容量 16 MB，厂牌以实测为准"，登记 `B19` |
| 应当记录 | 采样率假设为 16 kHz | 实机 ES8311/ES7210 I2S = **24 kHz** | 已在 §5.3 与 `C04` 待冻结项中标注 |

**下次重点关注**：`Q01–Q06` 的答复；板卡实物确认；ESP-IDF 安装授权。上下文载体：本文件 + `02-requirements.md`。
