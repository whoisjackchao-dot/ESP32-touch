# C02：面板启动参数契约（草案，未冻结）

> 状态：**已冻结（2026-10-30，M1 实测后修订）**。引脚来自官方原理图 + 实机启动日志（E07）；SPI mode=3 / 反色开 / gap(0,0) / data_endian=LITTLE 已由 M1 目视通过（E14）。
> 本契约只覆盖 240×240 ST7789 面板启动；不覆盖触摸。

## 1. 总线与引脚

| 信号 | GPIO | 说明 |
| --- | --- | --- |
| SCLK | **38** | SPI 时钟 |
| MOSI | **39** | 单向写；无 MISO |
| CS | **21** | 片选 |
| DC | **45** | 命令/数据；**strapping 脚**，复位时不得被外部强上拉 |
| RST | **40** | 硬复位；**JTAG MTDO** |
| BL | **46** | LEDC PWM；**strapping 脚**；高有效 |
| 触摸 RST | 47 | **不与 LCD 共用** |
| 触摸 INT | 48 | |

## 2. 参数（候选值）

| 参数 | 候选值 | 依据 | 实测状态 |
| --- | --- | --- | --- |
| 控制器 | ST7789 / ST7789V2 | 官方 wiki + 资源页 | 官方文档 |
| 分辨率 | **240 × 240** | 官方 wiki；板级组件名 | 实机组件加载（E07） |
| 色彩 | RGB565，16 bpp | 官方 demo | 官方文档 |
| SPI 主机 | SPI2_HOST | 官方 factory demo | 源码 |
| SPI 时钟 | **40 MHz** | 官方 demo | 源码 |
| **SPI mode** | **3** | 官方 factory | ✅ **M1 实测通过（2026-10-30，E14）** |
| 反色 | **开**（`invert_color(true)`） | 官方 demo | ✅ **M1 实测通过（E14）** |
| gap / offset | **(0, 0)** | 官方 demo 未设置 | ✅ **M1 实测通过（E14）** |
| 字节序 | **`data_endian = LCD_RGB_DATA_ENDIAN_LITTLE`，不做字节交换** | M1 实测定案 | ✅ **M1 实测通过（E14）** |
| 复位时序 | RST 低 ≥10 ms → 高 → 等 ≥120 ms → init | 通用 ST7789 | 未实测 |
| 背光 | LEDC low-speed，10-bit，5 kHz，**高有效**；`set_backlight(on)` 映射 duty 0 / 1023 | 官方 demo | 日志已见 `Backlight on` |

## 3. 不变量

1. `board_init()` 返回成功 ⇒ 面板已 init 且 `display on`。
2. 首次 `present()` 前必须完成一次全屏清屏（避免上电花屏）。
3. `set_backlight(false)` 只关背光，**不得**关闭面板或复位。
4. 面板初始化失败必须返回错误码并打印阶段，不得无限重试阻塞启动。

## 4. 变更方式

`mode / invert / gap` 任一实测结果与候选不同：记录到 `07-evidence-log.md`，更新本契约，并在 `05-plan.md` M1 出口注明。
**不允许**为了"能亮"而在业务代码里加临时魔数。
