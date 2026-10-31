# tools/host — M2/M3 主机侧工具（串口注入 / 帧导出 / PNG / G1–G6 自动跑测 / 真机按键引导）

用于 **ESP32 桌面宠物 M2 + M3**（240×240 值班页 + 6 态 + 3 卡栈；M3 两键/菜单/番茄钟）的
主机侧验证工具。协议与版式依据：

- `docs/08-m2-development.md` §3（版式落地取值）、§4（串口注入协议）、§5（用例）
- `docs/10-m3-development.md` §4（M3 串口注入：`K/L/J/H/m/y/R/t`）、§5（番茄钟页/菜单页版式）、§7（G5/G6）
- `docs/contracts/C03-layout-240x240.md`（M2 版式契约，含 §7 修订）
- `docs/contracts/C07-keymap.md`（M3 键表/菜单项，冻结）
- `firmware/components/pet_ui/include/pet_types.h`（状态枚举顺序）

**依赖**：Windows + 系统 Python `C:\Python314\python.exe`（已装 `pyserial 3.5`）。
`fb.py` / `layout_checks.py` / `layout_checks_m3.py` / `m3_common.py` / `run_*.py` 只用标准库
（**无 Pillow / numpy**）；仅串口驱动 `pet_serial.py` 使用 pyserial。`run_key_h.py` 用标准库
`msvcrt` 读控制台按键（Windows）。

---

## 1. 文件清单

| 文件 | 作用 |
| --- | --- |
| `pet_serial.py` | 串口驱动（COM7 @115200 8N1）：发单字符命令、逐行读回并自动跳过日志行、`p` 解析 STATUS/CARDS、`m` 解析 M3 MODE 行、`d` 导出帧（校验长度+CRC32，失败重试）、`drain` 抓日志、`reboot`（`R` 软重启→重连→等 READY）。可被其它脚本 `import`。 |
| `fb.py` | 115200 B RGB565-LE 帧 → PNG（`zlib`/`struct` 手写，高比特复制 RGB565→RGB888）；`--diff` 输出不同像素数/比例/包围盒/分区统计；`--selftest`。 |
| `layout_checks.py` | M2 值班页像素断言（卡底/强调条/网络点/OFFLINE 灰度/头顶标记/页脚/背景/卡内容非空白），逐项 PASS/FAIL + 坐标证据；`--selftest` 合成帧正例+反例。 |
| `layout_checks_m3.py` | **M3** 番茄钟页/菜单页像素断言（圆环心/半径/环厚/进度角与方向/倒计时/相位标题/底部统计；菜单 5 行矩形/选中反色/行间留空/底部统计），`--selftest` 正例 36 例；供 `run_g5/g6` 与手工复核。 |
| `pet_sim.py` | 无设备模拟器：M2 状态机/卡栈 + **M3** 模式/菜单/番茄钟/静音与统计 NVS 模拟、合成键、`m/y/R/t`；供 `run_*.py --simulate` 全链路自测；**不是**被测固件。 |
| `m3_common.py` | M3 跑测公共封装：打开设备、`n` 心跳、读 `m`、合成键、模式归位、相位等待、帧导出→PNG→像素断言、证据写盘。 |
| `run_g1.py` | UI-10：6 态 × 4 次 = **24 次观测**；每态导出 1 张 bin→PNG→layout_checks；6 态 PNG 哈希互异；输出 `evidence/g1.json` + `g1_summary.md`。 |
| `run_g3.py` | CARD-10..14：**20 轮**卡栈（x→c×3→断言→c 第 4 次→淘汰断言→导出 PNG→layout_checks）+ c#5/#6/#7/#8 特例；输出 `evidence/g3.json` + `g3_summary.md`。 |
| `run_g6.py` | **G6**：13 项合成键 × 20 次（两键短/长、菜单选中/执行/关闭、静音持久化），每次读 `m` 断言；每项 ≥19/20；输出 `evidence/g6.json` + `g6_summary.md` + 9 张 PNG。 |
| `run_g5.py` | **G5**：`t` 短时长（focus 6 s / break 3 s）跑 完成/暂停/放弃/继续 + `R` 重启持久化 + `y` 跨日清零；输出 `evidence/g5.json` + `g5_summary.md` + 4 张 PNG。 |
| `run_key_h.py` | **H 级真机按键人工测试引导**：打印"请短按 KEY1…"逐项提示，轮询 `m` 的 `key1/key2/pwr` 原始状态检测按下，按 `--plan short/long/menu/pwr` 判定 KEY-01/02/04/05；`--selftest` 用脚本化假按键做离线验证（不碰串口）；输出 `evidence/key_h.json` + `key_h_summary.md`。 |
| `evidence/` | 帧 `.bin`、`.png`、JSON 与摘要表；`evidence/sim/` 为模拟器自测产物（**不是**验收证据）；`evidence/selftest/` 为合成帧 PNG。 |

