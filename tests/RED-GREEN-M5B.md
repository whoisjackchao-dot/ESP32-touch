# M5b-1 host 测试记录（C08：跨通道去重 + 行切分复用）

- 日期：2026-10-31
- 任务：`task-8`（net-dev）。性质：**T 类新逻辑 RED→GREEN**（去重器是新实现；行切分复用既有 `pet_linebuf`）。
- 契约：`docs/contracts/C08-wifi-channel.md`（2026-10-31 冻结）§1 / §1.1；`C05` §1 行长/CRLF 语义。
- 被测（真实现）：
  - `firmware/components/pet_net/pet_net_dedupe.c`（纯逻辑：16 槽指纹环 + 1000 ms 窗口 + 32 位回绕）；
  - `firmware/components/pet_ui/pet_linebuf.c`（**只链接复用，未改动**；≤512 B、`\r` 剥除、超长丢弃）。
- 口径：只用公开头文件 API；无 stub、无 sleep、不碰串口/网络/硬件/真实时钟；`now_ms` 由测试注入；
  gcc 参数与既有运行器一致（`-std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter`，mingw gcc 16.1.0）。
- 只写 `tests/` 与 `firmware/components/pet_net/`；未改 `main.c`/其它组件/`docs/`/`host-plugin`；未运行刷机/串口。

## 1. RED（去重桩阶段，测试先失败）

实现前的占位 `pet_net_dedupe.c` 只保证编译（`feed` 恒 ACCEPT、指纹恒 0、init 不清零）。运行：

```powershell
pwsh -NoProfile -File tests/run_m5b_tests.ps1 -LogPath tests/m5b-red-run.log
```

统计与失败原文（完整见 `tests/m5b-red-run.log`，14,197 B）：

```text
[MODE] GREEN: firmware/components/pet_net/pet_net_dedupe.c + firmware/components/pet_ui/pet_linebuf.c
[CC] tests/test_m5b_net.c
[RUN] test_m5b_net.exe
  ASSERT FAIL tests/test_m5b_net.c:105: pet_net_dedupe_fingerprint("", 0) (got 0x0000000000000000, want 0xCBF29CE484222325)
  ASSERT FAIL tests/test_m5b_net.c:106: pet_net_dedupe_fingerprint("a", 1) (got 0x0000000000000000, want 0xAF63DC4C8601EC8C)
  ...
FAIL DED-01-fingerprint-vectors (8/11 asserts failed)
FAIL DED-02-init-and-first-accept (73/75 asserts failed)
...
FAIL NET-03-multi-line-order (5/13 asserts failed)
SUMMARY m5b_net: cases=11 passed=1 failed=10 asserts=193
=== EXIT 1 (test_m5b_net.exe) ===
RESULT: FAIL (exit 1)
```

RED 是**断言失败**（非编译/环境错误）：11 用例中 10 失败，唯一 PASS 的是 `NET-01`（纯 `pet_linebuf` 复用，桩不影响）。

## 2. 测试侧修正记录（如实保留）

补充真实实现后 GREEN 首跑仅剩 1 条失败，定位为**测试预期笔误**（实现正确）：

```text
  ASSERT FAIL tests/test_m5b_net.c:361: d.dropped == 4 (got 3, want 4)
FAIL NET-02-line-split-and-across-channel (1/16 asserts failed)
SUMMARY m5b_net: cases=11 passed=10 failed=1 asserts=193
```

原因：NET-02 中 1 s 窗口内的重复行共 3 条（USB 重发 1 + WiFi 重放 2），DROP 计数应为 3。只改测试期望（4→3），实现未改。

## 3. GREEN（实现完成）

```powershell
pwsh -NoProfile -File tests/run_m5b_tests.ps1
```

```text
[CC] tests/test_m5b_net.c
[RUN] test_m5b_net.exe
PASS DED-01-fingerprint-vectors (11 asserts)
PASS DED-02-init-and-first-accept (75 asserts)
PASS DED-03-window-boundary (9 asserts)
PASS DED-04-different-lines (10 asserts)
PASS DED-05-ring-capacity (22 asserts)
PASS DED-06-time-wrap (6 asserts)
PASS DED-07-drop-does-not-refresh (5 asserts)
PASS DED-08-null-guards (10 asserts)
PASS NET-01-linebuf-contract-reuse (16 asserts)
PASS NET-02-line-split-and-across-channel (16 asserts)
PASS NET-03-multi-line-order (13 asserts)
SUMMARY m5b_net: cases=11 passed=11 failed=0 asserts=193
=== EXIT 0 (test_m5b_net.exe) ===
RESULT: PASS (all M5b host tests green)
```

