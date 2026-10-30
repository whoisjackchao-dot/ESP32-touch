# M2 交付说明（2026-10-31）

> 交付节点：`M2` — 6 态动画 + 3 卡栈（240×240）。
> 契约：`05-plan.md` M2、`contracts/C03-layout-240x240.md`（含 §7 修订 R1–R8）、`docs/08-m2-development.md`。
> 证据：`07-evidence-log.md` E15–E20；`tests/RED-GREEN.md`；`tools/host/evidence/`。

## 1. 这次交付了什么

| 产物 | 文件 | 说明 |
| --- | --- | --- |
| 状态机 | `firmware/components/pet_ui/pet_state.c/.h` | 6 态、done 下降沿去抖（1500 ms）、DONE 驻留（5000 ms）、失联（15000 ms）、idle 眨眼/小发呆、12 帧相位（`frame×2+hair_right`）、3 卡栈（`C03 §3`：同名=title 且 project 才置顶；第 4 张淘汰最旧；空 title 仍接收） |
| 240 版式 | `firmware/components/pet_ui/pet_ui_240.c/.h` | 状态条网络点、宠物 `(72,20,96,128)`、居中标记 `>`/`!?`、3 卡 `232×28`（前缀+项目/标题/时长/强调条）、OFFLINE 整帧灰度+红点、CJK 16×16 标题 |
| 主程序 | `firmware/main/main.c` | 上电锁存→面板→8 fps 主循环→渲染→送屏；串口注入（M2 调试用）；帧缓冲导出（FBBIN/FBEND+CRC32） |
| 板级注释更正 | `firmware/main/board_lcd.c` | `D-02` 误报更正（IDF 只在最后 chunk 置完成回调，`present()` 等的是整帧） |
| host 单测 | `tests/`（新增） | mingw gcc，15 用例 / 231 断言；RED→GREEN 原始日志留档 |
| host 工具 | `tools/host/`（新增） | 串口驱动、RGB565→PNG、像素断言、G1/G3 自动跑测、MEM 采样、无设备模拟器 |
| 文档 | `docs/08`、`09`、`C03 §7`、`00/05/06/07`、README、NOTICE、README-M1 勘误 | 计划/契约修订/证据/追踪矩阵同步 |

## 2. 构建 / 刷机 / 验证（可复跑）

### 构建（Windows 原生，`E15`）

```powershell
$env:IDF_TOOLS_PATH='D:\Espressif'; $env:IDF_PATH='D:\Espressif\frameworks\esp-idf-v5.5.4'
. "$env:IDF_PATH\export.ps1"
cd D:\DSH\ESP32-touch
idf.py -B D:\DSH\esp-build\buddy154 -DIDF_TARGET=esp32s3 build
```

预期：`buddy154.bin binary size 0x84fd0`（544,720 B，factory 83% free），无 warning/error（`E16`）。

**新 clone 可复现**（`E21`，提交 `24876dd`）：`git clone` → `idf.py -B <新目录> build` → 无 warning，
产物同样 `0x84fd0`；clone 内 `tests/run_tests.ps1` 亦 PASS。二进制 sha256 因构建时间戳不同而不同，属正常。

### host 单测（不需要设备）

```powershell
pwsh -NoProfile -File tests/run_tests.ps1
# RESULT: PASS (all host tests green)  15 cases / 231 asserts
```

### 刷机 + 真机验收

```powershell
idf.py -B D:\DSH\esp-build\buddy154 -p COM7 flash
# 等串口出现 "m2: READY"，再（勿与 flash/monitor 并行）：
python tools\host\run_g1.py    # 24 次 6 态观测，约 30 s
python tools\host\run_g3.py    # 20 轮卡栈 + 4 特例，约 60 s
```

## 3. 真机证据（本轮）

| 证据 | 结果 | 文件 |
| --- | --- | --- |
| `E17` G1 / UI-10/11/12 | **24/24 PASS**，布局断言 6/6，6 态帧哈希互异 | `tools/host/evidence/g1.json`、`g1_summary.md`、`g1_obs0*.png` |
| `E18` G3 / CARD-10..15 | **20/20 轮** + 特例 **4/4** + 布局 **24/24** | `tools/host/evidence/g3.json`、`g3_summary.md`、`g3_round*.png`、`g3_special*.png` |
| `E19` MEM-01 | 10 min 堆采样，无持续下降；IDLE 眨眼 frame 0/1 均出现 | `tools/host/evidence/mem01.json`、`mem01_summary.md` |
| `E16` T 测试 | 15/15 用例、231 断言；RED 阶段 13/14 失败留档 | `tests/RED-GREEN.md`、`tests/*.log` |
| `E20` D-05 | 标记 `>`/`?` 被镜像 → 修复 + 方向回归断言 | `tests` UI-11b、`g1_obs02_working.png` |

**帧缓冲导出协议**（`docs/08 §4`）：`d` → `FBBIN 240 240 115200\n` + 115200 B RGB565LE + `\nFBEND <crc32> <off>\n`。
主机必须**立即读**、**精确读 115200 B**（像素中可能出现 `FBEND` 字节序列），并用设备 CRC 校验。

## 4. 用户目视清单（H 确认）

设备当前跑 M2 固件。上电后应看到：

1. 左上绿色 4×4 网络点；中间宠物（IDLE 会眨眼）。
2. 串口发 `1` → WORKING：宠物头顶白色 `>`；发 `2` → 橙色 `!?`；发 `3` → 笑脸；发 `4` → 难过脸。
3. 发 `5` → 整屏灰度 + 左上红点；发 `u` → 恢复。
4. 发 `c` 三次 → 三张卡（最新在顶、左侧青色 2 px 强调条、右下角时长 `1H02M` 样式）；第 4 次 → 最旧卡消失。
5. 发 `x` 清卡。

> 若屏幕观感与 `tools/host/evidence/*.png` 不一致，请以照片回报；帧缓冲导出只证明"设备渲染出的像素"，面板最终观感仍以人眼为准。

## 5. 明确未交付 / 已知限制

| 项 | 状态 |
| --- | --- |
| `FRM-01` 撕裂/闪烁 | 未人眼验证（设备自报 8 fps；无摄像头） |
| G1 分母 | 门槛原文"共 20 次"，算术为 24；实测按 24/24 报告（`C03 §7 R8`，待用户确认） |
| `C03 §7` 修订 | M2 开工期实现可行性修订 8 项，待用户验收确认 |
| 串口注入 | **M2 调试接口**，不是产品协议；M5 由事件协议取代 |
| 电池供电保持 | 沿用 M1 结论（仅 USB 供电验证） |
| 番茄钟/菜单/按键/语音/网络 | 未开始（M3–M5） |

## 6. 下一节点：M3

1. 冻结 `C07` 按键映射（KEY1/KEY2 短按 20 ms 去抖、600 ms 长按；`PWR` 保留电源语义）。
2. `pet_button` 接入 GPIO0/4；5 项菜单；25/5 番茄钟 + 圆环 + NVS 统计（含上游缺陷 `D-03` 跨日清零）。
3. 复用 M2 的 `pet_ui_240` 场景与 `pet_state`，新增 `pet_ui_pomo_240` 等视图（不改 135×240 死代码）。
