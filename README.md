# ESP32-touch

给 AI 编码 Agent 配的桌面宠物，运行在 **Waveshare ESP32-S3-Touch-LCD-1.54（SKU 33869）**：
240×240 竖屏、CST816 触摸、三键、双麦 + ES8311、QMI8658 六轴、8 MB PSRAM、16 MB Flash。

> 方法论：[dayu](docs/00-index.md)（先证据、再需求、再契约、最后编码）。
> **任何会话先读 [`docs/00-index.md`](docs/00-index.md)。**

## 当前状态

| 里程碑 | 内容 | 状态 |
| --- | --- | --- |
| `M1` | 板级点亮：GPIO2 电源锁存、SPI2 40 MHz、ST7789 240×240、背光、PSRAM 帧缓冲、一帧 IDLE + `READY` | ✅ **真机通过**（2026-10-30，目视：宠物+READY、颜色正常、居中完整） |
| `M2` | 6 态动画 + 3 卡栈（240×240 版式 `C03` 已冻结） | ⏳ **未开始**（实现未交付） |
| `M3` | 2 应用键 + 菜单 + 番茄钟 | 计划 |
| `M4` | 语音播报（ES8311 输出） | 计划 |
| `M5` | USB / WiFi / BLE 事件入口 + 宿主桥 + 验收 | 计划 |
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
docs/                   dayu 文档：索引、需求、决策、可行性、计划、合同、证据、追踪矩阵
firmware/               ESP-IDF 工程（target esp32s3）
  main/                 板级层：board_lcd.c、main.c
  components/pet_ui/    上游 MIT 渲染/文本 + 立绘（byte-identical）；M2 版式待加
tools/idf-env.sh        ESP-IDF v5.5.4 环境包装（用户态 Python 3.13 + local libusb）
artifacts/              出厂固件备份（**不随仓库分发**，见 .gitignore）
```

## 构建

要求 **ESP-IDF v5.5.x**（本项目在 v5.5.4 上验证）、xtensa-esp-elf 14.2.0。

```bash
source tools/idf-env.sh
idf.py -B ~/esp-build/buddy154 -DIDF_TARGET=esp32s3 build
```

> 构建目录必须放 WSL/Linux 原生盘；放 `/mnt/c` 会慢约 30 倍。
> 根目录 `CMakeLists.txt` 只是入口 shim，真实工程在 `firmware/`。

## 烧录（Windows 侧原生 USB，示例 COM7）

```powershell
python -m esptool --chip esp32s3 -p COM7 -b 921600 erase_flash
python -m esptool --chip esp32s3 -p COM7 -b 921600 --before default_reset --after hard_reset write_flash `
  --flash_mode dio --flash_size 16MB --flash_freq 80m `
  0x0 bootloader.bin 0x8000 partition-table.bin 0xf000 ota_data_initial.bin 0x20000 buddy154.bin
```

- 分区表见 [`docs/contracts/C06-partitions.md`](docs/contracts/C06-partitions.md)：factory 3 MB + 双 OTA 槽预留。
- 首次上电**必须先拉高 GPIO2**（电源锁存），否则电池供电会掉电；固件已在 `app_main` 第一步处理。
- 出厂固件备份存在于开发机 `artifacts/`，sha256 `6bc50c73…a1721d`；**不随仓库分发**。

## 许可与归属

- 代码 **MIT**（`LICENSE`）。
- `firmware/components/pet_ui/` 中的 `pet_render.c`、`pet_text.c`、`pet_state.c`、`font8x8_basic.h`、`assets/robot.look` 复制自
  [`whoisjackchao-dot/ESP32-buddy`](https://github.com/whoisjackchao-dot/ESP32-buddy)（MIT）。
- `assets/cjk16.h` 由 Noto Sans SC（**OFL-1.1**）光栅化，版权与许可注释随文件保留。
- 设计借鉴 [`second-state/vibebuddy`](https://github.com/second-state/vibebuddy)（GPL-3.0）；**未复制其任何源码/素材**。
- 不使用 "VibeBuddy" 名称或角色商标。

详见 [`NOTICE.md`](NOTICE.md)。
