#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""layout_checks_m3.py - M3 番茄钟页 / 菜单页 240x240 像素断言。

依据 `docs/10-m3-development.md` §5（M3 两页版式，C03 只覆盖值班页）：

番茄钟页（§5.1）
  底色            全屏 0x0000
  相位标题        居中 y=16，8x8 scale 1；READY/FOCUS/PAUSED/BREAK/DONE；
                  专注 0xFD20、休息 0x07E0、暂停/其它 0x8410
  圆环            心 (120,104)，外半径 76，环厚 10；轨道 0x4208；
                  进度 = elapsed/total 千分比，12 点顺时针；颜色随相位
  中心倒计时      环心居中，8x8 scale 2，白 M:SS
  底部统计        y=200 居中，8x8 scale 1，灰 0x8410 `DONE <n>  <m>M`

菜单页（§5.2）
  底色            全屏 0x0000
  标题            MENU 居中 y=12，8x8 scale 2，白
  行              x=4, w=232, h=28, y=48+i*32 (i=0..4)；卡底 0x18E3；
                  选中行反色（底 0xFFFF、字 0x0000）
  行文本          (x+8, y+10)，8x8 scale 1
  底部统计        y=212 居中，灰 `DONE <n>  <m>M   <DUTY|POMO>`

用法::

    python layout_checks_m3.py frame.bin --page pomo --phase FOCUS ^
        --remain-ms 3000 --total-ms 6000 --done 1 --focus-s 6
    python layout_checks_m3.py frame.bin --page menu --selected 2
    python layout_checks_m3.py --selftest --png-dir evidence/m3_selftest

自测（不需要设备）：合成帧正例 + 逐项破坏反例。`--selftest` 还覆盖
进度 0 / 500 / 1000 的颜色分布与 12 点顺时针方向。

退出码：0 全部 PASS；1 存在 FAIL；2 输入/参数错误。
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import layout_checks as lc  # noqa: E402  (复用像素 helper，避免重复实现)

WIDTH = lc.WIDTH
HEIGHT = lc.HEIGHT
FRAME_BYTES = lc.FRAME_BYTES
BG = 0x0000

# ---- 番茄钟页常量（docs/10 §5.1）----------------------------------------
POMO_CX, POMO_CY = 120, 104
RING_OUTER = 76
RING_THICK = 10
RING_INNER = RING_OUTER - RING_THICK  # 66
RING_TRACK = 0x4208
WHITE = 0xFFFF
GRAY = 0x8410
COLOR_FOCUS = 0xFD20
COLOR_BREAK = 0x07E0
TITLE_Y = 16
TITLE_H = 8
POMO_FOOTER_Y = 200
# 中心倒计时搜索窗：容纳 "25:00"（5 字符 × 8 × scale2 = 80 px 宽 / 16 px 高）
COUNTDOWN_RECT = (76, 90, 88, 28)
# 圆环外接框搜索区（排除 y<28 的标题与 y>=200 的页脚）
RING_SEARCH = (36, 28, 168, 160)

PHASES = ("IDLE", "FOCUS", "FOCUS_PAUSED", "BREAK", "BREAK_PAUSED",
          "FOCUS_DONE", "BREAK_DONE")
PHASE_TITLE = {
    "IDLE": "READY",
    "FOCUS": "FOCUS",
    "FOCUS_PAUSED": "PAUSED",
    "BREAK": "BREAK",
    "BREAK_PAUSED": "PAUSED",
    "FOCUS_DONE": "DONE",
    "BREAK_DONE": "DONE",
}
# 专注 0xFD20 / 休息 0x07E0 / 暂停与其它 0x8410（§5.1 相位标题颜色）
PHASE_COLOR = {
    "IDLE": GRAY,
    "FOCUS": COLOR_FOCUS,
    "FOCUS_PAUSED": GRAY,
    "BREAK": COLOR_BREAK,
    "BREAK_PAUSED": GRAY,
    "FOCUS_DONE": GRAY,
    "BREAK_DONE": GRAY,
}

# ---- 菜单页常量（docs/10 §5.2）-----------------------------------------
MENU_TITLE_Y = 12
MENU_ROW_X = 4
MENU_ROW_W = 232
MENU_ROW_H = 28
MENU_ROW_Y0 = 48
MENU_ROW_STEP = 32
MENU_ITEMS = 5
MENU_ROW_BG = 0x18E3
MENU_SEL_BG = 0xFFFF
MENU_SEL_TEXT = 0x0000
MENU_FOOTER_Y = 212
MENU_LABELS = ("MUTE: ON", "STATUS", "WIFI (M5)", "BT (M5)", "BACK")
MENU_TITLE = "MENU"


def menu_row_rect(i):
    return (MENU_ROW_X, MENU_ROW_Y0 + i * MENU_ROW_STEP, MENU_ROW_W, MENU_ROW_H)


# --------------------------------------------------------------------------
# small helpers (shared by synth + checks)
# --------------------------------------------------------------------------

