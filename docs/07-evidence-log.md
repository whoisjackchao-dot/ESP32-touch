# 证据日志（`E` 编号）

> 只记录**实际执行过**的命令与原始观察。证据不可互相冒充：
> 本文件的 `实测` 只证明"这条命令这次输出了这些字节"，不证明产品行为。

## E06：芯片 / Flash / PSRAM / USB 识别（2026-10-30）

**环境**：WSL 内调用 Windows PowerShell；设备接在 Windows 侧，`COM7`。
**工具**：`D:\Espressif\python_env\idf5.5_py3.14_env\Scripts\python.exe`，esptool **4.12.0**。

```powershell
python -m esptool --chip esp32s3 -p COM7 -b 460800 flash_id
```

**原始输出（节选）**：

```text
Chip is ESP32-S3 (QFN56) (revision v0.2)
Features: WiFi, BLE, Embedded PSRAM 8MB (AP_3v3)
Crystal is 40MHz
USB mode: USB-Serial/JTAG
MAC: 44:1b:f6:86:4b:e4
Manufacturer: 20
Device: 4018
Detected flash size: 16MB
Flash type set in eFuse: quad (4 data lines)
Flash voltage set by eFuse to 3.3V
```

**可得结论**：

- MCU = **ESP32-S3**，QFN56，**rev v0.2**，WiFi + BLE。
- 封装内 **8 MB PSRAM**（具体 octal/80 MHz 由 E07 证实）。
- **16 MB Flash**；JEDEC `20 4018` → **Micron 厂牌**，与原理图写的 `W25Q128JVSIQ`（Winbond，`EF 4018`）**不一致**；容量一致。
- **原生 USB-Serial/JTAG**，非 CH343。
- **不能证明**：屏幕/触摸/IMU/音频存在；PSRAM 模式；晶振精度。

## E07：出厂固件启动日志（2026-10-30）

**方法**：`esptool --after hard_reset read_mac` 触发复位，随即以 115200 打开 `COM7` 抓 10 s。
**原始输出（节选，`/tmp/boot2.txt` 共 412 行）**：

```text
I (530) esp_image: segment 1: ... 
I (873) octal_psram: vendor id    : 0x0d (AP)
I (874) octal_psram: density      : 0x03 (64 Mbit)
I (878) esp_psram: Found 8MB PSRAM device
I (878) esp_psram: Speed: 80MHz
I (1308) esp_psram: SPI SRAM memory test OK
I (1317) cpu_start: cpu freq: 240000000 Hz
I (1322) spi_flash: detected chip: generic
I (1322) spi_flash: flash io: qio
I (1325) main: Switch to partition factory
I (1685) Main: Display ESP-Brookesia phone demo
I (1686) esp32_s3_touch_lcd_1_54: SPI BUS init
I (1686) esp32_s3_touch_lcd_1_54: Install panel IO
I (2210) CST816S: IC id: 182
I (2210) esp_lvgl:touch: Touch input device registered successfully (IRQ mode: enabled)
I (2211) esp32_s3_touch_lcd_1_54: Backlight on
I (2251) ES8311: Work in Slave mode
I (2257) ES7210: Work in Slave mode
I (2264) ES7210: Enable ES7210_INPUT_MIC1
I (2267) ES7210: Enable ES7210_INPUT_MIC2
I (2269) I2S_IF: STD Mode 1 bits:16/16 channel:2 sample_rate:24000 mask:3
I (2284) Adev_Codec: Open codec device OK
I (2308) ES7210: Unmuted
I (2361) PCF85063A: PCF85063A initialized successfully
I (2474) QMI8658: QMI8658 initialized successfully
I (2489) main_task: Returned from app_main()
```

**可得结论（逐条）**：

