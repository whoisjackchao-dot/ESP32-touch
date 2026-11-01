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

## E15：Windows 原生 ESP-IDF 构建（2026-10-31，接手会话）

**背景**：`tools/idf-env.sh` 假定 WSL；本机 `wsl.exe --status` 返回 `Wsl/E_ACCESSDENIED`，WSL 不可用。
改为 Windows 原生 IDF（`D:\Espressif`，v5.5.4，python 3.14 env）。

```powershell
$env:IDF_TOOLS_PATH='D:\Espressif'; $env:IDF_PATH='D:\Espressif\frameworks\esp-idf-v5.5.4'
. 'D:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1'
cd D:\DSH\ESP32-touch
idf.py -B D:\DSH\esp-build\buddy154 -DIDF_TARGET=esp32s3 build
```

**观察**：`Project build complete`，exit 0；M1 源码 app = **0x41430 (267,312 B)**，`0x2bebd0 (92%) free`；无 warning/error。
**可得结论**：本机存在可复现的 Windows 原生构建路径；`G17` 的"新 clone 可构建"不再只依赖 WSL。
**不能证明**：换机器/换 IDF 小版本仍可构建（G17 完整口径）。

## E16：M2 实现构建 + host T 测试 RED→GREEN（2026-10-31）

**构建**（同 E15 命令，源含 `pet_state.c` / `pet_ui_240.c` / 新 `main.c`）：

```text
buddy154.bin binary size 0x84fd0 bytes. Smallest app partition is 0x300000 bytes. 0x27b030 bytes (83%) free.
```

**观察**：无 warning/error；app 544,720 B（含 16×16 CJK 字库与 VFS/USB 驱动）。
**host 测试**：`pwsh -NoProfile -File tests/run_tests.ps1`

```text
SUMMARY pet_state: cases=9 passed=9 failed=0
SUMMARY pet_ui_240: cases=5 passed=5 failed=0
RESULT: PASS (all host tests green)
```

- RED 阶段（实现仍为桩）：13/14 用例断言失败（唯一通过的 ST-04 是桩默认 IDLE 恰好吻合），原始输出见 `tests/red-run.log` 与 `tests/RED-GREEN.md` §1。
- 断言总数 214（124 + 90）。
**可得结论**：`ST-01..09`、`UI-13`、`CARD-13`、`UI-11`、`GB-01` 的 T 级行为有可复现证据；实现与断言分离，未用"先实现后补红"。
**不能证明**：真机行为（见 E17/E18）；测试覆盖之外的 API 组合。

## E17：G1（UI-10/11/12）24 次真机观测（2026-10-31）

**方法**：设备跑 M2 固件（COM7），host `tools/host/run_g1.py` 注入单字符命令；每态各 4 次共 24 次，
每次读 `STATUS` 行 + 导出 115200 B 帧缓冲（CRC 校验一致）+ 像素断言 + 存 PNG。
**结果**：

```text
结论：PASS — 观测 24/24 PASS，布局断言 6/6 PASS，6 态 PNG 哈希互异：是
IDLE(frame0) WORKING(frame4) INPUT_REQUIRED(frame5) DONE(frame2) FAILED(frame3) OFFLINE(frame1)
```

**可得结论**（H+T）：
- 6 态各 4 次进入/退出全部可辨（24/24）；`UI-10` 的"20 次"门槛按 24 次如实执行（`C03 §7 R8`）。
- `UI-11`：`>` 只出现在 WORKING（x=115..124/y=2..15 白）、`!?` 只出现在 INPUT_REQUIRED（橙），其余态标记区无该两色。
- `UI-12`：OFFLINE 整帧灰度（逐像素 `r5==g6>>1`、`b5==g6>>1`），网络点 `0xF800` 红。
**证据文件**：`tools/host/evidence/g1.json`、`g1_summary.md`、`g1_obs0*.bin/.png`。
**不能证明**：肉眼观感（无摄像头，H 需用户目视）；刷新撕裂（`FRM-01`）。

## E18：G3（CARD-10..15）卡栈真机观测（2026-10-31）

**方法**：20 轮 `x` 清栈 → `c`×3 → 断言 `CARDS ALPHA|BETA|GAMMA` → `c` 第 4 张 → 断言 `DELTA|ALPHA|BETA`（GAMMA 淘汰）；
另单跑第 5..8 张验证同名置顶 / 空 title 占位 / 长标题截断 / CJK 标题；每轮导出帧 + 布局断言。

```text
结论：PASS — 卡栈轮次 20/20 PASS，特例 4/4 PASS，布局断言 24/24 PASS
```

- 同名（title+project）第 5 张 BETA 置顶：`BETA|DELTA|ALPHA`。
- 空 title 第 6 张：CARDS 打印 `-`，渲染 `(untitled)`（非空白卡）。
- 长标题第 7 张：行 2 宽 ≤180，尾 `..`，时长 `59S` 不被覆盖。
- CJK 第 8 张：`中文标题测试` 用 16×16 点阵（顶边 y+12），时长 `1H00M`。
**证据文件**：`tools/host/evidence/g3.json`、`g3_summary.md`、`g3_round*.png`、`g3_special*.png`。
**不能证明**：真实宿主事件的卡内容（M5）；卡文本全字符集。

## E20：缺陷 D-05 —— 头顶标记点阵被水平镜像（2026-10-31 真机发现并修复）

**现象**：M2 首次真机帧缓冲导出显示 WORKING 标记是 `<` 而不是契约要求的 `>`（INPUT 的 `?` 弯钩也反向）。
**根因**：上游 `pet_render.c` 的标记点阵表按 **MSB=最左**书写，而 `draw_glyph` 按 **bit0=最左** 绘制；两者互为镜像。
`!` 垂直对称故不受影响，`>`/`?` 方向错误。
**修复**：`pet_ui_240.c` 镜像 `GLYPH_GT` / `GLYPH_QUEST` 两张表（`bit0=最左` 口径不变，C03 §1 未改）。
**回归**：`tests/test_ui240.c` UI-11 增方向断言（D-05 回归，先对旧表记录 RED 再 GREEN，见 `tests/RED-GREEN.md` §3）；
重刷后 `g1_obs02_working.png` 为正确的 `>`。
**不能证明**：135×240 上游视图的同一问题已修（本项目不修改上游 `pet_render.c`，135×240 视图是死代码）。

