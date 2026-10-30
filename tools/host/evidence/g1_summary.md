# G1（UI-10）24 次观测摘要

- 时间：2026-10-31T00:44:40.731+08:00
- 模式：serial（串口 COM7 @ 115200）
- 结论：**PASS** — 观测 24/24 PASS，布局断言 6/6 PASS，6 态 PNG 哈希互异：是

| # | 时间 | 注入 | 期望态 | 实测态 | frame | cards | state/frame/cards | 布局断言 | PNG sha256(前16) | crc32 |
|---|------|------|--------|--------|-------|-------|-------------------|----------|-------------------|-------|
| 1 | 2026-10-31T00:44:42.465+08:00 | `30` | IDLE | IDLE | 0 | 0 | P/P/P | PASS | 4cd1ba7ab12bb6e5 | a9f7eb93 |
| 2 | 2026-10-31T00:44:43.425+08:00 | `1` | WORKING | WORKING | 4 | 0 | P/P/P | PASS | c568ca052a0cb0e3 | 24ee1ef9 |
| 3 | 2026-10-31T00:44:44.385+08:00 | `2` | INPUT_REQUIRED | INPUT_REQUIRED | 5 | 0 | P/P/P | PASS | dadeadbe3e7869e4 | 268307fb |
| 4 | 2026-10-31T00:44:46.785+08:00 | `10` | DONE | DONE | 2 | 0 | P/P/P | PASS | 1a5ffe6bdfeff881 | b0ab754d |
| 5 | 2026-10-31T00:44:47.745+08:00 | `4` | FAILED | FAILED | 3 | 0 | P/P/P | PASS | 002aa0969ca5c2d1 | 1a193be3 |
| 6 | 2026-10-31T00:44:48.605+08:00 | `5` | OFFLINE | OFFLINE | 1 | 0 | P/P/P | PASS | 9a789341f6e2b09a | a547ce94 |
| 7 | 2026-10-31T00:44:50.145+08:00 | `30` | IDLE | IDLE | 0 | 0 | P/P/P | - | 4cd1ba7ab12bb6e5 | - |
| 8 | 2026-10-31T00:44:50.625+08:00 | `1` | WORKING | WORKING | 4 | 0 | P/P/P | - | c568ca052a0cb0e3 | - |
| 9 | 2026-10-31T00:44:51.106+08:00 | `2` | INPUT_REQUIRED | INPUT_REQUIRED | 5 | 0 | P/P/P | - | dadeadbe3e7869e4 | - |
| 10 | 2026-10-31T00:44:51.585+08:00 | `3` | DONE | DONE | 2 | 0 | P/P/P | - | 1a5ffe6bdfeff881 | - |
| 11 | 2026-10-31T00:44:52.065+08:00 | `4` | FAILED | FAILED | 3 | 0 | P/P/P | - | 002aa0969ca5c2d1 | - |
| 12 | 2026-10-31T00:44:52.445+08:00 | `5` | OFFLINE | OFFLINE | 1 | 0 | P/P/P | - | 9a789341f6e2b09a | - |
| 13 | 2026-10-31T00:44:53.505+08:00 | `30` | IDLE | IDLE | 0 | 0 | P/P/P | - | 4cd1ba7ab12bb6e5 | - |
| 14 | 2026-10-31T00:44:53.985+08:00 | `1` | WORKING | WORKING | 4 | 0 | P/P/P | - | c568ca052a0cb0e3 | - |
| 15 | 2026-10-31T00:44:54.465+08:00 | `2` | INPUT_REQUIRED | INPUT_REQUIRED | 5 | 0 | P/P/P | - | dadeadbe3e7869e4 | - |
| 16 | 2026-10-31T00:44:56.385+08:00 | `10` | DONE | DONE | 2 | 0 | P/P/P | - | 1a5ffe6bdfeff881 | - |
| 17 | 2026-10-31T00:44:56.865+08:00 | `4` | FAILED | FAILED | 3 | 0 | P/P/P | - | 002aa0969ca5c2d1 | - |
| 18 | 2026-10-31T00:44:57.245+08:00 | `5` | OFFLINE | OFFLINE | 1 | 0 | P/P/P | - | 9a789341f6e2b09a | - |
| 19 | 2026-10-31T00:44:58.305+08:00 | `30` | IDLE | IDLE | 0 | 0 | P/P/P | - | 4cd1ba7ab12bb6e5 | - |
| 20 | 2026-10-31T00:44:58.785+08:00 | `1` | WORKING | WORKING | 4 | 0 | P/P/P | - | c568ca052a0cb0e3 | - |
| 21 | 2026-10-31T00:44:59.266+08:00 | `2` | INPUT_REQUIRED | INPUT_REQUIRED | 5 | 0 | P/P/P | - | dadeadbe3e7869e4 | - |
| 22 | 2026-10-31T00:44:59.745+08:00 | `3` | DONE | DONE | 2 | 0 | P/P/P | - | 1a5ffe6bdfeff881 | - |
| 23 | 2026-10-31T00:45:00.225+08:00 | `4` | FAILED | FAILED | 3 | 0 | P/P/P | - | 002aa0969ca5c2d1 | - |
| 24 | 2026-10-31T00:45:00.605+08:00 | `5` | OFFLINE | OFFLINE | 1 | 0 | P/P/P | - | 9a789341f6e2b09a | - |

## 每态导出帧

| 态 | PNG | 帧 CRC32 | 帧 sha256 | PNG sha256 |
|----|-----|----------|-----------|------------|
| IDLE | g1_obs01_idle.png | a9f7eb93 | b117f682b6f0ee61 | 4cd1ba7ab12bb6e5 |
| WORKING | g1_obs02_working.png | 24ee1ef9 | 055bf51bcc9d0d94 | c568ca052a0cb0e3 |
| INPUT_REQUIRED | g1_obs03_input_required.png | 268307fb | 412f18ebe07b1b13 | dadeadbe3e7869e4 |
| DONE | g1_obs04_done.png | b0ab754d | 0cfb30c5cbc085e3 | 1a5ffe6bdfeff881 |
| FAILED | g1_obs05_failed.png | 1a193be3 | 261441057b8daa0a | 002aa0969ca5c2d1 |
| OFFLINE | g1_obs06_offline.png | a547ce94 | e2975de053c4fd9b | 9a789341f6e2b09a |
