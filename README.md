# ESP32-touch

给 AI 编码 Agent 配的桌面宠物，运行在 **Waveshare ESP32-S3-Touch-LCD-1.54（SKU 33869）**：
240×240 竖屏、CST816 触摸、三键、双麦 + ES8311、QMI8658 六轴、8 MB PSRAM、16 MB Flash。

> 方法论：[dayu](docs/00-index.md)（先证据、再需求、再契约、最后编码）。
> **任何会话先读 [`docs/00-index.md`](docs/00-index.md)。**

## 当前状态

| 里程碑 | 内容 | 状态 |
| --- | --- | --- |
| `M1` | 板级点亮：GPIO2 电源锁存、SPI2 40 MHz、ST7789 240×240、背光、PSRAM 帧缓冲、一帧 IDLE + `READY` | ✅ **真机通过**（2026-10-30，目视：宠物+READY、颜色正常、居中完整） |
| `M2` | 6 态动画 + 3 卡栈（240×240 版式 `C03`，含 2026-10-31 §7 修订） | ✅ **已交付**（2026-10-31）：真机 G1 **24/24**、G3 **20/20**、MEM-01 10 min、`D-05` 修复；人眼 H 待用户确认。见 [`docs/09-m2-delivery.md`](docs/09-m2-delivery.md) |
| `M3` | 2 应用键 + 5 项菜单 + 番茄钟（240×240） | ✅ **已交付**（2026-10-31）：T 21/21、G5 17/17、G6 260/260；真机按压 `KEY-01/02/05` **PENDING**（待用户）。见 [`docs/11-m3-delivery.md`](docs/11-m3-delivery.md) |
| `M5a` | **DSH 插件**（`host-plugin/` bundle）+ USB 事件入口（`C05`） | ✅ **已交付（2026-10-31）**：设备事件入口 H **16/16**（0.165 s）、插件 `node --test` 56/56、desktop 安装 + 重启后**真实 DSH 会话 E2E 用户目视 PASS**（WORKING `>` + 会话标题卡片 + 绿点）。见 [`docs/13-m5a-delivery.md`](docs/13-m5a-delivery.md) |
| `M4` | 语音播报（ES8311 输出） | 顺延（M5a 出口后） |
| `M5b` | WiFi / BLE 通道 + 配网 + NTP | 计划 |
| 后置 | 触摸、IMU、AEC、OTA、语音输入 | 需求保留，未取消 |

**v1 明确不做**：复制 GPL 代码、使用 VibeBuddy 商标、自动审批 Agent 动作。

## 硬件（已实机核对）

| 项 | 实测 |
| --- | --- |
| MCU | ESP32-S3 rev v0.2，原生 USB-Serial/JTAG（`303A:1001`） |
| 内存 | 8 MB **octal** PSRAM @80 MHz；16 MB Flash |
| 显示 | ST7789 **240×240**，4 线 SPI，40 MHz，`mode 3`，反色开，`gap(0,0)`，RGB565 little-endian |
| 触摸 | CST816 家族，`IC id 182`，I2C 0x15 |
| 音频 | ES8311（DAC，0x18）+ ES7210（4 通道 ADC，0x40，MIC1+MIC2） |
| IMU | QMI8658（0x6B） |
| 其他 | PCF85063A RTC（存在，未进 v1 依赖）；ETA6098 充电；3 键 |

> 详细证据见 [`docs/07-evidence-log.md`](docs/07-evidence-log.md)（E06–E14）。

## 仓库结构

```text
docs/                   dayu 文档：索引、需求、决策、可行性、计划、契约、证据、追踪矩阵、各节点交付
firmware/               ESP-IDF 工程（target esp32s3）
  main/                 板级层（board_lcd.c）与主循环/注入/事件入口（main.c）
  components/pet_ui/    上游 MIT 渲染/文本/解析（派生标注见 NOTICE）+ 本项目状态机/240 页面
host-plugin/            **DSH 插件包 `dsh-esp32-touch-bridge`**（bundle：package.json + cordis.patch.yml + lib + PowerShell 串口 helper）
tests/                  host 单测（mingw gcc；M2 15/231、M3 21/517、M5a 29/436，RED→GREEN 留档）
tools/host/             串口注入、帧缓冲导出→PNG、像素断言、G1/G3/G5/G6 跑测、C05 事件 H 脚本（pet_event.py）
tools/idf-env.sh        ESP-IDF v5.5.4 环境包装（WSL 路径；Windows 原生见下）
artifacts/              出厂固件备份（**不随仓库分发**，见 .gitignore）
```

