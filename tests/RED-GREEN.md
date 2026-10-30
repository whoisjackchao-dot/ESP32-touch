# M2 host 单测 RED→GREEN 记录（T 类）

- 日期：2026-10-31
- 被测：`firmware/components/pet_ui/pet_state.c`、`pet_ui_240.c`（E16 由 Lead 实现）；
  链接真实 `pet_render.c`、`pet_text.c`。
- 运行器：`tests/run_tests.ps1`（mingw gcc 16.1.0，无 CMake）。
- 命令（仓库根 `D:\DSH\ESP32-touch`）：

```powershell
pwsh -NoProfile -File tests/run_tests.ps1
```

- 测试只读公开头文件；时间由构造的 `now_ms` 驱动（无 sleep）。
- 用例：`tests/test_pet_state.c` 9 例（ST-01..07 + 动画/视图拆分），
  `tests/test_ui240.c` 6 例（UI-13 / CARD-13 / UI-11 / UI-11b D-05 镜像回归 / GB-01）。

## 1. RED（实现为桩，断言失败）

前提：`pet_state.c`/`pet_ui_240.c` 当时均为 `/* RED 桩（临时）：仅满足链接，行为未实现。*/`
（`pet_state.c` 1303 B，`pet_ui_240.c` 509 B；Lead 替换后的真实现见 §3）。

命令与完整原始输出：