---

## 2. 快速开始（设备就绪时）

在 `tools\host` 目录下执行（也可从仓库根用完整路径）。**不要**与 `idf.py flash` /
`idf.py monitor` 并行（同一 COM7 会打不开）。

### 2.1 单步调试（M2 + M3）

```powershell
C:\Python314\python.exe pet_serial.py --mode                  # 发 m，打印 MODE 行
C:\Python314\python.exe pet_serial.py --status                # 发 p，打印 STATUS/CARDS
C:\Python314\python.exe pet_serial.py --send "KLJ"           # 发命令串并回显日志
C:\Python314\python.exe pet_serial.py --reboot --mode         # R 软重启→等 READY→打印 MODE
C:\Python314\python.exe pet_serial.py --dump out.bin          # d 导出 115200 B（CRC 校验）
C:\Python314\python.exe fb.py out.bin out.png                 # bin → PNG
C:\Python314\python.exe layout_checks.py out.bin --state WORKING --cards 0
C:\Python314\python.exe layout_checks_m3.py out.bin --page pomo --phase FOCUS ^
    --remain-ms 3000 --total-ms 6000 --done 1 --focus-s 6
C:\Python314\python.exe layout_checks_m3.py out.bin --page menu --selected 2
C:\Python314\python.exe fb.py --diff a.bin b.bin               # 两帧逐像素差异
```

M3 命令（`docs/10 §4`）：`K`=KEY1 短、`L`=KEY1 长、`J`=KEY2 短、`H`=KEY2 长、
`m`=打印 MODE 行、`y`=模拟跨日、`R`=软重启、`t`=缩短本段（focus 6 s/break 3 s）。

`--wait m2:READY` 只捕获**启动日志**（`I (xxx) m2: READY ...`）。设备已在运行、READY 已过去时
直接 `--mode`/`--status` 即可，或 `--wait READY --wait-timeout 20` 用于复位后立即抓取。

### 2.2 G6（合成键 / 菜单 / 静音持久化）

```powershell
C:\Python314\python.exe run_g6.py                    # COM7，20 次/项 → evidence/g6.json
C:\Python314\python.exe run_g6.py --iterations 2     # 快速冒烟
C:\Python314\python.exe run_g6.py --simulate         # 无设备自测 → evidence/sim/
```

13 项（每项 20 次，门槛每项 ≥19/20）：

| 项 | 动作 | 判定 |
| --- | --- | --- |
| `duty_key1_short` | 值班 `K` | 只记 `SEND` 日志；mode/menu/phase/muted/done 不变 |
| `duty_key1_long` | 值班 `L` | `muted` 0↔1 翻转；随后 `R` 重启断言仍为 1（见持久化） |
| `duty_key2_short` | 值班 `J` | → POMO；再 `J` → DUTY |
| `duty_key2_long` | 值班 `H` | 开菜单 `menu=1`；再长按关闭 |
| `pomo_key1_short` | POMO `K`×3 | `IDLE→FOCUS→FOCUS_PAUSED→FOCUS`；`L` 放弃后 done 不变 |
| `pomo_key1_long` | POMO `K`→`L` | → IDLE 且 done 不变 |
| `pomo_key2_short` | POMO `J` | → DUTY（再 `J` 回 POMO 备下一轮） |
| `menu_key1_short` | 开菜单后 `K`×20 | `sel` 循环 0→1→2→3→4→0；导出 sel0..4 帧 |
| `menu_key2_long` | POMO 下 `H` 开、`H` 关 | menu 1→0，模式保持 POMO |
| `menu_exec_mute` | sel=0 `J` | muted 翻转且**留在菜单** |
| `menu_exec_back` | sel=4 `J` | 关闭菜单回 DUTY |
| `menu_exec_wifi` | sel=2 `J` | 关闭菜单 + 串口日志含 `M5` |
| `menu_exec_bt` | sel=3 `J` | 关闭菜单 + 串口日志含 `M5` |

预检：`m` 若显示 `total_ms != 1500000`（上一轮 G5 用过 `t`），自动 `R` 重启恢复默认 25/5，
避免 G6 期间 focus 提前到点导致误判（证据 `preflight.restore_default_durations`）。
帧证据：`g6_duty.png`（值班页 layout_checks）、`g6_pomo_idle/focus/paused.png`、
`g6_menu_sel0..4.png`（`layout_checks_m3`）。

### 2.3 G5（番茄钟完成/暂停/放弃/继续 + 重启持久化 + 跨日清零）