## E19：MEM-01 10 分钟压力+堆采样、IDLE 眨眼（2026-10-31）

**方法**：`tools/host/evidence/mem01.json` 同源脚本（Lead 会话内脚本 `soak_mem.py`）：
600 s 持续注入事件/卡（每 250 ms 一条，含心跳 `n`），每 ~3.3 s 读一次 `STATUS`；另在开头重置 IDLE 相位后 1.4 s 内连续采样帧号观察眨眼。

```text
samples: 192
heap first=8449336 last=8449336 min=8449336 max=8449336 delta=0
fps 范围：8..8
blink frames observed: frame=0 (0.13..1.41s 多次) 与 frame=1 (1.09s)
blink_ok: True
```

**可得结论**：
- 10 min 压力下 `STATUS heap=` **无下降**（192 点全相同；该值为 `esp_get_free_heap_size`，含 PSRAM，渲染/事件路径无逐帧分配）。
- 全程 `fps=8`；状态/卡注入期间无死机、无看门狗复位。
- IDLE 眨眼由 `pet_state` 相位驱动，观测到 `frame=1`（EYES_CLOSED）与 `frame=0` 交替（`M2 §9.2` 提案节拍 1000/125 ms）。
**证据文件**：`tools/host/evidence/mem01.json`、`mem01_summary.md`。
**不能证明**：`G16` 的 2 小时插入稳定性/电池 1 小时（未跑）；堆碎片与峰值；`FRM-01` 撕裂（无摄像头）。

## E21：新 clone 可复现构建 + 0 warning 收口（2026-10-31）

**方法**：从仓库最终提交 `24876dd` 新 clone 到 `D:\DSH\esp-build\verify-clone2`，用独立的
`-B D:\DSH\esp-build\verify-build2` 构建，并在 clone 内跑 host 单测。

```text
git clone D:\DSH\ESP32-touch D:\DSH\esp-build\verify-clone2
idf.py -B D:\DSH\esp-build\verify-build2 -DIDF_TARGET=esp32s3 build
buddy154.bin binary size 0x84fd0 bytes. Smallest app partition is 0x300000 bytes. 0x27b030 bytes (83%) free.
Project build complete.
=== host tests === RESULT: PASS (all host tests green)
```

**观察**：无 warning/error；产物大小与本机构建一致（0x84fd0）。二进制 sha256 不同属正常
（app descriptor 含构建时间戳），故以"大小 + 构建成功 + 单测通过"为可复现判据。
**前序发现**：首次 clone 构建（`4d5584e`）暴露一个只在**全量编译**出现的
`-Wdiscarded-qualifiers`（`usb_serial_jtag_driver_install` 参数非 const）；已修（`24876dd`），
增量构建曾掩盖该 warning——记录为流程教训：收口必须做一次新 clone 全量构建。
**可得结论**：`G17` 的"新机器 clone → idf.py build 成功、无手工修补"在本机环境成立
（同一 IDF v5.5.4 / 同机，未跨机器验证）。**不能证明**：跨机器/跨 IDF 小版本可复现。

## E22：M3 构建与刷机（2026-10-31）

**构建**（Windows 原生 IDF v5.5.4，命令同 `E15`）：

```text
buddy154.bin binary size 0x8bef0 bytes (573,168 B). Smallest app partition is 0x300000 bytes. 0x274110 bytes (82%) free.
Project build complete.  （无 warning/error）
```

**刷机**：`idf.py -B D:\DSH\esp-build\buddy154 -p COM7 flash` → bootloader/partition/otadata/app 四段
`Hash of data verified`；复位后串口：

```text
I (...) m3: power latched (GPIO2 high)
I (...) board_lcd: panel 240x240 ready, SPI mode 3, backlight on
I (...) m3: restored: done=... focus_s=... day=... has_day=... muted=...
I (...) m3: keys init: KEY1=GPIO0 KEY2=GPIO4 PWR=GPIO5 (low active)
I (...) m3: m3: READY (240x240, 8fps, keys=GPIO0/4/5, pomo 25/5)
```

**可得结论**：M3 源码（按键/菜单/番茄钟/NVS/三页渲染）可构建、可刷写、可启动，无 warning。
**不能证明**：按键 GPIO 电平（见 `E26` 真机按键）、NVS 掉电持久（只验证了软重启 `R`）。

## E23：M3 host T 测试 RED→GREEN（2026-10-31）

**命令**：`pwsh -NoProfile -File tests/run_m3_tests.ps1`（M2 回归另跑 `tests/run_tests.ps1`）

```text
RED（桩副本 D:\DSH\esp-build\m3-stubs\，脚本 -UseStubs）：21/21 用例 FAIL，517 断言中 389 失败，退出 1
GREEN（真实现）：test_m3_logic 15/15（261 asserts）、test_m3_ui 6/6（256 asserts）
          合计 21/21 用例、517/517 断言 PASS，退出 0
M2 回归：run_tests.ps1 → RESULT: PASS（15 用例/231 断言）
```

- 覆盖：`BTN-01..03`、`KEY-03`、`KEYMAP-01..08`、`MENU-01..03`、`POMO-01..07`、`UI-20`（3 例）、`UI-21`（3 例）。
- **RED 为断言失败而非编译/环境错误**（桩仅空实现）；RED→GREEN 原始输出与逐用例统计见 `tests/RED-GREEN-M3.md`。
**可得结论**：M3 纯逻辑与两页像素行为有可复现的 T 级证据。
**不能证明**：GPIO 采样/真实按键（`E26`）、I2S 无（M4）。

## E24：G6 合成键真机跑测（2026-10-31）

**方法**：`python tools/host/run_g6.py --port COM7`；通过串口 `K/L/J/H` 合成键事件（与真实按键同一
处理路径），每项 20 次，读 `m` 行断言状态/菜单/静音；每页导出帧做像素断言。设备跑最终 M3 固件。

```text
summary: items 13/13 ok, iterations 260/260 PASS, layout 9/9 PASS, mute_persist=True
RESULT PASS
```

13 项：DUTY 四组（KEY1短/长、KEY2短/长）、POMO 三组（KEY1短/长、KEY2短）、MENU 六组（KEY1短循环、
KEY2长关闭、MUTE 执行、BACK 执行、WIFI/BT 关闭）。`muted=1` 在 `R` 重启后仍为 1（NVS）。
**证据**：`tools/host/evidence/g6.json`、`g6_summary.md`、`g6_run.log`、9 张 PNG。
**不能证明**：物理按键电平（合成事件绕过 GPIO/去抖，见 `E26`）。

