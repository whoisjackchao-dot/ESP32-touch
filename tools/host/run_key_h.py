#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""run_key_h.py - H 级真机按键人工测试引导脚本（KEY-01/02/04/05）。

设备由**人**按键，脚本逐项打印提示、轮询 `m` 的原始状态字段（真机 2026-10-31
追加的 `key1/key2/pwr`，0/1）检测按下/松开，再用 `m` 的状态字段判定动作是否符合
`docs/contracts/C07-keymap.md` §3/§4。

计划（`--plan` 可多选，如 `--plan short long menu pwr`）：

  short  值班页 KEY1 短（SEND 只记日志，无状态变化）
         值班页 KEY2 短（DUTY→POMO）→ POMO KEY2 短（→DUTY）
  long   值班 KEY1 长（muted 翻转）；值班 KEY2 长（开菜单）；菜单内 KEY2 长（关闭）
  menu   长按 KEY2 开菜单 → 短按 KEY1 ×4（选中 1→2→3→4）→ 短按 KEY2 执行 BACK 关闭
  pwr    短按 PWR ×N（应无应用动作）；随后长按 PWR ≥1.5 s 进入关机流程
         （USB 供电不会真断电；背光灭/串口无响应为进入关机的证据）→ 提示复位，
         脚本等 READY 后确认恢复

每项默认 `--count 5` 次（验收口径 KEY-01/02 是 20 次：`--count 20`）。
运行中可按 `s` 跳过当前次、`q` 退出（已测结果会写盘）。

离线自测（不碰串口，不需要人手）：

    python run_key_h.py --selftest

用脚本化"假按键"驱动四个计划（含 PWR 关机→复位恢复），验证提示/判定/证据管线；
**不能**替代真人按键的 H 级结论。

用法::

    C:\\Python314\\python.exe run_key_h.py --plan short --count 5
    C:\\Python314\\python.exe run_key_h.py --plan short long menu pwr --count 20
    C:\\Python314\\python.exe run_key_h.py --plan pwr --count 5
    C:\\Python314\\python.exe run_key_h.py --selftest            # 离线自测

注意：
  * 值班页是唯一彩色页面；设备空闲 15 s 会进 OFFLINE，本脚本轮询 `m` 不等于
    有效事件，故同样带 `n` 心跳（节流，不干扰人工按键）。
  * 短按建议按住 0.2–0.4 s（<600 ms）；长按建议按住 0.8–1.2 s（≥600 ms）。
  * `PWR` 长按会走进关机流程：背光灭、GPIO2 低；USB 供电**不会真断电**，
    之后按设备 RST（或拔插 USB）复位，脚本会重新等 READY。
  * 真机 `m` 必须包含 key1/key2/pwr 原始按下字段（Lead 2026-10-31 追加）；
    缺失时脚本直接报错并说明原因（不猜测按键）。

退出码：0 PASS；1 FAIL/未完成；2 设备/端口打不开。
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from m3_common import (  # noqa: E402
    MODE_DUTY, Runner, log, now_iso, open_device, write_json, write_text)
from pet_serial import (  # noqa: E402
    DEFAULT_BAUD, DEFAULT_PORT, PetSerialError, SerialOpenError,
    SerialTimeout)

try:
    import msvcrt  # Windows 控制台无缓冲按键（仅用于 s/q 控制）
except ImportError:  # pragma: no cover - 非 Windows 环境
    msvcrt = None

PLANS = ("short", "long", "menu", "pwr")

# `--selftest` 时由脚本化"假按键"接管 wait_press/wait_press_start（见 _selftest）
_SELFTEST_HUMAN = None


class QuitRequested(Exception):
    pass


class ProtocolError(Exception):
    pass


# --------------------------------------------------------------------------
# console helper
# --------------------------------------------------------------------------

class ConsoleKeys:
    def poll(self):
        if msvcrt is None:
            return None
        try:
            if msvcrt.kbhit():
                ch = msvcrt.getwch()
                if ch in ("\x00", "\xe0"):  # 功能键前缀，吞掉第二字节
                    msvcrt.getwch()
                    return None
                return ch.lower()
        except (OSError, ValueError):  # 无控制台（非交互运行）
            return None
        return None


