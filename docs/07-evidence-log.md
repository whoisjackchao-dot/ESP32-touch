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