```powershell
C:\Python314\python.exe run_g5.py                    # COM7 → evidence/g5.json
C:\Python314\python.exe run_g5.py --reset --wait-ready   # 先 R 重启并等 READY
C:\Python314\python.exe run_g5.py --simulate         # 无设备自测 → evidence/sim/
```

时序（`t` 把本段改为 focus 6 s / break 3 s）：

1. `J` 进 POMO → `K` 启动 FOCUS（断言 `total_ms=6000`）
2. 跑 1 s → `K` 暂停（`FOCUS_PAUSED`）→ **再等 1 s 断言 `remain_ms` 完全不变**
3. `K` 继续 → 等到点（`FOCUS_DONE`，done +1、focus_s +6，`remain_ms=0`）
4. `K` 启动 BREAK（`total_ms=3000`）→ `L` 放弃 → IDLE，done 不变
5. 再跑一段并完成（done +2、focus_s +12）→ `J` 回 DUTY
6. `R` 重启 → 重连断言 done/focus_s/muted **NVS 持久**
7. `y` 模拟跨日 → done=0、focus_s=0、has_day=1、day+1 → 再 `R` 断言清零已落盘

统计断言全部用**增量**（NVS 里可能有上次会话残留）；帧证据：`g5_pomo_idle.png`（进度 0）、
`g5_pomo_focus_paused.png`、`g5_pomo_done.png`（进度 1000）、`g5_pomo_break.png`
（运行中帧，进度不断言）。

### 2.4 run_key_h（H 级真机按键人工测试）

设备端由**人**按键；脚本逐项提示并用 `m` 的 `key1/key2/pwr` 原始状态（0/1）检测按下，
再用 mode/menu/sel/phase/muted/done 判定动作是否符合 C07。

```powershell
C:\Python314\python.exe run_key_h.py --plan short --count 5
C:\Python314\python.exe run_key_h.py --plan short long menu pwr --count 20   # 验收口径
C:\Python314\python.exe run_key_h.py --plan pwr --count 5
C:\Python314\python.exe run_key_h.py --selftest        # 离线自测（脚本化假按键，不碰串口）
```

`--selftest` 用脚本化"假按键"跑四个计划（含 PWR 关机→复位恢复）与一个"按错键"反例，
验证提示/判定/证据管线；**不能**替代真人按键的 H 级结论。

| 计划 | 人工步骤 | 判定 |
| --- | --- | --- |
| `short` | 值班短按 KEY1 → 短按 KEY2 → 再短按 KEY2 | KEY1 无状态变化；KEY2 DUTY→POMO→DUTY |
| `long` | 值班长按 KEY1 → 长按 KEY2 → 菜单内长按 KEY2 | muted 翻转；menu 1→0 |
| `menu` | 长按 KEY2 开菜单 → 短按 KEY1 ×4 → 短按 KEY2 | sel 0→1→2→3→4；执行 BACK 关闭 |
| `pwr` | 短按 PWR ×N → 长按 PWR ≥1.5 s | 短按无应用动作；长按进入关机流程（日志/串口无响应）→ 提示复位等 READY 恢复 |

按键建议：短按按住 0.2–0.4 s（<600 ms），长按 0.8–1.2 s（≥600 ms）。
`PWR` 长按后 USB 供电**不会真断电**（背光灭、GPIO2 低），脚本会提示按 RST/拔插 USB 复位。
运行中按 `s` 跳过当前次、`q` 退出（已测结果仍写盘）。
证据：`evidence/key_h.json`、`evidence/key_h_summary.md`。

### 2.5 M2 回归（G1/G3）

```powershell
C:\Python314\python.exe run_g1.py                    # UI-10：24 次观测
C:\Python314\python.exe run_g3.py                    # CARD-10..14：20 轮 + 特例
C:\Python314\python.exe run_g1.py --simulate         # 无设备自测
C:\Python314\python.exe run_g3.py --simulate
```

G1：`x` 清栈 → `5`→`u` 校准链路 → 每轮按 `IDLE/WORKING/INPUT_REQUIRED/DONE/FAILED/OFFLINE`
注入并断言 `p` 的 `state/frame/cards`，每态导出 1 张帧做 layout_checks。
G3：每轮 `x`→`c`×3→断言→`c` 第 4 次→淘汰断言→导出帧，另跑 c#5..#8 特例。
轮间发 `n` 心跳刷新链路（`c/x/p` 不刷新 last_valid，15 s 会误判失联）。

### 2.6 复跑入口与退出码（验收/回归）

最终固件刷新后，**直接带上 `--port COM7` 复跑即可**（默认端口就是 COM7，显式写出便于留档）：

