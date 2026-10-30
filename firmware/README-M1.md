# README-M1：`esp32-buddy-touch154/firmware` 构建与交付说明

> 里程碑 **M1**（docs/05-plan.md）：1.54 板点亮 + 240×240 出正确一帧。
> 状态：**构建通过（exit 0，0 warning）；未刷机**（刷机需 `Q10` + 全片备份 `B17`，另一次会话执行）。

## 1. 这是什么

ESP-IDF v5.5.4 工程（ESP32-S3，Waveshare ESP32-S3-Touch-LCD-1.54，SKU 33869）：

1. **最先拉高 GPIO2** 电池供电锁存（脱离供电握手不掉电，`PWR-01`）；
2. 初始化 240×240 ST7789（SPI2，40 MHz，背光 LEDC GPIO46 高有效）；
3. 渲染 **一帧**：上游 `robot.look` 立绘 IDLE 态 2×（96×128，水平居中，y=40）+ `font8x8_basic` 的 `READY`（y=200），全屏黑底，一次 `draw_bitmap` 送出；
4. 串口日志输出 `DISPLAY READY M1 (240x240)`；
5. 主循环 `vTaskDelay(1000ms)` 空转。任何初始化阶段失败：记录阶段名并返回，**不挂死、不无限重试**。

## 2. 构建（已验证）

```bash
cd /home/home/esp32-buddy-touch154
source tools/idf-env.sh
idf.py -B ~/esp-build/buddy154 -DIDF_TARGET=esp32s3 build
```

- 仓库根 `CMakeLists.txt` 是入口 shim：组件/分区表/defaults 全部取自 `firmware/`，无源码副本。
- 也可在 `firmware/` 目录内直接构建（canonical 入口），但**不要**与根入口共用同一个 `-B` 目录（CMake cache 记录的源目录不同）。
- `idf.py` 命令本身**不刷机**；本会话未执行 `flash`/`erase`，未触碰 COM7。

### 构建产物与占用（2026-10-30 实测）

| 产物 | 大小 | 说明 |
| --- | --- | --- |
| `buddy154.bin`（app） | 267,312 B (0x41430) | factory 3 MB，余 92%（0x2bebd0 free） |
| `bootloader.bin` | 20,832 B | |
| `partition-table.bin` | 3,072 B | 与 `C06` 逐字节一致（diff 校验通过） |
| `ota_data_initial.bin` | 8,192 B | otadata 擦除态初始镜像 |

`esptool.py image_info`：ESP32-S3，Flash 16MB / DIO / 80m，6 段，checksum 与 hash 校验 valid。

## 3. 刷写偏移（供 M1 真机演示用；本次未执行）

| 文件 | 偏移 |
| --- | --- |
| `bootloader.bin` | `0x0` |
| `partition-table.bin` | `0x8000` |
| `ota_data_initial.bin`（可选） | `0xf000` |
| `buddy154.bin`（factory） | `0x20000` |

```bash
# 首刷前必须已完成出厂固件全片备份（B17，E10 sha256 已登记）
idf.py -B ~/esp-build/buddy154 -p COM7 flash
# 或显式偏移：
python -m esptool --chip esp32s3 -p COM7 write_flash \
  0x0     bootloader/bootloader.bin \
  0x8000  partition_table/partition-table.bin \
  0xf000  ota_data_initial.bin \
  0x20000 buddy154.bin
```

## 4. 代码来源

- `firmware/components/pet_ui/`：上游 **ESP32-buddy（MIT）** `components/pet` 的最小子集，逐字节复制（md5 核对），来源与许可见 `pet_ui/NOTICE.md`。未复制任何 `pet_hal_sticks3`/`pm1`/音频/按键/网络/BLE 模块；**无 second-state/vibebuddy（GPL）代码**。
- `firmware/main/`：本项目自有板级代码（`board_lcd.c/.h` 面板链路、`main.c` 顺序与渲染）。引脚号只出现在 `board_lcd.c`（C01 §1）。
- 初始化顺序严格按契约：GPIO2 锁存 → 背光灭（LEDC duty 0）→ SPI2(SCLK 38/MOSI 39) → panel IO(CS 21/DC 45/mode 3/40 MHz/队深 10) → ST7789(RST 40/RGB 序/16bpp/大端) → reset/init/invert(true)/gap(0,0)/disp_on → 背光 100%（5 kHz/10-bit）。

## 5. M1 明确**不**包含

动画/状态机、任务卡、番茄钟、按键交互、语音（PA/ES8311/ES7210）、WiFi/BLE、触摸（CST816 @ I2C 41/42）、IMU（QMI8658）、OTA、AEC。

## 6. 已知未验证项（真机 M1 必验，对应 B07）

| 项 | 现值 | 备注 |
| --- | --- | --- |
| SPI mode | **3**（候选） | 官方 factory=3 / LVGL 例程=0，两处不一致 |
| 反色 invert | **true**（候选） | 若颜色发白/发灰则改 false 并修订 C02 |
| gap / offset | **(0,0)**（候选） | 若整体偏移则实测修正 |
| 视觉输出 | **未验证** | 本次只构建未刷机；屏上效果（LCD-01/02、UI-01）待真机目视 |
| PSRAM 直读送屏 | 代码路径已写 | S3 EDMA 可访问 PSRAM；若 `draw_bitmap` 报错，退化路径已留（内部 RAM 兜底） |

## 7. 文件清单

```
CMakeLists.txt                      仓库根构建入口 shim
firmware/CMakeLists.txt             canonical 工程入口
firmware/partitions.csv             C06 冻结分区表（逐字节一致）
firmware/sdkconfig.defaults         最小配置（见下）
firmware/main/main.c                电源锁存 + 渲染一帧 + READY 日志
firmware/main/board_lcd.c/.h        ST7789 板级层（引脚唯一出现处）
firmware/components/pet_ui/         上游渲染子集（NOTICE.md 登记来源）
firmware/README-M1.md               本文件
```

`sdkconfig.defaults` 要点：esp32s3 / 16MB flash / `SPIRAM=y` + `MODE_OCT` + `SPEED_80M` / `ESP_CONSOLE_USB_SERIAL_JTAG` / 自定义分区表 / `LOG_DEFAULT_LEVEL_INFO` / `MAIN_TASK_STACK_SIZE=8192` / CPU 240 MHz。无 WiFi/BLE 组件被引入。

## 8. 勘误（2026-10-31，接手会话核对）

原文与本仓库实际内容不一致，以下以仓库实际为准：

| 原文声称 | 实际 |
| --- | --- |
| `firmware/CMakeLists.txt`（canonical 入口） | **不存在**；只有仓库根 `CMakeLists.txt` 入口 shim（`EXTRA_COMPONENT_DIRS` 指向 `firmware/`） |
| host 端 BMP 预览（M1 交付第 4 项） | **不存在**；M2 改为"真机帧缓冲导出 → PNG + 像素断言"（`tools/host/`，见 `docs/08-m2-development.md` §4） |
| `pet_state.c` 已从上游复制 | M1 时**不存在**；M2 才新增（`pet_state.c`，派生自上游，见 `NOTICE.md`） |
| 构建需在 WSL 原生盘 | 本机另有 Windows 原生 IDF v5.5.4（`D:\Espressif`），`idf.py build/flash` 已验证可用（`E15`）；WSL 非唯一路径 |
| M1 状态 "构建通过；未刷机" | 后续会话已完成真机刷写与目视（`E12`–`E14`），本文件第 3 节保留为历史步骤 |