```text
[CC] test_pet_state.c + pet_state.c
[CC] test_ui240.c + pet_ui_240.c + pet_render.c + pet_text.c
=== RUN test_pet_state.exe ===
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:117: v.state == PET_STATE_WORKING (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:123: v.state == PET_STATE_INPUT_REQUIRED (got 0, want 2)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:128: v.state == PET_STATE_DONE (got 0, want 3)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:133: v.state == PET_STATE_FAILED (got 0, want 4)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:138: v.state == PET_STATE_OFFLINE (got 0, want 5)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:139: v.gray == 1 (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:154: v.state == PET_STATE_FAILED (got 0, want 4)
FAIL ST-01-event-to-state (7/12 asserts failed)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:174: v.state == PET_STATE_WORKING (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:179: v.state == PET_STATE_WORKING (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:184: v.state == PET_STATE_DONE (got 0, want 3)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:185: v.frame == PET_FRAME_HAPPY (got 0, want 2)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:186: pet_state_take_sound(s) == PET_SOUND_DONE (got 0, want 2)
FAIL ST-02-done-edge-debounce (5/9 asserts failed)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:207: snapshot(s).state == PET_STATE_WORKING (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:212: snapshot(s).state == PET_STATE_WORKING (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:216: snapshot(s).state == PET_STATE_WORKING (got 0, want 1)
FAIL ST-03-done-edge-cancelled (3/6 asserts failed)
PASS ST-04-idle-agent-idle (5 asserts)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:253: snapshot(s).state == PET_STATE_DONE (got 0, want 3)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:256: snapshot(s).state == PET_STATE_DONE (got 0, want 3)
FAIL ST-05-done-dwell (2/4 asserts failed)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:273: snapshot(s).state == PET_STATE_WORKING (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:276: snapshot(s).state == PET_STATE_WORKING (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:280: v.state == PET_STATE_OFFLINE (got 0, want 5)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:281: v.gray == 1 (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:286: v.state == PET_STATE_WORKING (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:302: v.state == PET_STATE_OFFLINE (got 0, want 5)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:303: v.gray == 1 (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:307: v.state == PET_STATE_WORKING (got 0, want 1)
FAIL ST-06-link-timeout-recovery (8/12 asserts failed)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:330: v.card_count == 1 (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:331: v.has_card == 1 (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:332: v.cards[0].title == "ALPHA" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:333: v.card.title == "ALPHA" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:338: v.card_count == 2 (got 0, want 2)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:339: v.cards[0].title == "BETA" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:340: v.cards[1].title == "ALPHA" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:345: v.card_count == 3 (got 0, want 3)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:346: v.cards[0].title == "GAMMA" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:347: v.cards[1].title == "BETA" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:348: v.cards[2].title == "ALPHA" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:354: v.card_count == 3 (got 0, want 3)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:355: v.cards[0].title == "DELTA" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:356: v.cards[1].title == "GAMMA" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:357: v.cards[2].title == "BETA" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:373: v.card_count == 2 (got 0, want 2)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:374: v.cards[0].title == "T" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:375: v.cards[0].project == "P" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:376: v.cards[0].agent == "CX" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:377: v.cards[1].title == "B" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:388: v.card_count == 3 (got 0, want 3)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:389: v.cards[0].title == "T" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:390: v.cards[0].project == "P2" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:391: v.cards[1].title == "B" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:392: v.cards[2].title == "T" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:393: v.cards[2].project == "P1" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:400: v.card_count == 1 (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:402: v.cards[0].project == "acme/empty" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:411: v.card_count == 2 (got 0, want 2)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:412: v.cards[0].title == "NEW" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:413: v.cards[1].title == "OLD" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:416: v.card_count == 2 (got 0, want 2)
FAIL ST-07-card-stack (32/38 asserts failed)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:439: v.hair_right == !hair0 (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:443: v.frame == PET_FRAME_EYES_CLOSED (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:452: v.frame == PET_FRAME_EYES_CLOSED (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:465: v.state == PET_STATE_DONE (got 0, want 3)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:466: v.frame == PET_FRAME_HAPPY (got 0, want 2)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:479: v.state == PET_STATE_FAILED (got 0, want 4)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:480: v.frame == PET_FRAME_SAD (got 0, want 3)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:493: v.state == PET_STATE_OFFLINE (got 0, want 5)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:494: v.frame == PET_FRAME_EYES_CLOSED (got 0, want 1)
FAIL ST-08-animation (9/18 asserts failed)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:517: v.state == PET_STATE_WORKING (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:523: v.state == PET_STATE_INPUT_REQUIRED (got 0, want 2)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:529: v.state == PET_STATE_DONE (got 0, want 3)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:535: v.state == PET_STATE_FAILED (got 0, want 4)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:541: v.state == PET_STATE_OFFLINE (got 0, want 5)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:542: v.gray == 1 (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:550: v.card_count == 2 (got 0, want 2)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:551: v.has_card == 1 (got 0, want 1)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:552: v.cards[0].title == "TOP" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:553: v.cards[1].title == "BOTTOM" (got "")
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_pet_state.c:554: v.card.title == "TOP" (got "")
FAIL ST-09-view-fields (11/20 asserts failed)
SUMMARY pet_state: cases=9 passed=1 failed=8
=== EXIT 1 (test_pet_state.exe) ===
=== RUN test_ui240.exe ===
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:257: count_eq(&s, CARD_X0, unused_y, CARD_X0 + CARD_W - 1, 235, C_BG) == (236 - unused_y) * CARD_W (got 0, want 19488)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:271: count_eq(&s, 0, 148, 239, 151, C_BG) == 240 * 4 (got 0, want 960)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:273: count_eq(&s, 0, 236, 239, 239, C_BG) == 240 * 4 (got 0, want 960)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:277: count_eq(&s, 0, 0, 239, 239, C_BG) == 240 * 240 - 16 (got 0, want 57584)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:280: count_eq(&s, 4, 4, 7, 7, C_DOT_ON) == 16 (got 0, want 16)
  card[0] stack: CARD_BG pixels=0 (want >500)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:212: bg > 500
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:213: get_px(s, 230, y + 26) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:214: get_px(s, 50, y + 26) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:215: get_px(s, 8, y + 26) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:216: get_px(s, 230, y + 1) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:218: count_eq(s, CARD_X0, y, CARD_X0 + 1, y + CARD_H - 1, C_ACCENT) == 56 (got 0, want 56)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:257: count_eq(&s, CARD_X0, unused_y, CARD_X0 + CARD_W - 1, 235, C_BG) == (236 - unused_y) * CARD_W (got 0, want 12992)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:263: count_eq(&s, 0, CARD_Y0, 3, CARD_Y0 + n * CARD_ROW - 1, C_BG) == 4 * n * CARD_ROW (got 0, want 112)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:266: count_eq(&s, 236, CARD_Y0, 239, CARD_Y0 + n * CARD_ROW - 1, C_BG) == 4 * n * CARD_ROW (got 0, want 112)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:271: count_eq(&s, 0, 148, 239, 151, C_BG) == 240 * 4 (got 0, want 960)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:273: count_eq(&s, 0, 236, 239, 239, C_BG) == 240 * 4 (got 0, want 960)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:280: count_eq(&s, 4, 4, 7, 7, C_DOT_ON) == 16 (got 0, want 16)
  card[0] stack: CARD_BG pixels=0 (want >500)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:212: bg > 500
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:213: get_px(s, 230, y + 26) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:214: get_px(s, 50, y + 26) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:215: get_px(s, 8, y + 26) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:216: get_px(s, 230, y + 1) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:218: count_eq(s, CARD_X0, y, CARD_X0 + 1, y + CARD_H - 1, C_ACCENT) == 56 (got 0, want 56)
  card[1] stack: CARD_BG pixels=0 (want >500)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:212: bg > 500
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:213: get_px(s, 230, y + 26) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:214: get_px(s, 50, y + 26) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:215: get_px(s, 8, y + 26) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:216: get_px(s, 230, y + 1) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:223: count_eq(s, CARD_X0, y, CARD_X0 + 1, y + CARD_H - 1, C_CARD_BG) == 56 (got 0, want 56)
  card[2] stack: CARD_BG pixels=0 (want >500)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:212: bg > 500
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:213: get_px(s, 230, y + 26) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:214: get_px(s, 50, y + 26) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:215: get_px(s, 8, y + 26) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:216: get_px(s, 230, y + 1) == C_CARD_BG (got 42405, want 6371)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:223: count_eq(s, CARD_X0, y, CARD_X0 + 1, y + CARD_H - 1, C_CARD_BG) == 56 (got 0, want 56)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:263: count_eq(&s, 0, CARD_Y0, 3, CARD_Y0 + n * CARD_ROW - 1, C_BG) == 4 * n * CARD_ROW (got 0, want 336)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:266: count_eq(&s, 236, CARD_Y0, 239, CARD_Y0 + n * CARD_ROW - 1, C_BG) == 4 * n * CARD_ROW (got 0, want 336)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:271: count_eq(&s, 0, 148, 239, 151, C_BG) == 240 * 4 (got 0, want 960)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:273: count_eq(&s, 0, 236, 239, 239, C_BG) == 240 * 4 (got 0, want 960)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:280: count_eq(&s, 4, 4, 7, 7, C_DOT_ON) == 16 (got 0, want 16)
FAIL UI-13-card-stack (58/61 asserts failed)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:307: get_px(&s, 230, 177) == C_CARD_BG (got 42405, want 6371)
FAIL CARD-13-empty-fields (1/5 asserts failed)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:317: has_color(&s, 115, 2, 124, 15, C_MARK_WORK)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:322: has_color(&s, 111, 2, 128, 15, C_MARK_IN)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:339: has_color(&s, 115, 2, 124, 15, C_MARK_WORK)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:341: has_color(&s, 111, 2, 128, 15, C_MARK_IN)
FAIL UI-11-markers (4/11 asserts failed)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:363: count_eq(&s, 0, 148, 239, 151, C_BG) == 240 * 4 (got 0, want 960)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:364: count_eq(&s, 0, 20, 71, 147, C_BG) == 72 * 128 (got 0, want 9216)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:365: count_eq(&s, 168, 20, 239, 147, C_BG) == 72 * 128 (got 0, want 9216)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:371: count_ne(&s, 72, 20, 167, 147, C_BG) == 0 (got 12288, want 0)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:372: count_eq(&s, 0, 148, 239, 151, C_BG) == 240 * 4 (got 0, want 960)
FAIL UI-13-pet-layout (5/7 asserts failed)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:439: count_eq(&s, 4, 4, 7, 7, C_DOT_OFF) == 16 (got 0, want 16)
  non-gray pixel (0,0)=0xA5A5
  non-gray pixel (1,0)=0xA5A5
  non-gray pixel (2,0)=0xA5A5
  non-gray pixel (3,0)=0xA5A5
  non-gray pixel (4,0)=0xA5A5
  non-gray pixel (5,0)=0xA5A5
  non-gray pixel (6,0)=0xA5A5
  non-gray pixel (7,0)=0xA5A5
  non-gray pixels=57584 (dot excluded)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:460: nongray == 0 (got 57584, want 0)
  gray-map mismatch (4,152): src=0xA5A5 got=0xA5A5 want=0xA514
  gray-map mismatch (5,152): src=0xA5A5 got=0xA5A5 want=0xA514
  gray-map mismatch (6,152): src=0xA5A5 got=0xA5A5 want=0xA514
  gray-map mismatch (7,152): src=0xA5A5 got=0xA5A5 want=0xA514
  gray-map mismatch (8,152): src=0xA5A5 got=0xA5A5 want=0xA514
  gray-map mismatch (9,152): src=0xA5A5 got=0xA5A5 want=0xA514
  gray-map mismatch (10,152): src=0xA5A5 got=0xA5A5 want=0xA514
  gray-map mismatch (11,152): src=0xA5A5 got=0xA5A5 want=0xA514
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:501: map_bad == 0 (got 19488, want 0)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:503: nlevels >= 3
FAIL GB-01-offline-gray (4/6 asserts failed)
SUMMARY pet_ui_240: cases=5 passed=0 failed=5
=== EXIT 1 (test_ui240.exe) ===
RESULT: FAIL (2 test binary/binaries failed)
```