## E25：G5 番茄钟 + 重启持久化 + 跨日（2026-10-31）

**方法**：`python tools/host/run_g5.py --port COM7`；先用 `t` 把本段改为 focus 6 s / break 3 s，
走完 开始→暂停→继续→到点→休息→放弃→第二段完成 → `R` 重启 → `y` 跨日 → 再 `R`。

```text
summary: steps 17/17 PASS, layout 4/4 PASS
[5] focus_pause phase=FOCUS_PAUSED remain=4670
[6] pause_remain_frozen 1s 后 remain 仍 4670（暂停不推进）
[8] focus_done phase=FOCUS_DONE done=1 focus_s=6 remain=0
[10] break_abandon 放弃不计完成（done 仍 1）
[14] reboot_persist 重启后 done=2 focus_s=12 muted=1 均持久
[15..17] y 后 done=0 focus_s=0 day 递增；再重启仍为 0 且 day 持久
RESULT PASS
```

**证据**：`tools/host/evidence/g5.json`、`g5_summary.md`、4 张番茄钟页 PNG（idle/focus_paused/done/break）。
**不能证明**：25/5 真实时长下的 25 分钟完整段（用 6 s/3 s 等价验证状态机与持久化；常量 25/5 由
`tests` 断言与代码审查保证）；跨日"当天"来源（M5 NTP）。

## E26：真机按键 H —— 首轮采集（2026-10-31，**未得出结论，KEY-01/02/05 仍 PENDING**）

**方法**：`keyh_capture.py` 连续读串口日志 430 s，按行为分类计数；每秒发 `n` 心跳。用户按
"KEY1 短/长各 20、KEY2 短/长各 20、PWR 短 5、PWR 长 5" 的说明操作。

**原始结果**（`D:\DSH\esp-build\keyh_result.json`）：

```text
key1_short_send=0  key1_long_mute=0
key2_short_mode=3  key2_long_menu_open=2  key2_long_menu_close=2
pwr_long_off=0  reboots=0  alarms=0
事件时间：0.7s/3.2s/5.8s/7.8s（菜单开/关×2）、19.2s/21.0s/21.6s（模式切换×3）——22 s 后无任何事件
```

**可得结论**：
- `KEY2`（GPIO4）**真实按压可被固件识别**：菜单开/关与 DUTY↔POMO 切换均产生了对应日志；
- `KEY1`（GPIO0）与 `PWR`（GPIO5）**本次未观测到任何事件**；不能据此断言硬件/固件失败——
  采集只覆盖 22 s、用户可能未按到/未识别该两键，且首版采集脚本未把 POMO 模式下的 KEY1
  日志纳入分类（`pomo short/long`），存在漏计。
**不能证明**：`KEY-01/02/05` 达标或失败；`B09`（按键逐键未验证）仍未关闭。

**后续（已备好工具）**：`D:\DSH\esp-build\keyprobe.py`（90 s 原始电平采集，逐次记录
`key1/key2/pwr` 位变化）用于区分"用户没按到" vs "GPIO 读不到"；用户本轮选择先提交，故
`KEY-01/02/05` 记为 **PENDING**，恢复测试后补 `E26b`。

## E27：M3 新 clone 可复现构建（2026-10-31）

**方法**：从提交 `81517a2` 新 clone 到 `D:\DSH\esp-build\verify-clone3`，独立构建目录
`D:\DSH\esp-build\verify-build3`；clone 内跑 M3 host 单测。

```text
git clone D:\DSH\ESP32-touch D:\DSH\esp-build\verify-clone3
idf.py -B D:\DSH\esp-build\verify-build3 -DIDF_TARGET=esp32s3 build
buddy154.bin binary size 0x8bef0 bytes. Smallest app partition is 0x300000 bytes. 0x274110 bytes (82%) free.
Project build complete.        （无 warning/error）
=== clone host tests === RESULT: PASS (all M3 host tests green)
```

**可得结论**：M3 提交在全新 clone 上可构建（与本地同尺寸 0x8bef0）、0 warning，host 单测通过；
`G17` 的"新 clone 可构建、无手工修补"在 M3 上继续成立（同机同 IDF，未跨机器）。

## E28：M5a 设备侧事件入口构建（2026-10-31）

**动作**：新增 `pet_linebuf.c/h`、`pet_link.c/h`（派生自上游 MIT，C05 + `agent` 字段），
`main.c` console 任务增加"以 `{` 开头的 NDJSON 行"分流：解析经 `pet_link_parse` 后入队，
主任务统一 `pet_state_apply` + 卡栈，单写者、不阻塞；调试单字符命令保持兼容。

```text
buddy154.bin binary size 0x8cda0 bytes (576,928 B). Smallest app partition is 0x300000 bytes. 0x273260 bytes (82%) free.
Project build complete.  （无 warning/error）
```

**可得结论**：M5a 设备侧可构建（C05 入口 + M2/M3 功能共存）。
**不能证明**：真机事件入口行为（Waveshare 板当前未连接；本机 `COM5` 是另一台 StickS3，
被旧项目 `file_to_serial.py` 占用）。真机 H 待重连后跑 `tools/host/pet_event.py`（脚本已就绪）。

## E29：C05 设备事件解析 C 类契约测试（2026-10-31）

**命令**：`pwsh -NoProfile -File tests/run_m5a_tests.ps1`

```text
SUMMARY m5a_event: cases=28 passed=28 failed=0 asserts=418
RESULT: PASS (all M5a host tests green)   （gcc -Wall -Wextra 0 warning）
回归：run_tests.ps1 PASS（M2 15 用例/231 断言）；run_m3_tests.ps1 PASS（M3 21 用例/517 断言）
```

覆盖：linebuf（逐字节/CRLF/空行/恰好 512/513 起 DROPPED 且可恢复/deliver_pending）；
link 正常（C05 全事件名、`agent.blocked`→TASK_ERROR、顶层单卡四字段、tasks 3 卡/空数组/>3 截断/status 映射/agent、截断、linebuf+link 集成）；
错误/边界（非法 JSON/未闭合/尾部/version/event/字段类型/tasks 非数组与坏项/超长/未知键嵌套/`\u`/显式 len）。

