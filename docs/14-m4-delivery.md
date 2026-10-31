# M4 交付说明：语音播报（ES8311，2026-10-31）

> 契约：`contracts/C04-audio-output.md`（冻结 2026-10-31）；证据：`E37`（实现/构建/T）、`E38`（H 听感）。
> 用户听感确认原文："**都正确**"（三类语音、静音、重启保持、解除静音）。

## 1. 交付物

| 交付 | 位置 | 说明 |
| --- | --- | --- |
| 音频契约 | `docs/contracts/C04-audio-output.md` | 引脚/格式/触发/静音/播放序列/降级/G9 |
| 板级音频 | `firmware/main/board_audio.c/.h` | I2C(42/41) + I2S(MCLK8/BCLK9/WS10/DOUT12) + ES8311 0x18 + PA GPIO7；独立 audio 任务 |
| 播报策略 | `firmware/components/pet_ui/pet_audio_policy.c/.h` | T 类纯逻辑：`should_play` / `asset_for` / `asset_name` |
| ES8311 驱动 | `firmware/components/es8311/`（vendored） | esp-bsp v1.0.0~1，**Apache-2.0**，保留 LICENSE/README |
| 语音资产 | `firmware/main/assets/voice/{input,done,failed}.pcm` | 16 kHz / mono / s16le；上游 MIT 仓库资产（Edge TTS，见 NOTICE） |
| 集成 | `firmware/main/main.c` | 主循环 `pet_state_take_sound` + 静音门控 + `board_audio_play`；调试注入 `a`/`b`/`f` |
| 测试 | `tests/test_m4_audio.c` + `tests/run_m4_tests.ps1` | T：28 断言（RED 9 失败 → GREEN 28/28）；资产格式 |

## 2. 行为（与 C04 一致）

- 进入 `INPUT_REQUIRED` / `DONE` / `FAILED` 各播一次（`pet_state_take_sound` 取走即清）；`WORKING/IDLE/OFFLINE` 静默。
- 静音（KEY1 长按 / 菜单 MUTE）不播；`voice/muted` NVS 持久化，**跨重启保持**。
- 播放序列：PA 使能 → 解除 codec 静音 → 分块写 I2S（mono→stereo，480 帧/块）→ 静音尾 → codec 静音 → PA 关。
- 初始化失败只降级为无声（显示/按键/状态机不受影响）。

## 3. 验证

| 项 | 结果 | 证据 |
| --- | --- | --- |
| 构建 | `0xc4630`（804,400 B，74% free），**0 warning** | `E37` |
| T 测试 | **28/28 PASS**（RED→GREEN 留档）；M2/M3/M5a 回归全 PASS | `E37`、`tests/m4-run.log` |
| H 听感 | **通过**：三类语音可听、静音无声、重启后仍静音、解除静音恢复 | `E38`、`audio_h.log` |
| 非阻塞 | audio 独立任务，播放期间 UI/按键正常 | `E38` |
| 门槛 | `G9` **PARTIAL**：用户确认的缩减样本；**正式 60/60 + 静音 20/20 留到发布前** | `E38` |

## 4. 已知限制 / 未交付

- 仅播放；麦克风/PTT/AEC 后置（`R20`）。
- 无音量调节 UI；固定 70。
- 番茄钟到点提示音未接入（C04 范围外，仅日志）；后续可复用 `done` 资产。
- `G9` 正式计数、`KEY-02` 长按、`KEY-05` PWR 长按、`G6` 正式 20 次/项为**发布前待办**（`Q11` 缩减决定）。
- 刷机注意：factory app 偏移 **0x20000**（`C06`），手工 esptool 勿用 0x10000。\n- **本板无独立 RST 键**：PWR 长按进入关机后黑屏，恢复需拔插 USB 或串口硬复位（`E43` 记录）。

## 4.1 M4.1 增补：待机画面静音图标（2026-10-31 用户要求）

- 需求 `R30` / 契约 `C03 §7 R9`：待机页状态条右端 `(226,1,10,10)` 橙色静音图标；`UI-22` 像素断言 PASS。
- `pet_view_t` 新增 `bool muted`（主循环注入 `s_muted`）；`mute_on/off/restart` 帧缓冲证据见 `tools/host/evidence/m4_1/`、`E39`。

## 5. 复现

```powershell
# 构建
$env:IDF_TOOLS_PATH='D:\Espressif'; $env:IDF_PATH='D:\Espressif\frameworks\esp-idf-v5.5.4'
. "$env:IDF_PATH\export.ps1"; cd D:\DSH\ESP32-touch
idf.py -B D:\DSH\esp-build\buddy154 -DIDF_TARGET=esp32s3 build
# 烧录（idf.py 会自动用分区表；手工 esptool 时 app 写 0x20000）
idf.py -B D:\DSH\esp-build\buddy154 -p COMx flash
# 宿主测试
pwsh -NoProfile -File tests/run_m4_tests.ps1
# H：设备在线且未被插件占用时（或按 audio_h.py 的抢口方式）
python tools/host/pet_event.py --port COMx   # 事件驱动播报
```

## 6. 下一节点

`M5b`：WiFi TCP 8766 + BLE GATT + 配网 + NTP（`R21` 其余通道；`C05` 已冻结，多通道时补跨通道去重）。
发布前：补 `G6/G9` 正式计数、`KEY-02/05` 长按 H、`Q10` tag/Release 授权。