**11/11 用例、193/193 断言 PASS，退出码 0；编译 0 warning / 0 error。** 原始日志 `tests/m5b-run.log`。

| # | 用例 | 覆盖点（C08） | 断言 |
| --- | --- | --- | --- |
| 1 | `DED-01-fingerprint-vectors` | FNV-1a 64 标准向量（空串/`a`/`foobar`/事件名/整行 JSON）；确定性；显式 len；NULL 视为空串 | 11 |
| 2 | `DED-02-init-and-first-accept` | 脏内存 init 清零；首次 ACCEPT 后 fp/len/rx_ms/next/统计落位 | 75 |
| 3 | `DED-03-window-boundary` | 0/1/999 ms DROP，**恰好 1000 ms ACCEPT**，1500 ms DROP，2000 ms ACCEPT | 9 |
| 4 | `DED-04-different-lines` | 不同行互不影响；窗口按各自上次接收时间独立计算 | 10 |
| 5 | `DED-05-ring-capacity` | 16 槽环满：第 17 条不同行轮转覆盖最旧；覆盖后即使窗口内也 ACCEPT；仍在环内仍 DROP | 22 |
| 6 | `DED-06-time-wrap` | 32 位 `now_ms` 回绕（0xFFFFFF00 → 0x00000100 → 0x00000400） | 6 |
| 7 | `DED-07-drop-does-not-refresh` | **DROP 不刷新时间戳**（重复风暴只压 1 个窗口），锁定实现口径 | 5 |
| 8 | `DED-08-null-guards` | `d==NULL`/`line==NULL` 安全 DROP 且不计统计；空行（len=0）是合法行、去重生效 | 10 |
| 9 | `NET-01-linebuf-contract-reuse` | 复用真实 `pet_linebuf`：裸 `\n`、CRLF、空行、恰好 512 B READY、513 B DROPPED 且可恢复 | 16 |
| 10 | `NET-02-line-split-and-across-channel` | 字节流切行 + 去重联动：USB→WiFi 1 s 内同行 DROP；CRLF 重放仍 DROP；新行 ACCEPT | 16 |
| 11 | `NET-03-multi-line-order` | 单 chunk 多行顺序保持；整批重放全部 DROP | 13 |

## 4. 实现口径（与契约的对应/说明）

- 指纹 = **FNV-1a 64**（`fp` 与 `len` 分开存，命中要求两者都相等），对应契约"长度 + FNV-1a 64"；
- 同一指纹在 **<1000 ms**（严格小于）内重复 → DROP；`>=1000 ms` → ACCEPT 并刷新该槽时间；
- **DROP 不刷新**接收时间（否则持续重传会无限压制；NET-01 ">1 s 处理" 仍成立）；
- 16 槽轮转覆盖最旧条目；被覆盖的旧行不再命中（即使仍在窗口内）；
- 长度为 0 的空行是合法原始行（TCP 任务在交付前跳过空行，避免 main 解析空串）；
- 时间比较 `(uint32_t)(now - rx) < 1000`：与设备 `esp_timer_get_time()/1000` 低 32 位一致，回绕安全；
- **跨通道共用**：`pet_net_line_accept()` 是 USB/WiFi 共用去重门（WiFi 内部已调用；main 的 USB 路径需接线调用，见 `pet_net.h` 顶部说明）。

## 5. 固件构建验证（不属于 host 测试，便于 Lead 复核）

### 5.1 仓库工程（独立性验证，独立 build 目录）

```powershell
idf.py -B D:\DSH\esp-build\netdev-verify -DIDF_TARGET=esp32s3 build
```

- 结果：**0 warning / 0 error**，`Project build complete`；编译了 `firmware/components/pet_net/pet_net.c` 与 `pet_net_dedupe.c`（`pet_net` 已在组件列表）。
- app size：`buddy154.bin 0xc46c0 bytes`（804,544 B），app 分区 3 MB，**74% free**。
- 说明：`main.c` 未接线（本任务不允许改 main），链接器尚未拉取 `pet_net`/WiFi 代码，故镜像大小与基线一致；构建日志见
  `D:\DSH\esp-build\netdev-verify-build.log`（首次失败仅两处笔误：`NET_AP_IP`→`PET_NET_AP_IP`、注释内 `/*` 触发 `-Werror=comment`，已修复）与 `netdev-verify-build2.log`（成功）。

### 5.2 临时"main 接线"集成验证工程（不入仓库，位于 `D:\DSH\esp-build\netdev-integ`）