```powershell
C:\Python314\python.exe run_g6.py --port COM7     # G6，20 次/项，约 7 min
C:\Python314\python.exe run_g5.py --port COM7     # G5，约 25 s
```

| 退出码 | 含义 |
| --- | --- |
| `0` | 全部断言 PASS（证据 JSON/MD/PNG 已写盘） |
| `1` | 存在断言 FAIL（**证据照样写盘**，失败明细在 `*_summary.md` 与 JSON） |
| `2` | 串口打不开（设备未连接/被 `idf.py monitor` 占用/驱动问题） |
| `3` | 协议超时/CRC/导出失败（**仅** `pet_serial.py` CLI；`run_*.py` 内为退出码 1 + 证据） |

- 输出目录：默认 `tools/host/evidence/`（`--simulate` 时默认 `evidence/sim/`；`--outdir` 可改）。
- 幂等性：G5 统计断言用**增量基线**（可重复跑，NVS 残留不影响判定）；G6 结束会把
  `muted` 置 1（静音持久化用例），不影响下一次 G5/G6。
- 顺序：先 G6 后 G5 最省事；反之 G5 的 `t` 会让 `total_ms=6000`，G6 预检会自动 `R` 重启
  恢复默认 25/5（多约 4 s，证据 `preflight.restore_default_durations`）。
- 两项都会按 3 s 节流发 `n` 心跳（避免 M2 的 15 s OFFLINE 干扰），**不要**同时开
  `idf.py monitor`。

---

## 3. 协议要点与实测踩坑（已固化在驱动/脚本里）

1. **`d` 发完立即读，不 sleep**：设备会 ~0.1 s 内突发 115200 字节，主机延迟读会丢字节。
   `pet_serial.dump()` 发完命令直接进入读取循环。
2. **精确长度解析，不用 `FBEND` 字节序列截断**：先读 `FBBIN 240 240 115200` 头行 →
   精确读 115200 字节 → 再读 `FBEND <crc32> [off]` 行。像素数据里可能出现 `FBEND` 字节序列，
   搜索截断会误砍数据。
3. **CRC 校验**：主机 `zlib.crc32` 必须等于设备 `FBEND` 值；不等则整次重试（默认共 3 次尝试），
   仍失败抛 `FrameDumpError` 并给出设备/主机 CRC。
4. **日志行自动跳过**：STATUS/`FBEND` 之外的日志行被忽略（`--verbose` 可打印）；`p` 输出
   一行 `STATUS state= frame= cards= fps= heap=`，`cards>0` 时紧跟一行 `CARDS a|b|c`。
   需要抓日志时用 `dev.drain(duration)`（先 drain 再发 `m`，因为 `m` 读取会清输入缓冲）。
5. **空 title 的 CARDS 打印成 `-`**（firmware 调试占位），`run_g3.py` 断言时把 `-` 归一化为空串
   （仅用于断言比较；证据 JSON 保留原始行）。
6. 时序常量：注入后 ≤125 ms 生效；done 边沿去抖 1500 ms；DONE 驻留 5000 ms；
   无有效事件 15000 ms → OFFLINE；`n` 心跳可刷新链路并在 OFFLINE 时恢复。
7. 设备每帧 8 fps；帧缓冲 = 240×240×2 = 115200 B，RGB565 **little-endian**。
8. **M3 `m` 行实际格式（2026-10-31 真机）比 `docs/10 §4` 多 4 个字段**（Lead 追加，已实测）：
   ```text
   MODE mode=DUTY menu=0 sel=0 phase=IDLE remain_ms=1500000 total_ms=1500000 done=0 focus_s=0 muted=1 day=2 has_day=1 key1=0 key2=0 pwr=0
   ```
   `sel`=当前菜单选中行（打开菜单重置 0），`key1/key2/pwr`=原始按下状态。`parse_mode_line()`
   两种格式都解析（缺字段时对应值为 `None`）；本文件差异**已记录**，协议未改动。
9. **`m` 行是唯一 M3 状态来源**；`p` 仍是 M2 的 STATUS（模式/番茄钟不影响 STATUS）。
   跑测中要判 M3 状态一律读 `m`。
10. **`d` 导出的是"当前页面"**：DUTY→值班页、POMO→番茄钟页、MENU→菜单页（真机实测）。
    番茄钟/菜单页用 `layout_checks_m3.py` 断言，值班页才用 `layout_checks.py`。
11. **长跑必须心跳**：M2 契约 15 s 无有效事件进 OFFLINE（值班页整屏变灰、STATUS=OFFLINE），
    `m` 读取**不**刷新链路。`m3_common.Runner` 按 3 s 节流发 `n` 保活；G5/G6 长跑与
    6.5 s 等待都经过它。
