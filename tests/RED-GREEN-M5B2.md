# M5b-2 host 测试与构建记录（C09：BLE GATT 事件通道）

- 日期：2026-11-01
- 任务：`task-9`（ble-dev）。性质：**T 类新逻辑 RED→GREEN**（`pet_ble_lines` 行重装是新实现；行缓冲/CRLF/超长语义复用既有 `pet_linebuf`）。
- 契约：`docs/contracts/C09-ble-channel.md`（2026-11-01 冻结）§1 / §1.1 / §2；事件协议复用 `C05`；跨通道去重复用 `C08 §1.1`（`pet_net_line_accept`）。
- 被测（真实现）：
  - `firmware/components/pet_ble/pet_ble_lines.c`（纯逻辑：每连接行缓冲 + 完整行交付/超长丢弃计数）；
  - `firmware/components/pet_ui/pet_linebuf.c`（**只链接复用，未改动**；≤512 B、剥 `\r`、超长整行丢弃）。
- 口径：只用公开头文件 API；无 stub（测试期临时 RED 占位已删除）、无 sleep、不碰串口/BLE 硬件/NimBLE/网络/真实时钟。只写 `tests/` 与 `firmware/components/pet_ble/`。
- 只编译 + host 测试：**未刷机、未开串口**（COM7 由 Lead 的 2 h 长稳脚本占用）；设备侧 BLE-02/03/04/05、G12、G16 均为未验证项（见 §7）。

## 1. RED（行重装占位阶段，测试先失败）

实现前 `pet_ble_lines.c` 为只保证编译的占位（`feed` 恒返回 0、不交付不计数、`init` 不清零）。运行：

```powershell
pwsh -NoProfile -File tests/run_m5b_ble_tests.ps1 -LogPath tests/m5b2-red-run.log
```

完整失败原文见 `tests/m5b2-red-run.log`（10,952 B）。摘要：

```text
[CC] tests/test_m5b_ble.c
[RUN] test_m5b_ble.exe
  ASSERT FAIL .../test_m5b_ble.c:106: n == 3 (got 0, want 3)
  ASSERT FAIL .../test_m5b_ble.c:107: c.n == 3 (got 0, want 3)
  ...
FAIL BLE-01a-single-write-multi-line (10/14 asserts failed)
FAIL BLE-01b-across-write-half-line (10/15 asserts failed)
FAIL BLE-01c-crlf (11/12 asserts failed)
FAIL BLE-01d-empty-lines-skipped (4/8 asserts failed)
FAIL BLE-01e-oversize-drop-and-recover (16/25 asserts failed)
FAIL BLE-01f-mtu-sized-fragments (10/13 asserts failed)
FAIL BLE-01g-order-and-counts (11/11 asserts failed)
FAIL BLE-01h-defensive-args (5/13 asserts failed)
FAIL BLE-01i-dirty-init-and-reload (9/10 asserts failed)
FAIL BLE-01j-per-connection-isolation (15/16 asserts failed)
SUMMARY m5b_ble: cases=10 passed=0 failed=10 asserts=137
[EXIT] 1 (test_m5b_ble.exe)
RESULT: FAIL (exit 1)
```

RED 全部是**断言失败**（非编译/环境错误）：10/10 用例失败；编译 0 warning / 0 error。

## 2. GREEN（实现完成）

```powershell
pwsh -NoProfile -File tests/run_m5b_ble_tests.ps1
```

```text
[CC] tests/test_m5b_ble.c
[RUN] test_m5b_ble.exe
PASS BLE-01a-single-write-multi-line (14 asserts)
PASS BLE-01b-across-write-half-line (15 asserts)
PASS BLE-01c-crlf (12 asserts)
PASS BLE-01d-empty-lines-skipped (8 asserts)
PASS BLE-01e-oversize-drop-and-recover (25 asserts)
PASS BLE-01f-mtu-sized-fragments (13 asserts)
PASS BLE-01g-order-and-counts (11 asserts)
PASS BLE-01h-defensive-args (13 asserts)
PASS BLE-01i-dirty-init-and-reload (10 asserts)
PASS BLE-01j-per-connection-isolation (16 asserts)
SUMMARY m5b_ble: cases=10 passed=10 failed=0 asserts=137
[EXIT] 0 (test_m5b_ble.exe)
RESULT: PASS (all M5b-2 BLE host tests green)
```