def _clamp(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


def progress_color(phase):
    """环进度颜色：专注橙、休息绿、暂停/完成灰；IDLE 无进度 → None。"""
    phase = str(phase).upper()
    if phase == "IDLE":
        return None
    return PHASE_COLOR.get(phase, GRAY)


def progress_fraction(remain_ms, total_ms):
    if total_ms is None or total_ms <= 0 or remain_ms is None:
        return None
    return _clamp((float(total_ms) - float(remain_ms)) / float(total_ms), 0.0, 1.0)


def clock_angle_deg(dx, dy):
    """屏幕坐标下，12 点为 0°、顺时针递增的角度（0..360）。"""
    deg = math.degrees(math.atan2(dx, -dy))
    return deg if deg >= 0.0 else deg + 360.0


def sample_ring(frame, cx, cy, radius, step_deg=2):
    out = []
    for deg in range(0, 360, int(step_deg)):
        rad = math.radians(deg)
        x = int(round(cx + radius * math.sin(rad)))
        y = int(round(cy - radius * math.cos(rad)))
        if 0 <= x < WIDTH and 0 <= y < HEIGHT:
            out.append((deg, x, y, lc.get_px(frame, x, y)))
    return out


def color_bbox_any_in(frame, rect, colors):
    colors = set(colors)
    x0 = y0 = 10 ** 9
    x1 = y1 = -1
    for x, y, v in lc.iter_rect(frame, rect):
        if v in colors:
            x0 = min(x0, x)
            y0 = min(y0, y)
            x1 = max(x1, x)
            y1 = max(y1, y)
    if x1 < 0:
        return None
    return (x0, y0, x1 - x0 + 1, y1 - y0 + 1)


# --------------------------------------------------------------------------
# synthetic frame builders (self-test + simulator)
# --------------------------------------------------------------------------

def synth_pomo_frame(phase="FOCUS", remain_ms=None, total_ms=None,
                     done=0, focus_s=0, muted=0):
    phase = str(phase).upper()
    if phase not in PHASES:
        raise ValueError("unknown pomo phase %r" % phase)
    if total_ms is None:
        total_ms = 1500000 if phase in ("FOCUS", "FOCUS_PAUSED",
                                        "FOCUS_DONE") else 300000
    if remain_ms is None:
        remain_ms = total_ms
    frac = progress_fraction(remain_ms, total_ms) or 0.0

    frame = bytearray(FRAME_BYTES)
    # --- 相位标题（用文本宽度块模拟 8x8 字形） ---
    title = PHASE_TITLE[phase]
    tw = len(title) * 8
    lc.fill_rect(frame, (WIDTH - tw) // 2, TITLE_Y, tw, TITLE_H,
                 PHASE_COLOR[phase])
    # --- 圆环（环厚 10：66 <= r <= 76） ---
    pcolor = progress_color(phase)
    sweep = frac * 360.0
    for dy in range(-RING_OUTER, RING_OUTER + 1):
        for dx in range(-RING_OUTER, RING_OUTER + 1):
            d2 = dx * dx + dy * dy
            if d2 < RING_INNER * RING_INNER or d2 > RING_OUTER * RING_OUTER:
                continue
            color = RING_TRACK
            if pcolor is not None and frac > 0.0:
                if clock_angle_deg(dx, dy) <= sweep:
                    color = pcolor
            lc.set_px(frame, POMO_CX + dx, POMO_CY + dy, color)
    # --- 中心倒计时（白 scale2） ---
    secs = max(0, int(remain_ms) // 1000)
    text = "%d:%02d" % (secs // 60, secs % 60)
    tw = len(text) * 16
    lc.fill_rect(frame, POMO_CX - tw // 2, POMO_CY - 8, tw, 16, WHITE)
    # --- 底部统计（灰 scale1） ---
    foot = "DONE %d  %dM" % (int(done), int(focus_s) // 60)
    fw = len(foot) * 8
    lc.fill_rect(frame, (WIDTH - fw) // 2, POMO_FOOTER_Y, fw, 8, GRAY)
    return bytes(frame)


def synth_menu_frame(selected=0, muted=0, mode="DUTY", done=0, focus_s=0):
    selected = int(selected) % MENU_ITEMS
    mode = str(mode).upper()
    frame = bytearray(FRAME_BYTES)
    # --- 标题 MENU（白 scale2） ---
    tw = len(MENU_TITLE) * 16
    lc.fill_rect(frame, (WIDTH - tw) // 2, MENU_TITLE_Y, tw, 16, WHITE)
    # --- 5 行 ---
    for i in range(MENU_ITEMS):
        x, y, w, h = menu_row_rect(i)
        sel = (i == selected)
        lc.fill_rect(frame, x, y, w, h, MENU_SEL_BG if sel else MENU_ROW_BG)
        label = MENU_LABELS[i]
        if i == 0:
            label = "MUTE: %s" % ("ON" if muted else "OFF")
        lc.fill_rect(frame, x + 8, y + 10, len(label) * 8, 8,
                     MENU_SEL_TEXT if sel else WHITE)
    # --- 底部统计（灰 scale1） ---
    foot = "DONE %d  %dM   %s" % (int(done), int(focus_s) // 60, mode)
    fw = len(foot) * 8
    lc.fill_rect(frame, (WIDTH - fw) // 2, MENU_FOOTER_Y, fw, 8, GRAY)
    return bytes(frame)


# --------------------------------------------------------------------------
# report model
# --------------------------------------------------------------------------

@dataclass
class Report:
    page: str
    phase: str = None
    selected: int = None
    expected_progress: float = None
    measured_progress: float = None
    checks: list = field(default_factory=list)

    def add(self, name, ok, detail, evidence=None):
        self.checks.append(lc.Check(name, bool(ok), detail, list(evidence or [])))

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
            "page": self.page,
            "phase": self.phase,
            "selected": self.selected,
            "expected_progress": self.expected_progress,
            "measured_progress": self.measured_progress,
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
        out.append("RESULT %s (%d/%d checks passed, page=%s%s)"
                   % ("PASS" if self.ok else "FAIL", len(self.passed),
                      len(self.checks), self.page,
                      "" if self.phase is None
                      else ", phase=%s" % self.phase))
        return out

    def text(self, show_evidence=True):
        return "\n".join(self.lines(show_evidence))


# --------------------------------------------------------------------------
# 番茄钟页断言
# --------------------------------------------------------------------------

# 全屏底色留空区（y=0..13 标题上方 / y=24..27 标题与环顶之间 /
# y=182..197 环底与页脚之间 / y>=208 页脚下方 / 环左右外侧）
POMO_BG_RECTS = (
    ("bg.top", (0, 0, WIDTH, 14)),
    ("bg.title_ring_gap", (0, 24, WIDTH, 4)),
    ("bg.ring_footer_gap", (0, 182, WIDTH, 16)),
    ("bg.bottom", (0, 208, WIDTH, 32)),
    ("bg.left", (0, 28, 40, 152)),
    ("bg.right", (200, 28, 40, 152)),
)


def _check_bg_clean(rep, frame, rects):
    for label, rect in rects:
        hits = lc.count_color(frame, rect, BG)
        area = rect[2] * rect[3]
        ok = hits == area
        ev = []
        if not ok:
            mm = lc.first_mismatch(frame, rect, BG)
            if mm:
                ev = ["first_non_bg=(%d,%d)=0x%04X" % mm]
        rep.add(label, ok, "%s all 0x0000: %d/%d px" % (rect, hits, area), ev)


def check_pomo_frame(frame, phase, remain_ms=None, total_ms=None,
                     done=None, focus_s=None):
    """番茄钟页断言；返回 Report。``remain_ms``/``total_ms`` 给定时校验进度角。"""
    phase = str(phase).upper()
    if phase not in PHASES:
        raise ValueError("unknown phase %r (expect one of %s)"
                         % (phase, ", ".join(PHASES)))
    if len(frame) != FRAME_BYTES:
        rep = Report("pomo", phase)
        rep.add("frame_size", False,
                "%d bytes, expected %d" % (len(frame), FRAME_BYTES))
        return rep
    rep = Report("pomo", phase)
    rep.add("frame_size", True, "%d bytes = 240x240 RGB565LE" % len(frame))

    pcolor = progress_color(phase)
    expected = progress_fraction(remain_ms, total_ms)
    rep.expected_progress = expected

    # ---- 全屏底色留空 ----
    _check_bg_clean(rep, frame, POMO_BG_RECTS)

    # ---- 圆环存在 / 几何 / 环厚 ----
    samples = sample_ring(frame, POMO_CX, POMO_CY, (RING_INNER + RING_OUTER) // 2)
    ring_colors = {RING_TRACK}
    if pcolor is not None:
        ring_colors.add(pcolor)
    bad = [(d, x, y, v) for d, x, y, v in samples if v not in ring_colors]
    rep.add("ring_present", not bad,
            "mid-radius %d sampled at 2 deg: %d/%d px are track/progress"
            % ((RING_INNER + RING_OUTER) // 2, len(samples) - len(bad),
               len(samples)),
            ["(%d deg @%d,%d)=0x%04X" % (d, x, y, v) for d, x, y, v in bad[:5]])

    bbox = color_bbox_any_in(frame, RING_SEARCH, ring_colors)
    if bbox is None:
        rep.add("ring_bbox", False, "no ring color inside %s" % (RING_SEARCH,))
    else:
        bx, by, bw, bh = bbox
        cx = bx + (bw - 1) / 2.0
        cy = by + (bh - 1) / 2.0
        centered = abs(cx - POMO_CX) <= 2.5 and abs(cy - POMO_CY) <= 2.5
        sized = (abs(bw - (2 * RING_OUTER + 1)) <= 4
                 and abs(bh - (2 * RING_OUTER + 1)) <= 4)
        rep.add("ring_bbox", centered and sized,
                "ring bbox=(%d,%d,w=%d,h=%d) center=(%.1f,%.1f) expect "
                "center=(%d,%d) d=%d"
                % (bx, by, bw, bh, cx, cy, POMO_CX, POMO_CY,
                   2 * RING_OUTER + 1),
                ["contained=%s sized=%s" % (centered, sized)])

    # 环厚：在 270°（环左）沿半径步进，数环色像素
    run = 0
    first = None
    for r in range(40, 101):
        x = POMO_CX - r
        y = POMO_CY
        v = lc.get_px(frame, x, y)
        if v in ring_colors:
            run += 1
            if first is None:
                first = r
    thick_ok = 6 <= run <= 14
    rep.add("ring_thickness", thick_ok,
            "270deg radial ring-color run=%d px (expect ~%d, 6..14)"
            % (run, RING_THICK),
            ["first_ring_radius=%s (expect ~%d..%d)"
             % (first, RING_INNER, RING_OUTER)])

    # ---- 进度（角度分布 + 12 点顺时针方向） ----
    measured = infer_progress(frame, phase)
    rep.measured_progress = measured
    if expected is not None:
        ok = abs(measured - expected) <= 0.05
        rep.add("progress_fraction", ok,
                "measured=%.3f expected=%.3f (remain_ms=%s total_ms=%s)"
                % (measured, expected, remain_ms, total_ms),
                ["tolerance=0.05 (18 deg)"])
        if 0.02 < expected < 0.98:
            margin = 0.08  # 28.8 deg
            deg_p = (expected - margin) * 360.0
            deg_t = (expected + margin) * 360.0
            ok_p = ring_color_at(frame, deg_p) == pcolor
            ok_t = ring_color_at(frame, deg_t) == RING_TRACK
            rep.add("progress_direction", ok_p and ok_t,
                    "clockwise from 12: deg<%.1f progress=0x%04X, "
                    "deg>%.1f track=0x%04X"
                    % (expected * 360.0, pcolor, expected * 360.0,
                       RING_TRACK),
                    ["at %d deg -> 0x%04X (%s)"
                     % (round(deg_p), ring_color_at(frame, deg_p),
                        "progress" if ok_p else "NOT progress"),
                     "at %d deg -> 0x%04X (%s)"
                     % (round(deg_t), ring_color_at(frame, deg_t),
                        "track" if ok_t else "NOT track")])
        # 进度色出现在环上（expected > 0 时）/ 不出现（=0 时）
        n_prog = sum(1 for _, _, _, v in samples if pcolor and v == pcolor)
        if expected >= 0.995:
            rep.add("progress_color_full", n_prog == len(samples),
                    "progress=100%%: %d/%d ring samples are 0x%04X"
                    % (n_prog, len(samples), pcolor))
        elif expected <= 0.005:
            rep.add("progress_color_zero", n_prog <= 3,
                    "progress=0%%: %d/%d ring samples are progress color "
                    "(expect <=3)" % (n_prog, len(samples)))
        else:
            rep.add("progress_color_present", n_prog > 0,
                    "progress color 0x%04X on ring: %d/%d samples"
                    % (pcolor, n_prog, len(samples)))
    if pcolor is None:
        # IDLE：轨道应全为轨道色（不画进度）
        n_track = sum(1 for _, _, _, v in samples if v == RING_TRACK)
        rep.add("ring_track_all", n_track == len(samples),
                "IDLE: %d/%d ring samples are track 0x%04X"
                % (n_track, len(samples), RING_TRACK))

    # ---- 相位标题 ----
    band = (0, TITLE_Y - 2, WIDTH, TITLE_H + 4)
    non_bg = sum(1 for _, _, v in lc.iter_rect(frame, band) if v != BG)
    exp_color = PHASE_COLOR[phase]
    n_color = lc.count_color(frame, band, exp_color)
    rep.add("phase_title", non_bg > 0 and n_color > 0,
            "title band %s non_bg=%d, color 0x%04X px=%d (expected %r)"
            % (band, non_bg, exp_color, n_color, PHASE_TITLE[phase]),
            ["phase=%s color=0x%04X" % (phase, exp_color)])

    # ---- 中心倒计时（白） ----
    white_in = lc.count_color(frame, COUNTDOWN_RECT, WHITE)
    white_all = lc.count_color(frame, (0, 0, WIDTH, HEIGHT), WHITE)
    rep.add("countdown_text", white_in > 0 and white_all == white_in,
            "white px in countdown window %s = %d (whole frame %d)"
            % (COUNTDOWN_RECT, white_in, white_all),
            ["white only in countdown area: %s" % (white_all == white_in)])

    # ---- 底部统计 ----
    foot = (0, POMO_FOOTER_Y - 4, WIDTH, 16)
    n_gray = lc.count_color(frame, foot, GRAY)
    rep.add("footer_stats", n_gray > 0,
            "footer band %s gray 0x%04X px=%d (DONE <n>  <m>M)"
            % (foot, GRAY, n_gray))
    return rep


def infer_progress(frame, phase):
    """按 2° 均匀采样环中线，返回进度色像素占比（0..1）。"""
    phase = str(phase).upper()
    pcolor = progress_color(phase)
    samples = sample_ring(frame, POMO_CX, POMO_CY,
                          (RING_INNER + RING_OUTER) // 2)
    if pcolor is None:
        return 0.0
    n = sum(1 for _, _, _, v in samples if v == pcolor)
    return n / float(len(samples))


def ring_color_at(frame, deg):
    rad = math.radians(float(deg) % 360.0)
    r = (RING_INNER + RING_OUTER) // 2
    x = int(round(POMO_CX + r * math.sin(rad)))
    y = int(round(POMO_CY - r * math.cos(rad)))
    return lc.get_px(frame, x, y)


# --------------------------------------------------------------------------
# 菜单页断言
# --------------------------------------------------------------------------

MENU_BG_RECTS = (
    ("bg.top", (0, 0, WIDTH, 10)),
    ("bg.title_rows_gap", (0, 28, WIDTH, 20)),
    ("bg.bottom", (0, 220, WIDTH, 20)),
    ("bg.left", (0, 48, 4, 156)),
    ("bg.right", (236, 48, 4, 156)),
)


def infer_menu_selected(frame):
    """推断选中行：底 0xFFFF 占比最高者为选中行；无法判定返回 None。"""
    if len(frame) != FRAME_BYTES:
        return None
    best = None
    best_score = -1.0
    for i in range(MENU_ITEMS):
        rect = menu_row_rect(i)
        area = rect[2] * rect[3]
        n_white = lc.count_color(frame, rect, MENU_SEL_BG)
        n_gray = lc.count_color(frame, rect, MENU_ROW_BG)
        score = (n_white - n_gray) / float(area)
        if score > best_score:
            best_score = score
            best = i
    if best_score < 0.2:
        return None
    return best


def check_menu_frame(frame, selected=None, mode="DUTY", done=None, focus_s=None):
    """菜单页断言；返回 Report。``selected`` 给定时校验选中行索引。"""
    mode = str(mode).upper()
    if len(frame) != FRAME_BYTES:
        rep = Report("menu")
        rep.add("frame_size", False,
                "%d bytes, expected %d" % (len(frame), FRAME_BYTES))
        return rep
    rep = Report("menu", None, selected)
    rep.add("frame_size", True, "%d bytes = 240x240 RGB565LE" % len(frame))

    # ---- 全屏底色留空 ----
    _check_bg_clean(rep, frame, MENU_BG_RECTS)

    # ---- 标题 MENU（白 scale2） ----
    title_band = (0, MENU_TITLE_Y - 2, WIDTH, 20)
    n_title = lc.count_color(frame, title_band, WHITE)
    rep.add("menu_title", n_title > 0,
            "title band %s white px=%d (MENU scale2)" % (title_band, n_title))

    # ---- 行：矩形/底色/选中反色/文本非空 ----
    inferred = infer_menu_selected(frame)
    rep.selected = inferred
    if selected is not None:
        rep.add("selected_row", inferred == selected,
                "inferred selected=%s expected=%d" % (inferred, selected))
    for i in range(MENU_ITEMS):
        rect = menu_row_rect(i)
        area = rect[2] * rect[3]
        n_white = lc.count_color(frame, rect, MENU_SEL_BG)
        n_gray = lc.count_color(frame, rect, MENU_ROW_BG)
        is_sel = (i == inferred)
        if is_sel:
            base_ok = n_white >= int(area * 0.5)
            n_text = lc.count_color(frame, rect, MENU_SEL_TEXT)
            text_ok = n_text >= 8
            rep.add("row_%d" % i, base_ok,
                    "selected row%s base 0xFFFF: %d/%d px (>=50%%)"
                    % (rect, n_white, area))
            rep.add("row_%d_inverse_text" % i, text_ok,
                    "selected row%s inverse text 0x0000 px=%d (>=8)"
                    % (rect, n_text))
        else:
            base_ok = n_gray >= int(area * 0.5)
            content = sum(1 for _, _, v in lc.iter_rect(frame, rect)
                          if v != MENU_ROW_BG)
            text_ok = content >= 8
            rep.add("row_%d" % i, base_ok,
                    "row%s card bg 0x18E3: %d/%d px (>=50%%)"
                    % (rect, n_gray, area))
            rep.add("row_%d_text" % i, text_ok,
                    "row%s non-bg (text) px=%d (>=8)" % (rect, content))

    # ---- 行间 4 px 留空 ----
    gaps = []
    for i in range(MENU_ITEMS - 1):
        rect = (MENU_ROW_X, MENU_ROW_Y0 + i * MENU_ROW_STEP + MENU_ROW_H,
                MENU_ROW_W, MENU_ROW_STEP - MENU_ROW_H)
        hits = lc.count_color(frame, rect, BG)
        gaps.append((i, rect, hits, rect[2] * rect[3]))
    rep.add("row_gap", all(h == a for _, _, h, a in gaps),
            "row gaps all 0x0000: %s"
            % ", ".join("gap%d=%d/%d" % (i, h, a) for i, _, h, a in gaps))
    # 最后一行与页脚之间留空
    last_gap = (MENU_ROW_X, MENU_ROW_Y0 + (MENU_ITEMS - 1) * MENU_ROW_STEP
                + MENU_ROW_H, MENU_ROW_W, MENU_FOOTER_Y - 4
                - (MENU_ROW_Y0 + (MENU_ITEMS - 1) * MENU_ROW_STEP
                   + MENU_ROW_H))
    hits = lc.count_color(frame, last_gap, BG)
    rep.add("row_footer_gap", hits == last_gap[2] * last_gap[3],
            "%s all 0x0000: %d/%d px"
            % (last_gap, hits, last_gap[2] * last_gap[3]))

    # ---- 底部统计（灰） ----
    foot = (0, MENU_FOOTER_Y - 4, WIDTH, 20)
    n_gray = lc.count_color(frame, foot, GRAY)
    rep.add("footer_stats", n_gray > 0,
            "footer band %s gray 0x%04X px=%d (DONE <n>  <m>M   <mode>)"
            % (foot, GRAY, n_gray))
    return rep


# --------------------------------------------------------------------------
# self-test: synthetic positives + one-broken-item negatives
# --------------------------------------------------------------------------

def _selftest(png_dir=None):
    results = []
    positives = {}

    def record(name, frame, page, expect_ok, expect_fail_substr=None, **kw):
        if page == "pomo":
            rep = check_pomo_frame(frame, kw.get("phase", "FOCUS"),
                                   kw.get("remain_ms"), kw.get("total_ms"),
                                   kw.get("done"), kw.get("focus_s"))
        else:
            rep = check_menu_frame(frame, kw.get("selected"),
                                   kw.get("mode", "DUTY"),
                                   kw.get("done"), kw.get("focus_s"))
        if expect_ok:
            match = rep.ok
            matched = ("all %d checks passed" % len(rep.checks) if rep.ok
                       else "failed: " + ", ".join(rep.failed))
        else:
            hit = [n for n in rep.failed if expect_fail_substr in n]
            match = (not rep.ok) and bool(hit)
            matched = (", ".join(rep.failed) if rep.failed else "no failure")
            if hit:
                matched += " [matched %s]" % hit[0]
            else:
                matched += " [NOT MATCHED]"
        results.append({"case": name, "page": page,
                        "expect": "PASS" if expect_ok else "FAIL",
                        "actual": "PASS" if rep.ok else "FAIL",
                        "match": match, "checks": len(rep.checks),
                        "failed": rep.failed, "report": rep.to_dict()})
        print("%-40s expect=%-4s actual=%-4s %s | %s"
              % (name, "PASS" if expect_ok else "FAIL",
                 "PASS" if rep.ok else "FAIL", "OK" if match else "MISMATCH",
                 matched))
        return rep

    # ------------------------- 番茄钟页正例 -------------------------
    pomo_cases = [
        ("pomo_IDLE_p0", dict(phase="IDLE", remain_ms=1500000,
                              total_ms=1500000)),
        ("pomo_FOCUS_p0", dict(phase="FOCUS", remain_ms=1500000,
                               total_ms=1500000)),
        ("pomo_FOCUS_p500", dict(phase="FOCUS", remain_ms=750000,
                                 total_ms=1500000)),
        ("pomo_FOCUS_p1000", dict(phase="FOCUS", remain_ms=0,
                                  total_ms=1500000)),
        ("pomo_FOCUS_p167", dict(phase="FOCUS", remain_ms=5000,
                                 total_ms=6000)),
        ("pomo_FOCUS_p833", dict(phase="FOCUS", remain_ms=1000,
                                 total_ms=6000)),
        ("pomo_BREAK_p500", dict(phase="BREAK", remain_ms=1500,
                                 total_ms=3000)),
        ("pomo_FOCUS_PAUSED_p250", dict(phase="FOCUS_PAUSED",
                                        remain_ms=4500, total_ms=6000)),
        ("pomo_FOCUS_DONE_p1000", dict(phase="FOCUS_DONE", remain_ms=0,
                                       total_ms=6000, done=1, focus_s=6)),
        ("pomo_BREAK_DONE_p1000", dict(phase="BREAK_DONE", remain_ms=0,
                                       total_ms=3000)),
        ("pomo_title_25min", dict(phase="FOCUS", remain_ms=1260000,
                                  total_ms=1500000)),
    ]
    for name, kw in pomo_cases:
        frame = synth_pomo_frame(**kw)
        positives[name] = frame
        record(name, frame, "pomo", True, **kw)

    # ------------------------- 菜单页正例 -------------------------
    menu_cases = []
    for sel in range(MENU_ITEMS):
        menu_cases.append(("menu_sel%d" % sel,
                           dict(selected=sel, muted=0, mode="DUTY",
                                done=1, focus_s=6)))
    menu_cases.append(("menu_sel0_muted_pomo",
                       dict(selected=0, muted=1, mode="POMO", done=3,
                            focus_s=18)))
    for name, kw in menu_cases:
        frame = synth_menu_frame(**kw)
        positives[name] = frame
        record(name, frame, "menu", True, **kw)

    # ------------------------- 反例：只破坏一项 -------------------------
    def broken(base, mutator):
        f = bytearray(base)
        mutator(f)
        return bytes(f)

    def paint_ring(f, color):
        for dy in range(-RING_OUTER, RING_OUTER + 1):
            for dx in range(-RING_OUTER, RING_OUTER + 1):
                d2 = dx * dx + dy * dy
                if RING_INNER * RING_INNER <= d2 <= RING_OUTER * RING_OUTER:
                    lc.set_px(f, POMO_CX + dx, POMO_CY + dy, color)

    base_pomo = positives["pomo_FOCUS_p500"]
    record("neg_pomo_size", base_pomo[:1000], "pomo", False, "frame_size",
           phase="FOCUS", remain_ms=750000, total_ms=1500000)
    record("neg_pomo_ring_missing",
           broken(base_pomo, lambda f: paint_ring(f, BG)), "pomo", False,
           "ring_present", phase="FOCUS", remain_ms=750000,
           total_ms=1500000)
    record("neg_pomo_ring_center",
           broken(base_pomo, lambda f: _shift_ring(f)), "pomo", False,
           "ring_bbox", phase="FOCUS", remain_ms=750000,
           total_ms=1500000)
    record("neg_pomo_progress_zeroed",
           broken(base_pomo, lambda f: paint_ring(f, RING_TRACK)), "pomo",
           False, "progress_fraction", phase="FOCUS", remain_ms=750000,
           total_ms=1500000)
    record("neg_pomo_progress_full",
           broken(base_pomo, lambda f: paint_ring(f, COLOR_FOCUS)),
           "pomo", False, "progress_fraction", phase="FOCUS",
           remain_ms=750000, total_ms=1500000)
    record("neg_pomo_progress_reversed",
           broken(base_pomo, lambda f: _reverse_progress(f)), "pomo", False,
           "progress_direction", phase="FOCUS", remain_ms=750000,
           total_ms=1500000)
    record("neg_pomo_countdown_missing",
           broken(positives["pomo_IDLE_p0"],
                  lambda f: lc.fill_rect(f, COUNTDOWN_RECT[0],
                                         COUNTDOWN_RECT[1], COUNTDOWN_RECT[2],
                                         COUNTDOWN_RECT[3], BG)),
           "pomo", False, "countdown_text", phase="IDLE",
           remain_ms=1500000, total_ms=1500000)
    record("neg_pomo_title_missing",
           broken(positives["pomo_IDLE_p0"],
                  lambda f: lc.fill_rect(f, 0, TITLE_Y - 2, WIDTH,
                                         TITLE_H + 4, BG)),
           "pomo", False, "phase_title", phase="IDLE",
           remain_ms=1500000, total_ms=1500000)
    record("neg_pomo_footer_missing",
           broken(positives["pomo_IDLE_p0"],
                  lambda f: lc.fill_rect(f, 0, POMO_FOOTER_Y - 4, WIDTH, 16,
                                         BG)),
           "pomo", False, "footer_stats", phase="IDLE",
           remain_ms=1500000, total_ms=1500000)
    record("neg_pomo_bg_dirty",
           broken(positives["pomo_IDLE_p0"],
                  lambda f: lc.set_px(f, 10, 6, 0xFFFF)),
           "pomo", False, "bg.", phase="IDLE", remain_ms=1500000,
           total_ms=1500000)
    record("neg_pomo_wrong_phase_color",
           broken(base_pomo, lambda f: lc.fill_rect(
               f, 0, TITLE_Y - 2, WIDTH, TITLE_H + 4, BG)),
           "pomo", False, "phase_title", phase="FOCUS",
           remain_ms=750000, total_ms=1500000)

    # ------------------------- 反例：菜单页 -------------------------
    base_menu = positives["menu_sel2"]
    record("neg_menu_size", base_menu[:1000], "menu", False, "frame_size",
           selected=2)
    record("neg_menu_row_missing",
           broken(base_menu, lambda f: lc.fill_rect(
               f, *menu_row_rect(1), BG)), "menu", False, "row_1",
           selected=2)
    record("neg_menu_selected_not_inverse",
           broken(base_menu, lambda f: lc.fill_rect(
               f, *menu_row_rect(2), MENU_ROW_BG)), "menu", False,
           "selected_row", selected=2)
    record("neg_menu_base_wrong_color",
           broken(base_menu, lambda f: lc.fill_rect(
               f, *menu_row_rect(3), 0x07E0)), "menu", False, "row_3",
           selected=2)
    record("neg_menu_title_missing",
           broken(base_menu, lambda f: lc.fill_rect(
               f, 0, MENU_TITLE_Y - 2, WIDTH, 20, BG)), "menu", False,
           "menu_title", selected=2)
    record("neg_menu_footer_missing",
           broken(base_menu, lambda f: lc.fill_rect(
               f, 0, MENU_FOOTER_Y - 4, WIDTH, 20, BG)), "menu", False,
           "footer_stats", selected=2)
    record("neg_menu_bg_dirty",
           broken(base_menu, lambda f: lc.set_px(f, 10, 40, 0xFFFF)),
           "menu", False, "bg.", selected=2)
    record("neg_menu_wrong_selected_expected",
           base_menu, "menu", False, "selected_row", selected=4)

    # 可选：把正例合成帧写成 PNG，便于人眼复核断言依据
    if png_dir:
        try:
            import fb
        except ImportError:
            sys.path.insert(0, str(HERE))
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
    return {"cases": results, "total": total,
            "passed": total - len(bad), "ok": not bad}


def _shift_ring(f):
    """把圆环整体右移 6 px（破坏圆心）。"""
    snap = bytes(f)
    lc.fill_rect(f, 36, 24, 172, 164, BG)
    for y in range(24, 188):
        for x in range(36, 204):
            v = lc.get_px(snap, x, y)
            if v in (RING_TRACK, COLOR_FOCUS, COLOR_BREAK, GRAY):
                if 0 <= x + 6 < WIDTH:
                    lc.set_px(f, x + 6, y, v)


def _reverse_progress(f):
    """把进度镜像到 12 点**逆时针**方向（0..90° + 270..360°，破坏方向断言）。

    与 p500 正例的 0..180° 扇区面积相同（半边），故 progress_fraction 仍吻合，
    只有 progress_direction 会失败——单项破坏。
    """
    def paint(fr, color):
        for yy in range(24, 188):
            for xx in range(36, 204):
                ddx, ddy = xx - POMO_CX, yy - POMO_CY
                d2 = ddx * ddx + ddy * ddy
                if RING_INNER * RING_INNER <= d2 <= RING_OUTER * RING_OUTER:
                    lc.set_px(fr, xx, yy, color)

    paint(f, RING_TRACK)  # 先清成轨道色
    for y in range(24, 188):
        for x in range(36, 204):
            dx, dy = x - POMO_CX, y - POMO_CY
            if dx * dx + dy * dy < RING_INNER * RING_INNER:
                continue
            if dx * dx + dy * dy > RING_OUTER * RING_OUTER:
                continue
            ang = clock_angle_deg(dx, dy)
            if ang <= 90.0 or ang >= 270.0:  # 12 点逆时针半边
                lc.set_px(f, x, y, COLOR_FOCUS)


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
        prog="layout_checks_m3.py",
        description="M3 番茄钟页/菜单页 240x240 像素断言（docs/10 §5）")
    ap.add_argument("frame", nargs="?", help=".bin 帧（115200 B）")
    ap.add_argument("--page", choices=("pomo", "menu"),
                    help="页面：pomo=番茄钟页 / menu=菜单页")
    ap.add_argument("--phase", choices=PHASES,
                    help="番茄钟相位（pomo 页必填）")
    ap.add_argument("--remain-ms", type=int, default=None,
                    help="m 行 remain_ms（校验进度角）")
    ap.add_argument("--total-ms", type=int, default=None,
                    help="m 行 total_ms（校验进度角）")
    ap.add_argument("--done", type=int, default=None, help="当日完成段数（证据）")
    ap.add_argument("--focus-s", type=int, default=None,
                    help="当日累计专注秒（证据）")
    ap.add_argument("--selected", type=int, default=None,
                    help="菜单选中行 0..4（校验选中行）")
    ap.add_argument("--mode", default="DUTY", choices=("DUTY", "POMO"),
                    help="菜单页底部模式（证据）")
    ap.add_argument("--json", action="store_true", help="JSON 输出")
    ap.add_argument("--quiet", action="store_true", help="只打印 RESULT")
    ap.add_argument("--selftest", action="store_true",
                    help="合成帧正例/反例自测（不需要设备）")
    ap.add_argument("--png-dir", help="--selftest 时把正例合成帧写成 PNG")
    args = ap.parse_args(argv)

    if args.selftest:
        result = _selftest(args.png_dir)
        if args.json:
            print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0 if result["ok"] else 1

    if not args.frame:
        ap.error("need FRAME.bin (or --selftest)")
    if not args.page:
        ap.error("need --page pomo|menu")
    if args.selected is not None and not 0 <= args.selected < MENU_ITEMS:
        ap.error("--selected must be 0..4")
    frame = _read_frame(args.frame)
    try:
        if args.page == "pomo":
            if not args.phase:
                ap.error("need --phase for --page pomo")
            rep = check_pomo_frame(frame, args.phase, args.remain_ms,
                                   args.total_ms, args.done, args.focus_s)
        else:
            rep = check_menu_frame(frame, args.selected, args.mode,
                                   args.done, args.focus_s)
    except ValueError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(rep.to_dict(), ensure_ascii=False, indent=2))
    elif args.quiet:
        print("RESULT %s (%d/%d checks, page=%s)"
              % ("PASS" if rep.ok else "FAIL", len(rep.passed),
                 len(rep.checks), rep.page))
        if not rep.ok:
            print("FAILED: " + ", ".join(rep.failed))
    else:
        print(rep.text())
    return 0 if rep.ok else 1


if __name__ == "__main__":
    sys.exit(main())