RED 判读（实现为桩时的预期失败，不是编译/环境错误；两个测试程序均成功编译并运行）：

| 测试程序 | 用例 | PASS | FAIL |
| --- | --- | --- | --- |
| `test_pet_state.exe` | 9 | 1（ST-04 在桩的默认 IDLE 上恰好通过） | 8 |
| `test_ui240.exe` | 5 | 0 | 5 |
| 合计 | 14 | 1 | 13 |

- 失败原因与桩行为一致：`pet_state_apply` 空实现（状态恒 IDLE、无卡栈/无播报/无动画），
  `pet_ui_scene_240` 空实现（画布仍是测试预填的哨兵 `0xA5A5`，断言失败文本中的 `got 42405`
  即哨兵值，`want 6371` 即 `0x18E3`）。
- `CARD-13-empty-fields` 在桩上只有卡底色断言失败：占位符断言用"非卡底色像素 > 0"判定，
  哨兵画布会误通过；GREEN 时必须结合卡底色（实现真画卡后）才是有效判据。

## 2. 偏差与解释

本次**未发现需要修改契约的新冲突**；以下为测试采用的解释（与 `docs/08-m2-development.md §6`
的 C2/C3 修订一致）：

1. **整帧灰度 vs 红点**：C03 §3.5 规定"整屏灰度，状态点变红"，语义为"转灰之后再画红点"。
   因此 `GB-01` 的"每像素三通道相等"检查**排除** `(4..7,4..7)` 红点区域，红点区域另断言全 `0xF800`。
