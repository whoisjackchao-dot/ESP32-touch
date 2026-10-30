# NOTICE —— 第三方素材、许可与归属

本文件登记本项目使用的第三方内容。**代码许可与素材许可分开记录。**

## 1. 从 `whoisjackchao-dot/ESP32-buddy` 复制的文件（MIT）

| 本地路径 | 上游路径 | 许可 |
| --- | --- | --- |
| `firmware/components/pet_ui/pet_render.c/.h` | `firmware/components/pet/pet_render.c` + `include/pet_render.h` | MIT |
| `firmware/components/pet_ui/pet_text.c/.h` | `firmware/components/pet/pet_text.c` + `include/pet_text.h` | MIT |
| `firmware/components/pet_ui/include/pet_types.h`、`pet_menu.h`、`pet_pomo.h`、`pet_button.h` | 同名头文件 | MIT |
| `firmware/components/pet_ui/font8x8_basic.h` | 同名 | Public Domain（Daniel Hepper，基于 Marcel Sondaar / IBM VGA） |
| `firmware/components/pet_ui/assets/robot.look` | 同名 | MIT（上游原创，程序化生成） |

上游仓库：https://github.com/whoisjackchao-dot/ESP32-buddy （MIT）。
本项目对上述文件**保持字节一致**，未修改其内容；新增代码在 `firmware/main/` 与后续 `pet_ui_240.c`。

## 2. 字体

| 文件 | 来源 | 许可 |
| --- | --- | --- |
| `assets/cjk16.h` | 由 **Noto Sans SC**（Google Fonts，`ofl/notosanssc`）光栅化为 16×16 点阵 | **SIL Open Font License 1.1**；版权与许可声明随生成文件内注释保留 |
| `font8x8_basic.h` | Daniel Hepper（基于 Marcel Sondaar / IBM VGA 公有领域点阵） | Public Domain |

> OFL 允许再分发嵌入位图，但需保留版权与许可声明；本项目未改动字形数据。

## 3. 设计借鉴（非代码）

宠物状态机、事件模型（`task.start` / `task.done` / `task.error` / `agent.input_required` / `agent.idle` / `agent.blocked`）与显示分层思路，借鉴自
[`second-state/vibebuddy`](https://github.com/second-state/vibebuddy) 的**设计文档与行为规格**。

上游固件为 Rust（**GPL-3.0-or-later**），本项目为 C/ESP-IDF 净室实现，**未复制其任何源文件或素材**。
若将来复制上游任何代码，该部分必须按 GPL-3.0-or-later 处理，并撤销本节的"净室"声明——届时应开 ADR 并回报用户。

## 4. 商标

上游明确其 GPL-3.0-or-later 与 CC BY-SA 4.0 **均不授予** "VibeBuddy" 名称或角色商标权。
因此本项目：

- **不使用** "VibeBuddy" 作为项目名、产品名或品牌；
- **不暗示** 与上游存在隶属、赞助或官方关联。

## 5. 未随仓库分发的内容

- `artifacts/factory-*.bin`：开发机上的出厂固件备份（16 MB），**不随仓库分发**；sha256 记录在
  [`docs/07-evidence-log.md`](docs/07-evidence-log.md) E10。
- 构建产物（`build/`、`*.bin`、`*.elf`、`sdkconfig`）不提交。