12. **`t` 只改运行期时长**（focus 6 s / break 3 s，`pet_pomo_set_durations`），`R` 重启后
    恢复编译默认 25/5。`run_g6.py` 预检发现 `total_ms != 1500000` 时自动 `R` 恢复。
13. **`R` 软重启 = USB-Serial/JTAG 重新枚举**：`pet_serial.reboot()` 发 `R` 后关端口、
    按 0.25 s 重试重开（最多 40 次/20 s），再等 `READY`；若 READY 在重连窗口已错过，
    退化为用 `m` 应答确认存活（证据 `ready_caught=False`），不静默失败。
14. **菜单选中行判定**：真机 `m` 有 `sel` 字段（首选）；旧固件无该字段时，
    `m3_common.Runner.menu_selected()` 回退为导出菜单帧 + `layout_checks_m3.infer_menu_selected()`
    （按选中行反色底 0xFFFF 占比推断）。
15. **`run_key_h.py` 依赖原始按键字段**：`m` 需含 `key1/key2/pwr`；缺失时脚本直接报错说明
    （不猜按键）。短按检测靠轮询 `m`（40 ms），故建议按住 0.2–0.4 s，避免落在两帧之间。

---

## 4. `layout_checks.py` 断言清单（M2 值班页）

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
| `bg.bar_*` | 状态条 `(0,0,240,12)` 的 4 段留空区 | 全 `0x0000` |
| `bg.above_pet / below_pet / pet_left / pet_right` | 宠物四周 | 全 `0x0000` |
| `bg.card_left_margin / card_right_margin / footer` | 卡区左右边距、页脚 `(0,236,240,4)` | 全 `0x0000` |

---

## 5. `layout_checks_m3.py` 断言清单（M3 番茄钟页 / 菜单页）

坐标来自 `docs/10 §5`（M3 自定版式，C03 只覆盖值班页）。

### 5.1 番茄钟页（`--page pomo --phase ...`）

| 检查名 | 区域/方法 | 期望 |
| --- | --- | --- |
| `frame_size` | 整帧 | 115200 B |
| `bg.*` | 标题上方/标题与环顶/环底与页脚/页脚下方/环左右外侧 | 全 `0x0000` |
| `ring_present` | 环中线 r=71，每 2° 采样 180 点 | 全为轨道 `0x4208` 或进度色 |
| `ring_bbox` | 环色包围盒（搜索区 `(36,28,168,160)`） | 中心 `(120,104)`±2.5，尺寸 ≈153×153±4 |
| `ring_thickness` | 270° 方向沿半径数环色像素 | 6..14 px（期望 10） |
| `progress_fraction` | 进度色像素占比 vs `(total-remain)/total` | 误差 ≤0.05（18°） |
| `progress_direction` | 期望进度角 ±28.8° 取点 | 前侧=进度色、后侧=轨道色（**12 点顺时针**） |
| `progress_color_full/zero/present` | 进度色占比 | 1000→全进度色 / 0→无进度色 / 中间→有进度色 |
| `ring_track_all` | IDLE（无进度） | 环上全为轨道色 `0x4208` |
| `phase_title` | 标题带 `(0,14,240,12)` | 非空且含相位色：FOCUS `0xFD20` / BREAK `0x07E0` / 其它 `0x8410` |
| `countdown_text` | 中心窗 `(76,90,88,28)` | 有白色像素，且白像素只出现在该窗（`M:SS` scale2） |
| `footer_stats` | `y≈200` 带 | 有灰 `0x8410` 像素（`DONE <n>  <m>M`） |

### 5.2 菜单页（`--page menu [--selected i]`）

| 检查名 | 区域 | 期望 |
| --- | --- | --- |
| `frame_size` | 整帧 | 115200 B |
| `bg.*` | 标题上方/标题与首行/底部/左右边距 | 全 `0x0000` |
| `menu_title` | `(0,10,240,20)` | 有白像素（`MENU` scale2） |
| `selected_row` | 5 行反色底占比 | 推断选中行 == `--selected` |
| `row_i` | `(4, 48+i*32, 232, 28)` | 选中行底 `0xFFFF` ≥50%；其余行卡底 `0x18E3` ≥50% |
| `row_i_inverse_text` | 选中行内 | 有 `0x0000` 字像素 ≥8（选中行反色：底白字黑） |
| `row_i_text` | 非选中行内 | 有非卡底像素 ≥8（行文本非空） |
| `row_gap` | 行间 4 px（`y=76/108/140/172`） | 全 `0x0000` |
| `row_footer_gap` | 末行与页脚之间 | 全 `0x0000` |
| `footer_stats` | `y≈212` 带 | 有灰 `0x8410` 像素（`DONE <n>  <m>M   <DUTY|POMO>`） |