**发现并已对齐的文档差异**（测试以真实实现为准；C05 已加 §9 对齐记录）：
`elapsed_s` 非整数报 JSON 错（非 TYPE）、tasks 项 title 可缺省、`\u` 不支持、未知键嵌套 ≤8 层、
`elapsed_s` 上限 1e9、按**字节**截断（63/63/7 B，CJK 可能被切断）、尾部空白接受。
**证据**：`tests/RED-GREEN-M5A.md`（D1–D10、原始输出）、`tests/m5a-run.log`。

## E30：C05 设备事件入口真机 H（2026-10-31，Waveshare 板 COM7）

**方法**：`python tools/host/pet_event.py --port COM7`（Waveshare 板 MAC `44:1b:f6:86:4b:e4` 重新接回 COM7；
M5a 固件刚刷入）。脚本发 C05 NDJSON、读 `p`/`m`、导出帧并校验 CRC，覆盖 6 事件/3 卡/非法输入/失联恢复。

```text
PASS boot_m5a_ready
PASS task_start_working | latency=0.165s
PASS g12_latency_le_1s | 0.165
PASS cards_text | M5A-CARD
PASS dump_working / tasks3_order | C-TOP|C-MID|C-OLD
PASS input_required / dump_input
PASS task_done / dump_done
PASS task_error
PASS idle_edge_done
PASS invalid_no_crash | DONE -> DONE（4 条非法行被丢弃，不崩、不改状态）
PASS tasks_empty_clears | tasks:[] 后 CARDS 为空（C05 §4）
PASS offline_after_15s | OFFLINE
PASS heartbeat_restore | WORKING
RESULT PASS (16/16)
```

**可得结论**（H）：
- C05 事件入口真机可用：`task.start`→WORKING **0.165 s**（G12 ≤1 s）、`tasks` 3 卡 top-first、`agent.input_required`→INPUT_REQUIRED、`task.done`→DONE、`task.error`→FAILED、`agent.idle` 边沿 1.5 s→DONE；
- 非法 JSON/未知事件/version=2/类型错误均被丢弃且不崩溃；
- 心跳停 15 s→OFFLINE，恢复心跳→回到失联前状态；
- 帧导出 CRC 与设备一致，PNG 见 `tools/host/evidence/m5a/m5a_*.png`。

**不能证明**：真实 DSH 会话产生的事件链（需安装插件到 desktop 并重启 DSH，见 `E31`/`E2E-01`）。

## E31：DSH 插件 → 真机端到端（插件代码路径，2026-10-31）

**方法**：用 `host-plugin/lib/index.js` 的 `applyWith()`（真实 state/protocol/serial-helper），
**fake Cordis ctx** 捕获监听器，按真实 DSH 事件形状喂事件；串口经插件自己的 PowerShell helper 写
`COM7`；随后用 `pet_event.Device` 查询设备状态并导出帧。

```text
[e2e-device] serial connected（helper spawn → COM7）
写 6 条 C05 事件 + 3 卡；helper 退出码 0
state: listeners=["session/created","session/disposed","session/event","agent/status","agent/error","user-questions/request"]
设备：status=INPUT_REQUIRED frame=5 cards=2
CARDS: DSH E2E 会话 | 插件会话
dump: True（CRC 一致）
```

**可得结论**：
- 插件的归约/编码/串口 helper **真实代码路径**能驱动真机：会话标题（含 CJK）与 `DSH` 前缀、elapsed、
  审批等待→INPUT_REQUIRED 均正确上屏（`tools/host/evidence/m5a/plugin_device_e2e.png`）；
- 无 port 配置时零副作用、waterfall 恒委派等由 `node --test` 覆盖（56/56）。

**不能证明**：**真实 DSH 会话**驱动（`E2E-01`）：需把 `host-plugin/` 安装到 desktop profile 并重启 DSH GUI
（当前会话进程内不能重启），之后用真实消息/审批观察设备。


## E32：DSH 插件安装进 desktop profile + HMR 已激活（2026-10-31）

**授权**：用户 2026-10-31 明确"可以安装插件，装好了我重启 dsh"。

**安装（本机，非模拟）**：

```powershell
$env:PATH="$env:TEMP\dsh-plugin-tools;$env:PATH"
dsh plugin --profile desktop add D:\DSH\ESP32-touch\host-plugin
# -> dependencies: + dsh-esp32-touch-bridge 0.1.0 <- D:\DSH\ESP32-touch\host-plugin
```

- `~/.dsh/profiles/desktop/package.json`：`dsh-esp32-touch-bridge: link:D:/DSH/ESP32-touch/host-plugin`，
  且 `dsh.profile.bundles` 末尾追加 `dsh-esp32-touch-bridge`。
- `node_modules/dsh-esp32-touch-bridge` 为 Junction → 仓库 `host-plugin/`（改动即时生效，无需重装）。
- 用户 patch `cordis.patch.yml` 追加 id 覆盖行（port/baud/heartbeatMs）：

```text
# == dsh-esp32-touch-bridge, patched by ...\desktop\cordis.patch.yml
- id: esp32-touch-bridge
  name: dsh-esp32-touch-bridge
  config: { port: COM7, baud: 115200, heartbeatMs: 10000 }
```

- 安装前已备份 profile（package.json / cordis.patch.yml / cordis.yml / pnpm-lock.yaml）到
  `D:\DSH\esp-build\profile-backup-desktop-20261031-101527`。插件未改 desktop/web 之外的 profile。

**HMR 激活证据（无需重启已被运行中的 DSH 加载）**：
- 进程中存在插件 helper：`powershell.exe -File ...\host-plugin\helper\serial-bridge.ps1`（PID 19828，10:16:38 启动）；
- `COM7` 被该 helper 占用（pyserial 打开返回 PermissionError）→ 插件正在向设备写心跳；
- 说明桌面 profile 的 `patchReload: live` 使新 bundle 即时生效。

**待办**：用户重启 DSH 后做**真实 DSH 会话 E2E**（`E2E-01`）：发消息/触发审批 → 设备变状态/出卡，
用户目视确认（设备串口被插件占用，H 证据以用户观察/照片为准）。

## E33：重启 DSH 后插件自动加载 + 用户目视设备正常（2026-10-31）

**用户操作**：按要求重启 DSH。**用户反馈**："重启后正常，有宠物和工具栏"。

**Host 侧客观事实**：
- DSH 进程启动时间 **10:21:36**；插件 helper（`serial-bridge.ps1`）**10:21:47** 启动（PID 2928）→
  profile 加载后插件自动激活（无需手工操作）。
