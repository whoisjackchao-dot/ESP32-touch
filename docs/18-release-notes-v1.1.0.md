# v1.1.0 发布说明（2026-11-01）

> 仓库：https://github.com/whoisjackchao-dot/ESP32-touch
> 相对 v1.0.0：新增 **WiFi 事件通道 + 配网 + NTP + BLE GATT**（M5b），USB 插件通道不变。
> 许可：MIT（派生 MIT 上游；ES8311 组件 Apache-2.0；字体 OFL；语音资产见 NOTICE）。

## 1. 新增功能（M5b）

### 1.1 WiFi 事件通道（TCP :8766）

- STA 模式连接 2.4 GHz 路由器；TCP server **8766** 收 **C05 NDJSON** 事件行（与 USB 同协议）。
- 配置：`config` 见 `docs/contracts/C08-wifi-channel.md`；设备 IP 可通过串口 `p` 的 `NET ...` 行查看。
- 断线自动重连（退避 1/2/4/8/16/30 s）；有凭据 30 s 未连上 → 回落 AP 配网 120 s → 再试。

### 1.2 配网（AP 网页）

- 开机无凭据/菜单 WIFI/串口命令 `W` → AP：SSID **`ESP32-Touch-<MAC后4>`**，WPA2 口令 **`touch154`**。
- 浏览器打开 **http://192.168.4.1**：扫描列表选 SSID + 密码 → 保存重启；失败自动回落热点。
- 凭据存 NVS（`net/ssid`、`net/pass`）。

### 1.3 NTP 校时

- 获得 IP 后 SNTP（`pool.ntp.org`），时区 **CST-8**；`time_valid` 可在串口 `NET` 行看到。

### 1.4 BLE GATT 事件通道

- BLE peripheral（NimBLE），广播名 **`ESP32-Touch-<MAC后4>`**。
- Service：Nordic UART `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
  - RX（Write/WriteNoRsp）`6E400002-...`：写 C05 NDJSON（可跨 write 分片，行长 ≤512 B）
  - TX（Notify）`6E400003-...`：v1 仅注册
- 单连接；断开自动恢复广播；**v1 无配对/无加密**（近距离局域网级可信）。

### 1.5 跨通道去重

- **USB / WiFi / BLE 共用一个 1 s 指纹环**：同内容 1 s 内重复只处理一次（`C08 §1.1`）。

### 1.6 显示修复（BLE/WiFi 共存）

- BLE 启用后内部 DMA RAM 紧张导致刷屏 `ESP_ERR_NO_MEM`；LCD 单次 DMA 28,800 B → **3,840 B（8 行/块）**、
  队列深度 10→4，渲染恢复（C02 面板参数未改）。

## 2. 安装/升级

### 2.1 固件

```powershell
# 整包镜像（Release 附件，偏移 0x0；factory app 在 0x20000）
esptool.py --chip esp32s3 -p COM7 -b 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 esp32-touch-v1.1.0-full.bin
# 或源码构建
$env:IDF_TOOLS_PATH='D:\Espressif'; $env:IDF_PATH='D:\Espressif\frameworks\esp-idf-v5.5.4'
. "$env:IDF_PATH\export.ps1"; cd ESP32-touch
idf.py -B build -DIDF_TARGET=esp32s3 build
idf.py -B build -p COM7 flash
```

### 2.2 网络配置

1. 首次开机（无凭据）进入 AP `ESP32-Touch-XXXX`；
2. 手机/电脑连该 AP（口令 `touch154`），浏览器 **http://192.168.4.1**，选 2.4 GHz SSID + 密码保存；
3. 设备重启联网；串口 `p` 可见 `NET state=online ... ip=... time_valid=1`。

> **注意**：若用电脑做配网，电脑会暂时离开原网络（模型/远程会话可能断）；建议用手机。

### 2.3 BLE 使用

用 nRF Connect（手机）或 `bleak`（PC）：
连接 `ESP32-Touch-XXXX` → 找 NUS 服务 → 向 RX `6E400002-...` 写 UTF-8 NDJSON（同 C05，示例）：

```json
{"version":1,"event":"task.start","title":"BLE 测试","project":"demo","agent":"DSH","elapsed_s":1}
```

### 2.4 DSH 插件（不变）

`host-plugin/` 与 v1.0.0 相同（USB 通道）；`dsh plugin --profile desktop add <path>` + `config.port`。

## 3. 验证摘要（本机实测，2026-11-01）

| 项 | 结果 |
| --- | --- |
| 构建 | `0x18bcf0` = 1,621,232 B，3 MB 分区 **48% free**，0 warning |
| host 测试 | M2 231 / M3 517 / M5a 436 / M4 28 断言；M5b-1 193；BLE-01 137；插件 56/56 — 全 PASS |
| WiFi TCP（G12） | PC → `:8766` 发 C05，设备 **0.345 s** 变 WORKING |
| 配网（G13） | 成功路径 + 错密码→AP 回落（用户目视）PASS |
| NTP（NET-04） | `time_valid=1` |
| 断线重连（NET-05） | 路由器重启后自动退避重连、重获 IP、NTP 重同步 PASS |
| 跨通道去重 | USB/WiFi/BLE 1 s 内同内容只处理一次 PASS |
| BLE（BLE-03/04/05） | PC bleak：MTU 256、写 C05 → **0.234 s**；断开重连 0.443 s；WiFi+BLE 共存 PASS |
| G16 长稳 | 最终固件 2 h：120 样本、**0 重启**、heap 8,317,396→8,317,396、fps=8 PASS |

## 4. 已知限制

- BLE v1 无配对/加密；单连接；TX Notify 未启用。
- WiFi 无 TLS/token（局域网可信）；单客户端；仅 2.4 GHz。
- 配网 AP 口令固定 `touch154`；AP SSID 后缀为 MAC 低 16 位 hex。
- 本板无独立 RST 键：PWR 长按关机后需拔插 USB/串口硬复位。
- 语音输入/AEC/触摸/IMU/OTA 未包含（`Q03` 后置）。

## 5. 许可与来源

- 派生 ESP32-buddy（MIT，NOTICE）；ES8311 组件 Apache-2.0；字体 OFL 1.1；语音资产 Edge TTS（NOTICE）。
- 无 GPL 代码。