def prompt(text):
    print("")
    print("  " + "-" * 74)
    print("  " + text)
    print("  -" * 37)
    print("  （在设备上按键；s=跳过本次，q=退出）", flush=True)


# --------------------------------------------------------------------------
# device press detection
# --------------------------------------------------------------------------

def _state_brief(st):
    if not st:
        return "-"
    return ("mode=%s menu=%d sel=%s phase=%s muted=%d done=%d"
            % (st.get("mode"), st.get("menu", 0), st.get("sel"),
               st.get("phase"), st.get("muted", -1), st.get("done", -1)))


def _require_key_fields(st):
    missing = [k for k in ("key1", "key2", "pwr") if st.get(k) is None]
    if missing:
        raise ProtocolError(
            "`m` 行缺少原始按键字段 %s（真机需要 Lead 2026-10-31 追加的 "
            "key1/key2/pwr）；不猜测按键，请先更新固件。" % ", ".join(missing))


def wait_press(r, key, press_timeout, keys, poll=0.05):
    """轮询 `m` 等 key 原始状态 0→1→0；返回记录 dict。

    ``key``: "key1" / "key2" / "pwr"。检测到按下后打印提示。
    `--selftest` 时由 ``_SELFTEST_HUMAN`` 直接给出记录（不轮询真实设备）。
    """
    if _SELFTEST_HUMAN is not None:
        return _SELFTEST_HUMAN.fake_press(key, start_only=False)
    deadline = time.monotonic() + float(press_timeout)
    pressed = False
    t_press = None
    st = None
    while time.monotonic() < deadline:
        ch = keys.poll()
        if ch == "q":
            raise QuitRequested()
        if ch == "s":
            return {"skipped": True}
        try:
            st = r.mode()
        except SerialTimeout:
            if pressed:
                return {"skipped": False, "press": True, "release": False,
                        "link_lost": True,
                        "hold_ms": int((time.monotonic() - t_press) * 1000),
                        "state": None}
            raise
        _require_key_fields(st)
        val = st.get(key)
        if val and not pressed:
            pressed = True
            t_press = time.monotonic()
            print("    检测到 %s 按下（请松开）…" % key, flush=True)
        elif pressed and not val:
            hold = int((time.monotonic() - t_press) * 1000)
            print("    检测到 %s 松开（按住约 %d ms）" % (key, hold),
                  flush=True)
            return {"skipped": False, "press": True, "release": True,
                    "hold_ms": hold, "state": st}
        time.sleep(poll)
    return {"skipped": False, "press": pressed, "release": False,
            "timeout": True, "state": st}


def wait_press_start(r, key, press_timeout, keys, poll=0.05):
    """只等按下沿（pwr 长按用）：一旦检测到 raw=1 立即返回，不再发 `m`。

    这样按下之后设备打的关机日志不会被 `m` 的读取循环 flush/丢弃。
    """
    if _SELFTEST_HUMAN is not None:
        return _SELFTEST_HUMAN.fake_press(key, start_only=True)
    deadline = time.monotonic() + float(press_timeout)
    st = None
    while time.monotonic() < deadline:
        ch = keys.poll()
        if ch == "q":
            raise QuitRequested()
        if ch == "s":
            return {"skipped": True}
        st = r.mode()
        _require_key_fields(st)
        if st.get(key):
            print("    检测到 %s 按下（请继续按住/按要求操作）…" % key,
                  flush=True)
            return {"skipped": False, "press": True, "release": False,
                    "state": st}
        time.sleep(poll)
    return {"skipped": False, "press": False, "release": False,
            "timeout": True, "state": st}


# --------------------------------------------------------------------------
# check helpers
# --------------------------------------------------------------------------

def _new_step(ev, plan, step, ok, detail, before=None, after=None,
              press=None):
    row = {
        "plan": plan, "step": step, "ok": bool(ok), "detail": detail,
        "time": now_iso(),
        "before": before, "after": after, "press": press,
    }
    ev["rows"].append(row)
    print("    => %s | %s" % ("PASS" if ok else "**FAIL**", detail),
          flush=True)
    return ok