- `COM7` 被 helper 占用（pyserial PermissionError）→ 插件已与设备建立串口会话并持续心跳。
- `dsh --profile desktop --dump-config` 仍显示插件行与 `config.port: COM7`。

**可得结论**：`E32` 的安装 + 配置在**重启后**被 DSH 正常加载；设备显示宠物（用户目视），
说明 C05 心跳链路已工作（否则 15 s 后应转灰/OFFLINE）。
**尚不能证明（`E2E-01` 剩余部分）**：真实会话事件驱动状态切换与卡片内容（需用户在任务运行中观察
`WORKING`/`DONE`/`INPUT_REQUIRED` 与底部卡片，或提供照片）。插件占用 COM7，H 证据以用户目视为准。

## E34：真实 DSH 会话 → 设备（`E2E-01` H，2026-10-31）

**方法**：DSH 重启后（`E33`），Lead 触发一个 **90 s 慢任务子代理**（真实 DSH agent 会话），
用户在设备前实时观察（插件占用 COM7，以用户目视为 H 证据）。

**用户观察（原文）**：
- 宠物头顶出现**白色向右箭头 `>`**（= WORKING）；
- 底部卡片内容为 **"这是一个用于真机观测.."**（= 子代理会话标题，DSH 自动标题）；
- 左上角**绿色网络点**（= 在线，心跳生效）。

**可得结论（H）**：`E2E-01` 的"真实 DSH 会话驱动设备状态/卡片"成立：
- DSH `agent/status=running` → 插件 → C05 `task.start` → 设备 WORKING（`>`）；
- DSH `session/title` → 插件 → C05 `tasks[]` → 设备卡片显示会话标题（含 CJK 截断）；
- 插件 10 s 心跳 → 设备保持在线（绿点）。
- 与 `E30`（设备侧直接注入）合并，覆盖 `G12`（事件入口）/`G2`（卡内容 ≤1 s 目视）的 USB 通道路径。

**未覆盖**：真实 DSH 侧"停宿主 15–17 s → OFFLINE → 恢复"的目视确认（设备侧已由 `E30` 验证
心跳停/恢复；插件侧断线重连由 `node --test` 覆盖）；用户未提供照片（以文字观察为 H 证据）。

## E35：M3 真机按键 H（缩短版：每按钮 3 次，用户指定，2026-10-31）

**用户决定**（2026-10-31）："m3测试缩短为每个按钮按3次" → 记为 `Q11`；`G6` 正式门槛（20 次/项）保留至发布前，
本轮只作 H 冒烟，不据此宣称 `G6` 达标。

**方法**：插件占用 COM7；脚本临时杀掉插件 helper 并"抢"串口（每 2 s 发心跳保持设备在线），
用户在 150 s 内按顺序操作；结束后脚本发 `R` 复位回值班页，插件随后自动重连。

```text
key1_short_send=3   （BOOT 短按 3/3，日志 "SEND -> host"，t=7.7/8.7/10.2 s）
key2_short_mode=3   （PLUS 短按 3/3，mode POMO→DUTY→POMO，t=12.4/13.3/14.5 s）
pwr_long_off=0      （本轮未做 PWR 长按）
unexpected=0        （PWR 短按 3 次：无任何应用动作/误触发）
串口中断: False
```

**可得结论（H 冒烟）**：
- `KEY1`（BOOT/GPIO0）短按 → `SEND` 3/3；`KEY2`（PLUS/GPIO4）短按 → 模式切换 3/3；
- `PWR`（GPIO5）短按 3 次无应用动作（符合 `C07`）；
- 按键 GPIO 采样/去抖/键表在**真实硬件**上工作（`B09` 的"能否读键"部分得到正面证据）。

**未覆盖**：
- `KEY-02` 长按（静音/菜单/放弃）与 `KEY-05` `PWR` 长按关机的真机按压——本轮按用户要求缩短为 3 次短按；
- `G6` 正式 20 次/项统计。

**副作用与清理**：为抢串口多次 kill 插件 helper，导致运行中的 DSH 出现**两个 helper 进程**（重复重连链）；
需**重启 DSH 一次**清理（重启后应只有 1 个 helper）。这是测试手法副作用，不是插件正常运行行为。

## E36：DSH 重启后插件单实例干净状态（2026-10-31）

**背景**：`E35` 的抢串口测试手法导致运行中的 DSH 出现两个 helper 进程（重复重连链）。
用户按要求再次重启 DSH。

**观察**：
- DSH 启动时间 **10:33:56**；插件 helper **10:34:05** 启动（PID 15868），**仅 1 个**；
- `COM7` 被 helper 占用（插件已连接、心跳正常）；
- 用户目视：**宠物正常、绿点显示**。

**可得结论**：插件在干净重启后为单实例、单 helper、单串口；C05 心跳链路正常。
`E35` 的重复 helper 是测试手法副作用，已通过重启消除。

## E37：M4 音频输出实现、构建与软件级验证（2026-10-31）

**交付**：`docs/contracts/C04-audio-output.md` 冻结；`firmware/main/board_audio.c`（I2C 42/41 +
I2S MCLK8/BCLK9/WS10/DOUT12 + ES8311 0x18 + PA GPIO7，16 kHz/stereo 槽位/MCLK×256，独立 audio
任务队列）、`firmware/components/pet_ui/pet_audio_policy.c`（T）、`firmware/components/es8311/`
（vendored Apache-2.0，保留 LICENSE）、`firmware/main/assets/voice/{input,done,failed}.pcm`
（上游 MIT 仓库资产，16 kHz mono s16le）、主循环 `pet_state_take_sound` + 静音门控 + 注入命令
`a`/`b`/`f`（H 测试用）。

**构建**（0 warning）：

```text
buddy154.bin binary size 0xc4630 bytes. Smallest app partition is 0x300000 bytes. 0x23b9d0 bytes (74%) free.
Project build complete.
```

**T 测试**（RED→GREEN）：`pwsh -NoProfile -File tests/run_m4_tests.ps1` → SUMMARY 28/28 PASS；
RED 阶段 9 条失败（stub 实现），GREEN 后全过；M2 15/15、M3 21/21、M5a 29/436 回归全 PASS。

