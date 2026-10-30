# tools/host — M2 主机侧工具（串口注入 / 帧导出 / PNG / G1-G3 自动跑测）

用于 **ESP32 桌面宠物 M2**（240×240 值班页 + 6 态 + 3 卡栈）的主机侧验证工具。
协议与版式依据：

- `docs/08-m2-development.md` §3（版式落地取值）、§4（串口注入协议）、§5（用例）
- `docs/contracts/C03-layout-240x240.md`（版式契约，含 §7 修订）
- `firmware/components/pet_ui/include/pet_types.h`（状态枚举顺序）

**依赖**：Windows + 系统 Python `C:\Python314\python.exe`（已装 `pyserial 3.5`）。
`fb.py` / `layout_checks.py` / `pet_sim.py` / `run_*.py` 只用标准库（**无 Pillow / numpy**）；
仅串口驱动 `pet_serial.py` 使用 pyserial。

---

## 1. 文件清单

| 文件 | 作用 |
| --- | --- |
| `pet_serial.py` | 串口驱动（COM7 @115200 8N1）：发单字符命令、逐行读回并自动跳过日志行、`p` 解析 STATUS/CARDS、`d` 导出帧（校验长度+CRC32，失败重试共 3 次）。可被其它脚本 `import`。 |
| `fb.py` | 115200 B RGB565-LE 帧 → PNG（`zlib`/`struct` 手写，高比特复制 RGB565→RGB888）；`--diff` 输出不同像素数/比例/包围盒/分区统计；`--selftest`。 |
| `layout_checks.py` | 像素断言（卡底/强调条/网络点/OFFLINE 灰度/头顶标记/页脚/背景/卡内容非空白），逐项 PASS/FAIL + 坐标证据；`--selftest` 用合成帧跑正例+反例。 |
| `pet_sim.py` | 无设备模拟器（状态机 + 预置卡栈），供 `run_*.py --simulate` 做全链路自测；**不是**被测固件。 |
| `run_g1.py` | UI-10：6 态 × 4 次 = **24 次观测**；每态导出 1 张 bin→PNG→layout_checks；6 态 PNG 哈希互异；输出 `evidence/g1.json` + `g1_summary.md`。 |
| `run_g3.py` | CARD-10..14：**20 轮**卡栈（x→c×3→断言→c 第 4 次→淘汰断言→导出 PNG→layout_checks）+ c#5/#6/#7/#8 特例；输出 `evidence/g3.json` + `g3_summary.md`。 |
| `evidence/` | 帧 `.bin`、`.png`、JSON 与摘要表；`evidence/sim/` 为模拟器自测产物（**不是**验收证据）。 |

---

## 2. 快速开始（设备就绪时）

在 `tools\host` 目录下执行（也可从仓库根用完整路径）。

### 2.1 单步

```powershell
C:\Python314\python.exe pet_serial.py --status                 # 发 p，打印 STATUS/CARDS
C:\Python314\python.exe pet_serial.py --send "1pc"             # 发命令串并回显响应
C:\Python314\python.exe pet_serial.py --dump out.bin           # 发 d，导出 115200 B（CRC 校验）
C:\Python314\python.exe fb.py out.bin out.png                  # bin → PNG
C:\Python314\python.exe layout_checks.py out.bin --state WORKING --cards 0
C:\Python314\python.exe fb.py --diff a.bin b.bin               # 两帧逐像素差异
```

`--wait m2:READY` 只捕获**启动日志**（`I (xxx) m2: READY ...`）。设备已在运行、READY 已过去时
直接 `--status` 即可，或 `--wait m2:READY --wait-timeout 20` 用于复位后立即抓取。

### 2.2 G1（UI-10，24 次观测）

```powershell
C:\Python314\python.exe run_g1.py
```

时序：`x` 清栈 → `5`→`u` 校准链路（避免残留 OFFLINE）→ 每轮按
`IDLE / WORKING / INPUT_REQUIRED / DONE / FAILED / OFFLINE` 注入：

| 期望态 | 注入 | 说明 |
| --- | --- | --- |
| `IDLE` | `3` 然后 `0` | TASK_DONE → DONE，AGENT_IDLE → IDLE，确定性归位 |
| `WORKING` | `1` | |
| `INPUT_REQUIRED` | `2` | |
| `DONE` | 奇数轮 `1`→`0` 等 1.75 s（done 下降沿 1500 ms 去抖）；偶数轮 `3` 直进 | 两条路径都断言 frame=2 |
| `FAILED` | `4` | |
| `OFFLINE` | `5`（观测后立刻 `u` 恢复并断言非 OFFLINE） | |