**10/10 用例、137/137 断言 PASS，退出码 0；编译 0 warning / 0 error。** 原始日志 `tests/m5b2-run.log`（753 B）。
GREEN 首跑即全绿，无测试期望笔误。

| # | 用例 | 覆盖点（C09 §1.1 / C05） | 断言 |
| --- | --- | --- | --- |
| 1 | `BLE-01a-single-write-multi-line` | 一次 write 含 3 行按序交付；行尾 `\n` 不进入内容；行缓冲 NUL 结尾；尾半行跨 write 补齐 | 14 |
| 2 | `BLE-01b-across-write-half-line` | 一行拆 3 段；**逐字节**分片（极端 MTU）；半行 + 后续 write 的完整行 | 15 |
| 3 | `BLE-01c-crlf` | 单 write CRLF；`\r` 与 `\n` 分属两次 write；裸 `\n` 与 CRLF 混用 | 12 |
| 4 | `BLE-01d-empty-lines-skipped` | 裸 `\n`、连续空行、纯 `\r\n` 不交付、不计数；随后正常行可交付 | 8 |
| 5 | `BLE-01e-oversize-drop-and-recover` | 513 B 超长整行丢弃 + 可恢复；分多次 write 累积的超长行只计 1 次丢弃；**恰好 512 B 合法** | 25 |
| 6 | `BLE-01f-mtu-sized-fragments` | 按 20 B（默认 MTU 净荷）分片 JSON 行重装；两行按 7 B 分片交错 | 13 |
| 7 | `BLE-01g-order-and-counts` | 多次 write 交付顺序；返回值=本次交付行数；累计计数一致 | 11 |
| 8 | `BLE-01h-defensive-args` | `ls==NULL` / `data==NULL,len>0` / 0 长度 write / 空行；`cb==NULL` 只计数不交付 | 13 |
| 9 | `BLE-01i-dirty-init-and-reload` | 脏内存 `init` 全字段清零；断开/重连重装时上一连接半行不串入新连接 | 10 |
| 10 | `BLE-01j-per-connection-isolation` | 两个 `pet_ble_lines` 独立计数与缓冲（每连接一份的代理验证） | 16 |

## 3. 实现口径（与契约的对应/说明）

- **行重装**：`pet_ble_lines_feed()` 逐字节喂真实 `pet_linebuf`；`PET_LINE_READY` 且 `len>0` 才交付并 `lines_total++`；`PET_LINE_DROPPED`（>512 B 整行）记 `lines_dropped++` 并继续下一行。
- **空行**：裸 `\n` / 纯 `\r\n` 不交付（C05 只处理 JSON 行），与 WiFi TCP 任务口径一致。
- **CRLF**：剥除语义完全来自 `pet_linebuf`（只剥一个尾部 `\r`）；`\r` 与 `\n` 被拆到两次 write 也能正确剥除。
- **超长**：>512 B 的整行丢弃，恢复后下一行正常；`pet_ble.c` 在 `lines_dropped` 变化时打一条 `ESP_LOGW`（`rx: line too long ...`）。
- **每连接**：`BLE_GAP_EVENT_CONNECT` 与 `DISCONNECT` 都调用 `pet_ble_lines_init(&s_lines)`（C09 §1.1「每连接一个 pet_linebuf」；断开时半行丢弃）。
- **去重门**：`pet_ble.c` 的 `ble_deliver_line()` 对每条完整行调用 `pet_net_line_accept(now_ms, line, len)`（`now_ms = esp_timer_get_time()/1000`），通过后才调用 `pet_ble_line_cb_t`；重复行由共用指纹环丢弃并记日志（C08 §1.1）。
- **回调上下文**：在 NimBLE host 任务（GATT 写回调/GAP 事件）中同步调用；文档明确要求 main 回调只做 `pet_link_parse` + `xQueueSend`，不可阻塞。
- **MTU 无关性**：单次 write 长度可超过一行也可小于一行；`pet_ble.c` 按 128 B 分块从 mbuf 拷贝（`os_mbuf_copydata`）后喂行缓冲，**不依赖单包 ≤512 B**（C09 §1）。

