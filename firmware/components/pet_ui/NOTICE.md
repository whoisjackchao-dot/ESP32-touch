# pet_ui NOTICE —— 来源与许可

代码复制自上游 **ESP32-buddy**（MIT，`LICENSE © 2026 whoisjackchao-dot`），
路径 `/home/home/ESP32-buddy/firmware/components/pet/`，**逐字节未改动**
（md5 已核对）。本项目对其派生使用已获 `Q02=A` 批准（2026-10-30）。

| 本目录文件 | 上游来源 | 许可 |
| --- | --- | --- |
| `pet_render.c`、`pet_text.c` | `components/pet/pet_render.c`、`pet_text.c` | MIT |
| `include/pet_render.h`、`pet_text.h`、`pet_types.h`、`pet_menu.h`、`pet_pomo.h`、`pet_button.h` | `components/pet/include/` 同名文件 | MIT |
| `font8x8_basic.h` | `components/pet/font8x8_basic.h` | Public Domain（Daniel Hepper / Marcel Sondaar / IBM VGA） |
| `assets/robot.look` | `components/pet/assets/robot.look` | MIT（上游原创程序化生成素材） |
| `assets/cjk16.h` | `components/pet/assets/cjk16.h` | OFL 1.1（由 Noto Sans SC 光栅化生成） |

## 范围说明

- **未复制**（按 M1 契约禁止清单）：`pet_hal_sticks3.c/.h`、`pm1.c/.h`、
  `pet_audio.c/.h`、`pet_keys.c/.h`、`pet_net.c/.h`、`pet_ble.c/.h`，
  以及任何 second-state/vibebuddy（GPL）代码。
- `pet_button.h`、`pet_menu.h`、`pet_pomo.h`、`pet_types.h` 是
  `pet_render.h` 的传递依赖，只取头文件，不取对应 `.c`（无任何按键/番茄钟逻辑被编译）。
- `cjk16.h` 是 `pet_text.c` 编译期必需的头文件（1.5 MB 字库数据）；
  M1 不调用 16×16 中文绘制，链接器 `--gc-sections` 会将其从最终镜像中剔除。
- 上游文件头中的契约注释（M1/M2/M7 契约）原样保留，便于向上游追溯。

## 未使用函数的说明

`pet_render.c` 中 `pet_render_scene`（135×240 固定坐标）、`pet_render_pomo`、
`pet_render_menu`、`pet_render_skit` **不被 M1 调用**；`main.c` 直接使用
`pet_look_parse` + `pet_render_pet` 写 240×240 画布。135×240 几何仅存留于
未被链接的死代码中，不出现在最终镜像。