### 5.3 自测（合成帧正例 + 逐项破坏反例）

```text
$ python layout_checks_m3.py --selftest
SELFTEST PASS 36/36 cases
```

- 番茄钟正例 11 个：IDLE/FOCUS 进度 0、500、1000、167、833（6 s 演示）、BREAK 500、
  FOCUS_PAUSED 250、FOCUS_DONE 1000、BREAK_DONE 1000、25:00 倒计时。
- 菜单正例 6 个：sel 0..4 + muted/POMO 组合。
- 反例 19 个：每例只破坏一项（尺寸、环缺失/偏心、进度清零/满/反向、倒计时缺失、
  标题缺失/错色、页脚缺失、底色脏、行缺失/底色错/选中未反色/选中不符等），
  要求命中**对应**检查项。
- `--png-dir evidence/m3_selftest` 可把正例合成帧写成 PNG 供人眼复核。

---

## 6. 设备未连接时的自测（全部离线，不碰串口）

```text
$ python fb.py --selftest                            # SELFTEST PASS 7/7
$ python layout_checks.py --selftest                 # SELFTEST PASS 21/21 cases
$ python layout_checks_m3.py --selftest              # SELFTEST PASS 36/36 cases
$ python run_g1.py --simulate                        # observations 24/24 PASS, layout 6/6 PASS
$ python run_g3.py --simulate                        # rounds 20/20, specials 4/4, layout 24/24
$ python run_g6.py --simulate                        # items 13/13, iterations 260/260 PASS, layout 9/9
$ python run_g5.py --simulate                        # steps 17/17 PASS, layout 4/4 PASS
$ python run_key_h.py --selftest                     # SELFTEST PASS 5/5 cases
```

模拟器产物写 `evidence/sim/`（`g1.json`、`g3.json`、`g5.json`、`g6.json` 与 PNG），
**不代表真机验收**。`run_key_h.py` 必须有真人按键；`--selftest` 只用脚本化假按键验证
判定/证据管线（5/5 例：short / long / menu / pwr / 按错键反例），不产生真实 H 证据。

设备未连接时直接跑实跑脚本会明确报错（退出码 2），例如：

```text
$ python run_g6.py --port COM99
ERROR: cannot open COM99 @ 115200 8N1: could not open port 'COM99': FileNotFoundError(...)
 (device connected? port busy? close idf.py monitor?)
提示：设备未连接时可先跑离线自测：python run_g6.py --simulate --iterations 2
```

退出码约定：`0` 成功/PASS；`1` 断言 FAIL/未完成；`2` 串口打不开；
`3` 协议超时/CRC/导出失败（仅 `pet_serial.py` CLI）。

---

## 7. 本次实跑结果（2026-10-31，COM7 @115200，M3 固件）

> 设备：ESP32-S3-Touch-LCD-1.54；M3 固件由 Lead 刷入（USB 供电）。
> 证据等级：G5/G6 属 **H+T**（真机合成键 + 帧缓冲像素断言）；最终 H（人眼）仍需用户目视确认。
>
> **预刷机快照（2026-10-31 07:50–07:58）**：本节数据是最终固件（含 day 未知哨兵与 UI 修复）
> 刷入**之前**的快照；最终固件刷新后由 Lead 用 §2.6 的同一条命令复跑 G5/G6，
> 并以新的 `evidence/g5.json`、`evidence/g6.json`、`*_summary.md`、PNG 覆盖本节数据。
> 复跑判据不变（G5 17/17 步 + 4/4 帧；G6 13/13 项 ×20 次 + 9/9 帧 + 静音持久化）。

### 7.1 G5（番茄钟完成/暂停/放弃/继续 + 重启持久化 + 跨日清零）—— **17/17 PASS，布局 4/4 PASS**

- 时间：`2026-10-31T07:50:55.657+08:00` → `2026-10-31T07:51:18.015+08:00`
- `t` 后 `total_ms=6000`；focus 到点 `done=1 focus_s=6 remain_ms=0`；
  暂停 1 s 后 `remain_ms` 完全不变（4670 → 4670）；
  BREAK 放弃不计完成；第二段完成 `done=2 focus_s=12`；
  `R` 重启后 done/focus_s/muted 全部持久；`y` 后 done=0 focus_s=0 day=2，再 `R` 后仍为 0。
- 证据：`evidence/g5.json`、`evidence/g5_summary.md`、`g5_pomo_*.png/.bin`