## 4. 固件构建验证（仓库工程，独立 build 目录，未动 `buddy154`）

```powershell
# $env:IDF_TOOLS_PATH='D:\Espressif'; $env:IDF_PATH='D:\Espressif\frameworks\esp-idf-v5.5.4'
# . 'D:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1'
idf.py -B D:\DSH\esp-build\bledev-verify -DIDF_TARGET=esp32s3 build
```

- 结果：**0 warning / 0 error**，`Project build complete`；`pet_ble.c` / `pet_ble_lines.c` / `pet_net.c` 全部编译通过。
- app size：`buddy154.bin binary size 0x159020 bytes`（**1,413,152 B**），app 分区 3 MB，**55% free**。
- 说明：`main.c` 尚未接线 `pet_ble`（本任务不允许改 main），链接器未拉取 `pet_ble`/NimBLE 代码，
  故 1,413,152 B 只比 M5b-1 的 `0x157600`（1,404,928 B）多 **+8,224 B**，来源是 BT 启用后
  `esp_phy`/`esp_coex` 等的静态初始化代码，**不代表 BLE 通道的真实占用**。
- 首次构建失败为本次实现的一处笔误（`ble_gap_event_cb` 前向声明写成 `void`，实际返回 `int`），
  已修复；日志 `D:\DSH\esp-build\bledev-verify-build1.log`（失败）/ `bledev-verify-build2.log`（成功）。

### 4.1 临时"main 接线"集成探针（不入仓库，仅估算镜像大小）

为回答"main 正式接线后镜像多大"，在 `D:\DSH\esp-build\bledev-integ` 建了一个最小探针工程
（`EXTRA_COMPONENT_DIRS` 指向仓库组件；自带探针 `main`，不改仓库 main）：

| 探针 | 内容 | app size |
| --- | --- | --- |
| A | `pet_net_start` + 去重门 + `set_line_cb`（等价当前仓库 main 的 BLE 状态） | `0xCC5C0` = 837,056 B |
| B | A + `pet_ble_set_line_cb/start/get_status/state_name/stop`（拉入 NimBLE + 控制器） | `0xFF940` = 1,046,848 B |

- **BLE 通道净增量 = 0x33380 = 209,792 B（≈ +205 KB）**；`nm` 确认 `pet_ble_start`、`pet_ble_lines_feed`、
  `nimble_port_init`、`ble_gatts_add_svcs`、`esp_bt_controller_init` 均进入 ELF（BLE 协议栈真实链接）。
- **推算 main 正式接线后**：1,413,152 + 209,792 ≈ **1,622,944 B（≈1.55 MB）**，3 MB app 分区仍约 **48% 空闲**。
  与 M5b-1 预估（"约 1.2 MB + BLE"）一致，分区表无需调整。