def _check_no_change(plan, label, before, after, ev, press):
    keep = ("mode", "menu", "phase", "muted", "done", "focus_s")
    diffs = ["%s %s->%s" % (k, before.get(k), after.get(k))
             for k in keep if before.get(k) != after.get(k)]
    ok = not diffs
    detail = ("无应用动作/状态变化：OK" if ok
              else "出现未预期变化：" + ", ".join(diffs))
    return _new_step(ev, plan, label, ok, detail, before, after, press)


def _check_equals(plan, label, before, after, ev, press, expect):
    diffs = ["%s=%s(期望 %s)" % (k, after.get(k), v)
             for k, v in expect.items() if after.get(k) != v]
    ok = not diffs
    detail = ("符合预期：%s" % ", ".join("%s=%s" % (k, after.get(k))
                                         for k in expect)
              if ok else "不符合：" + "; ".join(diffs))
    return _new_step(ev, plan, label, ok, detail, before, after, press)


# --------------------------------------------------------------------------
# plans
# --------------------------------------------------------------------------

def plan_short(r, ev, args, keys):
    for i in range(1, args.count + 1):
        log("[short %d/%d] 值班页两键短按" % (i, args.count))
        st = r.ensure_duty()
        prompt("① 短按 KEY1（按住 0.2–0.4 s 松开；值班页应只记 SEND 日志）")
        rec = wait_press(r, "key1", args.press_timeout, keys)
        if rec.get("skipped"):
            _new_step(ev, "short", "iter%d.key1_short" % i, False, "用户跳过",
                      after=st, press=rec)
            continue
        if rec.get("timeout") or not rec.get("press"):
            _new_step(ev, "short", "iter%d.key1_short" % i, False,
                      "未检测到 KEY1 按下（超时/无原始状态）", after=st,
                      press=rec)
            continue
        after = r.mode()
        _check_no_change("short", "iter%d.key1_short_effect" % i, st, after,
                         ev, rec)
        before = after
        prompt("② 短按 KEY2（值班页 → 应切到番茄钟 POMO）")
        rec = wait_press(r, "key2", args.press_timeout, keys)
        if rec.get("skipped"):
            _new_step(ev, "short", "iter%d.key2_short_to_pomo" % i, False,
                      "用户跳过", after=before, press=rec)
            r.ensure_duty()
            continue
        after = r.mode()
        _check_equals("short", "iter%d.key2_short_to_pomo" % i, before, after,
                      ev, rec, {"mode": "POMO", "menu": 0})
        before = after
        prompt("③ 短按 KEY2（番茄钟页 → 应切回值班 DUTY）")
        rec = wait_press(r, "key2", args.press_timeout, keys)
        if rec.get("skipped"):
            _new_step(ev, "short", "iter%d.key2_short_to_duty" % i, False,
                      "用户跳过", after=before, press=rec)
            r.ensure_duty()
            continue
        after = r.mode()
        _check_equals("short", "iter%d.key2_short_to_duty" % i, before, after,
                      ev, rec, {"mode": "DUTY", "menu": 0})
        r.ensure_duty()