2. **状态条与标记不重叠**：按 docs §6 C2 解释为"实际内容区不重叠"；测试只断言网络点
   `(4..7,4..7)` 与标记区（`x=115..124` / `x=111..128, y=2..15`）各自颜色，不把状态条整行当纯色断言。
3. **卡行 2 坐标**：按 docs §6 C3 修订（CJK 顶边 `y+12`、ASCII 顶边 `y+16`）；测试的标题断言带为
   `y+12..y+27`、左侧 `x+4..x+180`，与修订一致，也避开右侧时长文字。
4. **灰度触发字段**：契约未明确 `pet_ui_scene_240` 依据 `v->state==OFFLINE` 还是 `v->gray` 转灰；
   测试对 OFFLINE 视图**同时**置 `state=OFFLINE` 与 `gray=true`，两种实现均可通过。
5. **精确边界**：`+1500ms` 转 DONE、`+5000ms` 回 IDLE、`+15000ms` 转 OFFLINE 均按"≥ 阈值"断言
   （`-1ms` 不触发、恰阈值触发）。若实现采用严格大于，GREEN 会暴露，需统一到契约文字。
6. **时间基准**：done 边沿的去抖起点取事件 `now_ms`；失联基准取"最后一次 `apply` 的 `now_ms`"
   （`tick` 不算活动），与 docs §2.2/§5 用例描述一致。