- 探针源代码与构建目录均在 `D:\DSH\esp-build\`（仓库外），不进入交付物。

## 5. NimBLE 关键配置（`firmware/sdkconfig.defaults` 追加，已在 fresh build 中生效）

| 配置 | 值 | 说明 |
| --- | --- | --- |
| `CONFIG_BT_ENABLED` | `y` | 打开 BT 协议栈 |
| `CONFIG_BT_BLUEDROID_ENABLED` | `n` | 不用 Bluedroid |
| `CONFIG_BT_NIMBLE_ENABLED` | `y` | 用 NimBLE（BLE only） |
| `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` | `1` | 单连接（C09 §1） |
| `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU` | `256` | 协商 MTU ≥247（默认 23 → 交换后 256） |
| `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE` | `6144` | host 任务栈；GATT 写回调内同步执行去重门 + main 回调，默认 4096 偏紧 |
| `CONFIG_BT_CTRL_BLE_MAX_ACT` | `2` | 广播 + 1 连接（默认 6，每实例约 828 B RAM） |
| `CONFIG_ESP_COEX_SW_COEXIST_ENABLE` | `y` | WiFi + BLE 软件共存（两者同时启用时 Kconfig 默认 y，显式写出便于审计） |

生成结果核对（`D:\DSH\esp-build\bledev-verify\sdkconfig`）：`CONFIG_BT_ENABLED=y`、
`CONFIG_BT_NIMBLE_ENABLED=y`、`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`、`CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256`、
`CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=6144`、`CONFIG_BT_CTRL_BLE_MAX_ACT=2`、`CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y`
（**只追加了 BT/NimBLE 行，原有 27 行配置全部保留**）。

> ⚠️ **sdkconfig.defaults 只对"首次配置"生效**：`D:\DSH\esp-build\buddy154\sdkconfig` 是加 BT 行之前生成的
> （其中没有 `CONFIG_BT_ENABLED`）。Lead 在正式接线 `pet_ble` 后，必须让该 build 目录重新生成 sdkconfig
> （删掉 `buddy154\sdkconfig` 后 `idf.py -B ... reconfigure`/`build`，或换新 build 目录），否则 `pet_ble.c`
> 编译会因 BT/NimBLE 未启用而失败。**不要**直接改 `buddy154`（本任务约束）。

## 6. main 接线说明（Lead 做）

1. `firmware/main/CMakeLists.txt`：`PRIV_REQUIRES` 追加 `pet_ble`（现有 `pet_net` 保留）。
2. `firmware/main/main.c`：
   - `#include "pet_ble.h"`；
   - 复用与 `net_line_cb` 相同的 `ble_line_cb(const char *line, size_t len)`：
     `pet_link_parse` → `xQueueSend(s_evt_q, &m, 0)`（不阻塞、不碰显示）；
   - 开机在 `pet_net_set_line_cb(net_line_cb); pet_net_start();` 附近加：
     `pet_ble_set_line_cb(ble_line_cb);`、`const esp_err_t berr = pet_ble_start();`
     失败只记日志（C09 §2：BLE OFF 不影响 WiFi/显示/按键）；自检日志可用
     `pet_ble_get_status()` + `pet_ble_state_name()` 输出 `BLE state=... addr=... mtu=... lines=...`；
   - **不要**在 BLE 行回调里调用 `pet_ble_stop()`（会与 NimBLE host 任务互相等待）。
3. USB 路径已调用 `pet_net_line_accept(now_ms_u32(), ...)`，BLE 路径内部已调用同一门 → USB/WiFi/BLE
   共享 1 s 指纹环（C08 §1.1）自动成立，无需再改 USB 路径。

## 7. 未验证项 / 风险（真机 H 测试由 Lead 统一做）

- **BLE-02 / BLE-03 / BLE-04 / BLE-05 / G12 / G16 均未在真机跑**（本任务明确不刷机、不开 COM7 长稳期间串口）。
- **WiFi + BLE 共存**：`CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y` 由 Kconfig 自动启用；WiFi 吞吐/时延与 BLE
  连接间隔会互相影响，需 H 测试（BLE-05）确认 USB 插件路径不受影响；2 h 长稳（G16）观察是否死机/泄漏。
- **内存**：NimBLE host 任务栈 6144 B；控制器/NimBLE 静态 RAM 约数十 KB（`esp_get_free_heap_size` 对比需
  H 测试记录）。回调在 host 任务上下文执行 main 的解析+入队，若 main 回调做重活可能栈溢出。
