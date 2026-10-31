# C08：WiFi 事件通道 + 配网 + NTP（M5b-1，冻结 2026-10-31）

> 状态：**已冻结（2026-10-31）**。上游行为参考：`ESP32-buddy`（MIT）`pet_net.c` / `contracts/M4`（行为）。
> 复用协议：`C05`（NDJSON 事件行，字段/错误处理不变）。本契约只新增**传输/配网/校时**，不改 C05 语义。
> 范围：M5b-1（WiFi）；BLE GATT 属 M5b-2（另立 `C09`）。

## 1. 传输（WiFi TCP）

| 项 | 规定 |
| --- | --- |
| 模式 | WiFi **STA**（2.4 GHz；不支持 5 GHz） |
| 服务 | **TCP server : 8766**，单客户端（新连接顶替旧连接） |
| 帧 | **NDJSON**（C05 §2），行长 ≤512 B，`\n` 结尾，`\r` 忽略 |
| 方向 | host → device（同 C05）；设备仅回人类日志（TCP 不回写） |
| 保活 | 客户端可发 `device.heartbeat`（10 s）；设备侧**任一通道**心跳/事件都刷新链路 |
| 断线 | 客户端断开不影响设备状态（15 s 无任何有效事件→OFFLINE） |
| 重连 | 设备监听常开；WiFi 断线自动重连（退避 1/2/4/8/16 s，上限 30 s） |

### 1.1 跨通道去重（v1，USB + WiFi）

- 设备维护最近 **16 条原始行**的指纹环（长度+FNV-1a 64）与接收时间；
- 同一指纹在 **1000 ms** 内重复（无论来自 USB 还是 WiFi）→ **丢弃并记日志**；
- 超出窗口不视为重复；不同内容不互相影响；
- 去重发生在解析前（原始字节行）；`pet_link` 仍按 C05 处理。

## 2. 配网（AP 网页）

| 项 | 规定 |
| --- | --- |
| 触发 | ① 开机无已保存凭据 → 自动进入 AP；② `KEY2` 长按菜单选择 **WIFI**；③ 串口命令 `W` |
| AP SSID | `ESP32-Touch-<MAC 后 4 hex>`（如 `ESP32-Touch-4BE4`） |
| AP 安全 | WPA2，密码固定 `touch154`（v1，README 明示） |
| 地址 | `http://192.168.4.1/` |
| 页面 | `GET /`：表单（SSID 下拉=扫描结果 + 密码 + 保存）；`GET /scan`：JSON AP 列表；`POST /save`：保存并重启 |
| 保存 | NVS `net/ssid`、`net/pass`（≤32/64 B）；保存后 `esp_restart()` |
| STA 失败回落 | 有凭据但 **30 s** 未连上 → 回 AP 模式 **120 s**，超时再重试 STA（循环） |
| 状态页 | `GET /status`：JSON `{state,ssid,ip,rssi,time_valid}`（供调试） |

## 3. NTP 校时

| 项 | 规定 |
| --- | --- |
| 协议 | SNTP（`pool.ntp.org`，可 Kconfig 覆盖），STA 获得 IP 后启动 |
| 时区 | 默认 **CST-8**（`UTC+8`，无夏令时）；`setenv("TZ","CST-8")` + `tzset()` |
| 生效 | 同步成功后 `time_valid=true`；番茄钟统计日期使用本地时间（替代 `D-03` 的"手动 day"路径） |
| 失败 | 不阻塞；`time_valid=false` 时统计沿用原行为（`day` 未知哨兵） |

## 4. 组件 API（`firmware/components/pet_net/`）

```c
typedef enum { PET_NET_OFF = 0, PET_NET_AP, PET_NET_CONNECTING, PET_NET_ONLINE } pet_net_state_t;

typedef struct {
    pet_net_state_t state;
    char ssid[33];
    char ip[16];      /* STA 时有效；AP 时为 192.168.4.1 */
    int  rssi;        /* dBm；无连接为 0 */
    bool time_valid;
} pet_net_status_t;

esp_err_t pet_net_start(void);                 /* 读 NVS 凭据；无凭据→AP；有→STA；创建 TCP server/SNTP */
void      pet_net_enter_provisioning(void);    /* 进入 AP 模式（菜单/命令） */
void      pet_net_get_status(pet_net_status_t *out);
/* 收到的 C05 行（已跨通道去重）回调给业务层；在 net 任务上下文调用 */
typedef void (*pet_net_line_cb_t)(const char *line, size_t len);
void      pet_net_set_line_cb(pet_net_line_cb_t cb);
```

- `pet_net_start()` 失败（WiFi 驱动/NVS）→ 记日志、返回错误；**不阻塞**显示/按键（同 C04 §5 精神）。
- 去重环为纯逻辑（`pet_net_dedupe.c`），host 可测；回调把行交给 main → `pet_link_parse` → 事件队列（同 USB 路径）。

## 5. 测试

| 用例 | 内容 | 级别 |
| --- | --- | --- |
| `NET-01` | 去重：同指纹 1 s 内丢弃；>1 s 处理；不同行不影响 | T |
| `NET-02` | C05 行长/CRLF 处理复用（与 USB 同解析器） | T |
| `NET-03` | TCP 收 NDJSON → 设备状态变化 ≤1 s（G12） | H |
| `G13` | AP 页面扫描 ≥3 AP；保存后 60 s 内拿到 IP；错密码 2 min 内回落 AP | H |
| `NET-04` | NTP 同步后 `time_valid=true`，本地日期用于统计（D-03） | H |
| `NET-05` | WiFi 断线重连；USB/WiFi 重复事件只处理一次 | H |
| `G16` | 插电 2 h 无死机/泄漏（发布前长稳） | H（发布前） |

## 6. 明确不做（M5b-1）

- BLE GATT（M5b-2）；TLS/鉴权（局域网可信，v1 无 token）；5 GHz；多客户端；MQTT/HTTP 事件入口；OTA。
- 不做云端语音（`Q08`）/AEC（`Q09`）。

## 7. 实现对齐记录（2026-10-31，真机 H）

| # | 契约表述 | 实现口径 |
| --- | --- | --- |
| `A1` | §2 状态页 `GET /status` | **仅 AP 配网模式**提供 `/` `/scan` `/save` `/status`；STA 模式不启动 HTTP。STA 自检用串口 `NET state=... ssid=... ip=... rssi=... time_valid=...` 行 |
| `A2` | §2 AP 保存后重启 | 保存 NVS 后 `esp_restart()`；用户需重新上电等待 STA 连接（实测 <30 s） |
| `A3` | §1.1 去重 | 同一 16 槽 FNV-1a64 环同时服务 USB 与 TCP；命中 `<1000 ms` 记 `dedupe: duplicate line dropped` |
| `A4` | AP SSID | 实测 `ESP32-Touch-<MAC 后2字节 hex>`（例 `4BE5`）；C08 写"后 4 hex"按 MAC 低 16 位生成 |

## 8. 变更方式

传输/去重窗口/配网流程任一变更 → 修订本文件 + 重跑 `NET-*`/`G13`；`C05` 字段语义不得变化。

