# G6（合成键 / 菜单 / 静音持久化）跑测摘要

- 时间：2026-10-31T12:30:07.159+08:00 → 2026-10-31T12:37:05.097+08:00
- 模式：serial（COM7 @ 115200）
- 每项次数：20；门槛：每项 >=19/20 PASS；**结论：PASS**
- 项汇总：13/13 项达标；总迭代 260，PASS 260，FAIL 0
- 帧布局断言：9/9 PASS

## 每项结果

| 项 | 说明 | 次数 | PASS | FAIL | 达标 |
| --- | --- | --- | --- | --- | --- |
| `duty_key1_short` | 值班 KEY1 短：只记 SEND 日志，模式/相位/muted/done 不变 | 20 | 20 | 0 | PASS |
| `duty_key1_long` | 值班 KEY1 长：muted 0<->1 翻转 | 20 | 20 | 0 | PASS |
| `duty_key2_short` | 值班 KEY2 短：DUTY->POMO，再按回 DUTY | 20 | 20 | 0 | PASS |
| `duty_key2_long` | 值班 KEY2 长：开菜单 menu=1，再长按关闭 | 20 | 20 | 0 | PASS |
| `pomo_key1_short` | POMO KEY1 短：IDLE->FOCUS->FOCUS_PAUSED->FOCUS | 20 | 20 | 0 | PASS |
| `pomo_key1_long` | POMO KEY1 长：->IDLE 且 done 不变 | 20 | 20 | 0 | PASS |
| `pomo_key2_short` | POMO KEY2 短：回 DUTY | 20 | 20 | 0 | PASS |
| `menu_key1_short` | 菜单 KEY1 短：选中循环 0..4 | 20 | 20 | 0 | PASS |
| `menu_key2_long` | 菜单 KEY2 长：关闭菜单 | 20 | 20 | 0 | PASS |
| `menu_exec_mute` | 菜单 MUTE 项：muted 翻转并留在菜单 | 20 | 20 | 0 | PASS |
| `menu_exec_back` | 菜单 BACK 项：关闭菜单 | 20 | 20 | 0 | PASS |
| `menu_exec_wifi` | 菜单 WIFI (M5)：关闭菜单 + 日志 M5 | 20 | 20 | 0 | PASS |
| `menu_exec_bt` | 菜单 BT (M5)：关闭菜单 + 日志 M5 | 20 | 20 | 0 | PASS |

## 静音持久化（R 重启）

- 重启前 muted=1（额外翻转 1 次使 muted=1）；重启后 muted=1 → PASS
- 重启后 `m`：`MODE mode=DUTY menu=0 sel=0 phase=IDLE remain_ms=1500000 total_ms=1500000 done=0 focus_s=0 muted=1 day=3 has_day=1 key1=0 key2=0 pwr=0`

## 帧证据（d → PNG → 像素断言）

| 帧 | 页面 | PNG | CRC32 | layout |
| --- | --- | --- | --- | --- |
| g6_duty | duty | `g6_duty.png` | 056b4cae | PASS |
| g6_pomo_idle | pomo | `g6_pomo_idle.png` | eff88910 | PASS |
| g6_pomo_focus | pomo | `g6_pomo_focus.png` | 5b6e9c64 | PASS |
| g6_pomo_paused | pomo | `g6_pomo_paused.png` | c1171c9a | PASS |
| g6_menu_sel0 | menu | `g6_menu_sel0.png` | e26f6724 | PASS |
| g6_menu_sel1 | menu | `g6_menu_sel1.png` | e8340ca9 | PASS |
| g6_menu_sel2 | menu | `g6_menu_sel2.png` | d1f34f0c | PASS |
| g6_menu_sel3 | menu | `g6_menu_sel3.png` | 2676f43f | PASS |
| g6_menu_sel4 | menu | `g6_menu_sel4.png` | 2a11b6b8 | PASS |

## 协议/环境备注

- m 行实际格式（2026-10-31 真机）比 docs/10 §4 多 sel/key1/key2/pwr；工具两种格式都解析（缺字段时 sel=None，菜单选中回退为帧推断）。
- M2 契约：15 s 无有效事件进 OFFLINE；本跑测用 `n` 心跳保活。
- 日志采样（发送日志）：I (1167) m3: SEND -> host (M5 not implemented)