| 帧 | 相位 | 期望/实测进度 | PNG | 帧 CRC32 |
| --- | --- | --- | --- | --- |
| `g5_pomo_idle` | IDLE | 0.000 / 0.000 | `g5_pomo_idle.png` | `d96adc3c` |
| `g5_pomo_focus_paused` | FOCUS_PAUSED | 0.222 / 0.222 | `g5_pomo_focus_paused.png` | `f10b8599` |
| `g5_pomo_done` | FOCUS_DONE | 1.000 / 1.000 | `g5_pomo_done.png` | `612be77d` |
| `g5_pomo_break` | BREAK | 运行中不断言 / 0.167 | `g5_pomo_break.png` | `95596865` |

### 7.2 G6（合成键 / 菜单 / 静音持久化）—— **13/13 项、260/260 次 PASS，布局 9/9 PASS**

- 时间：`2026-10-31T07:51:23.222+08:00` → `2026-10-31T07:58:20.856+08:00`
- 每项 20 次全部 PASS（`duty_key1_short/long`、`duty_key2_short/long`、`pomo_key1_short/long`、
  `pomo_key2_short`、`menu_key1_short`、`menu_key2_long`、`menu_exec_mute/back/wifi/bt`）。
  菜单选中 `sel` 循环 0→1→2→3→4→0；MUTE 项 muted 翻转且留在菜单；WIFI/BT 关闭菜单且
  日志出现 `M5 not implemented`；BACK 关闭。
- 静音持久化：`R` 重启后 `muted=1`（重启前 1，额外翻转 0 次）。
- 预检：`total_ms=1500000`（默认 25/5，未触发自动恢复）。
- 证据：`evidence/g6.json`、`evidence/g6_summary.md`、`g6_run.log`、`g6_*.png/.bin`

| 帧 | 页面 | PNG | 帧 CRC32 |
| --- | --- | --- | --- |
| `g6_duty` | 值班页 | `g6_duty.png` | `1a3e53bf` |
| `g6_pomo_idle` | 番茄钟 IDLE | `g6_pomo_idle.png` | `eff88910` |
| `g6_pomo_focus` | 番茄钟 FOCUS | `g6_pomo_focus.png` | `5b6e9c64` |
| `g6_pomo_paused` | 番茄钟 FOCUS_PAUSED | `g6_pomo_paused.png` | `c1171c9a` |
| `g6_menu_sel0..4` | 菜单（选中 0..4） | `g6_menu_sel0..4.png` | `75f03e5b` `b5a34a6a` `8c6409cf` `7be1b2fc` `7786f07b` |

> 真机 PNG 已人工核对：番茄钟页 `DONE` 全灰环 + `0:00`；`FOCUS` 橙标题 + `24:59`；
> 菜单页 5 行、选中行反色（白底黑字）、底部 `DONE 0  0M  DUTY`，与 `docs/10 §5` 一致。

### 7.3 M2 回归（同日 M2 段，供对照）

G1 **24/24 PASS**、6 态 PNG 哈希互异；G3 **20/20 轮 + 特例 4/4 + 布局 24/24 PASS**。
详见 `evidence/g1_summary.md`、`evidence/g3_summary.md`（M2 交付时留档）。

### 7.4 run_key_h（H 级人工按键）

`run_key_h.py` 已提供但**按键必须由人执行**；本会话未代替用户按键，故 `key_h.json`
需用户实跑后生成。脚本的提示/检测/超时路径已用 `--press-timeout 1.5` 做过非交互冒烟
（超时如实记 FAIL，不静默通过）。

---

## 8. 命令行参考

```text
pet_serial.py [--port COM7] [--baud 115200] [--timeout 3.0]
              [--send "1pc"] [--status] [--mode] [--dump out.bin]
              [--reboot [SPEC]] [--wait SPEC] [--wait-timeout 15]
              [--dump-timeout 30] [--read-time 0.5] [--json] [--verbose]
              [--list-ports]

fb.py in.bin out.png [--crc HEX] [--json]
fb.py --diff A.bin B.bin [--json]
fb.py --selftest

layout_checks.py FRAME.bin --state IDLE|WORKING|INPUT_REQUIRED|DONE|FAILED|OFFLINE
                [--cards 0..3] [--titles A,B,C] [--json] [--quiet]
layout_checks.py --selftest [--png-dir DIR]

layout_checks_m3.py FRAME.bin --page pomo --phase PHASE
                   [--remain-ms N] [--total-ms N] [--done N] [--focus-s N]
                   [--json] [--quiet]
layout_checks_m3.py FRAME.bin --page menu [--selected 0..4] [--mode DUTY|POMO]
                   [--json] [--quiet]
layout_checks_m3.py --selftest [--png-dir DIR]

run_g1.py [--port COM7] [--baud 115200] [--obs-per-state 4] [--wait 0.4]
          [--outdir DIR] [--wait-ready [SPEC]] [--relax-frame] [--simulate] [--verbose]
run_g3.py [--port COM7] [--rounds 20] [--outdir DIR]
          [--wait-ready [SPEC]] [--simulate] [--verbose]
run_g6.py [--port COM7] [--iterations 20] [--wait 0.25] [--dump-timeout 30]
          [--outdir DIR] [--wait-ready [SPEC]] [--simulate] [--verbose]
run_g5.py [--port COM7] [--wait 0.3] [--focus-timeout 10] [--reset]
          [--outdir DIR] [--wait-ready [SPEC]] [--simulate] [--verbose]
run_key_h.py [--port COM7] [--plan short|long|menu|pwr ...] [--count 5]
             [--press-timeout 30] [--shutdown-window 3] [--recover-timeout 60]
             [--outdir DIR] [--wait-ready [SPEC]] [--verbose]
```