每次观测：注入 → 等待（≤1.75 s）→ `p` 断言 `state / frame / cards`；每态第 1 次观测
`d` 导出 → PNG → layout_checks（含标记断言）。输出：`evidence/g1.json`、
`evidence/g1_summary.md`、`g1_obs01..06_*.bin/png`。

### 2.3 G3（CARD-10..14）

```powershell
C:\Python314\python.exe run_g3.py
```

每轮：`x` 清栈 → `c`×3 → 断言 `CARDS=ALPHA|BETA|GAMMA` → `c` 第 4 次 →
断言 `CARDS=DELTA|ALPHA|BETA` 且 GAMMA 被淘汰 → `d` 导出 → PNG → layout_checks。
之后 `x`+`c`×4 打底，再依次单发 `c` 验证：

| 特例 | 期望 CARDS | 断言 |
| --- | --- | --- |
| c#5 同名 BETA | `BETA|DELTA|ALPHA` | 同名（title+project）更新置顶 |
| c#6 空 title | `-|BETA|DELTA` | 空 title 被接收；行 2 渲染 `(untitled)`（像素非空白断言） |
| c#7 长标题 | `THIS-IS-A-VERY-LONG-TITLE-0123456789|-|BETA` | 截断 `..` 且不越过 x+183（不覆盖时长） |
| c#8 CJK | `中文标题测试|THIS-IS-A-VERY-LONG-TITLE-0123456789|-` | 16×16 CJK 顶边 y+12 |

轮间发 `n` 心跳刷新链路（`c/x/p` 不刷新 last_valid，15 s 会误判失联）。

---

## 3. 协议要点与实测踩坑（Lead 真机反馈，已固化在驱动里）

1. **`d` 发完立即读，不 sleep**：设备会 ~0.1 s 内突发 115200 字节，主机延迟读会丢字节。
   `pet_serial.dump()` 发完命令直接进入读取循环。
2. **精确长度解析，不用 `FBEND` 字节序列截断**：先读 `FBBIN 240 240 115200` 头行 →
   精确读 115200 字节 → 再读 `FBEND <crc32> [off]` 行。像素数据里可能出现 `FBEND` 字节序列，
   搜索截断会误砍数据。
3. **CRC 校验**：主机 `zlib.crc32` 必须等于设备 `FBEND` 值；不等则整次重试（默认共 3 次尝试），
   仍失败抛 `FrameDumpError` 并给出设备/主机 CRC。
4. **日志行自动跳过**：STATUS/`FBEND` 之外的日志行被忽略（`--verbose` 可打印）；`p` 输出
   一行 `STATUS state= frame= cards= fps= heap=`，`cards>0` 时紧跟一行 `CARDS a|b|c`。
5. **空 title 的 CARDS 打印成 `-`**（firmware `main.c` 的调试占位），`run_g3.py` 断言时把
   `-` 归一化为空串（仅用于断言比较；证据 JSON 保留原始行）。
6. 时序常量：注入后 ≤125 ms 生效；done 边沿去抖 1500 ms；DONE 驻留 5000 ms；
   无有效事件 15000 ms → OFFLINE；`n` 心跳可刷新链路并在 OFFLINE 时恢复。
7. 设备每帧 8 fps；帧缓冲 = 240×240×2 = 115200 B，RGB565 **little-endian**。

---

## 4. `layout_checks.py` 断言清单

坐标全部来自 `docs/08 §3` / C03（含修订 R1–R4）。

