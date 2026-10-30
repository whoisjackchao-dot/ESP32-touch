#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""pet_sim.py - M2 调试串口设备模拟器（无设备时端到端验证 run_g1/run_g3）。

实现的固件行为与 docs/08-m2-development.md + 上游 pet_state.c（本项目修订）一致：

  * 6 态状态机：1=WORKING 2=INPUT_REQUIRED 3=DONE 4=FAILED 0=AGENT_IDLE
    5=LINK_DOWN u=LINK_UP n=ANNOUNCEMENT
  * `0`：WORKING/INPUT_REQUIRED 时进入“待定完成”，1500 ms 去抖后 → DONE；
    其它态立即 → IDLE（与上游 done 下降沿去抖一致）
  * DONE 驻留 5000 ms → IDLE；15 s 无有效事件 → OFFLINE（prev 记录，u/n 恢复）
  * `c`：按 docs/08 §4 的 8 张预置卡顺序压栈（第 9 次循环）；同名（title+project）
    更新置顶；空 title **接收**（C03 §3.4 占位渲染，上游丢弃→本项目改）
  * `x`：清空卡栈，且**重置预置卡序号**（run_g3 每轮从 ALPHA/BETA/GAMMA 开始的前提）
  * `p`/`d`：由 status()/dump() 直接生成；帧内容用 layout_checks.synth_frame 合成

接口与 pet_serial.PetSerial 的 duck-typing 子集一致：
send / status / dump / wait_for_line / flush_input / close / describe。

