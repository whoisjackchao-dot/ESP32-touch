# G1（UI-10）24 次观测摘要

- 时间：2026-10-31T00:21:15.514+08:00
- 模式：serial（串口 COM7 @ 115200）
- 结论：**PASS** — 观测 24/24 PASS，布局断言 6/6 PASS，6 态 PNG 哈希互异：是

| # | 时间 | 注入 | 期望态 | 实测态 | frame | cards | state/frame/cards | 布局断言 | PNG sha256(前16) | crc32 |
|---|------|------|--------|--------|-------|-------|-------------------|----------|-------------------|-------|
| 1 | 2026-10-31T00:21:17.251+08:00 | `30` | IDLE | IDLE | 0 | 0 | P/P/P | PASS | 4cd1ba7ab12bb6e5 | a9f7eb93 |
| 2 | 2026-10-31T00:21:18.211+08:00 | `1` | WORKING | WORKING | 4 | 0 | P/P/P | PASS | c568ca052a0cb0e3 | 24ee1ef9 |
| 3 | 2026-10-31T00:21:19.171+08:00 | `2` | INPUT_REQUIRED | INPUT_REQUIRED | 5 | 0 | P/P/P | PASS | dadeadbe3e7869e4 | 268307fb |
| 4 | 2026-10-31T00:21:21.571+08:00 | `10` | DONE | DONE | 2 | 0 | P/P/P | PASS | 1a5ffe6bdfeff881 | b0ab754d |
| 5 | 2026-10-31T00:21:22.531+08:00 | `4` | FAILED | FAILED | 3 | 0 | P/P/P | PASS | 002aa0969ca5c2d1 | 1a193be3 |
| 6 | 2026-10-31T00:21:23.391+08:00 | `5` | OFFLINE | OFFLINE | 1 | 0 | P/P/P | PASS | 9a789341f6e2b09a | a547ce94 |
| 7 | 2026-10-31T00:21:24.931+08:00 | `30` | IDLE | IDLE | 0 | 0 | P/P/P | - | 4cd1ba7ab12bb6e5 | - |
| 8 | 2026-10-31T00:21:25.411+08:00 | `1` | WORKING | WORKING | 4 | 0 | P/P/P | - | c568ca052a0cb0e3 | - |
| 9 | 2026-10-31T00:21:25.891+08:00 | `2` | INPUT_REQUIRED | INPUT_REQUIRED | 5 | 0 | P/P/P | - | dadeadbe3e7869e4 | - |
| 10 | 2026-10-31T00:21:26.371+08:00 | `3` | DONE | DONE | 2 | 0 | P/P/P | - | 1a5ffe6bdfeff881 | - |
| 11 | 2026-10-31T00:21:26.851+08:00 | `4` | FAILED | FAILED | 3 | 0 | P/P/P | - | 002aa0969ca5c2d1 | - |
| 12 | 2026-10-31T00:21:27.231+08:00 | `5` | OFFLINE | OFFLINE | 1 | 0 | P/P/P | - | 9a789341f6e2b09a | - |
| 13 | 2026-10-31T00:21:28.291+08:00 | `30` | IDLE | IDLE | 0 | 0 | P/P/P | - | 4cd1ba7ab12bb6e5 | - |
| 14 | 2026-10-31T00:21:28.771+08:00 | `1` | WORKING | WORKING | 4 | 0 | P/P/P | - | c568ca052a0cb0e3 | - |
| 15 | 2026-10-31T00:21:29.251+08:00 | `2` | INPUT_REQUIRED | INPUT_REQUIRED | 5 | 0 | P/P/P | - | dadeadbe3e7869e4 | - |
| 16 | 2026-10-31T00:21:31.171+08:00 | `10` | DONE | DONE | 2 | 0 | P/P/P | - | 1a5ffe6bdfeff881 | - |
| 17 | 2026-10-31T00:21:31.651+08:00 | `4` | FAILED | FAILED | 3 | 0 | P/P/P | - | 002aa0969ca5c2d1 | - |
| 18 | 2026-10-31T00:21:32.031+08:00 | `5` | OFFLINE | OFFLINE | 1 | 0 | P/P/P | - | 9a789341f6e2b09a | - |
| 19 | 2026-10-31T00:21:33.091+08:00 | `30` | IDLE | IDLE | 0 | 0 | P/P/P | - | 4cd1ba7ab12bb6e5 | - |
| 20 | 2026-10-31T00:21:33.571+08:00 | `1` | WORKING | WORKING | 4 | 0 | P/P/P | - | c568ca052a0cb0e3 | - |
| 21 | 2026-10-31T00:21:34.051+08:00 | `2` | INPUT_REQUIRED | INPUT_REQUIRED | 5 | 0 | P/P/P | - | dadeadbe3e7869e4 | - |
| 22 | 2026-10-31T00:21:34.531+08:00 | `3` | DONE | DONE | 2 | 0 | P/P/P | - | 1a5ffe6bdfeff881 | - |
| 23 | 2026-10-31T00:21:35.011+08:00 | `4` | FAILED | FAILED | 3 | 0 | P/P/P | - | 002aa0969ca5c2d1 | - |
| 24 | 2026-10-31T00:21:35.391+08:00 | `5` | OFFLINE | OFFLINE | 1 | 0 | P/P/P | - | 9a789341f6e2b09a | - |

## 每态导出帧

| 态 | PNG | 帧 CRC32 | 帧 sha256 | PNG sha256 |
|----|-----|----------|-----------|------------|
| IDLE | g1_obs01_idle.png | a9f7eb93 | b117f682b6f0ee61 | 4cd1ba7ab12bb6e5 |
| WORKING | g1_obs02_working.png | 24ee1ef9 | 055bf51bcc9d0d94 | c568ca052a0cb0e3 |
| INPUT_REQUIRED | g1_obs03_input_required.png | 268307fb | 412f18ebe07b1b13 | dadeadbe3e7869e4 |
| DONE | g1_obs04_done.png | b0ab754d | 0cfb30c5cbc085e3 | 1a5ffe6bdfeff881 |
| FAILED | g1_obs05_failed.png | 1a193be3 | 261441057b8daa0a | 002aa0969ca5c2d1 |
| OFFLINE | g1_obs06_offline.png | a547ce94 | e2975de053c4fd9b | 9a789341f6e2b09a |