| 检查名 | 区域 | 期望 |
| --- | --- | --- |
| `frame_size` | 整帧 | 115200 B |
| `network_dot` | `(4,4,4,4)` | 在线全 `0x07E0` / OFFLINE 全 `0xF800`（16/16 px） |
| `marker_working` | `(115,2,10,14)` | 有 `0xFFFF` 墨迹，落在单元内，纵向恰好 y=2..15，墨迹中心 ≈x=120 |
| `marker_input` | `(111,2,18,14)` | 有 `0xFD20` 墨迹，同上的落点/纵向/居中 |
| `marker_absent` | `(111,2,18,14)` | 其它 4 态不得有 `0xFFFF` / `0xFD20` |
| `offline_gray` | 整帧（网络点除外） | `r5==(g6>>1)` 且 `b5==(g6>>1)`（与固件 `gray565` 同法） |
| `card_empty[i]` | 3 个卡矩形 | `cards=0`/空位时全 `0x0000`（不画空框） |
| `card_base[i]` | `(x+2, y+1, 230, 2)` | 顶 2 px 带全卡底 `0x18E3`（文本不进入该带；OFFLINE 取灰化值） |
| `card_accent[0]` | `(x, y, 2, 28)` | 最新卡全 `0x05FF`（判据 `0x18E3` 之外唯一强调色） |
| `card_accent[1..2]` | 同上 | 非最新卡该 2 px 无强调色 |
| `card_row1_content[i]` | `(x+4, y+3, 224, 8)` | 有非卡底像素（`[前缀] 项目` 不为空；空 project 渲染 `-`） |
| `card_row2_content[i]` | `(x+4, y+12, 180, 16)` | 有非卡底像素（title 空 → `(untitled)`，不得空白） |
| `card_title_no_overflow` | `(x+184, y+12, 44, 16)` | 无白色标题像素（长标题截断，不覆盖时长区） |
| `pet_drawn` | `(72,20,96,128)` | 有非背景像素 |
| `bg.bar_*` | 状态条 `(0,0,240,12)` 的 4 段留空区 | 全 `0x0000`（右半 M3 前留空、与标记不重叠） |
| `bg.above_pet / below_pet / pet_left / pet_right` | 宠物四周 | 全 `0x0000`（区域不重叠） |
| `bg.card_left_margin / card_right_margin / footer` | 卡区左右边距、页脚 `(0,236,240,4)` | 全 `0x0000` |

> 标记断言用「存在 + 落在契约单元内 + 7 行×2 = 14 px 高 + 水平居中」，
> 不绑定具体点阵实现；`>` / `!?` 的方向由真机 PNG 目视确认（Lead D-05 修复后取帧）。

---

## 5. 设备未连接时的自测（全部离线，不碰 COM7）

### 5.1 `fb.py --selftest`（纯 stdlib PNG/差异）

```text
$ python fb.py --selftest
PASS frame size: 115200 bytes
PASS rgb565->rgb888: high-bit replication at 3 sample pixels
PASS png roundtrip: 240x240, 19436 bytes PNG, 57600 px identical
PASS diff pixels/bbox: 6/57600 px, bbox (10,20)-(11,22), regions=status=0, pet=0, cards=0, footer=0, other=6
PASS diff identical: same frame -> 0 px
PASS size reject: 1 KB buffer rejected with clear message
PASS missing file reject: missing file rejected
SELFTEST PASS 7/7
```

### 5.2 `layout_checks.py --selftest`（合成帧正例 + 逐项破坏反例）

合成帧按 `docs/08 §3` 构造（240×240 黑底 + 宠物 96×128@(72,20) + 网络点 + 标记 + 3 卡 + 页脚）；
反例每次只破坏一项，要求命中**对应**检查项。

```text
$ python layout_checks.py --selftest --png-dir evidence/selftest
positive_IDLE                      expect=PASS actual=PASS OK | all 18 checks passed
positive_WORKING                   expect=PASS actual=PASS OK | all 18 checks passed
positive_INPUT_REQUIRED            expect=PASS actual=PASS OK | all 18 checks passed
positive_DONE                      expect=PASS actual=PASS OK | all 18 checks passed
positive_FAILED                    expect=PASS actual=PASS OK | all 18 checks passed
positive_OFFLINE                   expect=PASS actual=PASS OK | all 19 checks passed
positive_cards3                    expect=PASS actual=PASS OK | all 28 checks passed
neg_marker_missing_WORKING         expect=FAIL actual=FAIL OK | marker_working [matched marker_working]
neg_marker_on_IDLE                 expect=FAIL actual=FAIL OK | marker_absent [matched marker_absent]
neg_marker_wrong_color             expect=FAIL actual=FAIL OK | marker_working [matched marker_working]
neg_card_base                      expect=FAIL actual=FAIL OK | card_base[0] [matched card_base[0]]
neg_accent_missing                 expect=FAIL actual=FAIL OK | card_accent[0] [matched card_accent[0]]
neg_accent_wrong_color             expect=FAIL actual=FAIL OK | card_accent[0] [matched card_accent[0]]
neg_dot_red_online                 expect=FAIL actual=FAIL OK | network_dot [matched network_dot]
neg_dot_green_offline              expect=FAIL actual=FAIL OK | network_dot [matched network_dot]
neg_offline_not_gray               expect=FAIL actual=FAIL OK | offline_gray [matched offline_gray]
neg_footer_dirty                   expect=FAIL actual=FAIL OK | bg.footer [matched bg.footer]
neg_status_bar_dirty               expect=FAIL actual=FAIL OK | bg.bar_content_gap [matched bg.bar_content_gap]
neg_card_empty_drawn               expect=FAIL actual=FAIL OK | card_empty[0] [matched card_empty[0]]
neg_title_blank                    expect=FAIL actual=FAIL OK | card_row2_content[0] [matched card_row2_content[0]]
neg_truncated                      expect=FAIL actual=FAIL OK | frame_size [matched frame_size]
SELFTEST PASS 21/21 cases
wrote 7 synthetic PNG(s) to evidence\selftest
```

