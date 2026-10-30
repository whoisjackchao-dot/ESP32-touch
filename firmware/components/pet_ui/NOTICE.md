# pet_ui NOTICE —— 来源与许可

代码复制自上游 **ESP32-buddy**（MIT，`LICENSE © 2026 whoisjackchao-dot`），
路径 `/home/home/ESP32-buddy/firmware/components/pet/`；**逐字节复制**的文件见下表。
本项目对其派生使用已获 `Q02=A` 批准（2026-10-30）。

| 本目录文件 | 上游来源 | 许可 | 备注 |
| --- | --- | --- | --- |
| `pet_render.c`、`pet_text.c` | `components/pet/pet_render.c`、`pet_text.c` | MIT | 逐字节 |
| `pet_state.c` | `components/pet/pet_state.c`（行为规格） | MIT | **派生**：按 `C03 §3` 改卡栈语义（同名=title 且 project；空 title 仍入栈）；D-05 标记点阵修正见 `pet_ui_240.c` |
| `pet_ui_240.c` | 无（本项目新实现） | MIT | 240×240 版式，契约 `C03`（含 §7 修订）、`docs/08-m2-development.md` |
| `include/pet_render.h`、`pet_text.h`、`pet_menu.h`、`pet_pomo.h`、`pet_button.h` | `components/pet/include/` 同名文件 | MIT | 逐字节 |
| `include/pet_types.h` | `components/pet/include/pet_types.h` | MIT | **派生**：`pet_card_t` 增 `agent[PET_AGENT_MAX]`（`C03 §7 R2`），其余字段不变 |
| `include/pet_state.h` | `components/pet/include/pet_state.h` | MIT | **派生**：`pet_event_t` 增 `agent`；注释按本项目契约 |
| `include/pet_ui_240.h` | 无（本项目新增） | MIT | |
| `font8x8_basic.h` | `components/pet/font8x8_basic.h` | Public Domain（Daniel Hepper / Marcel Sondar / IBM VGA） | |
| `assets/robot.look` | `components/pet/assets/robot.look` | MIT（上游原创程序化生成素材） | |
| `assets/cjk16.h` | `components/pet/assets/cjk16.h` | OFL 1.1（由 Noto Sans SC 光栅化生成） | M2 起被卡片标题 16×16 真正使用 |

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