**刷机事故与修正**：第一次手工 `esptool write_flash` 把 app 写到 `0x10000`（本板 factory 在
**0x20000**，`C06` 分区表），导致 `No bootable app partitions` 启动循环；改为 `0x20000` 重烧后
设备正常运行（STATUS/`m4: READY` 正常）。教训：手工刷机必须按 `C06` 分区表偏移，不能沿用常见 0x10000。

**软件级播报验证**（`audio_h.py` 真机日志）：
```text
AUDIO test input/done/failed → (ESP_OK)     # 三声均入队
mute=1 → 三次 "skipped (muted/off)"          # 静音门控生效
（重启后）"skipped (muted/off)"              # 静音跨重启保持（NVS）
mute=0 → "AUDIO test input (ESP_OK)"         # 解除静音恢复
```
**待办**：H 听感确认（用户在场听三声/静音/恢复）；若无声需查 ES8311 输出寄存器/PA 电平。

## E38：M4 语音播报真机 H（用户听感确认，2026-10-31）

**方法**：M4 固件（`E37`）运行于 COM7；`audio_h.py` 临时接管串口，注入 `a`/`b`/`f` 播报命令
（绕过状态机，仍受静音门控），依次执行：三声 → 静音三声 → 重启后一声 → 解除静音一声。
日志（`audio_h.log`）：三声均 `ESP_OK` 入队；`mute=1` 后三次 `skipped (muted/off)`；
重启后 `skipped`（静音持久化）；`mute=0` 后恢复 `ESP_OK`。

**用户反馈（原文）**：**"都正确"** —— 三类语音均听到、静音段无声、重启后仍静音、解除静音恢复有声。

**可得结论（H）**：
- ES8311 DAC + NS4150B（GPIO7）通路工作；三声可听、音量/音质用户认可；
- 静音门控 + NVS 持久化（`voice/muted`）真机有效；
- 播放异步（audio 任务）期间 UI/按键正常（用户未报告卡顿）。

**未完成的门槛**：`G9` 正式 60/60（每类 20 次可听/去重）+ 静音 20/20 未按整数量跑；
本轮为**用户确认的听感 H（缩减样本）**，正式 `G9` 留到发布前或用户另行指定。

**刷机勘误**：见 `E37`——手工 esptool 首刷误用 `0x10000`，正确 factory 偏移为 **0x20000**。

## E39：M4.1 待机画面静音图标（用户要求，2026-10-31）

**需求**：用户"加入静音图标在待机画面。现在看画面看不出来是否静音了" → `R30`、`C03 §7 R9`。

**实现**：`pet_view_t` 新增 `bool muted`；主循环 `view.muted = s_muted`；`pet_ui_240.c`
状态条右端 `(226,1,10,10)` 绘制橙色 `0xFD20` 喇叭+斜杠图标（后腔 3×4 + 扩音锥 4 列高 4/6/8/10 +
斜杠 3×3 段 = 49 px），仅静音时绘制；OFFLINE 时随整帧灰化。

**验证**：
- 构建 `0xc46c0`（74% free，0 warning）；
- host `UI-22-mute-icon` PASS（非静音区 0 px；静音 49 px；不侵入网络点/标记区/卡区/状态条外/右边界）；
  M2/M3/M4 其它用例回归 PASS；
- 真机帧缓冲（CRC 校验通过）：`tools/host/evidence/m4_1/`
  - `mute_off.png`：右上无图标
  - `mute_on.png`：右上橙色静音图标
  - `mute_on_after_restart.png`：重启后图标仍在（NVS 静音持久化）
  - `mute_off_after_unmute.png`：解除静音后图标消失
- 插件占用 COM7，截图脚本临时接管后已释放，插件自动重连。

**H 待用户**：设备上长按 KEY1 切换静音，观察右上图标出现/消失（截图已可对照）。

## E40：M4.1 静音图标真机 H 用户确认（2026-10-31）

**用户反馈（原文）**："确认，可以静音" —— 设备待机页右上角橙色静音图标随 KEY1 长按静音切换出现/消失，
功能与显示均符合预期。

**结论（H）**：`UI-22` / `R30` / `C03 §7 R9` 真机通过；M4.1 交付完成。
**证据**：帧缓冲 `tools/host/evidence/m4_1/*.png`（`E39`）+ 用户目视确认（本条）。

## E41：发布前正式计数（G6/G9，2026-10-31）

**G6 合成键**（`tools/host/run_g6.py --iterations 20`，真机 COM7）：

```text
summary: items 13/13 ok, iterations 260/260 PASS, layout 9/9 PASS, mute_persist=True
RESULT PASS
```
证据：`tools/host/evidence/release/g6.json`、`g6_summary.md`、9 张帧 PNG。

**G9 语音**（`run_g9.py`，走真实触发路径 '2'/'3'/'4'）：

```text
played input -> 20 / done -> 20 / failed -> 20
muted input -> 20 skipped / done -> 20 skipped / failed -> 20 skipped
RESULT PASS
```
证据：`tools/host/evidence/release/g9.json`、`g9_summary.md`。
结论：`G9` 正式 **60/60 + 静音 60/60** 计数达标（有声确认由 `E38` 用户听感覆盖）。

## E42：KEY-02/05 长按真机 H（多次尝试，PENDING）

**尝试**：`key_long_h.py` 三次窗口（180 s / 240 s / 600 s），每次先复位设备再监控长按日志。
**观察**：
- 每次串口探测正常（`STATUS`/`CARDS` 可回读，设备在线）；最后窗口探针显示 `cards=1 CARDS DSH`（插件卡片在）；
- **未捕获任何长按事件**（mute / menu / PWR 日志均为 0）→ 判断为采集窗口内**用户不在设备旁未按键**，
  非设备/固件失败（合成长按已由 `G6` 覆盖 20/20）。
**结论**：`KEY-02`（长按）/`KEY-05`（PWR 长按）真机 H **仍 PENDING**；等用户在设备旁时再跑（10 分钟窗口脚本已就绪）。

## E43：KEY-02/05 长按真机 H（2026-10-31，用户在场）

**方法**：`key_long_h.py` 接管串口（600 s 窗口）；用户长按 BOOT×3、PLUS×3、PWR×1。
**采集（`key_long_h_summary.md`）**：