（`evidence/selftest/synth_*.png` 为 6 态 + 3 卡合成帧，可人眼复核断言依据。）

### 5.3 `--simulate` 全链路自测（状态机 + 卡栈 + 证据管线）

```text
$ python run_g1.py --simulate --wait-ready m2:READY
summary: observations 24/24 PASS, layout 6/6 PASS, hash distinct=True, preflight=True, final_online=True
RESULT PASS

$ python run_g3.py --simulate
summary: rounds 20/20, specials 4/4, layout 24/24, failures=0, final_online=True
RESULT PASS
```

模拟器产物写入 `evidence/sim/`（`g1.json`、`g3.json`、PNG），**不代表真机验收**。

### 5.4 设备未连接时直接跑实跑脚本

```text
$ python run_g1.py --port COM99
ERROR: cannot open COM99 @ 115200 8N1: could not open port 'COM99': FileNotFoundError(...)
 (device connected? port busy? close idf.py monitor?)
提示：设备未连接时可先跑离线自测：python layout_checks.py --selftest  /  python run_g1.py --simulate
exit=2
```

退出码约定：`0` 成功/PASS；`1` 断言 FAIL；`2` 串口打不开；`3` 协议超时/CRC/导出失败（仅 `pet_serial.py` CLI）。

---

## 6. 本次实跑结果（2026-10-31，COM7，M2 固件）

### 6.1 G1（UI-10 / UI-11 / UI-12）—— **24/24 PASS**

- 时间：`2026-10-31T00:21:15.514+08:00`，模式 `serial`，COM7 @115200
- 观测 24/24 PASS（6 态 × 4），布局断言 6/6 PASS，**6 态 PNG 哈希互异**
- 证据：`evidence/g1.json`、`evidence/g1_summary.md`、`evidence/g1_obs01..06_*.png/.bin`

| 态 | frame | PNG | 帧 CRC32 | 帧 sha256(前16) | PNG sha256(前16) |
| --- | --- | --- | --- | --- | --- |
| IDLE | 0 | `g1_obs01_idle.png` | `a9f7eb93` | `b117f682b6f0ee61` | `4cd1ba7ab12bb6e5` |
| WORKING | 4 | `g1_obs02_working.png` | `24ee1ef9` | `055bf51bcc9d0d94` | `c568ca052a0cb0e3` |
| INPUT_REQUIRED | 5 | `g1_obs03_input_required.png` | `268307fb` | `412f18ebe07b1b13` | `dadeadbe3e7869e4` |
| DONE | 2 | `g1_obs04_done.png` | `b0ab754d` | `0cfb30c5cbc085e3` | `1a5ffe6bdfeff881` |
| FAILED | 3 | `g1_obs05_failed.png` | `1a193be3` | `261441057b8daa0a` | `002aa0969ca5c2d1` |
| OFFLINE | 1 | `g1_obs06_offline.png` | `a547ce94` | `e2975de053c4fd9b` | `9a789341f6e2b09a` |

### 6.2 G3（CARD-10..14）—— **20/20 轮 + 特例 4/4 + 布局 24/24 PASS**

- 时间：`2026-10-31T00:20:29.758+08:00`，模式 `serial`，COM7 @115200
- 20 轮全部 `c×3 → ALPHA|BETA|GAMMA`、`c#4 → DELTA|ALPHA|BETA` 且 GAMMA 淘汰，state=IDLE
- 证据：`evidence/g3.json`、`evidence/g3_summary.md`、`evidence/g3_round01..20.png/.bin`、
  `evidence/g3_special5..8_*.png/.bin`