## 构建（两种环境，均已验证）

要求 **ESP-IDF v5.5.x**（本项目在 v5.5.4 上验证）、xtensa-esp-elf 14.2.0。

**Windows 原生（本机已验证，`E15`）**：

```powershell
$env:IDF_TOOLS_PATH='D:\Espressif'; $env:IDF_PATH='D:\Espressif\frameworks\esp-idf-v5.5.4'
. "$env:IDF_PATH\export.ps1"
cd D:\DSH\ESP32-touch
idf.py -B D:\DSH\esp-build\buddy154 -DIDF_TARGET=esp32s3 build
```

**WSL/Linux**：`source tools/idf-env.sh && idf.py -B ~/esp-build/buddy154 -DIDF_TARGET=esp32s3 build`
（构建目录放 Linux 原生盘；放 `/mnt/c` 会慢约 30 倍）。

> 根目录 `CMakeLists.txt` 只是入口 shim，真实工程在 `firmware/`。
> host 单测：`pwsh -NoProfile -File tests/run_tests.ps1`（需 mingw gcc）。

## 烧录与运行（Windows 侧原生 USB，示例 COM7）

```powershell
idf.py -B D:\DSH\esp-build\buddy154 -p COM7 flash
# 或显式偏移：
python -m esptool --chip esp32s3 -p COM7 -b 921600 --before default_reset --after hard_reset write_flash `
  --flash_mode dio --flash_size 16MB --flash_freq 80m `
  0x0 bootloader.bin 0x8000 partition-table.bin 0xf000 ota_data_initial.bin 0x20000 buddy154.bin
```

- 分区表见 [`docs/contracts/C06-partitions.md`](docs/contracts/C06-partitions.md)：factory 3 MB + 双 OTA 槽预留。
- 首次上电**必须先拉高 GPIO2**（电源锁存），否则电池供电会掉电；固件已在 `app_main` 第一步处理。
- 出厂固件备份存在于开发机 `artifacts/`，sha256 `6bc50c73…a1721d`；**不随仓库分发**。
- M2 固件启动后串口打 `m2: READY`；单字符注入（`1/2/3/4/0/5/u/c/x/p/d`）见
  [`docs/08-m2-development.md`](docs/08-m2-development.md) §4——这是 **M2 调试接口**，不是产品协议（M5 由事件协议取代）。
- 自动验收：`python tools/host/run_g1.py`、`python tools/host/run_g3.py`（勿与 monitor/flash 并行）。

## 许可与归属

- 代码 **MIT**（`LICENSE`）。
- `firmware/components/pet_ui/` 中 `pet_render.c`、`pet_text.c` 逐字节复制自
  [`whoisjackchao-dot/ESP32-buddy`](https://github.com/whoisjackchao-dot/ESP32-buddy)（MIT）；
  `pet_state.c`、`pet_types.h`、`pet_state.h` 为**派生**（卡栈语义按 `C03 §3` 修订、增 `agent` 字段），
  `pet_ui_240.c`/`pet_ui_240.h` 为本项目新实现。逐文件来源见 `firmware/components/pet_ui/NOTICE.md`。
- `assets/cjk16.h` 由 Noto Sans SC（**OFL-1.1**）光栅化，版权与许可注释随文件保留。
- 设计借鉴 [`second-state/vibebuddy`](https://github.com/second-state/vibebuddy)（GPL-3.0）；**未复制其任何源码/素材**。
- 不使用 "VibeBuddy" 名称或角色商标。

详见 [`NOTICE.md`](NOTICE.md)。


