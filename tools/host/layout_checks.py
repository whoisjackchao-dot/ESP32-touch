#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""layout_checks.py - 240x240 版式像素断言（C03 契约 + docs/08-m2-development.md §3）。

对 `d` 导出的 115200 字节 RGB565-LE 帧做逐区域断言，输出逐项 PASS/FAIL
与坐标证据。断言项（与 docs/08 §3 落地取值一致）：

  frame_size              115200 字节
  network_dot             (4,4,4,4) 在线 0x07E0 / OFFLINE 0xF800
  marker_working          WORKING 态 (115,2,10,14) 有 0xFFFF（`>` 字形）
  marker_input            INPUT_REQUIRED 态 (111,2,18,14) 有 0xFD20（`!?`）
  marker_absent           其余态标记区无 0xFFFF / 0xFD20
  offline_gray            OFFLINE 整帧灰度（网络点除外）：r5==(g6>>1) 且 b5==(g6>>1)
  card_empty[i]           cards=0 时 3 个卡矩形全黑 0x0000
  card_base[i]            卡矩形顶 2px 带为卡底 0x18E3（文本不进入该带）
  card_accent[i]          最新卡左侧 (x,y,2,28) 全 0x05FF；其余卡该 2px 无强调色
  card_row1_content[i]    卡行 1 区有非卡底像素（空字段占位符不得空白）
  card_row2_content[i]    卡行 2 区有非卡底像素（空 title → "(untitled)"）
  bg.*                    状态条留空区/宠物四周/卡区左右边距/页脚均为 0x0000
  pet_drawn               宠物矩形 (72,20,96,128) 有像素

自测（不需要设备）：

    python layout_checks.py --selftest [--png-dir evidence/selftest]

CLI（对真帧）：

    python layout_checks.py frame.bin --state WORKING [--cards 0] [--json]

退出码：0 全部 PASS；1 存在 FAIL；2 输入/参数错误。
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

WIDTH = 240
HEIGHT = 240
FRAME_BYTES = WIDTH * HEIGHT * 2  # 115200

BG = 0x0000
CARD_RECTS = ((4, 152, 232, 28), (4, 180, 232, 28), (4, 208, 232, 28))
CARD_BG = 0x18E3
ACCENT = 0x05FF
ACCENT_W = 2

NET_RECT = (4, 4, 4, 4)
DOT_ON = 0x07E0
DOT_OFF = 0xF800

MARK_WORKING = (115, 2, 10, 14)   # `>`: 5 列 ×2
MARK_INPUT = (111, 2, 18, 14)     # `!?`: 3 + 1 列空 + 5 列，×2
MARK_UNION = (111, 2, 18, 14)
MARK_COL_WORKING = 0xFFFF
MARK_COL_INPUT = 0xFD20

FOOTER_RECT = (0, 236, 240, 4)
PET_RECT = (72, 20, 96, 128)

STATE_NAMES = ("IDLE", "WORKING", "INPUT_REQUIRED", "DONE", "FAILED", "OFFLINE")

# 状态条内“必须留黑”的区段（C03 §7 R1：内容只允许在左 x=0..7 与右 x=196..239，
# 头顶标记 x=111..128 只占 y=2..15，故这些区段与任何内容都不重叠）。
BAR_BLANK_RECTS = (
    ("bg.bar_left_margin", (0, 0, 4, 12)),
    ("bg.bar_content_gap", (8, 0, 103, 12)),
    ("bg.bar_mid_blank", (129, 0, 67, 12)),
    ("bg.bar_right_blank", (196, 0, 44, 12)),
)
SCENE_BLANK_RECTS = (
    ("bg.above_pet", (0, 16, 240, 4)),
    ("bg.pet_left", (0, 20, 72, 128)),
    ("bg.pet_right", (168, 20, 72, 128)),
    ("bg.below_pet", (0, 148, 240, 4)),
    ("bg.card_left_margin", (0, 152, 4, 84)),
    ("bg.card_right_margin", (236, 152, 4, 84)),
    ("bg.footer", FOOTER_RECT),
)