| 特例 | 实测 CARDS | PNG |
| --- | --- | --- |
| c#5 同名 BETA 置顶 | `BETA|DELTA|ALPHA` | `g3_special5_beta_dedup.png` |
| c#6 空 title 占位 | `-|BETA|DELTA`（行 2 渲染 `(untitled)`） | `g3_special6_untitled.png` |
| c#7 长标题截断 | `THIS-IS-A-VERY-LONG-TITLE-0123456789|-|BETA`（显示 `THIS-IS-A-VERY-LONG-..`） | `g3_special7_long.png` |
| c#8 CJK 标题 | `中文标题测试|THIS-IS-A-VERY-LONG-TITLE-0123456789|-` | `g3_special8_cjk.png` |

> 真机 PNG 已人工核对：`>` / `!?` 方向正确（D-05 修复后），长标题 `..` 截断不覆盖时长，
> CJK 16×16 渲染正常，OFFLINE 整帧灰 + 红点。G1/G3 属 T/C 级证据；最终 H 目视结论仍需用户确认。

---

## 7. 命令行参考

```text
pet_serial.py [--port COM7] [--baud 115200] [--timeout 3.0]
              [--send "1pc"] [--status] [--dump out.bin]
              [--wait m2:READY] [--wait-timeout 15] [--dump-timeout 30]
              [--read-time 0.5] [--json] [--verbose] [--list-ports]

fb.py in.bin out.png [--crc HEX] [--json]
fb.py --diff A.bin B.bin [--json]
fb.py --selftest

layout_checks.py FRAME.bin --state IDLE|WORKING|INPUT_REQUIRED|DONE|FAILED|OFFLINE
                [--cards 0..3] [--titles A,B,C] [--json] [--quiet]
layout_checks.py --selftest [--png-dir DIR]

run_g1.py [--port COM7] [--baud 115200] [--obs-per-state 4] [--wait 0.4]
          [--outdir DIR] [--wait-ready [SPEC]] [--relax-frame] [--simulate] [--verbose]
run_g3.py [--port COM7] [--rounds 20] [--outdir DIR]
          [--wait-ready [SPEC]] [--simulate] [--verbose]
```

`run_g1/g3.py --outdir` 默认 `evidence/`（`--simulate` 默认 `evidence/sim/`）；
默认端口 `COM7`、115200 8N1；导入用法：

```python
from pet_serial import PetSerial
with PetSerial("COM7") as dev:          # 自动 open/close
    st = dev.status()                    # {"state","frame","cards","titles",...}
    info = dev.dump("frame.bin")         # 校验 CRC32，失败自动重试（共 3 次）
    dev.send("1"); dev.send("c")
```

---

## 8. 假设 / 已知限制

- **G1 分母为 24**（6 态 × 4）：docs/08 §6 C1 已登记「20 次」算术冲突，按 24/24 记录。
- **G3 依赖 `x` 重置预置卡序号**：firmware `main.c` 中 `case 'x'` 清栈且 `s_preset_idx = 0`，
  故每轮都从 `GAMMA/BETA/ALPHA/DELTA` 开始；若固件改掉该行为，run_g3 第 1 轮即会在 CARDS
  断言处报错（不会静默通过）。
- **frame 期望表**：`IDLE ∈ {0,1}`（眨眼/小发呆）；`WORKING=4`；`INPUT_REQUIRED=5`；
  `DONE=2`；`FAILED=3`；`OFFLINE=1`。`--relax-frame` 可把 frame 不符降级为警告。
- 帧导出的 PNG 是**某一瞬间**的帧：IDLE 的毛发相位/眨眼会变，故 6 态哈希互异只比较
  「每态一张样本帧」，不要求同态多帧一致。
- `d` 期间固件静音日志后恢复；主机若正在其它程序里打开 COM7（如 `idf.py monitor`）会打不开，
  需先关闭占用进程。
- 长标题「不越界」断言以 C03 §2 的标题色 `0xFFFF` / 时长色 `0x8410` 为前提；若实现改色需同步改断言。
- `pet_sim.py` 只用于验证 host 侧时序/解析/断言/证据管线，**不能**替代真机验收；
  `evidence/sim/` 与 `evidence/` 真机产物分开存放。