- **MTU**：preferred 256（>247 满足 C09），手机端实际可能协商 517 或 23；代码按任意 write 长度分块处理，
  不依赖单包 ≤512。若手机只给 23 B，512 B 行需要 ~23 次 write，行重装已覆盖。
- **"新连接顶替旧连接"（C09 §1）的实现口径**：`MAX_CONNECTIONS=1` + 连接期间不广播，BLE 规范下
  无法在已连接时对第二台设备开放可连接广播；实际行为 = **旧连接断开后立即恢复广播，新设备才能连入**。
  v1 的手机 nRF Connect 场景可接受；若要求"不断开旧连接直接顶替"，需要多连接（v2 再评估）。
- **单实例广播**：断开/连接失败/广播完成都会自动重启广播；`pet_ble_start()` 幂等；`pet_ble_stop()` 仅测试用
  （`nimble_port_stop()` 需 host 任务在跑；若 host 任务创建失败（`nimble_port_freertos_init` 无返回值、不可检测）
  调用 stop 可能等待，属测试 API 的已知边界；正常启动不会走到）。
- **TX Notify 未实现发送**（C09 允许 v1 可选、默认关）：只注册特征并记录订阅事件。
- **无配对/无加密**（C09 §1）：局域网级可信；`CONFIG_BT_NIMBLE_NVS_PERSIST` 保持默认 `n`，无 bonding 存储。
- **NVS**：`pet_ble_start()` 内部 `nvs_flash_init()`（main 已初始化时幂等）；失败→BLE OFF、返回错误，不影响其它功能。
- **BLE MAC 名**：广播名用 `esp_read_mac(ESP_MAC_BT)` 后 2 字节，与 WiFi AP SSID 的口径一致（C08 A4）。

## 8. 文件清单（本次新增/修改）

| 文件 | 行数 | 说明 |
| --- | --- | --- |
| `firmware/components/pet_ble/include/pet_ble.h` | 75 | C09 §2 API + 集成/失败降级/NUS UUID/广播名常量 |
| `firmware/components/pet_ble/include/pet_ble_lines.h` | 34 | 行重装纯逻辑 API（host 可测） |
| `firmware/components/pet_ble/pet_ble.c` | 588 | NimBLE peripheral：NUS GATT、广播、写回调、跨通道去重、状态、start/stop |
| `firmware/components/pet_ble/pet_ble_lines.c` | 42 | 行重装真实现（复用 `pet_linebuf`，不改它） |
| `firmware/components/pet_ble/CMakeLists.txt` | 9 | 组件定义（REQUIRES `bt nvs_flash esp_timer pet_net pet_ui`） |
| `firmware/sdkconfig.defaults` | 47（+20 行） | **仅追加** BT/NimBLE/coex 配置，保留原有 27 行 |
| `tests/test_m5b_ble.c` | 481 | 10 用例 / 137 断言（BLE-01a..j） |
| `tests/run_m5b_ble_tests.ps1` | 92 | mingw gcc 运行器（`-LogPath` 可留 RED 档） |
| `tests/m5b2-run.log` | 753 B | GREEN 原始输出 |
| `tests/m5b2-red-run.log` | 10,952 B | RED 原始输出（占位阶段） |
| `tests/RED-GREEN-M5B2.md` | 本文件 | 记录/口径/构建/接线/风险 |

## 9. 约束遵守

- 未改 `firmware/main/`、其它 components、`docs/`、`host-plugin/`、`tools/`；未提交 git。
- **未开串口、未刷机、未碰插件/DSH**；构建产物仅在 `D:\DSH\esp-build\bledev-verify` 与临时探针
  `D:\DSH\esp-build\bledev-integ*`（均在仓库外）；`D:\DSH\esp-build\buddy154` 未做任何写操作。