| 结论 | 证据 |
| --- | --- |
| PSRAM = **8 MB octal，80 MHz**，SPI SRAM 自检通过 | `octal_psram` + `esp_psram: Speed: 80MHz` |
| CPU 240 MHz，Flash QIO | `cpu freq: 240000000 Hz`、`flash io: qio` |
| 出厂固件 = **`01_factory`**（Waveshare demo），IDF **v5.5.2** | E09 app_desc |
| 板级组件名 = **`esp32_s3_touch_lcd_1_54`** | 日志前缀 |
| 显示走 **SPI**（非 i80） | `SPI BUS init` / `Install panel IO` |
| 触摸 = **CST816 家族**，芯片 ID **182 (0xB6)**，IRQ 模式注册成功 | `CST816S: IC id: 182` |
| 背光正常点亮 | `Backlight on` |
| **ES8311 + ES7210 都在**，ES7210 **MIC1 + MIC2 双麦**，Unmuted | 对应日志行 |
| I2S = **24 kHz / 16-bit / 2 ch**，ES8311/ES7210 为 Slave | `sample_rate:24000`、`Work in Slave mode` |
| **RTC = PCF85063A 存在且初始化成功** | `PCF85063A initialized successfully` |
| **QMI8658 存在且初始化成功** | `QMI8658 initialized successfully` |
| SD 卡未插入 | `sdmmc_card_init failed (0x107)` |

**不能证明**：面板 SPI mode / 反色 / gap；CST816 具体变体（182=0xB6，按常见映射更像 CST816D，但驱动按 CST816S 工作）；三键 GPIO；ES7210 主麦/参考麦通道；RTC 地址与备份电池；IMU 轴向。

## E08：分区表（只读，2026-10-30）

```powershell
python -m esptool --chip esp32s3 -p COM7 -b 460800 read_flash 0x8000 0x1000 C:\Users\Public\pt.bin
```

解析结果：

```text
0x009000  0x004000  type=1 sub=2   nvs
0x00d000  0x002000  type=1 sub=0   otadata
0x00f000  0x001000  type=1 sub=1   phy_init
0x020000  0x650000  type=0 sub=0   factory   (6.3 MB)
0x670000  0x400000  type=0 sub=16  ota_0     (4 MB)
0xa70000  0x450000  type=1 sub=130 assets    (4.3 MB)
```

**可得结论**：出厂固件已有 **factory + ota_0 + assets + otadata**；`assets` 是 Waveshare demo 的资源分区。说明这块板**原生支持 OTA 分区布局**，后续可参考但**不直接照抄**（我们的 app 命名/大小不同）。

## E09：出厂 App 描述（只读，2026-10-30）

```powershell
python -m esptool --chip esp32s3 -p COM7 -b 460800 read_flash 0x20000 0x200 C:\Users\Public\appdesc.bin
```

```text
app_desc magic 0xabcd5432
version: 1
project: 01_factory
time: 14:50:45 Mar 19 2026
idf: v5.5.2
```

**可得结论**：出厂固件确为 Waveshare `01_factory` 示例，用 IDF **v5.5.2** 构建。可据此认定实物与官方 1.54 demo 同源。

## E10：出厂固件全片备份（2026-10-30，刷写前置）

```powershell
python -m esptool --chip esp32s3 -p COM7 -b 921600 read_flash 0x0 0x1000000 C:\Users\Public\factory_full.bin
```

**观察**：`Read 16777216 bytes at 0x00000000 in 214.5 seconds (625.9 kbit/s)`，随后 hard reset。

**校验**：

```text
6bc50c73d68b33f9db5e108ae5794c85b3eb5534672a704b73fd4d9043a1721d  factory_full.bin (Windows)
6bc50c73d68b33f9db5e108ae5794c85b3eb5534672a704b73fd4d9043a1721d  artifacts/factory-33869-mac441bf6864be4-16MB.bin (本项目)
```

**可得结论**：16 MB 全片镜像已落盘并双端哈希一致；可据此回退到出厂 `01_factory`。
**不能证明**：该镜像能通过 `write_flash` 成功恢复（未演练回退）。

## E11：ESP-IDF v5.5.4 环境（2026-10-30）

安装方式：`~/esp/esp-idf`（tag `v5.5.4`，depth 1 + submodules）；用户态 Python **3.13.18**（系统仅 3.14，无 sudo）；只装 `esp32s3`。

