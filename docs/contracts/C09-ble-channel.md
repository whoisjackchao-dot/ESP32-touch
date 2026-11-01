# C09：BLE GATT 事件通道（M5b-2，冻结 2026-11-01）

> 状态：**已冻结（2026-11-01）**。协议复用 `C05`（NDJSON 事件行，字段/错误处理不变）；本契约只新增 BLE 传输。
> 跨通道去重沿用 `C08 §1.1`（USB/WiFi/BLE **共用一个 1 s 指纹环**）。
> 实现建议：ESP-IDF NimBLE（`bt` 组件），BLE peripheral。

## 1. 传输（BLE GATT）

| 项 | 规定 |
| --- | --- |
| 角色 | 设备 = GATT server / BLE peripheral；NimBLE |
| 广播名 | `ESP32-Touch`（可带 MAC 后 2 字节：`ESP32-Touch-<4hex>`） |
| Service | **Nordic UART Service** UUID `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX 特征 | UUID `6E400002-...`，**Write / Write Without Response**（host→device，C05 NDJSON） |
| TX 特征 | UUID `6E400003-...`，**Notify**（device→host，仅人类可读日志/状态文本；v1 可选，默认关闭） |
| 帧 | 与 C05 相同：UTF-8 NDJSON，行长 ≤512 B，`\n` 结尾；**允许跨多个 GATT write 分片**，按连接重装 |
| 安全 | **v1 无配对/无加密**（局域网级可信；README 明示）。v2 再评估 bonding |
| 连接 | 单连接（新连接顶替旧连接）；断开后继续广播；设备不依赖 BLE 保持状态 |
| MTU | 协商默认 ≥247（NimBLE 默认 23→协商）；不依赖单包 ≤512 |

### 1.1 行重装

- 每连接一个 `pet_linebuf`（≤512 B，剥 `\r`）；一次 write 可含 0..N 行；
- 半行跨 write 缓存；超长行丢弃并记日志（同 C05 §5）；
- 行完整后 → **跨通道去重门**（`C08 §1.1`，与 USB/WiFi 共用）→ `main` 的 `pet_link_parse` → 事件队列。

## 2. 组件 API（`firmware/components/pet_ble/`）

```c
typedef enum { PET_BLE_OFF = 0, PET_BLE_ADVERTISING, PET_BLE_CONNECTED } pet_ble_state_t;

typedef struct {
    pet_ble_state_t state;
    char addr[18];   /* 连接时对端 MAC 字符串；否则空 */
    int  mtu;
    uint32_t lines_total;   /* 已接收完整行数（调试） */
} pet_ble_status_t;

esp_err_t pet_ble_start(void);     /* 初始化 NimBLE、注册 GATT、开始广播；幂等；失败不阻塞显示 */
void      pet_ble_stop(void);      /* 停广播/断开（测试用） */
void      pet_ble_get_status(pet_ble_status_t *out);
typedef void (*pet_ble_line_cb_t)(const char *line, size_t len); /* 已去重；BLE host 任务上下文 */
void      pet_ble_set_line_cb(pet_ble_line_cb_t cb);
```

- 去重调用：`pet_net_line_accept(now_ms_u32(), line, len)`（`C08` 共用门；`pet_ble` 依赖 `pet_net` 仅为此函数）。
- 回调在 NimBLE host 任务上下文，**不可阻塞**（同 WiFi 回调约束）。
- `pet_ble_start()` 失败（NimBLE 初始化/内存/共存）→ 记日志、状态 OFF、返回错误；WiFi/显示/按键不受影响。
- WiFi 与 BLE 共存：BLE 常开广播；WiFi STA/AP 同时工作（ESP32-S3 软件共存）。

## 3. 测试

| 用例 | 内容 | 级别 |
| --- | --- | --- |
| `BLE-01` | 行重装：单 write 多行 / 跨 write 半行 / CRLF / 超长丢弃 | T（纯逻辑） |
| `BLE-02` | 去重门共享：BLE 与 USB 1 s 内同内容只处理一次 | T+H |
| `BLE-03` | 手机 nRF Connect / PC bleak 写入 C05 → 设备状态 ≤1 s（G12 BLE 通道） | H |
| `BLE-04` | 断开重连后继续可写；广播恢复 | H |
| `BLE-05` | WiFi + BLE 同时工作互不影响（USB 插件保持） | H |
| `G16` | 插电 2 h 长稳（BLE 广播 + WiFi） | H（发布前） |

## 4. 明确不做（M5b-2）

- 配对/bonding/加密（v1）；BLE 音频/OTA；多连接；TX notify 设备日志（可选，默认关）。
- 5 GHz/蓝牙 Mesh；手机 App 专属协议（一律 C05 NDJSON）。

## 5. 变更方式

UUID/特征/重装/安全任一变更 → 修订本文件 + 重跑 `BLE-*`/`G12`（BLE 通道）；`C05` 语义不变。