# 合成帧所用的头顶字形（与 firmware pet_render.c 同表；bit0 = 最左列）
GLYPH_GT = (0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10)
GLYPH_BANG = (0x02, 0x02, 0x02, 0x02, 0x02, 0x00, 0x02)
GLYPH_QUEST = (0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04)

PRESET_PROJECT = {
    "ALPHA": "acme/api",
    "BETA": "acme/web",
    "GAMMA": "openai/chat",
    "DELTA": "",
    "THIS-IS-A-VERY-LONG-TITLE-0123456789": "acme/long",
    "中文标题测试": "acme/cjk",
    "": "acme/empty",
}


# --------------------------------------------------------------------------
# pixel helpers
# --------------------------------------------------------------------------

def get_px(frame, x, y):
    off = (y * WIDTH + x) * 2
    return frame[off] | (frame[off + 1] << 8)


def set_px(frame, x, y, color):
    off = (y * WIDTH + x) * 2
    frame[off] = color & 0xFF
    frame[off + 1] = (color >> 8) & 0xFF


def fill_rect(frame, x, y, w, h, color):
    for yy in range(y, y + h):
        if yy < 0 or yy >= HEIGHT:
            continue
        for xx in range(x, x + w):
            if 0 <= xx < WIDTH:
                set_px(frame, xx, yy, color)


def iter_rect(frame, rect):
    x, y, w, h = rect
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            yield xx, yy, get_px(frame, xx, yy)


def count_color(frame, rect, color):
    return sum(1 for _, _, v in iter_rect(frame, rect) if v == color)


def first_mismatch(frame, rect, color):
    for xx, yy, v in iter_rect(frame, rect):
        if v != color:
            return xx, yy, v
    return None


def color_bbox(frame, rect, color):
    x0 = y0 = 10 ** 9
    x1 = y1 = -1
    for xx, yy, v in iter_rect(frame, rect):
        if v == color:
            x0 = min(x0, xx)
            y0 = min(y0, yy)
            x1 = max(x1, xx)
            y1 = max(y1, yy)
    if x1 < 0:
        return None
    return (x0, y0, x1 - x0 + 1, y1 - y0 + 1)


def gray565(color):
    """与 firmware pet_render.c 的 gray565 同法（含亮度权重与取整）。"""
    r8 = ((color >> 11) & 0x1F) * 255 // 31
    g8 = ((color >> 5) & 0x3F) * 255 // 63
    b8 = (color & 0x1F) * 255 // 31
    level = (r8 * 77 + g8 * 150 + b8 * 29) >> 8
    g6 = (level * 63 + 127) // 255
    r5 = g6 >> 1
    b5 = g6 >> 1
    return (r5 << 11) | (g6 << 5) | b5


# --------------------------------------------------------------------------
# report model
# --------------------------------------------------------------------------

@dataclass
class Check:
    name: str
    ok: bool
    detail: str
    evidence: list = field(default_factory=list)

    def to_dict(self):
        return {"name": self.name, "ok": self.ok,
                "detail": self.detail, "evidence": list(self.evidence)}


@dataclass
class Report:
    state: str
    card_count: int
    checks: list = field(default_factory=list)

    def add(self, name, ok, detail, evidence=None):
        self.checks.append(Check(name, bool(ok), detail, list(evidence or [])))

    @property
    def ok(self):
        return all(c.ok for c in self.checks)

    @property
    def failed(self):
        return [c.name for c in self.checks if not c.ok]

    @property
    def passed(self):
        return [c.name for c in self.checks if c.ok]

    def to_dict(self):
        return {
            "state": self.state,
            "card_count": self.card_count,
            "ok": self.ok,
            "checks": [c.to_dict() for c in self.checks],
        }

    def lines(self, show_evidence=True):
        out = []
        for c in self.checks:
            out.append("%s %-24s %s" % ("PASS" if c.ok else "FAIL",
                                        c.name, c.detail))
            if show_evidence and c.evidence:
                for ev in c.evidence:
                    out.append("       @ " + ev)
        out.append("RESULT %s (%d/%d checks passed, state=%s, cards=%d)"
                   % ("PASS" if self.ok else "FAIL",
                      len(self.passed), len(self.checks),
                      self.state, self.card_count))
        return out

    def text(self, show_evidence=True):
        return "\n".join(self.lines(show_evidence))