def plan_long(r, ev, args, keys):
    for i in range(1, args.count + 1):
        log("[long %d/%d] 两键长按 + 菜单关闭" % (i, args.count))
        before = r.ensure_duty()
        prompt("① 长按 KEY1 0.8–1.2 s（值班页 → 静音应翻转 muted 0↔1）")
        rec = wait_press(r, "key1", args.press_timeout, keys)
        if rec.get("skipped") or not rec.get("press"):
            _new_step(ev, "long", "iter%d.key1_long_mute" % i, False,
                      "跳过/未检测到按键", after=before, press=rec)
            continue
        after = r.mode()
        expect_mute = 0 if before["muted"] else 1
        _check_equals("long", "iter%d.key1_long_mute" % i, before, after, ev,
                      rec, {"muted": expect_mute, "mode": "DUTY", "menu": 0})
        before = after
        prompt("② 长按 KEY2 0.8–1.2 s（值班页 → 应打开菜单 menu=1）")
        rec = wait_press(r, "key2", args.press_timeout, keys)
        if rec.get("skipped") or not rec.get("press"):
            _new_step(ev, "long", "iter%d.key2_long_open_menu" % i, False,
                      "跳过/未检测到按键", after=before, press=rec)
            r.ensure_duty()
            continue
        after = r.mode()
        _check_equals("long", "iter%d.key2_long_open_menu" % i, before, after,
                      ev, rec, {"menu": 1})
        before = after
        prompt("③ 在菜单内长按 KEY2 0.8–1.2 s（应关闭菜单 menu=0）")
        rec = wait_press(r, "key2", args.press_timeout, keys)
        if rec.get("skipped") or not rec.get("press"):
            _new_step(ev, "long", "iter%d.key2_long_close_menu" % i, False,
                      "跳过/未检测到按键", after=before, press=rec)
            r.ensure_duty()
            continue
        after = r.mode()
        _check_equals("long", "iter%d.key2_long_close_menu" % i, before,
                      after, ev, rec, {"menu": 0})
        r.ensure_duty()


def plan_menu(r, ev, args, keys):
    for i in range(1, args.count + 1):
        log("[menu %d/%d] 打开菜单 → 选中 1→2→3→4 → 执行 BACK" % (i, args.count))
        before = r.ensure_duty()
        prompt("① 长按 KEY2 0.8–1.2 s（值班页 → 应打开菜单，选中 MUTE）")
        rec = wait_press(r, "key2", args.press_timeout, keys)
        if rec.get("skipped") or not rec.get("press"):
            _new_step(ev, "menu", "iter%d.open" % i, False,
                      "跳过/未检测到按键", after=before, press=rec)
            r.ensure_duty()
            continue
        after = r.mode()
        ok = _check_equals("menu", "iter%d.open" % i, before, after, ev, rec,
                           {"menu": 1, "sel": 0})
        if not ok:
            r.ensure_duty()
            continue
        before = after
        for target in (1, 2, 3, 4):
            prompt("② 短按 KEY1（按住 0.2–0.4 s；选中行应变为 %d）" % target)
            rec = wait_press(r, "key1", args.press_timeout, keys)
            if rec.get("skipped") or not rec.get("press"):
                _new_step(ev, "menu", "iter%d.sel%d" % (i, target), False,
                          "跳过/未检测到按键", after=before, press=rec)
                break
            after = r.mode()
            ok = _check_equals("menu", "iter%d.sel%d" % (i, target), before,
                               after, ev, rec,
                               {"menu": 1, "sel": target})
            before = after
            if not ok:
                break
        if not (before.get("menu") == 1 and before.get("sel") == 4):
            r.ensure_duty()
            continue
        prompt("③ 短按 KEY2（选中 BACK → 应关闭菜单 menu=0）")
        rec = wait_press(r, "key2", args.press_timeout, keys)
        if rec.get("skipped") or not rec.get("press"):
            _new_step(ev, "menu", "iter%d.exec_back" % i, False,
                      "跳过/未检测到按键", after=before, press=rec)
            r.ensure_duty()
            continue
        after = r.mode()
        _check_equals("menu", "iter%d.exec_back" % i, before, after, ev, rec,
                      {"menu": 0, "mode": "DUTY"})
        r.ensure_duty()


