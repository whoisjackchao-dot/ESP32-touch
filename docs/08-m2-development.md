# M2 开发：6 态动画 + 3 卡栈（实施契约与证据计划）

> 日期：2026-10-31；阶段 3（开发）
> 依据：`05-plan.md` M2、`contracts/C03-layout-240x240.md`（冻结，本次附修订记录）、
> 上游 `ESP32-buddy`（MIT，`Q02=A`）`pet_state.c` 的行为规格。
> 起点事实：M1 真机通过（`E14`）；本机 Windows 原生 IDF v5.5.4 构建已复现（`E15`）。

## 1. 本轮交付（= M2 定义，不多不少）

1. `pet_state`：6 态状态机、`done` 下降沿去抖、DONE 驻留、失联阈值、idle 眨眼/小发呆、**3 卡栈**（规则见 §3）。
2. `pet_ui_240`：240×240 整屏场景（状态条/宠物/头顶标记/3 卡/页脚）+ 单卡 232×28 渲染；OFFLINE 整屏灰度。
3. 主循环：125 ms（8 fps）节拍；`tick(now)` 驱动动画；复用同一 fb，等待最后一 chunk 完成后再渲染下一帧。
4. 调试注入通道（串口单字符命令，§4）：仅用于 M2 演示与 G1/G3 注入；M5 由事件协议取代。
5. host 单测（`tests/`，mingw gcc）：T 类断言先 RED 后 GREEN。
6. 证据：`E15`（原生构建）、`E16`（构建+刷机）、`E17`（G1 24 次观测）、`E18`（G3 20 次）、`E19`（MEM-01 10 min）。

**明确不做**（属后续节点）：番茄钟/菜单/按键（M3）、语音（M4）、WiFi/BLE/事件协议/宿主桥（M5）、
触摸/IMU/AEC/OTA（后置）、休闲小剧（未在 `05-plan.md` M2 交付内）。

## 2. 接口（实现必须与此一致；host 测试按此写）

### 2.1 `pet_types.h` 增量

```c
#define PET_AGENT_MAX 8
typedef struct {
    char        title[PET_TITLE_MAX];
    char        agent[PET_AGENT_MAX]; /* "OC"/"CC"/"CX"…（≤3 字符，空=不显示前缀） */
    char        project[PET_PROJECT_MAX];
    pet_state_t state;
    int32_t     elapsed_s;
} pet_card_t;
```

> 变更原因：`C03 §2` 行 1 要求 `[前缀] 项目`，而上游 `pet_card_t` 无前缀字段（上游把前缀列在 M8
> 需求、未落到线格式）。本字段为**新增可空字段**，不改既有字段语义；见 `C03 §7` 修订 R2。

### 2.2 `pet_state.h`（新增到 `components/pet_ui/`）

沿用上游接口（事件/视图/播报/卡栈），仅做两处本项目修订：

```c
struct pet_state *pet_state_create(void);
void pet_state_destroy(struct pet_state *s);
void pet_state_apply(struct pet_state *s, const pet_event_t *ev);
void pet_state_tick(struct pet_state *s, uint32_t now_ms);
void pet_state_view(const struct pet_state *s, pet_view_t *out);
pet_sound_t pet_state_take_sound(struct pet_state *s);
void pet_state_set_card(struct pet_state *s, const pet_card_t *card);   /* C03 §3 */
void pet_state_set_cards(struct pet_state *s, const pet_card_t *cards, int count);
```

- `pet_event_t` 增 `const char *agent;`（可空）。
- 同名判定 = **title 相同且 project 相同**（`C03 §3.2`；上游只比 title，本项目按契约改）。
- 空 title 的卡**接收**（`C03 §3.4` 要求空字段渲染占位符，不得整卡丢弃）；上游是丢弃，本项目改。

### 2.3 `pet_ui_240.h`（新增）

