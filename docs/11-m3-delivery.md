# M3 交付说明（2026-10-31）

> 交付节点：`M3` — 2 应用键 + 5 项菜单 + 番茄钟（240×240）。
> 契约：`05-plan.md` M3、`contracts/C07-keymap.md`（2026-10-31 冻结）、`docs/10-m3-development.md`。
> 证据：`07-evidence-log.md` E22–E26；`tests/RED-GREEN-M3.md`；`tools/host/evidence/g5*、g6*`。

## 1. 这次交付了什么

| 产物 | 文件 | 说明 |
| --- | --- | --- |
| 按键 | `firmware/components/pet_ui/pet_button.c` | 20 ms 采样、600 ms 长按、松键判短、事件 FIFO（上游 MIT 行为一致） |
| 键表 | `pet_keymap.c/.h` | DUTY/POMO × 两键 × 短/长 8 组映射（`C07 §3`）；DUTY `KEY1` 长按=静音 |
| 菜单 | `pet_menu.c/.h`（5 项）+ `pet_ui_menu_240.c` | MUTE/STATUS/WIFI(M5)/BT(M5)/BACK；KEY1 短循环、KEY2 短执行、KEY2 长关闭；选中行反色 |
| 番茄钟 | `pet_pomo.c` + `pet_ui_pomo_240.c` | 25/5 状态机（开始/暂停/继续/放弃/到点/推进）、240 圆环（心 120,104 R76 T10，12 点顺时针）、中心倒计时、底部统计；运行期可缩短时长（G5 演示） |
| 持久化 | `pet_pomo_store.c` | NVS `pomo/stats`（16 B blob，day 未知哨兵 0x80000000）、`voice/muted` |
| 主程序 | `firmware/main/main.c` | 20 ms 按键任务（GPIO0/4/5）+ 主任务三页渲染 + 模式/菜单/番茄钟串行处理 + `PWR` 长按关机流程 + 注入协议扩展 |
| host 单测 | `tests/test_m3_logic.c`、`test_m3_ui.c`、`run_m3_tests.ps1` | 21 用例 / 517 断言，RED→GREEN 留档 |
| host 工具 | `tools/host/run_g5.py`、`run_g6.py`、`layout_checks_m3.py`、`run_key_h.py`、`m3_common.py`、`pet_serial.py`/`pet_sim.py` 扩展 | 番茄钟/菜单/合成键自动跑测、真机按键引导、像素断言、模拟器 |

## 2. 交互（`C07`）

| 键 | 值班 | 番茄钟 | 菜单内 |
| --- | --- | --- | --- |
| KEY1（BOOT/GPIO0）短 | SEND（M5 接入，M3 仅日志） | 开始/暂停/继续/推进 | 选中项下移 |
| KEY1 长 | 静音翻转（持久化） | 放弃番茄钟 | 无动作 |
| KEY2（PLUS/GPIO4）短 | 进入番茄钟 | 回值班（先落盘） | 执行选中项 |
| KEY2 长 | 打开菜单 | 打开菜单 | 关闭菜单 |
| PWR（GPIO5）长 ≥1.5 s | 关机流程（落盘→背光灭→GPIO2 低） | 同左 | 同左 |
| PWR 短 | 无动作 | 无动作 | 无动作 |

## 3. 构建 / 刷机 / 验证

```powershell
$env:IDF_TOOLS_PATH='D:\Espressif'; $env:IDF_PATH='D:\Espressif\frameworks\esp-idf-v5.5.4'
. "$env:IDF_PATH\export.ps1"; cd D:\DSH\ESP32-touch
idf.py -B D:\DSH\esp-build\buddy154 -DIDF_TARGET=esp32s3 build      # 0x8bef0（573,168 B，82% free）
idf.py -B D:\DSH\esp-build\buddy154 -p COM7 flash
pwsh -NoProfile -File tests\run_m3_tests.ps1                        # 21/21 用例、517 断言
python tools\host\run_g6.py --port COM7                             # 约 7 min，260/260
python tools\host\run_g5.py --port COM7                             # 约 25 s，17/17
```

> 手动体验：`J` 进番茄钟 → `K` 开始/暂停/继续 → `L` 放弃；`H` 开/关菜单；`K` 菜单内切选中、`J` 执行；
> `t` 把本段缩短为 6 s/3 s；`R` 软重启；`m` 打印模式/番茄钟状态（详见 `docs/10 §4`）。

## 4. 真机证据

| 证据 | 结果 | 文件 |
| --- | --- | --- |
| `E23` T | 21/21 用例、517/517 断言；RED 389/517 失败留档 | `tests/RED-GREEN-M3.md`、`tests/*m3*.log` |
| `E24` G6 | 13 项 ×20 = **260/260**，布局 9/9，静音重启持久 | `tools/host/evidence/g6.json`、`g6_summary.md`、9 PNG |
| `E25` G5 | **17/17** 步，布局 4/4；暂停冻结、到点统计、放弃不计、重启持久、跨日清零 | `evidence/g5.json`、`g5_summary.md`、4 PNG |
| `E26` 真机按键 | **PENDING（未结论）**：首轮 430 s 采集只观测到 KEY2 事件 7 次（菜单/模式），KEY1/PWR 未观测到；用户选择先提交 | `07-evidence-log.md` E26；诊断脚本 `D:\DSH\esp-build\keyprobe.py` |
| `E22` 构建 | 0 warning、可刷写启动 | `07-evidence-log.md` |

## 5. 明确未交付 / 已知限制

| 项 | 状态 |
| --- | --- |
| 番茄钟 25 分钟真实完整段 | 未等 25 min；用 6 s/3 s（`t`）等价验证状态机/持久化，常量由 T 断言保证 |
| 跨日"当天"来源 | 无时间源（R23：无网络不猜测）→ `set_day` 逻辑与 NVS 就绪，**NTP 属 M5**；M3 用 `y` 模拟 |
| 菜单 WIFI/BT | 显示 `(M5)`，选中仅关闭 + 日志，不假装可用 |
| `PWR` 真断电 | USB 供电下拉低 GPIO2 不会断电（物理事实）；真关机需电池供电 |
| 语音播报/静音"听感" | M4（M3 只持久化标志并记 alarm 日志） |
| 触摸/IMU/AEC/OTA | 后置（v1 后置，未取消） |

## 6. 下一节点：M4（语音播报）

1. `C04` 音频输出契约冻结（ES8311 @0x18 + I2S GPIO8/9/10/12 + PA GPIO7、3 条 PCM、静音语义）。
2. 播放触发：INPUT_REQUIRED / DONE / FAILED 各一次；WORKING 期间静默；静音开关生效且**重启后仍静音**（M3 的 `voice/muted` 直接复用）。
3. `G9`：60/60 + 静音 20/20；"只有 QUEUED 日志却没真响"不算完成。