```text
$ . tools/idf-env.sh && idf.py --version
ESP-IDF v5.5.4
$ xtensa-esp32s3-elf-gcc --version | head -1
xtensa-esp-elf-gcc (crosstool-NG esp-14.2.0_20260121) 14.2.0
$ cmake --version | head -1
cmake version 3.30.2
$ ninja --version
1.12.1
```

**已知偏差**：官方 `install.sh` 因 openocd 需要 `libusb-1.0.so.0` 而 abort（无 sudo、系统无该库）。处理：`apt-get download libusb-1.0-0` + `dpkg -x` 到 `~/.local/share/esp-libs`，由 `tools/idf-env.sh` 注入 `LD_LIBRARY_PATH`；openocd 版本检查随后通过。
**不覆盖**：本项目固件尚未构建；openocd 仅验证 `--version`，未连调试器。

## E12：M1 首次刷写与真机失败（2026-10-30）

**动作**：`erase_flash`（9.3 s）→ 写 bootloader `0x0` / partition `0x8000` / otadata `0xf000` / app `0x20000`，全部 `Hash of data verified`。
**观察**（串口，115200）：

```text
E (615) spi_master: check_trans_valid(1141): trans tx_buffer should be NULL and SPI_TRANS_USE_TXDATA should be cleared to skip MOSI phase.
E (615) lcd_panel.io.spi: panel_io_spi_tx_param(241): spi transmit (polling) command failed
E (615) lcd_panel.st7789: panel_st7789_init(187): io tx param failed
E (615) board_lcd: stage 'esp_lcd_panel_init' failed: ESP_ERR_INVALID_ARG
I (615) main_task: Returned from app_main()
```

**根因**：`esp_lcd_panel_io_spi_config_t.lcd_cmd_bits / lcd_param_bits` 未初始化（为 0）→ 命令事务 `length=0` 但 `tx_buffer` 非空，触发 SPI 主机校验失败。不是面板/接线问题。
**修正**：显式 `.lcd_cmd_bits = 8, .lcd_param_bits = 8`，重建后 app 仍 267,312 B。
**可得结论**：首次 H 证据为 **RED**；修正后有 GREEN 启动日志（E13）。
**不能证明**：画面正确（无反读通道）。

## E13：M1 修复后启动成功（2026-10-30）

**动作**：仅重写 app `0x20000`（`sha256 9e3ad68c…b2fd4`），硬复位后抓串口。

```text
I (575) m1: power latched (GPIO2 high)
I (695) board_lcd: panel 240x240 ready, SPI mode 3, backlight on
I (735) m1: DISPLAY READY M1 (240x240)
```

**可得结论**：GPIO2 锁存、SPI2 40 MHz mode 3、ST7789 240×240 init、反色、gap(0,0)、背光、PSRAM 帧缓冲、`robot.look` 解析、`draw_bitmap` 全部返回成功。
**不能证明**：颜色/几何/反色是否正确（无相机、无回读）；`data_endian=LITTLE` 为本轮选择，若实机偏色需改 BIG + 字节交换。

## E14：M1 目视验收（2026-10-30，H）

用户目视确认（设备保持运行，画面为 M1 固件）：

| 判定项 | 用户反馈 | 结论 |
| --- | --- | --- |
| 内容 | "宠物 + READY 都在" | `UI-01` ✅ |
| 颜色 | "颜色正常" | `LCD-02`（反色/字节序）✅ |
| 位置 | "居中且完整" | `LCD-01`（240×240 几何/无偏移）✅ |

**由此确认的候选参数**（原为 `B07` 未验证项）：

- SPI **mode 3** 正确；
- `invert_color(true)` 正确；
- `gap(0,0)` 正确；
- **`data_endian = LCD_RGB_DATA_ENDIAN_LITTLE`**（配 little-endian 帧缓冲、不做字节交换）正确；
- 240×240 全屏刷新、PSRAM 帧缓冲路径可用；
- GPIO2 拉高后设备保持运行（USB 供电下；**电池供电保持仍未单独验**）。

**不能证明**：40 MHz 的时序余量、长时间稳定性（G16）、电池供电（`PWR-01` 仅日志+USB）、触摸/IMU。
