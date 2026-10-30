# ADR-01：固件底座选型

- 状态：**已批准**（2026-10-30，`Q02`）
- 决策：**派生 MIT 的 `whoisjackchao-dot/ESP32-buddy` 固件作为基线**，新增 1.54 板级层；`vibebuddy` 只借鉴设计与协议。

## 背景

两个上游：

| | `second-state/vibebuddy` | `whoisjackchao-dot/ESP32-buddy` |
| --- | --- | --- |
| 许可 | **GPL-3.0-or-later** | **MIT** |
| 语言/栈 | Rust（esp-hal/embassy）+ Rust 宿主 daemon | **C / ESP-IDF 5.5** + Python 宿主 |
| 目标板 | ATK-DNESP32S3-BOX，320×240 **i80**，CHSC5432，XL9555 | M5Stack StickS3，135×240 **SPI**，ES8311，M5PM1 |
| 形态 | 瘦终端，强依赖宿主 | 瘦终端 + Python 桥，核心逻辑 host 可测 |
| 功能 | 触摸/OTA/分页/角色包/Relay | 6 态/3 卡/番茄钟/休闲/PTT/BLE |

目标板：Waveshare 1.54，240×240 **SPI**，CST816，ES8311+ES7210，QMI8658，16 MB/8 MB。

## 决策

选 **A：派生 MIT 基线**。

理由：
1. **许可干净**：GPL 代码复制会传染，与 `R27` 冲突；MIT 基线允许本项目保持 MIT。
2. **总线匹配**：目标板是 SPI 屏，MIT 基线也是 SPI；vibebuddy 是 i80 并口，显示驱动需整体重写。
3. **已有验收资产**：状态机、协议、任务卡、番茄钟、休闲、BLE/WiFi 均有真机证据与 host 单测。
4. **本机工具链**：C/ESP-IDF 与官方 demo 一致（IDF 5.5.x），Rust esp-hal 需另一套工具链。

## 后果

**正面**
- 复用 `pet_state / pet_link / pet_linebuf / pet_button / pet_keymap / pet_menu / pet_pomo / pet_leisure / pet_voice / pet_text`。
- 事件协议（NDJSON v1）与宿主桥可直接沿用。

**负面 / 必须做**
- 240×240 版式全部重做（`C03`）。
- 板级层重写（`C01/C02`）：ST7789 SPI、GPIO2 电源锁存、ADC 电池、GPIO7 功放。
- 音频采集重写：目标板麦克风走 **ES7210**，基线走 ES8311 ADC。
- 分区表重划（`C06`）。
- 基线遗留缺陷要修：`pet_pomo_set_day()` 无调用者、`BACK: WINDOW` 默认无人消费、静音不持久化。

## 被否决的方案

| 方案 | 否决理由 |
| --- | --- |
| B：移植 vibebuddy Rust 固件 | GPL 传染；i80→SPI、CHSC5432→CST816S、XL9555→直连 GPIO 全不匹配；无宿主即空壳 |
| C：全新净室重写 | 成本最高，放弃已验收资产；无收益 |

## 切换规则

若 A 在 M1 的关键门槛（面板启动 240×240 正确一帧）连续 3 次失败且根因无法在 A 的板级层解决，**停止并回报用户**，不得自动切到 B。
