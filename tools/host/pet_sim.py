#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""pet_sim.py - M2/M3 调试串口设备模拟器（无设备时端到端验证 run_g1/run_g3/run_g5/run_g6）。

实现的固件行为与 docs/08-m2-development.md + docs/10-m3-development.md（含
pet_pomo.c / pet_keymap.c / pet_menu.c）一致：

  M2：
  * 6 态状态机：1=WORKING 2=INPUT_REQUIRED 3=DONE 4=FAILED 0=AGENT_IDLE
    5=LINK_DOWN u=LINK_UP n=ANNOUNCEMENT
  * `0`：WORKING/INPUT_REQUIRED 时进入“待定完成”，1500 ms 去抖后 → DONE；
    其它态立即 → IDLE（与上游 done 下降沿去抖一致）
  * DONE 驻留 5000 ms → IDLE；15 s 无有效事件 → OFFLINE（prev 记录，u/n 恢复）
  * `c`：按 docs/08 §4 的 8 张预置卡顺序压栈（第 9 次循环）；同名（title+project）
    更新置顶；空 title **接收**（C03 §3.4 占位渲染，上游丢弃→本项目改）
  * `x`：清空卡栈，且**重置预置卡序号**（run_g3 每轮从 ALPHA/BETA/GAMMA 开始的前提）
  * `p`/`d`：由 status()/dump() 直接生成；M2 帧内容用 layout_checks.synth_frame 合成

  M3（docs/10 §4/§5；`m` 行字段与 2026-10-31 真机一致，含 sel/key1/key2/pwr）：
  * K/L/J/H 合成键事件（KEY1 短/长、KEY2 短/长），按 C07 键表 + 菜单/番茄钟状态机执行
  * `m` 打印 MODE 行；`t` 缩短本段时长（focus 6 s / break 3 s）
  * `y` 模拟跨日（has_day ? day+1 : 1）并清统计；`R` 软重启（保留 NVS 静音/统计）
  * 持久化模拟：`muted` 与 `pomo stats`（done/focus_s/day）存于 self.nvs，`R` 后恢复
  * `d` 按当前页面合成帧：MENU 优先 → POMO → （DUTY）M2 值班页

接口与 pet_serial.PetSerial 的 duck-typing 子集一致：
send / status / mode / dump / wait_for_line / flush_input / drain / reboot /
close / describe。

