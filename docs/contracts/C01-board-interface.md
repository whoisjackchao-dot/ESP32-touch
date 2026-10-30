# C01：板级接口契约（草案，未冻结）

> 状态：**已冻结（2026-10-30）**。冻结后任何变更必须记录原因、影响面与重跑用例。
> 目标：让 `pet_state / pet_render / pet_link / pet_engine` 与具体板卡解耦；修正上游"HAL 是唯一硬件层"名不副实的问题。

## 1. 所有权

| 规则 | 说明 |
| --- | --- |
| 只有 `board/` 目录可以调用 ESP-IDF 驱动 | `gpio_*`、`i2c_*`、`spi_*`、`i2s_*`、`esp_lcd_*`、`ledc_*`、`adc_*` 只允许出现在 `board/` |
| 业务层只能通过本契约访问硬件 | 包含按键读取、电池、充电状态、电源锁存 |
| 禁止业务层直接调用 `pm1` / 任何 PMIC / 任何 GPIO | 上游 `pet_engine` 直接调 `pm1_battery()` 是反例 |

## 2. 接口（C，草案）

```c
typedef enum { PET_CHARGE_UNKNOWN = 0, PET_CHARGE_IDLE, PET_CHARGE_CHARGING, PET_CHARGE_FULL } pet_charge_t;

typedef struct {
    /* 生命周期 */
    esp_err_t (*init)(void);          /* 上电后最先调用；内部完成电源锁存、I2C、面板、背光 */
    void      (*deinit)(void);        /* 仅测试用 */

    /* 时间与显示 */
    uint32_t  (*now_ms)(void);
    void      (*present)(const pet_surface_t *surface);
    void      (*set_backlight)(bool on);

    /* 音频输出 */
    void      (*play_sound)(pet_sound_t sound);
    bool      (*audio_busy)(void);

    /* 面板尺寸（只读） */
    int       panel_width;
    int       panel_height;

    /* 电源 */
    int       (*battery_mv)(void);    /* 读不到返回 -1，禁止返回 0 冒充 0% */
    pet_charge_t (*charge_state)(void);
    void      (*power_off)(void);     /* GPIO2 拉低 */
} pet_board_t;

const pet_board_t *pet_board_get(void);
esp_err_t          pet_board_init(void);
```

## 3. 语义与不变量

| 项 | 规定 |
| --- | --- |
| `init()` 成功 | 背光可控、面板可刷、I2C 可用、**GPIO2 已拉高**（电池供电不掉电） |
| `init()` 失败 | 返回错误并记录具体阶段；**不得**让系统挂死；串口仍可输出 |
| `now_ms()` | 单调递增；溢出按 32 位回绕处理；启动后 0 起 |
| `present()` | 调用方保证 `surface` 生命周期覆盖调用；实现可阻塞至 DMA 完成；禁止在中断中调用 |
| `battery_mv()` | 失败返回 `-1`；UI 必须显示"未知"，不得显示 0% |
| `charge_state()` | 读不到返回 `UNKNOWN` |
| `power_off()` | 先关背光/音频，再拉低 GPIO2；不得返回 |
| 线程 | `init/present/set_backlight/play_sound` 只在主循环任务调用；`now_ms/battery_mv/charge_state` 可任意任务调用 |

## 4. 本契约不覆盖（v1）

- 触摸（`R18` 后置）：二期新增 `pet_board_touch_*`，不改本接口已有字段。
- IMU（`R19` 后置）：二期新增 `pet_board_imu_*`。
- 麦克风/AEC（`R20` 后置）：二期新增采集接口。
- OTA（`R25` 后置）：走独立 `board_ota_*`，不进 `pet_board_t`。
- 具体 GPIO 编号：由 `C02` 与板级实现决定，业务层不得出现数字引脚。

## 5. 测试

| 级别 | 内容 |
| --- | --- |
| T | host 端以 stub 实现 `pet_board_t`，验证引擎在 `init` 失败、`battery_mv=-1`、`present` 空指针保护下的行为 |
| H | 真机验证 `init` 成功、背光可控、`present` 一帧、`power_off` 生效 |

## 6. 变更方式

冻结后任何字段增删/语义变化必须：① 记录原因；② 更新 `05-plan.md` 受影响节点；③ 重跑相关 T/H 用例。