```c
#define PET_UI_240_WIDTH  240
#define PET_UI_240_HEIGHT 240
/* 整屏：清屏 + 状态条 + 宠物 + 头顶标记 + 3 卡 + OFFLINE 灰度 */
void pet_ui_scene_240(const pet_view_t *v, const pet_look_t *look, pet_surface_t *s);
/* 单卡：232×28，(x,y)=卡左上；newest=true 时画左侧 2px 强调条 */
void pet_ui_card_240(const pet_card_t *c, pet_surface_t *s, int x, int y, bool newest);
```

实现约束：不修改上游 `pet_render.c` 的 135×240 函数坐标；`pet_ui_240` 复用
`pet_render_pet`（传入 `state=PET_STATE_IDLE` 的副本以抑制其内置标记，标记由本文件按 C03 居中绘制）
与 `pet_text_draw` / `pet_text_draw16`。

## 3. C03 实现细则（本次落地取值，均由 C03 §7 修订记录约束）

| 项 | 取值 |
| --- | --- |
| 状态条 | 网络点 `(4,4,4,4)`；在线 `0x07E0`，OFFLINE 红 `0xF800`；右侧电池/时间 M2 留空 |
| 宠物 | `pet_render_pet` @ `(72,20)`，2×（96×128） |
| 头顶标记 | 2×、7 行、bit0=最左；**顶边 y=2**，横向**居中于 x=120**：`>` 宽 10 → x=115；`!?` 宽 18 → x=111；颜色 `0xFFFF` / `0xFD20` |
| 卡矩形 | `(4,152,232,28)` / `(4,180,232,28)` / `(4,208,232,28)`；卡底 `0x18E3`，无边框 |
| 卡行 1 | `(x+4, y+3)`，8×8 scale 1，`[前缀] 项目`，非 ASCII 以 `?` 占位，宽 ≤224，溢出截断尾 `..` |
| 卡行 2 | 标题：CJK 16×16 顶边 `y+12`，ASCII 8×8 顶边 `y+16`（16px 行内垂直居中），宽 ≤180，溢出尾 `..` |
| 时长 | 8×8 右对齐，右边界 `x+228`，顶边 `y+16`，宽 ≤44；卡用格式 `12M` / `1H02M` / `45S` |
| 强调条 | 最新卡左侧 `(x, y, 2, 28)`，`0x05FF` |
| 空字段 | project 空 → `-`；title 空 → `(untitled)`；两者皆空仍显示占位符 |
| OFFLINE | 先按正常色渲染整屏，再把**整帧**按亮度权重转灰（与 `pet_render_pet` 的 `gray565` 同法），最后画红点 |

> `pet_format_elapsed` 现有格式为 `45S/12M/3H`（`C03 §2` 写 `12m/1h02m`）。**不修改**共享函数，
> 在 `pet_ui_240.c` 内提供卡用格式 `12M` / `1H02M` / `45S`（≤5 字符），见 `C03 §7` 修订 R4。

## 4. M2 调试注入协议（串口，USB-Serial-JTAG，COM7）

单字节命令（不需要回车；`\r\n` 忽略）。设备在**主循环**里处理，最迟 125 ms 生效。

| 字节 | 含义 |
| --- | --- |
| `1` | `PET_EV_TASK_START` |
| `2` | `PET_EV_AGENT_INPUT_REQUIRED` |
| `3` | `PET_EV_TASK_DONE` |
| `4` | `PET_EV_TASK_ERROR` |
| `0` | `PET_EV_AGENT_IDLE` |
| `5` | `PET_EV_LINK_DOWN` |
| `u` | `PET_EV_LINK_UP` |
| `n` | `PET_EV_ANNOUNCEMENT`（心跳，只刷新链路） |
| `c` | 压入下一条**预置卡**（顺序见下） |
| `x` | 清空卡栈 |
| `p` | 打印一行状态 |
| `d` | 导出当前帧缓冲（二进制） |

