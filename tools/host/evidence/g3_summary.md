# G3（CARD-10..14）卡栈观测摘要

- 时间：2026-10-31T00:45:01.662+08:00
- 模式：serial（串口 COM7 @ 115200）
- 结论：**PASS** — 卡栈轮次 20/20 PASS，特例 4/4 PASS，布局断言 24/24 PASS

## 20 轮卡栈

| 轮 | 时间 | 清栈后 cards | c×3 CARDS | c#4 CARDS | 淘汰 | state | 布局 | PNG | crc32 |
|----|------|--------------|-----------|-----------|------|-------|------|-----|-------|
| 1 | 2026-10-31T00:45:02.671+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round01.png | e38967a9 |
| 2 | 2026-10-31T00:45:04.459+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round02.png | b82019ff |
| 3 | 2026-10-31T00:45:06.139+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round03.png | b82019ff |
| 4 | 2026-10-31T00:45:07.813+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round04.png | e38967a9 |
| 5 | 2026-10-31T00:45:09.495+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round05.png | b82019ff |
| 6 | 2026-10-31T00:45:11.174+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round06.png | b82019ff |
| 7 | 2026-10-31T00:45:12.852+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round07.png | e38967a9 |
| 8 | 2026-10-31T00:45:14.537+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round08.png | b82019ff |
| 9 | 2026-10-31T00:45:16.210+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round09.png | b82019ff |
| 10 | 2026-10-31T00:45:17.897+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round10.png | e38967a9 |
| 11 | 2026-10-31T00:45:19.573+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round11.png | 5040df85 |
| 12 | 2026-10-31T00:45:21.258+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round12.png | b82019ff |
| 13 | 2026-10-31T00:45:22.936+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round13.png | e38967a9 |
| 14 | 2026-10-31T00:45:24.612+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round14.png | e38967a9 |
| 15 | 2026-10-31T00:45:26.290+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round15.png | b82019ff |
| 16 | 2026-10-31T00:45:27.974+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round16.png | 0be9a1d3 |
| 17 | 2026-10-31T00:45:29.659+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round17.png | e38967a9 |
| 18 | 2026-10-31T00:45:31.338+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round18.png | b82019ff |
| 19 | 2026-10-31T00:45:33.013+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round19.png | 0be9a1d3 |
| 20 | 2026-10-31T00:45:34.691+08:00 | 0 | ALPHA|BETA|GAMMA | DELTA|ALPHA|BETA | GAMMA | IDLE | PASS | g3_round20.png | e38967a9 |

## 单卡特例

| 特例 | 时间 | CARDS | 期望 | 断言 | 布局 | PNG | 说明 |
|------|------|-------|------|------|------|-----|------|
| c#5 同名 BETA 置顶 | 2026-10-31T00:45:37.241+08:00 | BETA|DELTA|ALPHA | BETA|DELTA|ALPHA | PASS | PASS | g3_special5_beta_dedup.png | 同名（title+project）更新并移到 cards[0] |
| c#6 空 title 占位 | 2026-10-31T00:45:38.059+08:00 | -|BETA|DELTA | |BETA|DELTA | PASS | PASS | g3_special6_untitled.png | 空 title 必须接收并渲染 (untitled)；固件 CARDS 行用 `-` 打印空 title，断言把首项 `-` 归一化为空串 |
| c#7 长标题截断 | 2026-10-31T00:45:38.894+08:00 | THIS-IS-A-VERY-LONG-TITLE-0123456789|-|BETA | THIS-IS-A-VERY-LONG-TITLE-0123456789||BETA | PASS | PASS | g3_special7_long.png | 行 2 宽 ≤180，溢出尾 `..`，不覆盖时长区 |
| c#8 CJK 标题 | 2026-10-31T00:45:39.738+08:00 | 中文标题测试|THIS-IS-A-VERY-LONG-TITLE-0123456789|- | 中文标题测试|THIS-IS-A-VERY-LONG-TITLE-0123456789| | PASS | PASS | g3_special8_cjk.png | 16×16 CJK，顶边 y+12 |