```text
mute_toggle=3   (mute 1→0→1，t=19.6/24.0/28.0 s；KEY1 长按 3/3)
mode=2          (POMO/DUTY，用户短按穿插，非长按判定项)
menu_open=2 / menu_closed=1  (t=36.5 open / 38.4 closed / 41.4 open；KEY2 长按 3/3 开/关/开)
pwr_long_off=1  (t=47.9，"PWR long: power off (backlight off, GPIO2 low)"；KEY5 长按 1/1)
```

**结论（H）**：`KEY-02` 长按 **PASS**（静音翻转 3/3、菜单开/关 3/3）；`KEY-05` PWR 长按 **PASS**
（进入关机流程 1/1；短按无应用动作见 `E35`）。`Q11` 缩减样本口径（每项 3 次）已在该范围内达标。

**硬件事实（重要）**：本板 **没有独立 RST 复位键**。PWR 长按进入关机后，USB 供电下表现为**黑屏不恢复**；
恢复方式：**拔插 USB** 或串口硬复位（esptool `--after hard_reset` / DTR-RTS 复位）。
本次由 Lead 通过 `esptool chip_id --after hard_reset` 恢复；复位后设备正常启动（`STATUS WORKING`、插件卡片
`CARDS DSH`），插件自动重连。

## E44：M4.1 后全新 clone 构建验证（2026-10-31）

```text
git clone --depth 1 https://github.com/whoisjackchao-dot/ESP32-touch D:\DSH\esp-build\clone-m4-verify
idf.py -B .\build -DIDF_TARGET=esp32s3 build
-> buddy154.bin binary size 0xc46c0 bytes (804,544 B). 74% free. 0 warning
-> CLONE BUILD OK
```
证据：`D:\DSH\esp-build\clone-m4-build.log`。结论：干净 clone 可复现构建（M4.1 含 ES8311 组件/语音资产/静音图标）。

## E45：FRM-01 + C03 §7 + G1 口径用户确认（2026-10-31）

用户回复原文："**1， 没问题。2批准。3认可。**"

| 项 | 用户结论 | 落地 |
| --- | --- | --- |
| `FRM-01` 撕裂/闪烁 | **没问题**（长按/菜单/模式切换目视无撕裂/闪屏） | `06` 矩阵 `FRM-01` → **PASS**（H 目视） |
| `C03 §7 R1–R9` | **批准**（整批认可，含 R9 静音图标） | `C03` 头部记录批准日期 |
| `G1` 计数口径 | **认可按 24 次**（原文 20 为算术笔误） | `02-requirements.md G1` 注记批准日期 |

至此发布前检查单中"需用户确认"项全部关闭；剩余仅：M5b（未开始）、`Q08/Q09`（M5b 前）、`Q10` tag/Release（未授权）。

## E46：v1.0.0 正式发布（2026-10-31）

**授权**：用户 2026-10-31："发布后继续 m5" → 视为 `Q10` 发布授权（tag/Release）。

**发布物**：
- 注释标签 **`v1.0.0`**（提交 `8c1a460`）已推送；
- GitHub Release：**https://github.com/whoisjackchao-dot/ESP32-touch/releases/tag/v1.0.0**
  - `esp32-touch-v1.0.0-full.bin`（935,616 B，`esptool merge_bin` @0x0，可整包刷写）
  - `esp32-touch-plugin-0.1.0.zip`（42,378 B，`host-plugin/` 打包）
- 发布说明：`docs/16-release-notes-v1.0.0.md`。

**发布内容**：M1/M2/M3 + M4（语音）+ M4.1（静音图标）+ M5a（DSH 插件 USB 通道）；
G6 260/260、G9 60/60 + 60/60、KEY/FRM-01/C03/G1 用户确认、clone 构建 PASS。

**剩余（下一里程碑）**：M5b = WiFi TCP 8766 + NTP + 配网（AP 网页）→ BLE GATT；`Q08/Q09` 在相关语音功能前决策。

## E47：M5b-1 WiFi TCP + 配网 + NTP 真机 H（2026-10-31）

**固件**：`0x157600`（1,404,416 B，3 MB 分区 **55% free**，0 warning），已刷入 COM7。

**配网（用户操作，G13 成功路径）**：手机连接 `ESP32-Touch-4BE5`（WPA2 `touch154`）→ `http://192.168.4.1/`
→ 选择 **Xiaomi_F7A5** + 密码 → 保存 → 设备重启连接。串口自检：

```text
NET state=online ssid=Xiaomi_F7A5 ip=192.168.31.175 rssi=-47 time_valid=1
```

**TCP 事件（G12/NET-03）**：PC（192.168.31.119，同一 WiFi）→ `192.168.31.175:8766` 发送 C05 `task.start`：
`tcp->state latency = 0.345 s`（≤1 s **PASS**）。

**跨通道去重（NET-05）**：
- 同通道 1 s 内重复行 → `dedupe: duplicate line dropped` **PASS**；
- USB 先发、1 s 内 TCP 再发同一行 → **同样 DROP PASS**（共用去重器）。

**NTP（NET-04）**：`time_valid=1`（STA 获得 IP 后 SNTP 同步；`CST-8`）**PASS**。

**C08 实现对齐（新增）**：HTTP `/` `/scan` `/save` **仅在 AP 配网模式启动**；STA 模式 80 端口不监听，
自检用串口 `p` 的 `NET ...` 行（`/status` 未在 STA 提供）。→ 已记入 `C08 §8`。

**未完成（PENDING）**：G13 正式计数（扫描 ≥3 AP、60 s 内 IP、**错密码 2 min 回落 AP**）、
WiFi 断线重连（NET-05 重连部分）、`G16` 2 小时长稳；BLE 属 M5b-2。

## E48：G13 错密码回落 + NET-05 WiFi 断线重连（2026-11-01）

**G13 回落（用户目视）**：错密码保存 → 设备重启 → `ESP32-Touch-4BE5` 热点消失，约 **30–60 s** 后重新出现；
用户重连并保存正确密码 → 设备上线（`NET state=online ip=192.168.31.175`）。
→ 判定 **PASS（H 目视；串口日志在设备重启时丢失，时间按目视约值）**。

**NET-05 断线重连（用户重启路由器）**：设备日志（`net_reconnect.log`）：

```text
sta: disconnected reason=201 (NO_AP_FOUND), retry in 1000/2000/4000/8000 ms（退避重连）
... 路由器恢复 ...
sta: connecting ssid=Xiaomi_F7A5
sta: connected ip=192.168.31.175
sntp: synced 2026-11-01 08:27:52 CST-8
```