注意：模拟器**不是**被测固件，只用于验证 host 脚本的时序、解析、断言与证据管线；
真机证据必须用 COM7 实跑（run_*.py 不带 --simulate）。
"""

from __future__ import annotations

import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import layout_checks as lc  # noqa: E402
import layout_checks_m3 as lc3  # noqa: E402

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

# M3（docs/10 §4/§5 + pet_pomo.c/pet_keymap.c/pet_menu.c）
POMO_FOCUS_MS = 1500000
POMO_BREAK_MS = 300000
POMO_G5_FOCUS_MS = 6000
POMO_G5_BREAK_MS = 3000
MENU_ITEMS = 5
MODE_DUTY = "DUTY"
MODE_POMO = "POMO"

ALLOWED_CHARS = set("123450uncxpd") | set("KLJHmytR")


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
        # ---- M3 运行期状态 ----
        self.mode_name = MODE_DUTY
        self.menu_open = False
        self.menu_selected = 0
        self.muted = 0
        self.pomo = {}
        self._pomo_init()
        self.nvs = {"muted": 0, "stats": None}   # 模拟 NVS（R 重启后保留）
        self._logs = []                          # 等 drain() 取走的日志行

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
        self._logs.clear()

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

    # -- M3: pomodoro / menu / mute / mode --------------------------------
    def _pomo_init(self):
        now = self._now()
        self.pomo = {
            "phase": "IDLE",
            "since": now,
            "elapsed": 0,
            "total": POMO_FOCUS_MS,
            "focus_ms": POMO_FOCUS_MS,
            "break_ms": POMO_BREAK_MS,
            "done": 0,
            "focus_s": 0,
            "alarm": False,
        }
        self.has_day = False
        self.day = None

    def _pomo_start(self, phase, total_ms, now):
        self.pomo["phase"] = phase
        self.pomo["total"] = total_ms
        self.pomo["elapsed"] = 0
        self.pomo["since"] = now

    def _pomo_enter_idle(self):
        self.pomo["phase"] = "IDLE"
        self.pomo["elapsed"] = 0

    def _persist_stats(self):
        # 与 firmware 的 encode_stats 一致：day 为 int32（未设日期时是 0，不是 None）
        self.nvs["stats"] = {
            "day": 0 if self.day is None else self.day,
            "done": self.pomo["done"],
            "focus_s": self.pomo["focus_s"],
            "has_day": self.has_day,
        }

    def _persist_mute(self):
        self.nvs["muted"] = self.muted

    def _log(self, text):
        self._logs.append(text)

    def _pomo_tick(self, now):
        """与 pet_pomo.c 的 pet_pomo_tick 同逻辑（按真实 monotonic 时间推进）。"""
        p = self.pomo
        if p["phase"] not in ("FOCUS", "BREAK"):
            return
        dt = (now - p["since"]) & 0xFFFFFFFF
        if dt == 0:
            return
        if dt > 0x7FFFFFFF:  # 时间回拨（32 位有符号比较）
            p["since"] = now
            return
        p["elapsed"] += dt
        p["since"] = now
        if p["phase"] == "FOCUS" and p["elapsed"] >= p["focus_ms"]:
            p["phase"] = "FOCUS_DONE"
            p["alarm"] = True
            p["done"] += 1
            p["focus_s"] += p["focus_ms"] // 1000
            self._persist_stats()
            self._log("pomo: FOCUS done -> FOCUS_DONE (alarm logged)")
        elif p["phase"] == "BREAK" and p["elapsed"] >= p["break_ms"]:
            p["phase"] = "BREAK_DONE"
            p["alarm"] = True
            self._log("pomo: BREAK done -> BREAK_DONE (alarm logged)")

    def _pomo_command(self, cmd, now):
        p = self.pomo
        if cmd == "LONG":
            if p["phase"] != "IDLE":
                self._pomo_enter_idle()
                self._persist_stats()
                self._log("pomo: abandon -> IDLE")
            return
        phase = p["phase"]
        if phase == "IDLE":
            self._pomo_start("FOCUS", p["focus_ms"], now)
        elif phase == "FOCUS":
            p["phase"] = "FOCUS_PAUSED"
        elif phase == "FOCUS_PAUSED":
            p["phase"] = "FOCUS"
            p["since"] = now
        elif phase == "FOCUS_DONE":
            self._pomo_start("BREAK", p["break_ms"], now)
        elif phase == "BREAK":
            p["phase"] = "BREAK_PAUSED"
        elif phase == "BREAK_PAUSED":
            p["phase"] = "BREAK"
            p["since"] = now
        elif phase == "BREAK_DONE":
            self._pomo_enter_idle()

    def set_durations(self, focus_ms, break_ms):
        if focus_ms > 0:
            self.pomo["focus_ms"] = focus_ms
        if break_ms > 0:
            self.pomo["break_ms"] = break_ms
        if self.pomo["phase"] == "IDLE":
            self.pomo["total"] = self.pomo["focus_ms"]

    def set_day(self, day):
        if self.has_day and self.day == day:
            return
        self.has_day = True
        self.day = day
        self.pomo["done"] = 0
        self.pomo["focus_s"] = 0
        self._persist_stats()

    def _key_event(self, key, ev, now):
        """合成键事件（C07 §3 映射 + pet_menu.c / pomo 命令）。"""
        self._pomo_tick(now)
        if self.menu_open:
            if ev == "SHORT" and key == 1:
                self.menu_selected = (self.menu_selected + 1) % MENU_ITEMS
            elif ev == "SHORT" and key == 2:
                sel = self.menu_selected
                if sel == 0:
                    self.muted = 0 if self.muted else 1
                    self._persist_mute()
                    self._log("menu: MUTE -> %s"
                              % ("ON" if self.muted else "OFF"))
                elif sel == 1:
                    self.menu_open = False
                    self._log("menu: STATUS (read-only) -> close")
                elif sel == 2:
                    self.menu_open = False
                    self._log("menu: WIFI (M5) -> M5 not implemented")
                elif sel == 3:
                    self.menu_open = False
                    self._log("menu: BT (M5) -> M5 not implemented")
                else:
                    self.menu_open = False
                    self._log("menu: BACK -> close")
            elif ev == "LONG" and key == 2:
                self.menu_open = False
                self._log("menu: KEY2 long -> close")
            else:
                self._log("menu: KEY1 long ignored")
            return

        if self.mode_name == MODE_DUTY:
            if key == 1 and ev == "SHORT":
                self._log("duty: KEY1 short -> SEND (M3 stub, M5 接入)")
            elif key == 1 and ev == "LONG":
                self.muted = 0 if self.muted else 1
                self._persist_mute()
                self._log("duty: mute -> %s"
                          % ("ON" if self.muted else "OFF"))
            elif key == 2 and ev == "SHORT":
                self.mode_name = MODE_POMO
                self._persist_stats()
                self._log("mode: DUTY -> POMO")
            elif key == 2 and ev == "LONG":
                self.menu_open = True
                self.menu_selected = 0
                self._log("menu: opened (sel=0)")
            return

        # POMO
        if key == 1:
            self._pomo_command(ev, now)
        elif key == 2 and ev == "SHORT":
            self.mode_name = MODE_DUTY
            self._persist_stats()
            self._log("mode: POMO -> DUTY (stats saved)")
        elif key == 2 and ev == "LONG":
            self.menu_open = True
            self.menu_selected = 0
            self._log("menu: opened in POMO (sel=0)")

    def _mode_state(self):
        self._pomo_tick(self._now())
        p = self.pomo
        remain = 0
        if p["elapsed"] < p["total"]:
            remain = p["total"] - p["elapsed"]
        return {
            "mode": self.mode_name,
            "menu": 1 if self.menu_open else 0,
            "phase": p["phase"],
            "remain_ms": remain,
            "total_ms": p["total"],
            "done": p["done"],
            "focus_s": p["focus_s"],
            "muted": self.muted,
            "day": self.day,
            "has_day": 1 if self.has_day else 0,
            "sel": self.menu_selected,
            "key1": 0,
            "key2": 0,
            "pwr": 0,
            "fields": {},
            "raw": "",
        }

    def mode(self, timeout=None, retries=0):
        st = self._mode_state()
        st["raw"] = (
            "MODE mode=%s menu=%d sel=%d phase=%s remain_ms=%d total_ms=%d "
            "done=%d focus_s=%d muted=%d day=%s has_day=%d key1=0 key2=0 pwr=0"
            % (st["mode"], st["menu"], st["sel"], st["phase"],
               st["remain_ms"], st["total_ms"], st["done"], st["focus_s"],
               st["muted"], "-" if st["day"] is None else st["day"],
               st["has_day"]))
        return st

    def drain(self, duration=0.3):
        lines = list(self._logs)
        self._logs.clear()
        return lines

    def reboot(self, ready_spec="READY", wait_timeout=20.0, settle=0.0,
               max_open_attempts=1):
        """模拟 `R` 软重启：运行期状态清空，NVS（静音/统计）恢复。"""
        now = self._now()
        # M2 运行期状态复位
        self.state = "IDLE"
        self.prev_state = "IDLE"
        self.since_ms = now
        self.last_valid_ms = now
        self.pending_done_at = 0
        self.cards = []
        self.preset_index = 0
        # M3 运行期状态复位
        self.mode_name = MODE_DUTY
        self.menu_open = False
        self.menu_selected = 0
        self.muted = self.nvs["muted"]
        self._pomo_init()
        stats = self.nvs["stats"]
        if stats:
            self.has_day = True
            self.day = stats["day"]
            self.pomo["done"] = stats["done"]
            self.pomo["focus_s"] = stats["focus_s"]
        line = "I (1234) m3: READY (simulated reboot, NVS restored)"
        return {"command": "R", "ready_spec": ready_spec,
                "ready_line": line, "ready_caught": True, "skipped": [],
                "open_attempts": 1, "reopened": True}

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
            elif ch == "K":
                self._key_event(1, "SHORT", self._now())
            elif ch == "L":
                self._key_event(1, "LONG", self._now())
            elif ch == "J":
                self._key_event(2, "SHORT", self._now())
            elif ch == "H":
                self._key_event(2, "LONG", self._now())
            elif ch == "m":
                pass  # 由 mode() 生成响应
            elif ch == "t":
                self.set_durations(POMO_G5_FOCUS_MS, POMO_G5_BREAK_MS)
                self._log("pomo: durations -> focus %d ms / break %d ms"
                          % (POMO_G5_FOCUS_MS, POMO_G5_BREAK_MS))
            elif ch == "y":
                self.set_day((self.day + 1) if self.has_day else 1)
                self._log("pomo: cross-day simulated (day=%s, stats cleared)"
                          % self.day)
            elif ch == "R":
                pass  # 由 reboot() 执行（真机由 esp_restart 执行）
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
        self._pomo_tick(self._now())
        page = "duty"
        if self.menu_open:
            page = "menu"
            st = self._mode_state()
            frame = lc3.synth_menu_frame(
                selected=self.menu_selected, muted=self.muted,
                mode=self.mode_name, done=st["done"], focus_s=st["focus_s"])
        elif self.mode_name == MODE_POMO:
            page = "pomo"
            st = self._mode_state()
            frame = lc3.synth_pomo_frame(
                phase=st["phase"], remain_ms=st["remain_ms"],
                total_ms=st["total_ms"], done=st["done"],
                focus_s=st["focus_s"], muted=st["muted"])
        else:
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
            "page": page,
        }
