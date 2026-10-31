# pet_ui NOTICE —— 来源与许可

代码复制自上游 **ESP32-buddy**（MIT，`LICENSE © 2026 whoisjackchao-dot`），
路径 `/home/home/ESP32-buddy/firmware/components/pet/`；**逐字节复制**的文件见下表。
本项目对其派生使用已获 `Q02=A` 批准（2026-10-30）。

| 本目录文件 | 上游来源 | 许可 | 备注 |
| --- | --- | --- | --- |
| `pet_render.c`、`pet_text.c` | `components/pet/pet_render.c`、`pet_text.c` | MIT | 逐字节 |
| `pet_state.c` | `components/pet/pet_state.c`（行为规格） | MIT | **派生**：按 `C03 §3` 改卡栈语义（同名=title 且 project；空 title 仍入栈）；D-05 标记点阵修正见 `pet_ui_240.c` |
| `pet_ui_240.c` | 无（本项目新实现） | MIT | 240×240 版式，契约 `C03`（含 §7 修订）、`docs/08-m2-development.md` |
| `pet_button.c`、`pet_menu.c` | `components/pet/pet_button.c`、`pet_menu.c` | MIT | 行为一致（M3 按键/菜单） |
| `pet_linebuf.c`、`pet_link.c` | `components/pet/pet_linebuf.c`、`pet_link.c` | MIT | **派生**（M5a）：按 `C05` 增补 `agent` 字段（顶层 + tasks 项）；协议语义同上游 M4/M8 |
| `include/pet_linebuf.h`、`pet_link.h` | `components/pet/include/` 同名文件 | MIT | **派生**：注释指向本项目 `C05`；`pet_link_msg_t` 同上游 |
| `pet_pomo.c` | `components/pet/pet_pomo.c` | MIT | **派生**：本段时长可 `pet_pomo_set_durations()` 运行期覆盖（G5 演示）；默认仍 25/5；NVS 存储拆到 `pet_pomo_store.c` |
| `pet_keymap.c` | `components/pet/pet_keymap.c`（行为规格） | MIT | **派生**：类型独立于上游 `pet_engine`；DUTY `KEY1` 长按映射为静音（C07 §3） |
| `pet_pomo_store.c` | `components/pet/pet_pomo_store.c` + 本项目静音 | MIT | **派生**：新增 `voice/muted` NVS 读写（M4 音频复用） |
| `pet_ui_pomo_240.c`、`pet_ui_menu_240.c` | 无（本项目新实现） | MIT | M3 番茄钟页/菜单页，契约 `docs/10-m3-development.md` §5 |
| `include/pet_render.h`、`pet_text.h`、`pet_menu.h`、`pet_button.h` | `components/pet/include/` 同名文件 | MIT | 逐字节 |
| `include/pet_pomo.h` | `components/pet/include/pet_pomo.h` | MIT | **派生**：`pet_pomo_t` 增 `focus_ms/break_ms` 与 `pet_pomo_set_durations()`（M3 扩展） |
| `include/pet_types.h` | `components/pet/include/pet_types.h` | MIT | **派生**：`pet_card_t` 增 `agent[PET_AGENT_MAX]`（`C03 §7 R2`），其余字段不变 |
| `include/pet_state.h` | `components/pet/include/pet_state.h` | MIT | **派生**：`pet_event_t` 增 `agent`；注释按本项目契约 |
| `include/pet_ui_240.h`、`pet_ui_pomo_240.h`、`pet_ui_menu_240.h`、`pet_keymap.h`、`pet_pomo_store.h` | 无（本项目新增） | MIT | |
| `font8x8_basic.h` | `components/pet/font8x8_basic.h` | Public Domain（Daniel Hepper / Marcel Sondar / IBM VGA） | |
| `assets/robot.look` | `components/pet/assets/robot.look` | MIT（上游原创程序化生成素材） | |
| `assets/cjk16.h` | `components/pet/assets/cjk16.h` | OFL 1.1（由 Noto Sans SC 光栅化生成） | M2 起被卡片标题 16×16 真正使用 |

## 范围说明

- **未复制**：`pet_hal_sticks3.c/.h`、`pm1.c/.h`、`pet_audio.c/.h`、
  `pet_keys.c/.h`、`pet_net.c/.h`、`pet_ble.c/.h`、`pet_engine.c`、`pet_leisure.c`、
  `pet_render` 的 135×240 视图逻辑，以及任何 second-state/vibebuddy（GPL）代码。
- M1/M2 时 `pet_button/menu/pomo` 只有头文件被作为 `pet_render.h` 的传递依赖保留；
  **M3 起**其 `.c` 被真正编译（按键/菜单/番茄钟），来源与派生差异见上表。
- `cjk16.h` 是 `pet_text.c` 编译期必需的头文件（1.5 MB 字库数据）；
  M2 起卡片标题 16×16 使用，链接器保留所需字形数据（数据段整体保留）。
- 上游文件头中的契约注释原样保留，便于向上游追溯。

## 未使用函数的说明

`pet_render.c` 中 `pet_render_scene`（135×240 固定坐标）、`pet_render_pomo`、
`pet_render_menu`、`pet_render_skit` **不被本项目调用**（M2/M3 均用 `pet_ui_*_240` 新实现）；
135×240 几何仅存留于未被链接的死代码中。