def plan_pwr(r, ev, args, keys):
    # ---- 短按：应无应用动作 ----
    for i in range(1, args.count + 1):
        log("[pwr %d/%d] PWR 短按（应无应用动作）" % (i, args.count))
        before = r.ensure_duty()
        prompt("① 短按 PWR（<0.5 s；应无任何应用动作）")
        rec = wait_press(r, "pwr", args.press_timeout, keys)
        if rec.get("skipped") or not rec.get("press"):
            _new_step(ev, "pwr", "iter%d.short" % i, False,
                      "跳过/未检测到按键", after=before, press=rec)
            continue
        if rec.get("link_lost"):
            _new_step(ev, "pwr", "iter%d.short" % i, False,
                      "PWR 短按期间链路丢失（可能误触发关机）；请复位后重跑",
                      after=before, press=rec)
            _recover_after_shutdown(r, ev, args)
            continue
        try:
            after = r.mode()
        except SerialTimeout:
            _new_step(ev, "pwr", "iter%d.short" % i, False,
                      "PWR 短按后串口无响应（不应发生）", after=before,
                      press=rec)
            _recover_after_shutdown(r, ev, args)
            continue
        _check_no_change("pwr", "iter%d.short_no_action" % i, before, after,
                         ev, rec)

    # ---- 长按：进入关机流程 ----
    log("[pwr] 长按 PWR ≥1.5 s（关机流程；USB 供电不会真断电）")
    before = r.ensure_duty()
    prompt("② 长按 PWR ≥1.5 s 再松开（背光应灭、设备进入关机流程）")
    rec = wait_press_start(r, "pwr", args.press_timeout, keys)
    if rec.get("skipped"):
        _new_step(ev, "pwr", "long_shutdown", False, "用户跳过", after=before,
                  press=rec)
        return
    if not rec.get("press"):
        _new_step(ev, "pwr", "long_shutdown", False,
                  "未检测到 PWR 按下", after=before, press=rec)
        return
    # 已检测到按下：不再发 `m`（避免把关机日志 flush 掉），只收串口日志
    logs = []
    deadline = time.monotonic() + args.shutdown_window
    while time.monotonic() < deadline:
        chunk = r.drain(0.2)
        if chunk:
            logs.extend(chunk)
        print("    等待关机流程… %d 行日志" % len(logs), end="\r", flush=True)
    print("")
    markers = ("pwr", "power", "shutdown", "关机", "off")
    log_hit = [ln for ln in logs
               if any(m in ln.lower() for m in markers)]
    # 关机后再确认设备是否还能应答 `m`（进入关机流程后应无响应）
    try:
        r.dev.flush_input()
        r.dev.mode(timeout=1.5, retries=0)
        responds = True
    except (SerialTimeout, PetSerialError):
        responds = False
    ok = bool(log_hit) or (not responds)
    detail = ("关机证据：日志匹配=%s；`m` 无响应=%s"
              % (log_hit[:2] if log_hit else "无", not responds))
    _new_step(ev, "pwr", "long_shutdown", ok, detail, before, None, rec)
    ev["pwr_shutdown"] = {"press": rec, "log_lines": logs[:20],
                          "log_hit": log_hit, "responds_after": responds}
    if not ok:
        print("    !! 未取得关机证据：设备仍响应且无相关日志；不猜测，记为 FAIL")
        r.ensure_duty()
        return
    _recover_after_shutdown(r, ev, args)


def _recover_after_shutdown(r, ev, args):
    """提示用户复位后等待 READY 并确认恢复。"""
    prompt("③ 设备已进关机流程。请按设备 RST 键（或拔插 USB）复位；"
           "脚本将等待启动 READY 并确认恢复")
    try:
        line, _skipped = r.dev.wait_for_line("READY", args.recover_timeout)
        r._last_hb = time.monotonic()
        st = r.mode()
        ok2 = st["mode"] == "DUTY" and st["menu"] == 0
        _new_step(ev, "pwr", "recover_after_shutdown", ok2,
                  "复位后 READY：%s；m：%s"
                  % (line.strip(), _state_brief(st)), after=st)
    except (SerialTimeout, PetSerialError) as exc:
        _new_step(ev, "pwr", "recover_after_shutdown", False,
                  "复位后未恢复：%s" % exc)


PLAN_FUNCS = {
    "short": plan_short,
    "long": plan_long,
    "menu": plan_menu,
    "pwr": plan_pwr,
}


# --------------------------------------------------------------------------
# offline self-test：脚本化"假按键"（不碰串口）
# --------------------------------------------------------------------------

