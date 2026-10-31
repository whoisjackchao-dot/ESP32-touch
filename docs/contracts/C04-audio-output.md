# C04：音频输出契约（语音播报，M4 冻结）

> 状态：**已冻结（2026-10-31）**。依据：`R15`（三条短语音）、`Q03`（本期最小可跑）、
> `03-feasibility.md §5.1`（音频引脚实测/原理图）、上游 `ESP32-buddy`（MIT）M5 语音契约（行为）。
> 范围：**仅播放**（ES8311 DAC → NS4150B → 喇叭）；麦克风/AEC 后置（`R20`），不在本契约。

## 1. 硬件（冻结）

| 项 | 值 | 证据 |
| --- | --- | --- |
| Codec | **ES8311** @ I2C `0x18`（7-bit） | `E09` 实机日志 |
| I2C | SDA=**GPIO42**，SCL=**GPIO41**，100 kHz，master | `§5.1` |
| I2S | MCLK=**8**、BCLK=**9**、WS=**10**、DOUT(ESP→codec)=**12**；DIN=11 本期不用 | `§5.1` |
| 功放 | **NS4150B**，使能 **GPIO7**，**高有效**；仅播报期间拉高 | `§5.1` |
| 采样 | **16 kHz / 16-bit / 单声道源**；I2S 用 **STEREO 槽位**（左右写同一样本） | 上游实测：MONO 槽位会让 WS 变 32 kHz、声音 2 倍速 |
| 时钟 | ESP32 为 I2S **master**；MCLK = 16 kHz × **256** = 4.096 MHz；ES8311 为 slave | 上游实测参数 |
| I2S 通道 | `I2S_NUM_0`，DMA 6×240 帧 | 上游 |

## 2. 资产（冻结）

| 项 | 规定 |
| --- | --- |
| 文件 | `firmware/main/assets/voice/{input,done,failed}.pcm` |
| 格式 | **raw PCM：s16 little-endian，单声道，16 kHz**（无 WAV 头） |
| 时长 | 每段 0.1–3.0 s（超出判非法，播放前校验长度） |
| 嵌入 | `EMBED_FILES`（同 `robot.look` 做法），不占文件系统 |
| 来源 | 派生自上游 `ESP32-buddy`（MIT 仓库）`components/pet/assets/*.pcm`；由 Edge TTS 合成，再分发条款见 `NOTICE.md` §语音资产 |

## 3. 触发（冻结，与 M2 状态机一致）

| 进入状态 | 播报 | 说明 |
| --- | --- | --- |
| `INPUT_REQUIRED` | `input.pcm` 一次 | `pet_state_take_sound()` 取走即清（天然去重） |
| `DONE` | `done.pcm` 一次 | 含 `agent.idle` 去抖合成；同一事件不重播 |
| `FAILED` | `failed.pcm` 一次 | |
| `WORKING` / `IDLE` / `OFFLINE` | **静默** | 工作中不出声 |

- 主循环每帧 `sound = pet_state_take_sound(s_state)`；`sound != NONE && !muted` → `board_audio_play(sound)`。
- **静音**：`s_muted`（M3 `voice/muted` NVS 持久化，`KEY1` 长按/菜单 MUTE 切换）→ 不播；静音状态**跨重启保留**
  （与 `C07 §3` 一致；上游"不持久化"的行为**不采用**，以本项目已冻结的 M3 决策为准）。
- 队列满（深度 4）：**丢弃并记日志**，绝不阻塞主循环。

## 4. 播放序列（冻结）

1. `GPIO7` 拉高（功放使能）→ 延时 ~10 ms 等功放稳定；
2. `es8311_voice_volume_set(70)` + `es8311_voice_mute(false)`；
3. 分块（480 帧）写 I2S（mono→stereo 复制），单块超时 2000 ms；
4. 写 **320 帧静音尾**（防止停在波形沿产生直流/长鸣）；
5. `es8311_voice_mute(true)` → `GPIO7` 拉低；
6. 全流程在**独立 audio 任务**（优先级 4，栈 4096）执行；主循环只入队。

## 5. 失败与降级（冻结）

| 情况 | 行为 |
| --- | --- |
| I2C/I2S/ES8311 初始化失败 | 记录具体阶段错误；**不阻止**显示/按键/状态机；后续 `play` 为 no-op |
| 播放中写 I2S 失败 | 终止本次播放，正常执行静音/关功放，记日志；不崩溃 |
| 资产长度非法 | 不播放，记 `AUDIO bad asset` |
| 未初始化即 play | no-op（返回 `ESP_ERR_INVALID_STATE`） |

## 6. 测试

| 级别 | 内容 | 用例 |
| --- | --- | --- |
| T | 触发/去重/静音判定（纯逻辑 `pet_audio_policy`）+ 资产格式校验（s16le/长度/时长） | `AUD-01..03` |
| D | 资产来源/NOTICE、es8311 组件许可（Apache-2.0 保留 LICENSE） | `AUD-05` |
| H | 真机听到 input/done/failed 各一次；静音 20 次无声；重启后静音保持；播放不卡动画 | `AUD-04/06`、`G9` |

### 6.1 验收门槛

- `G9`：60/60 触发（每类 20 次，各响一次）+ 静音 20/20 无声；重启后静音保持（1/1）。
- 播放期间 UI 帧率不下降（8 fps 目视/日志）。
- 用户可听确认（H）。

## 7. 明确不做

- 麦克风/PTT/AEC（`R20` 后置）；音量调节 UI；TTS 运行时合成；WAV/MP3 解码；耳机检测。

## 8. 变更方式

引脚/格式/触发/静音任一变更 → 修订本文件 + 重跑 `AUD-*` 与 `G9`；不得静默改变去重语义。