`p` 输出（一行，UTF-8）：

```text
STATUS state=<IDLE|WORKING|INPUT_REQUIRED|DONE|FAILED|OFFLINE> frame=<0..5> cards=<0..3> fps=<n> heap=<bytes>
CARDS <title>|<title>|<title>        （仅 cards>0 时输出；top-first；title 内不含 `|`）
```

`d` 输出（先屏蔽日志，完成后恢复）：

```text
FBBIN 240 240 115200
<115200 字节：RGB565 little-endian，行主序>
FBEND <crc32-ieee-hex>
```

`c` 的预置卡序列（第 9 次起循环）：

| # | agent | title | project | elapsed_s | state |
| --- | --- | --- | --- | --- | --- |
| 1 | `OC` | `GAMMA` | `openai/chat` | 75 | WORKING |
| 2 | `CC` | `BETA` | `acme/web` | 900 | INPUT_REQUIRED |
| 3 | `CX` | `ALPHA` | `acme/api` | 5 | DONE |
| 4 | `OC` | `DELTA` | *(空)* | 3725 | FAILED |
| 5 | `CC` | `BETA` | `acme/web` | 1200 | INPUT_REQUIRED（**同名更新，应置顶**） |
| 6 | `OC` | *(空)* | `acme/empty` | 10 | WORKING（**空 title 占位**） |
| 7 | `CX` | `THIS-IS-A-VERY-LONG-TITLE-0123456789` | `acme/long` | 59 | WORKING（**截断**） |
| 8 | `OC` | `中文标题测试` | `acme/cjk` | 3600 | DONE（**CJK 16×16**） |

## 5. 用例与证据（T/C/H）

| 用例 | 类别 | 判定 | 本轮方式 |
| --- | --- | --- | --- |
| `ST-01..07` | T | 事件→状态、done 边沿去抖、取消、DONE 驻留、失联/恢复、空值/同名卡 | `tests/test_pet_state.c` RED→GREEN |
| `UI-13` | T | 版式矩形不重叠、卡区固定、空字段不空白 | `tests/test_ui240.c` 像素断言 |
| `GB-01` | T | 灰度转换单调且三通道相等 | `tests/test_ui240.c` |
| `UI-10` | H+T | 6 态各 4 次 = **24 次**观测（门槛原文写 20，见 §6 冲突 C1） | 注入 + 帧缓冲导出 + 人眼复核 |
| `UI-11` | H+T | `>` / `!?` 仅对应态出现（像素位置断言） | 同上 |
| `UI-12` | H+T | OFFLINE 整帧灰度 + 红点 | 同上 |
| `CARD-10..15` | H+T | 3 卡顺序/第 4 卡淘汰/同名置顶/空占位/长截断/CJK | 注入 + 导出断言 |
| `FRM-01` | H | 8 fps 连续渲染无撕裂/闪烁 | `fps` 日志 + 人眼 |
| `MEM-01` | H | 连续 10 min 堆不下降 | 后台运行 `p` 采样 |

## 6. 本轮发现的文档冲突（按 dayu 铁律 11：登记并修订受影响文档）