# --------------------------------------------------------------------------
# inference
# --------------------------------------------------------------------------

def infer_card_count(frame):
    """从“最新卡左侧 2px 强调条”推断栈深（0..3）。"""
    n = 0
    for i, rect in enumerate(CARD_RECTS):
        x, y, _, h = rect
        probe = get_px(frame, x, y + h // 2)
        if probe == ACCENT:
            n = i + 1
        else:
            break
    return n


# --------------------------------------------------------------------------
# the actual checks
# --------------------------------------------------------------------------

def _expected_canvas(state, color):
    return gray565(color) if state == "OFFLINE" else color


def check_frame(frame, state, card_count=None, titles=None):
    """Run every layout assertion; returns a Report (never raises for content)."""
    state = str(state).strip().upper()
    if state not in STATE_NAMES:
        raise ValueError("unknown state %r (expected one of %s)"
                         % (state, ", ".join(STATE_NAMES)))
    if card_count is None:
        card_count = infer_card_count(frame) if len(frame) == FRAME_BYTES else 0
    card_count = max(0, min(3, int(card_count)))
    rep = Report(state, card_count)

    if len(frame) != FRAME_BYTES:
        rep.add("frame_size", False,
                "%d bytes, expected %d" % (len(frame), FRAME_BYTES))
        return rep
    rep.add("frame_size", True, "%d bytes = 240x240 RGB565LE" % len(frame))

    card_bg = _expected_canvas(state, CARD_BG)
    accent = _expected_canvas(state, ACCENT)
    offline = state == "OFFLINE"

    # ---- 网络点 ---------------------------------------------------------
    expect_dot = DOT_OFF if offline else DOT_ON
    dot_hits = count_color(frame, NET_RECT, expect_dot)
    dot_px = list(iter_rect(frame, NET_RECT))
    detail = "(%d,%d,4,4) == 0x%04X: %d/16 px" % (
        NET_RECT[0], NET_RECT[1], expect_dot, dot_hits)
    if dot_hits != 16:
        bad = [(x, y, v) for x, y, v in dot_px if v != expect_dot][:4]
        rep.add("network_dot", False, detail,
                ["%s=0x%04X" % ((x, y), v) for x, y, v in bad])
    else:
        rep.add("network_dot", True, detail)

    # ---- 头顶标记 -------------------------------------------------------
    # 契约（docs/08 §3）：2×、7 行、顶边 y=2、横向居中于 x=120；
    # `>` 单元宽 10（x=115..124），`!?` 单元宽 18（x=111..128）。
    # 字形墨迹在单元内左右内缩，故断言“存在 + 落在单元内 + 纵向恰好 14 px（7 行×2）
    # + 墨迹水平中心 ≈ x=120”，而不是要求墨迹铺满整个单元。
    union_white = count_color(frame, MARK_UNION, MARK_COL_WORKING)
    union_orange = count_color(frame, MARK_UNION, MARK_COL_INPUT)

    def _marker_report(name, region, color, hits, other_hits, other_color):
        box = color_bbox(frame, region, color)
        ok = hits > 0 and other_hits == 0 and box is not None
        ev = ["region=%s expected_color=0x%04X" % (region, color)]
        if box is not None:
            x0, y0, bw, bh = box
            cx = x0 + (bw - 1) / 2.0
            contained = (region[0] <= x0 and x0 + bw <= region[0] + region[2]
                         and region[1] <= y0 and y0 + bh <= region[1] + region[3])
            vertical = (y0 == 2 and bh == 14)
            centered = 118.5 <= cx <= 121.5
            ok = ok and contained and vertical and centered
            ev.append("ink_bbox=(%d,%d,w=%d,h=%d) center_x=%.1f" % (
                x0, y0, bw, bh, cx))
            ev.append("contained=%s vertical_span_y2..15=%s centered_120=%s"
                      % (contained, vertical, centered))
        else:
            ev.append("ink_bbox=None")
        rep.add(name, ok,
                "%s: 0x%04X px=%d bbox=%s other=0x%04X px=%d"
                % (name, color, hits, box, other_color, other_hits), ev)

    if state == "WORKING":
        _marker_report("marker_working", MARK_WORKING, MARK_COL_WORKING,
                       union_white, union_orange, MARK_COL_INPUT)
    elif state == "INPUT_REQUIRED":
        _marker_report("marker_input", MARK_INPUT, MARK_COL_INPUT,
                       union_orange, union_white, MARK_COL_WORKING)
    else:
        ok = union_white == 0 and union_orange == 0
        rep.add("marker_absent", ok,
                "marker region %s has no 0xFFFF/0xFD20: white=%d orange=%d"
                % (MARK_UNION, union_white, union_orange))

    # ---- OFFLINE 整帧灰度（网络点除外，红点按设计非灰）------------------
    if offline:
        bad = []
        for y in range(HEIGHT):
            for x in range(WIDTH):
                if 4 <= x < 8 and 4 <= y < 8:
                    continue
                v = get_px(frame, x, y)
                r5 = (v >> 11) & 0x1F
                g6 = (v >> 5) & 0x3F
                b5 = v & 0x1F
                if r5 != (g6 >> 1) or b5 != (g6 >> 1):
                    bad.append((x, y, v))
                    if len(bad) >= 5:
                        break
            if len(bad) >= 5:
                break
        rep.add("offline_gray", not bad,
                "all %d px outside network dot satisfy r5==g6>>1 and b5==g6>>1"
                % (WIDTH * HEIGHT - 16) if not bad else
                "%d non-gray px found (first %d shown)" % (len(bad), 5),
                ["(%d,%d)=0x%04X" % b for b in bad])

    # ---- 卡片区 ---------------------------------------------------------
    for i, rect in enumerate(CARD_RECTS):
        x, y, w, h = rect
        if i >= card_count:
            hits = count_color(frame, rect, BG)
            area = w * h
            ok = hits == area
            ev = []
            if not ok:
                mm = first_mismatch(frame, rect, BG)
                ev = ["first_non_bg=(%d,%d)=0x%04X" % mm] if mm else []
            rep.add("card_empty[%d]" % i, ok,
                    "%s must be all 0x0000: %d/%d px" % (rect, hits, area), ev)
            continue

        # 卡底：文本从 y+3 起，故 y+1..y+2 的 2px 带必须是卡底色（含 OFFLINE 灰化值）
        band = (x + ACCENT_W, y + 1, w - ACCENT_W, 2)
        hits = count_color(frame, band, card_bg)
        area = band[2] * band[3]
        ok = hits == area
        ev = []
        if not ok:
            mm = first_mismatch(frame, band, card_bg)
            ev = ["first_non_bg=(%d,%d)=0x%04X (expected 0x%04X)"
                  % (mm + (card_bg,))] if mm else []
        rep.add("card_base[%d]" % i, ok,
                "%s top 2px band 0x%04X: %d/%d px" % (rect, card_bg, hits, area),
                ev)

        # 强调条（最新卡 x..x+1 全 0x05FF；其它卡该 2px 必须是卡底色）
        accent_rect = (x, y, ACCENT_W, h)
        if i == 0:
            hits = count_color(frame, accent_rect, accent)
            area = ACCENT_W * h
            ok = hits == area
            ev = []
            if not ok:
                mm = first_mismatch(frame, accent_rect, accent)
                ev = ["first_non_accent=(%d,%d)=0x%04X (expected 0x%04X)"
                      % (mm + (accent,))] if mm else []
            rep.add("card_accent[0]", ok,
                    "newest %s == 0x%04X: %d/%d px"
                    % (accent_rect, accent, hits, area), ev)
        else:
            hits = count_color(frame, accent_rect, accent)
            rep.add("card_accent[%d]" % i, hits == 0,
                    "non-newest %s must have no 0x%04X: %d px"
                    % (accent_rect, accent, hits))

        # 行 1 / 行 2 非空白（空字段必须渲染占位符，不得整卡空白）
        row1 = (x + 4, y + 3, 224, 8)
        row2 = (x + 4, y + 12, 180, 16)
        for label, rrect in (("row1", row1), ("row2", row2)):
            content = [(xx, yy, v) for xx, yy, v in iter_rect(frame, rrect)
                       if v != card_bg]
            ok = len(content) > 0
            ev = []
            if content:
                xs = [c[0] for c in content]
                ys = [c[1] for c in content]
                ev = ["content_px=%d bbox=x %d..%d y %d..%d"
                      % (len(content), min(xs), max(xs), min(ys), max(ys))]
            rep.add("card_%s_content[%d]" % (label, i), ok,
                    "%s %s has non-card-bg pixels: %d"
                    % (label, rrect, len(content)), ev)

    # 行 2 不越界（标题最大宽度 180 → 右侧 x+184..x+228 只允许时长色 0x8410 或卡底）
    if card_count > 0:
        x, y = CARD_RECTS[0][0], CARD_RECTS[0][1]
        overflow = (x + 184, y + 12, 44, 16)  # x+184..x+227
        n_white = count_color(frame, overflow, 0xFFFF)
        rep.add("card_title_no_overflow", n_white == 0,
                "top card %s has no white title px beyond x+183: %d"
                % (overflow, n_white),
                ["overflow_rect=%s" % (overflow,)])

    # ---- 宠物存在 -------------------------------------------------------
    pet_px = sum(1 for _, _, v in iter_rect(frame, PET_RECT) if v != BG)
    rep.add("pet_drawn", pet_px > 0,
            "%s has %d non-bg px" % (PET_RECT, pet_px),
            ["pet_rect=%s" % (PET_RECT,)])

    # ---- 状态条留空 / 场景四周 / 页脚 ----------------------------------
    for name, rect in BAR_BLANK_RECTS + SCENE_BLANK_RECTS:
        hits = count_color(frame, rect, BG)
        area = rect[2] * rect[3]
        ok = hits == area
        ev = []
        if not ok:
            mm = first_mismatch(frame, rect, BG)
            ev = ["first_non_bg=(%d,%d)=0x%04X" % mm] if mm else []
        rep.add(name, ok, "%s all 0x0000: %d/%d px" % (rect, hits, area), ev)

    return rep


# --------------------------------------------------------------------------
# synthetic frame builder (self-test + simulator)
# --------------------------------------------------------------------------

def _text_width(text):
    return sum(16 if ord(ch) > 0x7F else 8 for ch in text)


def _has_cjk(text):
    return any(ord(ch) > 0x7F for ch in text)


def draw_glyph(frame, x, y, rows, width, color, scale=2):
    for r, bits in enumerate(rows):
        for c in range(width):
            if bits & (1 << c):
                fill_rect(frame, x + c * scale, y + r * scale, scale, scale, color)


def draw_card_synth(frame, rect, title, project, newest):
    x, y, w, h = rect
    fill_rect(frame, x, y, w, h, CARD_BG)
    if newest:
        fill_rect(frame, x, y, ACCENT_W, h, ACCENT)
    # 行 1：[前缀] 项目（用块模拟文字；颜色 0x8410）
    text1 = project if project else "-"
    w1 = min(224, max(8, _text_width(text1)))
    fill_rect(frame, x + 4, y + 4, w1, 4, 0x8410)
    # 行 2：标题（空 → "(untitled)"；颜色 0xFFFF；CJK 16px 顶边 y+12，ASCII y+16）
    text2 = title if title else "(untitled)"
    w2 = min(180, max(8, _text_width(text2)))
    if _has_cjk(text2):
        fill_rect(frame, x + 4, y + 14, w2, 12, 0xFFFF)
    else:
        fill_rect(frame, x + 4, y + 16, w2, 6, 0xFFFF)
    # 时长：右对齐右边界 x+228，颜色 0x8410
    wd = 24
    fill_rect(frame, x + 228 - wd + 1, y + 16, wd, 6, 0x8410)


def synth_frame(state, cards=None, online=None):
    """按 docs/08 §3 构造一张合成帧（用于无设备自测/模拟）。

    ``cards``: top-first 的 ``[(title, project), ...]``（0..3 张）。
    ``online``: 默认 = state != "OFFLINE"；显式 False 时强制灰化。
    """
    state = str(state).strip().upper()
    frame = bytearray(FRAME_BYTES)
    # 宠物 96x128 @ (72,20)：几块调色板色，模拟立绘；按状态做可见姿态差异
    fill_rect(frame, 72, 20, 96, 128, 0x8410)
    fill_rect(frame, 84, 28, 72, 72, 0xC618)
    fill_rect(frame, 96, 48, 8, 8, 0xFFFF)
    fill_rect(frame, 120, 48, 8, 8, 0xFFFF)
    fill_rect(frame, 104, 68, 32, 4, 0x8000)
    fill_rect(frame, 76, 132, 88, 12, 0x39E7)
    if state == "WORKING":          # 专注：头部色调变化
        fill_rect(frame, 84, 28, 72, 72, 0x9CF3)
    elif state == "INPUT_REQUIRED":  # 睁大眼
        fill_rect(frame, 92, 44, 16, 16, 0xFFFF)
        fill_rect(frame, 116, 44, 16, 16, 0xFFFF)
    elif state == "DONE":            # 开心：绿嘴 + 红脸颊
        fill_rect(frame, 100, 70, 40, 6, 0x07E0)
        fill_rect(frame, 84, 60, 8, 6, 0xF800)
        fill_rect(frame, 148, 60, 8, 6, 0xF800)
    elif state == "FAILED":          # 闭眼 + 蓝嘴
        fill_rect(frame, 96, 48, 8, 8, 0x39E7)
        fill_rect(frame, 120, 48, 8, 8, 0x39E7)
        fill_rect(frame, 104, 74, 32, 4, 0x001F)
    # 头顶标记
    if state == "WORKING":
        draw_glyph(frame, MARK_WORKING[0], MARK_WORKING[1], GLYPH_GT, 5,
                   MARK_COL_WORKING)
    elif state == "INPUT_REQUIRED":
        draw_glyph(frame, MARK_INPUT[0], MARK_INPUT[1], GLYPH_BANG, 3,
                   MARK_COL_INPUT)
        draw_glyph(frame, MARK_INPUT[0] + 8, MARK_INPUT[1], GLYPH_QUEST, 5,
                   MARK_COL_INPUT)
    # 卡片
    if cards:
        for i, item in enumerate(list(cards)[:3]):
            if isinstance(item, str):
                title, project = item, PRESET_PROJECT.get(item, "")
            else:
                title, project = item
            draw_card_synth(frame, CARD_RECTS[i], title, project, i == 0)
    # 网络点
    off = (state == "OFFLINE") if online is None else (not online)
    if off:
        for y in range(HEIGHT):
            for x in range(WIDTH):
                v = get_px(frame, x, y)
                set_px(frame, x, y, gray565(v))
        fill_rect(frame, NET_RECT[0], NET_RECT[1], NET_RECT[2], NET_RECT[3],
                  DOT_OFF)
    else:
        fill_rect(frame, NET_RECT[0], NET_RECT[1], NET_RECT[2], NET_RECT[3],
                  DOT_ON)
    return bytes(frame)


# --------------------------------------------------------------------------
# self-test: positive frames per state + deliberately broken frames
# --------------------------------------------------------------------------

def _selftest(png_dir=None):
    results = []

    def run_case(name, frame, state, cards, expect_ok, expect_fail_substr=None,
                 titles=None):
        rep = check_frame(frame, state, cards, titles)
        if expect_ok:
            ok = rep.ok
            matched = "all %d checks passed" % len(rep.checks) if rep.ok else \
                "failed: " + ", ".join(rep.failed)
        else:
            hit = [n for n in rep.failed if expect_fail_substr in n]
            ok = (not rep.ok) and bool(hit)
            matched = (", ".join(rep.failed) if rep.failed else "no failure") + \
                (" [matched %s]" % hit[0] if hit else " [NOT MATCHED]")
        results.append({"case": name, "state": state, "cards": cards,
                        "expect": "PASS" if expect_ok else "FAIL",
                        "actual": "PASS" if rep.ok else "FAIL",
                        "match": ok, "checks": len(rep.checks),
                        "failed": rep.failed, "report": rep.to_dict()})
        print("%-34s expect=%-4s actual=%-4s %s | %s"
              % (name, "PASS" if expect_ok else "FAIL",
                 "PASS" if rep.ok else "FAIL",
                 "OK" if ok else "MISMATCH", matched))
        return rep, ok

    # ---------- 正例：6 态无卡 ----------
    positives = {}
    for state in STATE_NAMES:
        frame = synth_frame(state)
        positives[state] = frame
        run_case("positive_%s" % state, frame, state, 0, True)

    # ---------- 正例：3 卡（最新/中间/最旧） ----------
    cards3 = [("ALPHA", "acme/api"), ("BETA", "acme/web"),
              ("GAMMA", "openai/chat")]
    frame_cards = synth_frame("IDLE", cards3)
    positives["CARDS3"] = frame_cards
    run_case("positive_cards3", frame_cards, "IDLE", 3, True,
             titles=[c[0] for c in cards3])

    # ---------- 反例：每例只破坏一项 ----------
    def broken(base, mutator):
        f = bytearray(base)
        mutator(f)
        return bytes(f)

    frame_working = positives["WORKING"]
    frame_idle = positives["IDLE"]
    frame_offline = positives["OFFLINE"]

    run_case("neg_marker_missing_WORKING", broken(frame_working, lambda f: fill_rect(
        f, MARK_UNION[0], MARK_UNION[1], MARK_UNION[2], MARK_UNION[3], BG)),
        "WORKING", 0, False, "marker")
    run_case("neg_marker_on_IDLE", broken(frame_idle, lambda f: draw_glyph(
        f, MARK_WORKING[0], MARK_WORKING[1], GLYPH_GT, 5, MARK_COL_WORKING)),
        "IDLE", 0, False, "marker")
    run_case("neg_marker_wrong_color", broken(frame_working, lambda f: (
        fill_rect(f, MARK_UNION[0], MARK_UNION[1], MARK_UNION[2],
                  MARK_UNION[3], BG),
        draw_glyph(f, MARK_WORKING[0], MARK_WORKING[1], GLYPH_GT, 5,
                   MARK_COL_INPUT))), "WORKING", 0, False, "marker")
    run_case("neg_card_base", broken(frame_cards, lambda f: fill_rect(
        f, CARD_RECTS[0][0] + 3, CARD_RECTS[0][1] + 1, 12, 2, BG)),
        "IDLE", 3, False, "card_base")
    run_case("neg_accent_missing", broken(frame_cards, lambda f: fill_rect(
        f, CARD_RECTS[0][0], CARD_RECTS[0][1], ACCENT_W, CARD_RECTS[0][3],
        CARD_BG)), "IDLE", 3, False, "card_accent")
    run_case("neg_accent_wrong_color", broken(frame_cards, lambda f: fill_rect(
        f, CARD_RECTS[0][0], CARD_RECTS[0][1], ACCENT_W, CARD_RECTS[0][3],
        0xF800)), "IDLE", 3, False, "card_accent")
    run_case("neg_dot_red_online", broken(frame_idle, lambda f: fill_rect(
        f, NET_RECT[0], NET_RECT[1], NET_RECT[2], NET_RECT[3], DOT_OFF)),
        "IDLE", 0, False, "network_dot")
    run_case("neg_dot_green_offline", broken(frame_offline, lambda f: fill_rect(
        f, NET_RECT[0], NET_RECT[1], NET_RECT[2], NET_RECT[3], DOT_ON)),
        "OFFLINE", 0, False, "network_dot")
    run_case("neg_offline_not_gray", broken(frame_offline, lambda f: set_px(
        f, 100, 100, 0xF800)), "OFFLINE", 0, False, "offline_gray")
    run_case("neg_footer_dirty", broken(frame_idle, lambda f: set_px(
        f, 10, 237, 0xFFFF)), "IDLE", 0, False, "bg.footer")
    run_case("neg_status_bar_dirty", broken(frame_idle, lambda f: set_px(
        f, 50, 5, 0xFFFF)), "IDLE", 0, False, "bg.bar_content_gap")
    run_case("neg_card_empty_drawn", broken(frame_idle, lambda f: set_px(
        f, CARD_RECTS[0][0], CARD_RECTS[0][1] + 10, ACCENT)), "IDLE", 0,
        False, "card_empty")
    run_case("neg_title_blank", broken(frame_cards, lambda f: fill_rect(
        f, CARD_RECTS[0][0] + 4, CARD_RECTS[0][1] + 12, 180, 16, CARD_BG)),
        "IDLE", 3, False, "card_row2_content")
    run_case("neg_truncated", synth_frame("IDLE")[:1000], "IDLE", 0, False,
             "frame_size")

    # 可选：把正例合成帧写成 PNG，便于人眼复核断言依据
    if png_dir:
        try:
            import fb  # noqa: F401  (同目录标准库实现)
        except ImportError:
            sys.path.insert(0, str(Path(__file__).resolve().parent))
            import fb  # noqa: F811
        out = Path(png_dir)
        out.mkdir(parents=True, exist_ok=True)
        written = []
        for name, frame in positives.items():
            rgb = fb.frame_to_rgb888(frame)
            path = out / ("synth_%s.png" % name)
            fb.write_png(path, rgb)
            written.append(str(path))
        print("wrote %d synthetic PNG(s) to %s" % (len(written), out))

    total = len(results)
    bad = [r for r in results if not r["match"]]
    print("SELFTEST %s %d/%d cases"
          % ("PASS" if not bad else "FAIL", total - len(bad), total))
    if bad:
        print("MISMATCHED: " + ", ".join(r["case"] for r in bad))
    return {"cases": results,
            "total": total,
            "passed": total - len(bad),
            "ok": not bad}


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def _read_frame(path):
    p = Path(path)
    try:
        return p.read_bytes()
    except OSError as exc:
        raise SystemExit("ERROR: cannot read %s: %s" % (p, exc))


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    ap = argparse.ArgumentParser(
        prog="layout_checks.py",
        description="240x240 版式像素断言（C03 / docs-08 §3）")
    ap.add_argument("frame", nargs="?", help=".bin 帧（115200 B）")
    ap.add_argument("--state", help="六态之一：%s" % ", ".join(STATE_NAMES))
    ap.add_argument("--cards", type=int, default=None, help="栈深 0..3（默认按强调条推断）")
    ap.add_argument("--titles", help="comma-separated card titles (evidence only)")
    ap.add_argument("--json", action="store_true", help="JSON 输出")
    ap.add_argument("--quiet", action="store_true", help="只打印 RESULT")
    ap.add_argument("--selftest", action="store_true", help="合成帧正例/反例自测")
    ap.add_argument("--png-dir", help="--selftest 时把正例合成帧写成 PNG")
    args = ap.parse_args(argv)

    if args.selftest:
        result = _selftest(args.png_dir)
        if args.json:
            print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0 if result["ok"] else 1

    if not args.frame:
        ap.error("need FRAME.bin (or --selftest)")
    if not args.state:
        ap.error("need --state (or --selftest)")
    if args.cards is not None and not 0 <= args.cards <= 3:
        ap.error("--cards must be 0..3")
    titles = args.titles.split(",") if args.titles else None
    frame = _read_frame(args.frame)
    try:
        rep = check_frame(frame, args.state, args.cards, titles)
    except ValueError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(rep.to_dict(), ensure_ascii=False, indent=2))
    elif args.quiet:
        print("RESULT %s (%d/%d checks, state=%s, cards=%d)"
              % ("PASS" if rep.ok else "FAIL", len(rep.passed),
                 len(rep.checks), rep.state, rep.card_count))
        if not rep.ok:
            print("FAILED: " + ", ".join(rep.failed))
    else:
        print(rep.text())
    return 0 if rep.ok else 1


if __name__ == "__main__":
    sys.exit(main())