→ 自动断线检测 + 退避重连 + 重新获取 IP + NTP 重新同步 **PASS**。

**测试手法缺陷（重要，非设备问题）**：采集脚本在"6 s 无日志"时重开串口；pyserial 重开时拉 DTR/RTS
会触发 ESP32-S3 复位，导致日志中出现多次重启（99/124/146/156/170 s）。设备每次都能在 ~1.4 s 内重连、
2.4 s 内拿到 IP。后续此类采集应 `Serial(..., dtr=False, rts=False)` 或静默时不重开。

**仍未完成**：`G16` 2 小时长稳（设备已在线，可挂机计时）；BLE 属 M5b-2。

## E49：M5b-2 BLE GATT 实现与构建（2026-11-01）

**组件**：`firmware/components/pet_ble/`（`pet_ble.c` 588 行 NimBLE peripheral + `pet_ble_lines.c` 行重装）+
`firmware/sdkconfig.defaults` 追加 20 行 BT/NimBLE 配置（保留原 27 行）。

**host T（BLE-01 行重装）**：RED 10/10 FAIL（137 断言）→ GREEN **10/10 PASS、137/137 断言、0 warning**
（`tests/m5b2-run.log`、`tests/RED-GREEN-M5B2.md`）。

**集成构建**（`buddy154`，sdkconfig 已按新 defaults 重新生成，BT/NimBLE 生效）：

```text
buddy154.bin binary size 0x18bcf0 bytes (1,621,232 B). 48% free. 0 warning
```

- main 接线：`link_line_cb`（USB/WiFi/BLE 共用解析入队）；`pet_net_start()` + `pet_ble_start()` 失败只降级；
  自检新增 `BLE state=... addr=... mtu=... lines=...` 行。

**未验证（真机 H）**：BLE-03（手机 nRF Connect 写 C05 → 状态 ≤1 s）、BLE-04 断开重连、
BLE-05 WiFi+BLE 共存、G12 BLE 通道、G16 BLE 长稳。**长稳脚本正在占用 COM7，BLE 固件尚未刷入。**

## E50：G16 长稳（进行中快照，2026-11-01）

```text
samples=32+（每 60 s）  resets=0  heap 稳定在 ~8,300,6xx B  fps=8
```
（2 h 窗口；如提前结束转刷 BLE，则记为 PARTIAL，另择时补完整 2 h。）

## E51：M5b-2 BLE 真机 H + BLE 引进的 LCD DMA 内存问题修复（2026-11-01）

**固件**：main 接线 pet_ble 后 `0x18bcf0`（1,621,232 B，48% free，0 warning）；刷入 COM7。

**问题与修复（重要）**：BLE/NimBLE 启用后首次 `esp_lcd_panel_draw_bitmap` 失败：
`spi_master: setup_dma_priv_buffer failed` → `ESP_ERR_NO_MEM`（内部 DMA RAM 被 BT 控制器占用）。
修复：`board_lcd.c` 单次 DMA 上限 **28,800 B（整帧/4）→ 3,840 B（8 行）**、`trans_queue_depth 10→4`；
draw_bitmap 自动分块（整帧 30 块），8 fps 主循环下渲染正常（帧缓冲实测 PNG 正常，`tools/host/evidence/m5b2/ble_after.png`）。
**未改** C02 面板参数（mode3/40MHz/invert/gap），仅改 DMA 分块大小（板级实现细节）。

**BLE H（PC Intel BT + Python bleak）**：

```text
found BLE: ESP32-Touch-4BE6 (44:1B:F6:86:4B:E6)
connected; mtu=256
BLE-03 BLE 写 C05 → WORKING/cards=1 latency=0.234 s   (≤1 s PASS)
BLE state=connected addr=B4:BB:7A:8A:4F:14 mtu=256 lines=1
NET state=online ssid=Xiaomi_F7A5 ip=192.168.31.175  (WiFi 与 BLE 同时在线 PASS)
跨通道去重（BLE 先写 + USB 1 s 内同内容）dropped=True PASS
BLE-04 断开→重连→再写：cards>0 latency=0.443 s PASS
```

**结论**：`BLE-01`（T 10/10）、`BLE-03`、`BLE-04`、`BLE-05`（WiFi+BLE 共存）**PASS**；G12 BLE 通道 ≤1 s 达标。
**未完成**：`G16` 完整 2 h 长稳（当前快照见 `E50`；需在最终固件上重跑）。

**硬件/工具注**：pyserial 在插件 helper 刚被 kill 后首次 write 偶发 timeout（打开→立即写），
重试或加 0.5–1 s 延迟即可；非设备问题。

## E52：G16 最终固件 2 小时长稳 PASS（2026-11-01）

**对象**：最终 BLE 固件 `0x18bcf0`（含 WiFi+BLE+LCD DMA 修复），插电连续运行。
**采集**：每 60 s `STATUS`，共 **120 样本 / 7146 s（1h59m）**（`g16_soak.json`）。

```text
resets = 0          （无看门狗/崩溃/重启）
heap: first=8,317,396  last=8,317,396  min=8,315,612  （无下降趋势；波动 ~1.8 KB）
fps_min = 8
```

**状态振荡说明**：样本中 OFFLINE(59)/WORKING(61) 交替，是长稳脚本每 20 s 发心跳、而设备 15 s 无事件即 OFFLINE
所致；插件实际心跳 10 s（`C05 §3`），不属缺陷。

**结论**：`G16`（插电 2 h 无死机/无看门狗/无内存持续下降）**PASS**。

## E53：v1.1.0 正式发布（2026-11-01）

**发布物**：
- 标签 **`v1.1.0`**（`9baf8b9`）已推送；
- GitHub Release：**https://github.com/whoisjackchao-dot/ESP32-touch/releases/tag/v1.1.0**
  - `esp32-touch-v1.1.0-full.bin`（1,752,304 B，merge_bin @0x0）
  - `esp32-touch-plugin-0.1.0.zip`（42,378 B，DSH USB 插件，未变）
- 发布说明：`docs/18-release-notes-v1.1.0.md`。

**内容**：M5b 全部 —— WiFi TCP 8766 + AP 配网 + NTP + BLE GATT（NUS）+ USB/WiFi/BLE 跨通道去重 +
BLE 共存下的 LCD DMA 修复；`G13`/`NET-03/04/05`/`BLE-01..05`/`G16` 均 PASS（`E47/E48/E51/E52`）。