注意：模拟器**不是**被测固件，只用于验证 host 脚本的时序、解析、断言与证据管线；
真机证据必须用 COM7 实跑（run_g1.py / run_g3.py 不带 --simulate）。
"""

from __future__ import annotations

import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import layout_checks as lc  # noqa: E402

# docs/08-m2-development.md §4 预置卡序列（第 9 次起循环）
PRESET_CARDS = (
    {"agent": "OC", "title": "GAMMA", "project": "openai/chat",
     "elapsed_s": 75, "state": "WORKING"},
    {"agent": "CC", "title": "BETA", "project": "acme/web",
     "elapsed_s": 900, "state": "INPUT_REQUIRED"},
    {"agent": "CX", "title": "ALPHA", "project": "acme/api",
     "elapsed_s": 5, "state": "DONE"},
    {"agent": "OC", "title": "DELTA", "project": "",
     "elapsed_s": 3725, "state": "FAILED"},
    {"agent": "CC", "title": "BETA", "project": "acme/web",
     "elapsed_s": 1200, "state": "INPUT_REQUIRED"},
    {"agent": "OC", "title": "", "project": "acme/empty",
     "elapsed_s": 10, "state": "WORKING"},
    {"agent": "CX", "title": "THIS-IS-A-VERY-LONG-TITLE-0123456789",
     "project": "acme/long", "elapsed_s": 59, "state": "WORKING"},
    {"agent": "OC", "title": "中文标题测试", "project": "acme/cjk",
     "elapsed_s": 3600, "state": "DONE"},
)

STATE_FRAME = {
    "IDLE": 0,
    "WORKING": 4,
    "INPUT_REQUIRED": 5,
    "DONE": 2,
    "FAILED": 3,
    "OFFLINE": 1,
}

DONE_DEBOUNCE_MS = 1500
DONE_DWELL_MS = 5000
LINK_TIMEOUT_MS = 15000

ALLOWED_CHARS = set("123450uncxpd")


class PetSim:
    """与 PetSerial 同接口的确定性模拟器（真实 sleep 由调用者的 sleep 决定）。"""

    def __init__(self, verbose=False):
        self.verbose = verbose
        self.state = "IDLE"
        self.prev_state = "IDLE"
        self.since_ms = self._now()
        self.last_valid_ms = self.since_ms
        self.pending_done_at = 0
        self.cards = []          # top-first: dict(title, project, ...)
        self.preset_index = 0
        self._open = False
        self.debug_lines = []

    # -- lifecycle -------------------------------------------------------
    def open(self):
        self._open = True
        return self

    def close(self):
        self._open = False

    def __enter__(self):
        return self.open()

    def __exit__(self, *exc_info):
        self.close()

    def describe(self):
        return {"port": "SIM", "baud": 115200, "mode": "simulate"}

    def flush_input(self):
        self.debug_lines.clear()

    # -- time ------------------------------------------------------------
    @staticmethod
    def _now():
        return int(time.monotonic() * 1000)

    # -- state machine ---------------------------------------------------
    def _tick(self):
        now = self._now()
        if self.pending_done_at and now >= self.pending_done_at:
            self.pending_done_at = 0
            self._set_state("DONE", now)
        if self.state == "DONE" and now - self.since_ms >= DONE_DWELL_MS:
            self._set_state("IDLE", now)
        if self.state != "OFFLINE" and now - self.last_valid_ms >= LINK_TIMEOUT_MS:
            self.prev_state = self.state
            self._set_state("OFFLINE", now)

    def _set_state(self, state, now=None):
        now = self._now() if now is None else now
        self.state = state
        self.since_ms = now

    def _event(self, kind):
        now = self._now()
        self.last_valid_ms = now
        self._tick()
        if kind == "TASK_START":
            self.pending_done_at = 0
            self._set_state("WORKING", now)
        elif kind == "INPUT_REQUIRED":
            self._set_state("INPUT_REQUIRED", now)
        elif kind == "TASK_DONE":
            self.pending_done_at = 0
            self._set_state("DONE", now)
        elif kind == "TASK_ERROR":
            self.pending_done_at = 0
            self._set_state("FAILED", now)
        elif kind == "AGENT_IDLE":
            if self.state in ("WORKING", "INPUT_REQUIRED"):
                self.pending_done_at = now + DONE_DEBOUNCE_MS
            else:
                self.pending_done_at = 0
                self._set_state("IDLE", now)
        elif kind == "LINK_DOWN":
            if self.state != "OFFLINE":
                self.prev_state = self.state
            self._set_state("OFFLINE", now)
        elif kind == "LINK_UP":
            self._set_state("IDLE" if self.prev_state == "OFFLINE"
                            else self.prev_state, now)
        elif kind == "ANNOUNCEMENT":
            if self.state == "OFFLINE":
                self._set_state("IDLE" if self.prev_state == "OFFLINE"
                                else self.prev_state, now)

    def _push_preset_card(self):
        card = dict(PRESET_CARDS[self.preset_index])
        self.preset_index = (self.preset_index + 1) % len(PRESET_CARDS)
        # 同名（title + project 都相同）更新置顶
        found = None
        for i, existing in enumerate(self.cards):
            if (existing["title"] == card["title"]
                    and existing["project"] == card["project"]):
                found = i
                break
        if found is not None:
            self.cards.pop(found)
        self.cards.insert(0, card)
        del self.cards[3:]

    # -- command input ---------------------------------------------------
    def send(self, data):
        if isinstance(data, bytes):
            text = data.decode("ascii", "strict")
        else:
            text = str(data)
        for ch in text:
            if ch in "\r\n":
                continue
            if ch not in ALLOWED_CHARS:
                raise ValueError("simulator: unknown command byte %r" % ch)
            if ch == "1":
                self._event("TASK_START")
            elif ch == "2":
                self._event("INPUT_REQUIRED")
            elif ch == "3":
                self._event("TASK_DONE")
            elif ch == "4":
                self._event("TASK_ERROR")
            elif ch == "0":
                self._event("AGENT_IDLE")
            elif ch == "5":
                self._event("LINK_DOWN")
            elif ch == "u":
                self._event("LINK_UP")
            elif ch == "n":
                self._event("ANNOUNCEMENT")
            elif ch == "c":
                self._push_preset_card()
            elif ch == "x":
                self.cards = []
                self.preset_index = 0
            elif ch in "pd":
                pass  # 由 status()/dump() 生成响应
        return list(text)

    def send_command(self, chars):
        return self.send(chars)

    # -- protocol responses ---------------------------------------------
    def wait_for_line(self, spec, timeout=None):
        tag, pattern = None, str(spec)
        if ":" in pattern:
            t, _, p = pattern.partition(":")
            if t.strip() and p.strip():
                tag, pattern = t.strip(), p.strip()
        line = "I (1234) m2: READY (simulated device)"
        ok = (tag in line) if tag else True
        ok = ok and (pattern in line)
        if not ok:
            raise TimeoutError("simulator: no line matching %r" % spec)
        return line, []

    def _titles(self):
        return [c["title"] for c in self.cards]

    def status(self, timeout=None, retries=0):
        self._tick()
        raw_titles = self._titles()
        # 与真机 main.c print_status 一致：空 title 在 CARDS 行打印成 `-`
        printed = [t if t else "-" for t in raw_titles]
        cards_line = None
        if printed:
            cards_line = "CARDS " + "|".join(printed)
        status_line = ("STATUS state=%s frame=%d cards=%d fps=8 heap=123456"
                       % (self.state, STATE_FRAME[self.state], len(printed)))
        return {
            "state": self.state,
            "frame": STATE_FRAME[self.state],
            "cards": len(printed),
            "fps": 8,
            "heap": 123456,
            "titles": printed,
            "status_line": status_line,
            "cards_line": cards_line,
        }

    def dump(self, path, timeout=30.0, retries=1):
        self._tick()
        cards = [(c["title"], c["project"]) for c in self.cards]
        frame = lc.synth_frame(self.state, cards)
        target = Path(path)
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(frame)
        except OSError as exc:
            raise RuntimeError("simulator: cannot write %s: %s"
                               % (target, exc)) from exc
        crc = "%08x" % (zlib.crc32(frame) & 0xFFFFFFFF)
        return {
            "path": str(target),
            "bytes": len(frame),
            "width": lc.WIDTH,
            "height": lc.HEIGHT,
            "crc32": crc,
            "device_crc32": crc,
            "crc_ok": True,
            "attempts": 1,
            "header": "FBBIN 240 240 115200 (simulated)",
            "footer": "FBEND %s (simulated)" % crc,
            "state": self.state,
            "titles": self._titles(),
        }