为验证 `main` 引用 `pet_net` 时**可编译 + 可链接**（解决"未接线所以未链接"的盲区），在 build 区建了一个最小探针 main：
`pet_net_set_line_cb` → `pet_net_start` → `pet_net_get_status` → `pet_net_line_accept` ×2 → `pet_net_enter_provisioning`。

- 结果：**0 warning / 0 error**，链接成功；`nm` 确认 `pet_net_start`/`pet_net_line_accept`/`pet_net_dedupe_*` 等符号进入 ELF；
- app size：`netdev_integ.bin 0xcac60`（830,560 B，3 MB 分区 74% free）；
- 与仓库镜像的差异主要是 WiFi/HTTP/Netif/LwIP 代码被拉入（text 261,881 → 699,264 B）；
- **推算 main 正式接线后**：约 804,544 + 437K(WiFi/pet_net text) ≈ **1.2 MB**，3 MB app 分区仍约 60% 空闲，无需改分区表。

## 6. 未验证项 / 风险（真机 H 测试由 Lead 统一做）

- **NET-03 / G13 / NET-04 / NET-05 均未在真机跑**（本任务明确不刷机、不碰 COM7）：TCP 8766 实际收发时延、AP 页面扫描/保存/重启、错密码 30 s 回落 AP + 120 s 回 STA、SNTP 首次同步与 `time_valid`。
- 32 字节 SSID 边界：`wifi_config_t.sta.ssid` 为 32 B 定长字段，恰好 32 B 的 SSID 无法以 NUL 结尾（按 802.11 上限处理）；
  若 IDF 驱动实现依赖 `strlen` 会有风险，普通 ≤31 B SSID 不受影响。
- AP 用 **APSTA** 模式（为 `/scan` 提供 STA 接口，不发起连接）；AP 模式下若历史 STA 配置仍在，代码不调用 `esp_wifi_connect`，
  但需真机确认不会自动回连（H 测试时观察日志 `sta: connected` 不应在 AP 期间出现）。
- `/scan` 为阻塞扫描（约 2–3 s，httpd 任务栈 6 KB），个别手机会短暂抖动；`/save` 应答后延迟 600 ms 再 `esp_restart`，
  若手机网络极慢可能收不到页面（不影响保存本身）。
- `pet_net_start()` 同步做 NVS/netif/wifi 初始化 + `esp_wifi_start`（约百毫秒级，开机一次）；若实测拖慢首帧，可改为任务内初始化。
- 去重 DROP 日志为 `ESP_LOGI`（未限频）：极端重传风暴下可能刷日志；H 长稳（G16）时留意。
- 回调在 `pet-tcp` 任务上下文：main 回调必须只做 `pet_link_parse` + `xQueueSend`，不可阻塞。

## 7. 文件清单（本次新增）

| 文件 | 行数 | 说明 |
| --- | --- | --- |
| `firmware/components/pet_net/include/pet_net.h` | 78 | C08 §4 API + 集成说明（main 接线/去重门/失败降级） |
| `firmware/components/pet_net/include/pet_net_dedupe.h` | 53 | 去重纯逻辑 API/结构（16 槽、1000 ms） |
| `firmware/components/pet_net/pet_net_dedupe.c` | 72 | FNV-1a 64 + 环 + 窗口（host 可测） |
| `firmware/components/pet_net/pet_net.c` | 1,274 | STA/AP 配网/TCP 8766/HTTP/SNTP/状态/去重门 |
| `firmware/components/pet_net/CMakeLists.txt` | 9 | 组件定义（REQUIRES esp_wifi/esp_netif/esp_event/esp_http_server/nvs_flash/lwip/esp_timer/pet_ui） |
| `tests/test_m5b_net.c` | 435 | 11 用例 / 193 断言 |
| `tests/run_m5b_tests.ps1` | 91 | mingw gcc 运行器（`-LogPath` 可留 RED 档） |
| `tests/m5b-run.log` | 767 B | GREEN 原始输出 |
| `tests/m5b-red-run.log` | 14,197 B | RED 原始输出（桩阶段） |
| `tests/RED-GREEN-M5B.md` | 本文件 | 记录/口径/构建/风险 |

## 8. 约束遵守

- 未改 `firmware/main/`、其它 components、`docs/`、`host-plugin/`、`tools/`；未提交 git；未刷机、未开串口、未动插件/DSH。
- 构建产物仅在 `D:\DSH\esp-build\netdev-verify`（以及临时集成验证 `D:\DSH\esp-build\netdev-integ*`，均在仓库外）。