| ID | 冲突 | 处理 |
| --- | --- | --- |
| `C1` | `G1` 写"6 态各进入/退出 4 次，共 20 次"，算术应为 24 | 实测按 24 次执行并把分母写清：24/24；门槛数值请用户在 M2 验收时确认 |
| `C2` | `C03 §1` 状态条 `(0,0,240,12)` 与头顶标记 `y=2..14` 区域重叠，与"任何区域不得重叠"矛盾 | 修订：状态条**实际内容区**为左 `x=0..7` 网络点、右 `x=196..239`（M3 起）；标记 `x=111..128`，像素不重叠；见 `C03 §7 R1` |
| `C3` | `C03 §2` 8×8 两行（y+3 / y+14）放不下 16×16 CJK（y+14..29 越界 2 px） | 修订行 2 坐标：CJK 顶边 y+12、ASCII 顶边 y+16（同一 16px 行内），见 `C03 §7 R3` |
| `C4` | `README.md`/`README-M1.md` 声称存在 `firmware/CMakeLists.txt`、`pet_state.c`、host BMP 预览，仓库中均不存在 | 以仓库实际为准：本节点补 `pet_state.c`/`pet_ui_240.c`；README-M1 勘误；不补"host BMP 预览"（改由帧缓冲导出 + `tools/host` 替代） |
| `C5` | `06-test-matrix.md` `D-02` 称"只等首个 chunk"是缺陷 | 查 IDF v5.5.4 源码：`en_trans_done_cb` **只在最后 chunk 置位**，`present()` 实际等的是最后一 chunk，非缺陷；更正注释与矩阵 |

## 7. 分工与写作用域（并行）

| 角色 | 写作用域 | 交付 |
| --- | --- | --- |
| Lead | `firmware/`、`docs/` | 状态机/渲染/主循环/注入、契约修订、证据与最终验收 |
| host-tests | `tests/`（新建） | T 类用例 + 运行器；RED→GREEN 记录（先 RED 后由 Lead 实现） |
| host-tools | `tools/host/`（新建） | 串口驱动、帧缓冲导出→PNG、G1/G3 自动跑测与断言脚本；只用合成数据自测 |

## 8. 残余风险

- 串口注入通道（USB-Serial-JTAG driver + VFS）未在真机验证过；失败则回退为"编译期 demo 序列"并记录。
- G1/G3 的"目视"在无摄像头条件下用**帧缓冲导出 + 像素断言**替代，属 T/C 级证据；最终 H 结论仍需用户目视确认（图片随验收提交）。
- 12 帧立绘只用 `frame×2 + hair_right` 两个相位；`hair_right` 相位 500 ms 由契约 M2 §8 推定，未单独 H 验证。

## 9. 实施结果（2026-10-31 收口，与 §1 承诺逐条对照）

| 承诺 | 结果 | 证据 |
| --- | --- | --- |
| `pet_state` 6 态/边沿/驻留/失联/动画/卡栈 | 已实现；按 `C03 §3` 改同名判定与空 title 语义 | `E16`（T 9/9） |
| `pet_ui_240` 整屏 + 单卡 + 离线灰化 | 已实现（含 CJK 标题 16×16；标记方向缺陷 `D-05` 已修） | `E16`（T 5/5）、`E17`、`E20` |
| 8 fps 主循环 + fb 复用 | 已实现；`present()` 语义经 IDF 源码复核（`D-02` 误报关闭） | `E17` `fps=8`、`E16` 构建 |
| 串口注入（§4 协议） | 已实现；真机实测发现两处主机侧坑（立即读、精确长度）并写入 `tools/host/README.md` | `E17`/`E18` 方法 |
| host T 测试 RED→GREEN | 15 用例 / 231 断言；RED 阶段 13/14 失败留档 | `tests/RED-GREEN.md` |
| G1/G3 证据 | G1 24/24、G3 20/20 + 特例 4/4 | `E17`/`E18` |

**与计划的偏差（均已登记）**：
1. `G1` 实测分母 24（原文 20，算术错误）→ `C03 §7 R8`。
2. `C03` 内部 4 处不可实现/矛盾 → `C03 §7 R1–R6`（行 2 CJK 基线、状态条内容区、前缀字段、时长格式、截断符、边框）。
3. 新增 `D-05`（上游标记点阵镜像）→ 修复 + `UI-11b` 回归。
4. host 工具从"Lead 自己写抓图脚本"升级为 `tools/host/`（由 teammate 交付，含 G1/G3 自动跑测与模拟器）。
5. 原计划"host BMP 预览"不存在于仓库，M2 以"设备帧缓冲导出→PNG"替代（`README-M1` 已勘误）。