7. **常量来源**：`pet_state.h` 当前未导出 `PET_DONE_EDGE_DEBOUNCE_MS` 等宏；测试用
   `#ifndef` + 契约值（1500/5000/15000）兜底，实现若在头文件导出同名宏则自动采用。
8. `pet_ui_card_240` 未被单独调用，版式断言统一走 `pet_ui_scene_240`（场景内卡区）；
   该函数签名仍由头文件保持可链接。

## 3. GREEN（真实现，全部通过）

Lead 已把 RED 桩替换为真实现：

| 文件 | RED 桩 | GREEN 真实现（首轮） |
| --- | --- | --- |
| `firmware/components/pet_ui/pet_state.c` | 1303 B | 9084 B（mtime 2026-10-31 00:07:30） |
| `firmware/components/pet_ui/pet_ui_240.c` | 509 B | 12235 B（mtime 00:06:48） |

> D-05 镜像修正后 `pet_ui_240.c` 已更新为 12545 B（mtime 00:18:54，
> SHA256 `1F39FE16C6E82FF6327730D20A9950F6D76CCB052941F58F4BABF1869EF678FD`）；
> 下方 §3 输出是 12235 B 首轮记录，D-05 修正后的最终输出见 §3.1。

同一命令重跑（仓库根 `D:\DSH\ESP32-touch`）：

```powershell
pwsh -NoProfile -File tests/run_tests.ps1
```

完整原始输出（`tests/green-run.log` 同文）：

```text
[CC] test_pet_state.c + pet_state.c
[CC] test_ui240.c + pet_ui_240.c + pet_render.c + pet_text.c
=== RUN test_pet_state.exe ===
PASS ST-01-event-to-state (12 asserts)
PASS ST-02-done-edge-debounce (9 asserts)
PASS ST-03-done-edge-cancelled (6 asserts)
PASS ST-04-idle-agent-idle (5 asserts)
PASS ST-05-done-dwell (4 asserts)
PASS ST-06-link-timeout-recovery (12 asserts)
PASS ST-07-card-stack (38 asserts)
PASS ST-08-animation (18 asserts)
PASS ST-09-view-fields (20 asserts)
SUMMARY pet_state: cases=9 passed=9 failed=0
=== EXIT 0 (test_pet_state.exe) ===
=== RUN test_ui240.exe ===
PASS UI-13-card-stack (61 asserts)
PASS CARD-13-empty-fields (5 asserts)
PASS UI-11-markers (11 asserts)
PASS UI-13-pet-layout (7 asserts)
PASS GB-01-offline-gray (6 asserts)
SUMMARY pet_ui_240: cases=5 passed=5 failed=0
=== EXIT 0 (test_ui240.exe) ===
RESULT: PASS (all host tests green)
```

逐用例统计：

| 测试程序 | 用例 | PASS | FAIL | 断言数 |
| --- | --- | --- | --- | --- |
| `test_pet_state.exe` | 9 | 9 | 0 | 124 |
| `test_ui240.exe` | 5 | 5 | 0 | 90 |
| 合计 | 14 | 14 | 0 | 214 |

退出码：`run_tests.ps1` = 0，两个测试程序各 0。

### GREEN 首跑发现并修正的测试自身问题（非实现问题）

首次 GREEN 运行结果为 13/14 通过；唯一失败是 `ST-09-view-fields` 的三行断言：

```text
ASSERT FAIL ...\test_pet_state.c:552: v.cards[0].title == "TOP" (got "BOTTOM")
ASSERT FAIL ...\test_pet_state.c:553: v.cards[1].title == "BOTTOM" (got "TOP")
ASSERT FAIL ...\test_pet_state.c:554: v.card.title == "TOP" (got "BOTTOM")
```

