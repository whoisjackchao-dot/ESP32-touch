# 发布前检查单（v1 收尾，2026-10-31）

> 目标：把"能演示"收口成"可发布/可交付"。范围：正式门槛计数、剩余 H 项、契约验收、许可与发布授权。
> 状态图例：✅ 通过 / 🟡 部分或缩减样本 / ⏳ PENDING（等用户/设备）/ ❌ 不满足。

## 1. 构建与自动化（本机可复现）

| 项 | 命令 | 结果 | 状态 |
| --- | --- | --- | --- |
| 固件构建 | `idf.py -B D:\DSH\esp-build\buddy154 -DIDF_TARGET=esp32s3 build` | `0xc46c0`，0 warning，74% free | ✅ |
| 宿主 M2 单测 | `pwsh -File tests/run_tests.ps1` | 15 用例/231 断言 PASS（含 UI-22） | ✅ |
| 宿主 M3 单测 | `pwsh -File tests/run_m3_tests.ps1` | 21 用例/517 断言 PASS | ✅ |
| 宿主 M5a 契约测试 | `pwsh -File tests/run_m5a_tests.ps1` | 29 用例/436 断言 PASS | ✅ |
| 宿主 M4 测试 | `pwsh -File tests/run_m4_tests.ps1` | 28/28 PASS | ✅ |
| 插件测试 | `cd host-plugin; node --test` | 56/56 PASS | ✅ |
| M2 回归（真机 G1） | M3 固件 `run_g1.py` | 24/24（见 `E20`；M4.1 后未重跑，UI-22 只影响状态条右端） | 🟡 待最终重跑 |
| **G6 合成键** | `python tools/host/run_g6.py --iterations 20` | **260/260 PASS**，layout 9/9，静音持久 PASS | ✅ `E41` |
| **G9 语音** | `python run_g9.py`（release 脚本） | **60/60 有声 + 60/60 静音跳过** | ✅ `E41` |
| G5 番茄钟 | `python tools/host/run_g5.py` | 17/17（`E25`） | ✅（M4.1 后未重跑） |

## 2. 真机 H（需用户在设备旁）

| 用例 | 要求 | 现状 | 状态 |
| --- | --- | --- | --- |
| `KEY-01` 短按 | 两键短按（`Q11` 缩减：各 3 次） | BOOT 3/3、PLUS 3/3、PWR 短按无动作 | ✅ `E35` |
| `KEY-02` 长按 | 静音/菜单长按（`Q11` 缩减） | **PASS**：静音翻转 3/3、菜单开/关 3/3（用户在场） | ✅ `E43` |
| `KEY-05` PWR 长按 | 进入关机流程（USB 供电下验证"进入流程"） | **PASS** 1/1 进入关机；短按 3/3 无动作 | ✅ `E43` |
| `FRM-01` 撕裂 | 快速切换画面时无撕裂/闪屏 | 用户目视"没问题" | ✅ `E45` |
| M2 真机人眼 | 6 态/卡片视觉 | `E19` 已通过（"真机和设计一致"） | ✅ |
| M4 听感 | 三声/静音/重启保持 | 用户"都正确" | ✅ `E38` |
| M4.1 静音图标 | 待机页可见 | 用户"确认，可以静音" | ✅ `E40` |
| M5a 插件 E2E | 真实 DSH 会话驱动设备 | 用户目视 WORKING/卡片/绿点 | ✅ `E34` |\n| 无 RST 键恢复 | 本板无复位键；PWR 长按后黑屏 | 拔插 USB / 串口硬复位（`E43`） | ✅ 已记录 |

## 3. 契约与决策验收（需用户确认）

| 项 | 内容 | 状态 |
| --- | --- | --- |
| `C03 §7 R1–R9` | M2 起 9 项版式/口径修订（R9=静音图标） | ✅ 用户整批批准（2026-10-31） |
| `G1` 计数口径 | 原文"6 态各 4 次共 20 次"算术为 24；实测按 24/24 | ✅ 用户认可 24（2026-10-31） |
| `Q11` | M3 按键 H 缩减为每按钮 3 次；`G6` 正式 20 次/项保留到发布前 | ✅ 已记录（本轮 `G6` 合成 20/20 已补） |
| `Q04` | 宿主=DSH 插件 | ✅ 已批准（`E32–E34`） |
| `Q08` 语音转写 | 智谱 ASR / 本地 ESP-SR / 云 | ⏳ M5b 前（不影响 v1 USB 版本） |
| `Q09` AEC | 是否进二期 | ⏳ M5b 前 |
| `Q10` tag/Release | 发布动作授权 | ⏳ 未授权；**禁止** tag/Release 直到用户明确 |

## 4. 许可与合规

| 项 | 状态 |
| --- | --- |
| 派生 MIT 上游（pet_*）标注 | ✅ `firmware/components/pet_ui/NOTICE.md` |
| ES8311 组件 Apache-2.0（保留 LICENSE/版权头） | ✅ `firmware/components/es8311/LICENSE` |
| 语音资产（Edge TTS 合成）再分发提示 | ✅ `NOTICE.md` §M4 音频 |
| 字体 `cjk16.h` OFL 1.1 | ✅ NOTICE |
| 无 GPL 代码 | ✅（`R27`；未复制 vibebuddy） |
| 仓库根 LICENSE（MIT） | ✅ |
| 插件包 LICENSE | ✅ `host-plugin/LICENSE` |

## 5. 发布动作（未授权，仅清单）

1. 最终构建 + **新 clone 构建**验证（`E27` 做法；M4/M4.1 后重跑一次）。
2. 生成 `manifest`/版本号（如 `v1.0.0`），更新 README「当前状态」为已发布。
3. `git tag -a v1.0.0` + GitHub Release（**需 `Q10` 授权**）。
4. 可选：把 `host-plugin` 发布 npm 或 GitHub 子目录（**需用户授权**）。
5. 归档证据：`docs/07-evidence-log.md`、`tools/host/evidence/`、`tests/*.log`。

## 6. 未完成清单（诚实）

- `KEY-02`/`KEY-05` 长按真机 H（需用户在场，脚本 `D:\DSH\esp-build\key_long_h.py` 已就绪，10 分钟窗口）。
- `FRM-01` 人眼撕裂观察（需用户）。
- `C03 §7` 与 `G1` 口径用户确认。
- ~~最终新 clone 构建~~ ✅ 已完成（`E44`：0xc46c0，0 warning）。
- M5b（WiFi/BLE/配网/NTP）未开始；`Q08/Q09` 未决。
- `G6/G9` 的"真机按压 20 次"部分由**合成注入**完成；物理长按仍缺 `KEY-02`。

## 7. 复现命令速查

```powershell
# 构建 / 刷写（factory app 偏移必须 0x20000）
$env:IDF_TOOLS_PATH='D:\Espressif'; $env:IDF_PATH='D:\Espressif\frameworks\esp-idf-v5.5.4'
. "$env:IDF_PATH\export.ps1"; cd D:\DSH\ESP32-touch
idf.py -B D:\DSH\esp-build\buddy154 -DIDF_TARGET=esp32s3 build
idf.py -B D:\DSH\esp-build\buddy154 -p COM7 flash

# 宿主测试
pwsh -File tests\run_tests.ps1; pwsh -File tests\run_m3_tests.ps1
pwsh -File tests\run_m5a_tests.ps1; pwsh -File tests\run_m4_tests.ps1
cd host-plugin; node --test

# 真机门槛（插件占用 COM7 时先临时停 helper，工具会自行抢口）
python tools\host\run_g6.py --port COM7 --iterations 20
python D:\DSH\esp-build\run_g9.py
```


