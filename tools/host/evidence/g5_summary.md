# G5（番茄钟完成/暂停/放弃/继续 + 重启持久化 + 跨日清零）跑测摘要

- 时间：2026-10-31T08:01:57.567+08:00 → 2026-10-31T08:02:19.874+08:00
- 模式：serial（COM7 @ 115200）
- **结论：PASS** — 步骤 17/17 PASS；帧布局断言 4/4 PASS
- 起始基线：done=0 focus_s=0 muted=1 total_ms=1500000 has_day=1 day=2

## 步骤

| # | 步骤 | 判定 | 证据 |
| --- | --- | --- | --- |
| 1 | `preflight_duty_idle` | PASS | mode=DUTY menu=0 phase=IDLE |
| 2 | `t_short_durations` | PASS | total_ms after `t` = 6000（文档：下一段起 focus=6000 ms；下一段总时长在 focus_start 再断言） |
| 3 | `enter_pomo` | PASS | mode=POMO menu=0 phase=IDLE |
| 4 | `focus_start` | PASS | phase=FOCUS total_ms=6000 (expect 6000) done=0 |
| 5 | `focus_pause` | PASS | phase=FOCUS_PAUSED remain_ms=4670 |
| 6 | `pause_remain_frozen` | PASS | 1 s 后 phase=FOCUS_PAUSED remain_ms=4670（暂停前 4670，应不变） |
| 7 | `focus_resume` | PASS | phase=FOCUS |
| 8 | `focus_done` | PASS | phase=FOCUS_DONE done=1 (expect 1) focus_s=6 (expect 6) remain_ms=0 |
| 9 | `break_start` | PASS | phase=BREAK total_ms=3000 (expect 3000) |
| 10 | `break_abandon` | PASS | phase=IDLE done=1（放弃不计完成，应仍 1） |
| 11 | `segment2_start` | PASS | phase=FOCUS total_ms=6000 |
| 12 | `segment2_done` | PASS | phase=FOCUS_DONE done=2 (expect 2) focus_s=12 (expect 12) |
| 13 | `back_duty` | PASS | mode=DUTY |
| 14 | `reboot_persist` | PASS | 重启后 mode=DUTY phase=IDLE done=2 (expect 2) focus_s=12 (expect 12) muted=1 (expect 1) |
| 15 | `crossday_zero` | PASS | y 后 done=0 (expect 0) focus_s=0 (expect 0) has_day=1 day=3 |
| 16 | `crossday_day` | PASS | has_day=1 day=3 (expect 1/3) |
| 17 | `crossday_persist` | PASS | 重启后 done=0 (expect 0) focus_s=0 (expect 0) has_day=1 day=3 |

## 帧证据（d → PNG → 像素断言）

| 帧 | 页面 | PNG | CRC32 | layout | 备注 |
| --- | --- | --- | --- | --- | --- |
| g5_pomo_idle | pomo | `g5_pomo_idle.png` | d96adc3c | PASS | phase=IDLE progress=0.000 |
| g5_pomo_focus_paused | pomo | `g5_pomo_focus_paused.png` | f10b8599 | PASS | phase=FOCUS_PAUSED progress=0.222 |
| g5_pomo_done | pomo | `g5_pomo_done.png` | 612be77d | PASS | phase=FOCUS_DONE progress=1.000 |
| g5_pomo_break | pomo | `g5_pomo_break.png` | 95596865 | PASS | phase=BREAK progress=—（运行中帧，进度不断言） |

## 协议/环境备注

- m 行实际格式（2026-10-31 真机）比 docs/10 §4 多 sel/key1/key2/pwr；工具两种格式都解析。
- M2 契约：15 s 无有效事件进 OFFLINE；本跑测用 `n` 心跳保活。
- G5 用 `t` 把本段改为 focus 6 s / break 3 s；`R` 重启后回到默认 25/5。
- 统计断言用增量（NVS 里可能有上次会话残留的 done/focus_s）。