class _SelftestSim:
    """pet_sim.PetSim + PWR 关机/复位语义（仅 --selftest 用）。"""

    def __init__(self):
        from pet_sim import PetSim
        self._sim = PetSim()
        self.shutdown = False

    # -- duck-typed device API -------------------------------------------
    def open(self):
        return self._sim.open()

    def close(self):
        return self._sim.close()

    def describe(self):
        return {"port": "SELFTEST", "baud": 115200, "mode": "selftest"}

    def send(self, data):
        return self._sim.send(data)

    def flush_input(self):
        return self._sim.flush_input()

    def drain(self, duration=0.3):
        if self.shutdown:
            return ["I (9999) m3: PWR long -> shutdown (simulated)"]
        return self._sim.drain(duration)

    def mode(self, timeout=None, retries=0):
        if self.shutdown:
            raise SerialTimeout("selftest: device in shutdown (no MODE)")
        return self._sim.mode(timeout, retries)

    def status(self, timeout=None, retries=0):
        if self.shutdown:
            raise SerialTimeout("selftest: device in shutdown (no STATUS)")
        return self._sim.status(timeout, retries)

    def wait_for_line(self, spec, timeout=None):
        if self.shutdown:
            self.shutdown = False
            self._sim.reboot()   # 模拟用户按 RST 后重新启动
            return "I (1) m2: READY (simulated reset after shutdown)", []
        return self._sim.wait_for_line(spec, timeout)

    def reboot(self, *a, **kw):
        self.shutdown = False
        return self._sim.reboot(*a, **kw)


class _FakeHuman:
    """按脚本模拟人手按键：`press()` 时对模拟器施加对应合成键事件。"""

    def __init__(self, script, sim, negate=False):
        self.script = list(script)
        self.sim = sim
        self.negate = negate
        self.applied = []

    def fake_press(self, key, start_only=False):
        if not self.script:
            raise RuntimeError("selftest: no scripted press left for %r" % key)
        item = self.script.pop(0)
        if not self.negate and item["key"] != key:
            raise RuntimeError("selftest: scripted %r but plan asked %r"
                               % (item["key"], key))
        self.applied.append((key, item))
        if item.get("pwr_long"):
            self.sim.shutdown = True
            return {"skipped": False, "press": True, "release": False,
                    "state": None}
        # 应用键：short/long 对应的合成键命令；negate 时故意按下"错"的键
        if key == "key1":
            ch = "K" if item.get("hold", "short") == "short" else "L"
        elif key == "key2":
            ch = "J" if item.get("hold", "short") == "short" else "H"
        else:  # pwr 短按：无应用动作
            ch = None
        if self.negate:
            ch = {"key1": "J", "key2": "K", "pwr": None}[key]
        if ch:
            self.sim.send(ch)
        return {"skipped": False, "press": True, "release": True,
                "hold_ms": 900 if item.get("hold") == "long" else 300,
                "state": self.sim.mode()}


def _selftest():
    import argparse as _argparse
    import tempfile

    global _SELFTEST_HUMAN
    scripts = {
        "short": [{"key": "key1"}, {"key": "key2"}, {"key": "key2"}],
        "long": [{"key": "key1", "hold": "long"},
                 {"key": "key2", "hold": "long"},
                 {"key": "key2", "hold": "long"}],
        "menu": ([{"key": "key2", "hold": "long"}]
                 + [{"key": "key1"}] * 4 + [{"key": "key2"}]),
        "pwr": [{"key": "pwr"}, {"key": "pwr", "pwr_long": True}],
    }
    results = []

    def run_case(plan, negate, expect_pass):
        global _SELFTEST_HUMAN
        sim = _SelftestSim()
        sim.open()
        human = _FakeHuman(scripts[plan], sim, negate=negate)
        _SELFTEST_HUMAN = human
        outdir = Path(tempfile.mkdtemp(prefix="key_h_selftest_"))
        args = _argparse.Namespace(
            plan=[plan], count=1, press_timeout=1.0, shutdown_window=0.05,
            recover_timeout=2.0, ready_timeout=2.0, wait_ready=None,
            verbose=False, simulate=False, outdir=str(outdir))
        try:
            ev = run(Runner(sim, outdir, wait=0.02), args, outdir)
        finally:
            _SELFTEST_HUMAN = None
            sim.close()
        ok = (ev["result"] == "PASS") == expect_pass and not human.script
        results.append({"case": "%s%s" % (plan, "_neg" if negate else ""),
                        "result": ev["result"], "expect_pass": expect_pass,
                        "match": ok, "checks": len(ev["rows"])})
        print("%-14s result=%-4s expect=%-4s %s"
              % (results[-1]["case"], ev["result"],
                 "PASS" if expect_pass else "FAIL",
                 "OK" if ok else "MISMATCH"), flush=True)
        return ok

    print("key_h selftest: 脚本化假按键（不碰串口）")
    ok = True
    for plan in PLANS:
        ok = run_case(plan, negate=False, expect_pass=True) and ok
    ok = run_case("short", negate=True, expect_pass=False) and ok
    passed = sum(1 for r in results if r["match"])
    print("SELFTEST %s %d/%d cases"
          % ("PASS" if ok else "FAIL", passed, len(results)))
    return 0 if ok else 1