`run_*.py --outdir` 默认 `evidence/`（`--simulate` 默认 `evidence/sim/`）；
默认端口 `COM7`、115200 8N1；导入用法：

```python
from pet_serial import PetSerial
with PetSerial("COM7") as dev:          # 自动 open/close
    st = dev.status()                    # M2: {"state","frame","cards","titles",...}
    mt = dev.mode()                      # M3: {"mode","menu","sel","phase","remain_ms",
                                         #      "total_ms","done","focus_s","muted","day","has_day",
                                         #      "key1","key2","pwr",...}
    info = dev.dump("frame.bin")         # 校验 CRC32，失败自动重试（共 3 次）
    logs = dev.drain(0.5)                # 抓 0.5 s 内的日志行
    dev.send("K"); dev.send("J")
    rb = dev.reboot()                    # R → 重连 → 等 READY
```

---

## 9. 假设 / 已知限制（如实）

- **`m` 行比 `docs/10 §4` 多 `sel/key1/key2/pwr` 字段**（Lead 2026-10-31 追加，真机实测）：
  工具兼容两种格式并在 `pet_serial` 文档注明；若后续固件移除这些字段，
  菜单选中断言自动回退为帧推断，`run_key_h.py` 会直接报"缺原始按键字段"。
- **G1 分母为 24**（6 态 × 4）：docs/08 §6 C1 已登记「20 次」算术冲突，按 24/24 记录。
- **G3 依赖 `x` 重置预置卡序号**：firmware `case 'x'` 清栈且 `s_preset_idx = 0`；
  若固件改掉该行为，run_g3 第 1 轮即会在 CARDS 断言处报错（不会静默通过）。
- **frame 期望表**：`IDLE ∈ {0,1}`（眨眼/小发呆）；`WORKING=4`；`INPUT_REQUIRED=5`；
  `DONE=2`；`FAILED=3`；`OFFLINE=1`。`--relax-frame` 可把 frame 不符降级为警告。
- **帧导出的 PNG 是某一瞬间的帧**：IDLE 的毛发相位/眨眼会变，故 6 态哈希互异只比较
  「每态一张样本帧」，不要求同态多帧一致。
- **`t` 缩短时长仅本进程运行期有效**；G5 跑完建议 `R` 重启（G6 预检也会自动恢复）。
- **G5 的 BREAK 帧是运行中截取**（dump 约 1 s，进度会前进），故该帧只断言标题/环/倒计时/
  页脚与相位颜色，**不**断言进度角；进度 0/500/1000 由 `layout_checks_m3 --selftest` 覆盖，
  真机 0 与 1000 由 G5 的 IDLE/FOCUS_DONE 帧覆盖。
- **run_key_h.py 依赖人手按键**：本会话只做了提示/超时路径冒烟；`KEY-01/02/04/05` 的
  真人结论需用户用 `--count 20` 跑一遍后确认。PWR 长按在 USB 供电下不会真断电
  （`docs/contracts/C07 §5`），"进入关机流程"以日志/串口无响应为证据，不声称物理断电。
- **`d` 期间固件静音日志后恢复**；主机若正在其它程序里打开 COM7（如 `idf.py monitor`）会打不开，
  需先关闭占用进程。host 工具**不要**与 flash/monitor 并行。
- **`layout_checks_m3.py` 的文本类断言只验证"位置/颜色/非空"**，不识别具体字形内容
  （无 OCR）；`M:SS` 数值、菜单行文案由 `m` 字段与真机 PNG 人眼复核。
- `pet_sim.py` 只用于验证 host 侧时序/解析/断言/证据管线，**不能**替代真机验收；
  `evidence/sim/` 与 `evidence/` 真机产物分开存放。
- **已知差异记录**：`run_g6.py` 的 `m` 行实际字段数（多 4 个）与 `docs/10 §4` 不同；
  未静默改协议，仅在工具内兼容解析，待 Lead 决定是否回写文档。