根因：测试先 `set_card(TOP)` 再 `set_card(BOTTOM)`，按 C03 §3.1 `cards[0]` 是**最新**，
正确期望应为 `cards[0]=BOTTOM`、`cards[1]=TOP`；原期望写反，是实现正确、测试错误。
处理：只修正这三行的期望值（未删除/放宽任何断言，`ST-07` 对同一栈语义的断言本就正确），
重跑后 14/14 全绿。RED 阶段这三行同样为失败态，覆盖范围未削弱。

green 原始日志保留为 `tests/green-run.log`；RED 原始日志保留为 `tests/red-run.log`。

### 3.1 D-05 回归（标记点阵镜像，2026-10-31 真机缺陷）

**缺陷**：上游 135×240 的头顶标记点阵表按 MSB=最左书写，而绘制函数按 bit0=最左取列，
导致 WORKING `>` 被画成 `<`、INPUT_REQUIRED `?` 的弯钩左右反向。Lead 已在
`pet_ui_240.c` 把 `GLYPH_GT`/`GLYPH_QUEST` 镜像修正（`!` 垂直对称不受影响）。

**回归用例**：`tests/test_ui240.c` 新增 `UI-11b-d05-marker-orientation`（17 条断言），
锁定修正后朝向。实测几何（已修正实现，7 行 × 2×2 块）：

| 标记 | 行（y） | 修正后（GREEN） | 未修正 MSB 表（RED） |
| --- | --- | --- | --- |
| `>` | 2..3（顶行） | x=115..116（最左块） | x=123..124（最右块） |
| `>` | 8..9（第 4 行，尖角） | x=121..122 | x=117..118 |
| `>` | 14..15（底行） | x=115..116 | x=123..124 |
| `?` | 2..3（顶横条） | x=121..126 | 同（0x0E 对称） |
| `?` | 4..5 | x=119..120 + x=127..128 | 同（0x11 对称） |
| `?` | 6..7（右侧竖笔） | x=127..128 | x=119..120 |
| `?` | 8..9（弯钩左下折） | x=125..126 | x=121..122 |
| `?` | 14..15（底部点） | x=123..124 | 同 |

> **`>` 坐标勘误（Lead 2026-10-31 确认）**：Lead 上一条消息中"顶行最右块
> x=123..124、第 4 行最左块 x=115..116"的坐标描述为**口误**（尖角方向说反）。
> 正确语义：`>` 尖角朝右 → 顶/底行在 x=115..116、第 4 行尖角在 x=121..122；
> 未镜像 MSB 表才是 `<`（顶/底在右 x=123..124、尖角在左 x=117..118）。
> **以本用例断言为准，断言不改。**

**RED 复现方式**（不修改 `firmware/`，遵守 Lead 写作用域）：`tests/run_d05_red.ps1`
读取**当前** `pet_ui_240.c`，仅把两张表还原为上游 MSB 版本生成临时副本
`tests/build/d05-red/pet_ui_240_msb.c`（不进入固件构建），用同一 `test_ui240.c` 链接该
副本运行。效果等同"临时改回 MSB 表跑一次再改回"，但不写 `firmware/`；
当前 `pet_ui_240.c` SHA256 保持 `1F39FE16…678FD` 未被改动。

命令（仓库根）：

```powershell
pwsh -NoProfile -File tests/run_d05_red.ps1
```

RED 原始输出（`tests/d05-red-run.log` 同文；脚本退出码 0 = 成功复现 RED，测试程序 exit 1）：