# --------------------------------------------------------------------------
# summary
# --------------------------------------------------------------------------

def summary_markdown(ev):
    out = ["# H 级真机按键人工测试（run_key_h.py）摘要", ""]
    out.append("- 时间：%s → %s" % (ev["started"], ev["finished"]))
    out.append("- 模式：%s（%s @ %s）；计划：%s；每计划次数：%d"
               % (ev["mode"], ev["port"], ev["baud"],
                  ", ".join(ev["plans"]), ev["count"]))
    out.append("- **结论：%s** — 检查项 %d，PASS %d，FAIL %d，跳过 %d"
               % (ev["result"], ev["summary"]["checks_total"],
                  ev["summary"]["checks_pass"], ev["summary"]["checks_fail"],
                  ev["summary"]["checks_skipped"]))
    out.append("- 说明：本脚本由人按键、脚本判定；HE 目视结论仍需用户确认。"
               "验收口径 KEY-01/02 为每项 20 次（`--count 20`）。")
    for plan in ev["plans"]:
        rows = [row for row in ev["rows"] if row["plan"] == plan]
        if not rows:
            continue
        out += ["", "## 计划 `%s`（%d 条检查）" % (plan, len(rows)), "",
                "| 步骤 | 判定 | 说明 |", "| --- | --- | --- |"]
        for row in rows:
            out.append("| `%s` | %s | %s |"
                       % (row["step"],
                          "PASS" if row["ok"] else "**FAIL**",
                          str(row["detail"]).replace("|", "\\|")))
    if ev.get("pwr_shutdown"):
        out += ["", "## PWR 长按关机证据", ""]
        ps = ev["pwr_shutdown"]
        out.append("- 按下记录：%s" % ps.get("press"))
        out.append("- 串口日志（前 20 行）：%s"
                   % ("; ".join(ps.get("log_lines", [])) or "无"))
        out.append("- 关机后仍响应 `m`：%s" % ps.get("responds_after"))
    fails = [row for row in ev["rows"] if not row["ok"]]
    if fails:
        out += ["", "## 失败明细", ""]
        for row in fails:
            out.append("- `%s`：%s" % (row["step"], row["detail"]))
    out += ["", "## 备注", ""]
    for note in ev.get("notes", []):
        out.append("- " + note)
    out.append("")
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def run(r, args, outdir):
    ev = {
        "tool": "run_key_h.py",
        "version": 1,
        "started": now_iso(),
        "finished": None,
        "mode": r.describe().get("mode", "serial"),
        "port": r.describe().get("port"),
        "baud": r.describe().get("baud"),
        "plans": list(args.plan),
        "count": args.count,
        "press_timeout": args.press_timeout,
        "rows": [],
        "pwr_shutdown": None,
        "notes": [
            "PWR 长按进入关机流程：USB 供电不会真断电（背光灭/串口无响应为证据），"
            "需按 RST 或拔插 USB 复位。",
            "短按建议 0.2–0.4 s；长按建议 0.8–1.2 s（长按阈值 600 ms）。",
            "设备空闲 15 s 进 OFFLINE（M2 契约）；轮询期间 `m` 不刷新链路，"
            "故 Runner 带 `n` 心跳（节流）。",
        ],
        "summary": {},
        "result": "FAIL",
        "error": None,
    }
    keys = ConsoleKeys()
    quit_flag = False
    try:
        if args.wait_ready:
            line, _skipped = r.dev.wait_for_line(args.wait_ready,
                                                 args.ready_timeout)
            ev["notes"].append("启动 READY：%s" % line.strip())
            log("preflight: READY -> %s" % line)
        st = r.ensure_duty()
        ev["notes"].append("起始状态：%s" % _state_brief(st))
        for plan in args.plan:
            log("")
            log("===== 计划 %s =====" % plan.upper())
            PLAN_FUNCS[plan](r, ev, args, keys)
    except QuitRequested:
        quit_flag = True
        ev["error"] = "用户按 q 提前退出（已测结果保留）"
        log("用户退出：已测结果写盘")
    except ProtocolError as exc:
        ev["error"] = str(exc)
        log("ERROR: %s" % exc)
    except (SerialTimeout, PetSerialError, OSError) as exc:
        ev["error"] = "%s: %s" % (type(exc).__name__, exc)
        log("ERROR: %s" % ev["error"])
    finally:
        rows = ev["rows"]
        skipped = sum(1 for row in rows if "跳过" in str(row.get("detail")))
        fails = [row for row in rows if not row["ok"]]
        known = [row for row in rows if "跳过" not in str(row.get("detail"))]
        ev["summary"] = {
            "checks_total": len(rows),
            "checks_pass": len(known) - len(fails),
            "checks_fail": len(fails),
            "checks_skipped": skipped,
        }
        ok = (not quit_flag and not fails
              and ev["error"] is None and len(rows) > 0)
        ev["result"] = "PASS" if ok else "FAIL"
        ev["finished"] = now_iso()
        outdir.mkdir(parents=True, exist_ok=True)
        write_json(outdir / "key_h.json", ev)
        write_text(outdir / "key_h_summary.md", summary_markdown(ev))
        log("")
        log("summary: checks %d, PASS %d, FAIL %d, skipped %d, result=%s"
            % (len(rows), ev["summary"]["checks_pass"], len(fails), skipped,
               ev["result"]))
        log("evidence: %s" % (outdir / "key_h.json"))
        log("summary : %s" % (outdir / "key_h_summary.md"))
    return ev


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    ap = argparse.ArgumentParser(
        prog="run_key_h.py",
        description="H 级真机按键人工测试引导（KEY-01/02/04/05）")
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    ap.add_argument("--timeout", type=float, default=3.0)
    ap.add_argument("--plan", nargs="+", choices=PLANS, default=["short"],
                    help="计划：short / long / menu / pwr（可多选）")
    ap.add_argument("--count", type=int, default=5,
                    help="每计划次数（验收用 20；默认 %(default)s）")
    ap.add_argument("--press-timeout", type=float, default=30.0,
                    help="每次等待用户按键的超时秒数（默认 %(default)s）")
    ap.add_argument("--shutdown-window", type=float, default=3.0,
                    help="PWR 长按后收关机证据的窗口秒数（默认 %(default)s）")
    ap.add_argument("--recover-timeout", type=float, default=60.0,
                    help="关机后等用户复位/READY 的超时秒数（默认 %(default)s）")
    ap.add_argument("--selftest", action="store_true",
                    help="离线自测：脚本化假按键跑四个计划（不碰串口/不需要人手）")
    ap.add_argument("--outdir", default=None, help="证据目录（默认 evidence/）")
    ap.add_argument("--wait-ready", nargs="?", const="READY", default=None,
                    metavar="SPEC", help="先等启动日志（默认 READY）")
    ap.add_argument("--ready-timeout", type=float, default=20.0)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        return _selftest()

    if args.outdir:
        outdir = Path(args.outdir)
    else:
        outdir = HERE / "evidence"
    args.simulate = False

    try:
        dev = open_device(args)
    except SerialOpenError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 2
    try:
        runner = Runner(dev, outdir, wait=0.05, heartbeat=3.0,
                        verbose=args.verbose)
        ev = run(runner, args, outdir)
    finally:
        dev.close()
    return 0 if ev["result"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
