# C06：16 MB 分区表契约（草案，未冻结）

> 状态：**已冻结（2026-10-30）**。v1 只用 `factory`；`ota_0/ota_1` 为二期 OTA 预留，现在写入不生效但避免二次重划。
> 依据：实物 16 MB Flash（E06）；出厂固件已有 `factory + ota_0 + assets`（E08）。

## 1. 表

```csv
# Name,     Type, SubType, Offset,   Size
nvs,        data, nvs,     0x9000,   0x6000
otadata,    data, ota,     0xf000,   0x2000
phy_init,   data, phy,     0x11000,  0x1000
factory,    app,  factory, 0x20000,  0x300000
ota_0,      app,  ota_0,   0x320000, 0x300000
ota_1,      app,  ota_1,   0x620000, 0x300000
voices,     data, 0x40,    0x920000, 0x200000
link,       data, 0x41,    0xb20000, 0x2000
```

| 分区 | 大小 | 用途 | v1 是否使用 |
| --- | --- | --- | --- |
| `nvs` | 24 KB | WiFi 凭据、统计、静音、BLE 绑定 | ✅ |
| `otadata` | 8 KB | OTA 选择 | ❌（预留给二期） |
| `phy_init` | 4 KB | RF 校准 | ✅（IDF 自动） |
| `factory` | **3 MB** | v1 唯一 app；也是 OTA 失败回退 | ✅ |
| `ota_0` | 3 MB | 二期 OTA 槽 A | ❌ |
| `ota_1` | 3 MB | 二期 OTA 槽 B | ❌ |
| `voices` | 2 MB | 语音/角色资源 | ❌（v1 用内嵌 PCM） |
| `link` | 8 KB | 配对密钥/远端地址（若二期采用） | ❌ |

剩余约 **4.9 MB** 未分配，留作后续（TF/日志/资源）。

## 2. 约束

1. `factory` 起始 **0x20000**，与 ESP-IDF 默认对齐一致。
2. 每个 app 槽 ≤ **3 MB**；当前上游 app 约 1.79 MB，新增触摸/IMU/ES7210 后预计 <2.5 MB，留有余量。
3. `nvs` 不得小于 0x6000（上游用 0x6000）。
4. 修改本表 = 破坏性变更：必须重刷、旧 NVS 可能丢失；需用户批准并记录。

## 3. 验证

- `idf.py partition-table` 输出与本表一致。
- 刷机后 `esptool read_flash 0x8000 0x1000` 解析一致（同 E08 方法）。
- v1 不做 OTA，但 `otadata` 必须存在且为擦除态，避免 bootloader 误判。