```text
[CC] D-05 RED: test_ui240.c + (MSB 表副本) pet_ui_240_msb.c + pet_render.c + pet_text.c
PASS UI-13-card-stack (61 asserts)
PASS CARD-13-empty-fields (5 asserts)
PASS UI-11-markers (11 asserts)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:359: count_eq(&s, 115, 2, 116, 3, C_MARK_WORK) == 4 (got 0, want 4)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:360: count_eq(&s, 117, 2, 124, 3, C_MARK_WORK) == 0 (got 4, want 0)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:362: count_eq(&s, 121, 8, 122, 9, C_MARK_WORK) == 4 (got 0, want 4)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:363: count_eq(&s, 115, 8, 120, 9, C_MARK_WORK) == 0 (got 4, want 0)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:366: count_eq(&s, 115, 14, 116, 15, C_MARK_WORK) == 4 (got 0, want 4)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:367: count_eq(&s, 117, 14, 124, 15, C_MARK_WORK) == 0 (got 4, want 0)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:379: count_eq(&s, 127, 6, 128, 7, C_MARK_IN) == 4 (got 0, want 4)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:380: count_eq(&s, 119, 6, 120, 7, C_MARK_IN) == 0 (got 4, want 0)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:382: count_eq(&s, 125, 8, 126, 9, C_MARK_IN) == 4 (got 0, want 4)
  ASSERT FAIL D:\DSH\ESP32-touch\tests\test_ui240.c:383: count_eq(&s, 121, 8, 122, 9, C_MARK_IN) == 0 (got 4, want 0)
FAIL UI-11b-d05-marker-orientation (10/17 asserts failed)
PASS UI-13-pet-layout (7 asserts)
PASS GB-01-offline-gray (6 asserts)
SUMMARY pet_ui_240: cases=6 passed=5 failed=1
RESULT: D-05 RED reproduced as expected (exit 1; UI-11b FAIL, UI-11-markers PASS)
```

RED 判读：未镜像表下 10 条方向断言失败（`>` 6 条 + `?` 4 条），而只查"颜色存在"的
`UI-11-markers` 仍通过——说明新断言精确锁定了镜像缺陷，而非颜色/位置存在性问题。

**GREEN**：恢复 Lead 的镜像实现后运行 `pwsh -NoProfile -File tests/run_tests.ps1`
（`tests/green-run.log` 同文）：

```text
[CC] test_pet_state.c + pet_state.c
[CC] test_ui240.c + pet_ui_240.c + pet_render.c + pet_text.c
=== RUN test_pet_state.exe ===
PASS ST-01-event-to-state (12 asserts)
PASS ST-02-done-edge-debounce (9 asserts)
PASS ST-03-done-edge-cancelled (6 asserts)
PASS ST-04-idle-agent-idle (5 asserts)
PASS ST-05-done-dwell (4 asserts)
PASS ST-06-link-timeout-recovery (12 asserts)
PASS ST-07-card-stack (38 asserts)
PASS ST-08-animation (18 asserts)
PASS ST-09-view-fields (20 asserts)
SUMMARY pet_state: cases=9 passed=9 failed=0
=== EXIT 0 (test_pet_state.exe) ===
=== RUN test_ui240.exe ===
PASS UI-13-card-stack (61 asserts)
PASS CARD-13-empty-fields (5 asserts)
PASS UI-11-markers (11 asserts)
PASS UI-11b-d05-marker-orientation (17 asserts)
PASS UI-13-pet-layout (7 asserts)
PASS GB-01-offline-gray (6 asserts)
SUMMARY pet_ui_240: cases=6 passed=6 failed=0
=== EXIT 0 (test_ui240.exe) ===
RESULT: PASS (all host tests green)
```

最终逐用例统计（含 D-05 回归）：

| 测试程序 | 用例 | PASS | FAIL | 断言数 |
| --- | --- | --- | --- | --- |
| `test_pet_state.exe` | 9 | 9 | 0 | 124 |
| `test_ui240.exe` | 6 | 6 | 0 | 107 |
| 合计 | 15 | 15 | 0 | 231 |

## 4. 结论

- T 类 host 单测 RED→GREEN 完成：桩上 13/14 失败（编译与运行正常，属断言失败），
  真实现上 14/14 通过、214 条断言全过、运行器退出码 0。
- D-05 回归已补：新增 `UI-11b-d05-marker-orientation`（17 条方向断言）；未镜像 MSB 表上
  10 条失败（RED 可复现），镜像修正后全绿。最终 15/15 用例、231 条断言通过、退出码 0。
- 无契约修改；§2 所列解释均为对既有 C03/docs §3/§6 的落读，未发现新的接口矛盾。
- 未运行 IDF/未刷机；仅编译 `tests/` + 真实 `firmware/components/pet_ui` 源码；
  D-05 RED 使用 `tests/build/` 下的临时副本，未改动 `firmware/`。

