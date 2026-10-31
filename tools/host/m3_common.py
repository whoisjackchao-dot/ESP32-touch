#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""m3_common.py - M3 host 跑测公共工具（run_g5 / run_g6 / run_key_h 共用）。

封装：
  * 打开设备（真机 PetSerial / 无设备 PetSim `--simulate`）
  * `m` 行读取 + 周期性 `n` 心跳（M2 契约：设备 15 s 无有效事件会进 OFFLINE，
    长等待/轮询期间必须保活，否则值班页/日志会被 OFFLINE 状态干扰）
  * 合成键（K/L/J/H）、模式/相位归位、相位等待
  * 导出帧 → PNG → layout_checks_m3（番茄钟/菜单页）或 layout_checks（值班页）
  * 证据写盘小工具（JSON / Markdown，原子替换）

仅使用标准库 + pyserial（由 pet_serial 使用）。
"""

from __future__ import annotations

import hashlib
import json
import sys
import time
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import fb  # noqa: E402
import layout_checks as lc  # noqa: E402
import layout_checks_m3 as lc3  # noqa: E402
from pet_serial import (  # noqa: E402
    DEFAULT_BAUD, DEFAULT_PORT, FrameDumpError, PetSerial, PetSerialError,
    SerialOpenError, SerialTimeout)

PHASES = lc3.PHASES
MODE_DUTY = "DUTY"
MODE_POMO = "POMO"


# --------------------------------------------------------------------------
# misc helpers
# --------------------------------------------------------------------------

def now_iso():
    return datetime.now().astimezone().isoformat(timespec="milliseconds")


def log(msg=""):
    print(msg, flush=True)


def write_text(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    tmp.replace(path)


def write_json(path, obj):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(obj, ensure_ascii=False, indent=2),
                   encoding="utf-8")
    tmp.replace(path)


def open_device(args):
    """按 --simulate 打开 PetSim，否则打开真机 PetSerial。"""
    if getattr(args, "simulate", False):
        from pet_sim import PetSim
        dev = PetSim(verbose=getattr(args, "verbose", False))
        dev.open()
        return dev
    dev = PetSerial(args.port, args.baud, timeout=getattr(args, "timeout", 3.0),
                    verbose=getattr(args, "verbose", False))
    dev.open()
    return dev


def frame_crc32(frame):
    return "%08x" % (fb.crc32_hex(frame) and int(fb.crc32_hex(frame), 16))


def frame_sha256(frame):
    return hashlib.sha256(frame).hexdigest()


# --------------------------------------------------------------------------
# Runner
# --------------------------------------------------------------------------

class Runner:
    """设备操作 + 心跳 + 证据帧导出的薄封装。"""

    def __init__(self, dev, outdir, wait=0.4, heartbeat=3.0,
                 dump_timeout=30.0, verbose=False):
        self.dev = dev
        self.outdir = Path(outdir)
        self.wait = float(wait)
        self.hb_interval = float(heartbeat)
        self.dump_timeout = float(dump_timeout)
        self.verbose = verbose
        self._last_hb = time.monotonic()
        self.events = []          # 心跳/日志抽样（证据）

    # -- basics ----------------------------------------------------------
    def describe(self):
        return self.dev.describe()

    def _maybe_heartbeat(self, force=False):
        now = time.monotonic()
        if not force and (now - self._last_hb) < self.hb_interval:
            return False
        try:
            self.dev.send("n")
        except PetSerialError:
            return False
        self._last_hb = now
        return True

    def heartbeat(self, force=True):
        return self._maybe_heartbeat(force=force)

    def sleep(self, seconds):
        """分片睡眠，期间维持 `n` 心跳。"""
        deadline = time.monotonic() + float(seconds)
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            self._maybe_heartbeat()
            time.sleep(min(remaining, 0.2))

    def send(self, chars):
        self.dev.send(chars)
        return list(chars)

    def mode(self):
        self._maybe_heartbeat()
        return self.dev.mode()

    def status(self):
        self._maybe_heartbeat()
        return self.dev.status()

    def drain(self, duration=0.4):
        return self.dev.drain(duration)

    def flush_input(self):
        self.dev.flush_input()

    def reboot(self, ready_spec="READY", timeout=20.0):
        rb = self.dev.reboot(ready_spec, timeout)
        self._last_hb = time.monotonic()
        return rb

    # -- synthetic keys --------------------------------------------------
    def key(self, ch, wait=None):
        """发一个合成键命令并返回动作后的 `m` 状态。"""
        self._maybe_heartbeat()
        self.dev.send(ch)
        self.sleep(self.wait if wait is None else wait)
        return self.dev.mode()

    def key_quiet(self, ch, wait=None):
        """发命令但不读 `m`（用于需要先抓日志的用例）。"""
        self._maybe_heartbeat()
        self.dev.send(ch)
        self.sleep(self.wait if wait is None else wait)

    def keys(self, chars, wait=None):
        st = None
        for ch in chars:
            st = self.key(ch, wait)
        return st

    # -- state helpers ---------------------------------------------------
    def wait_state(self, predicate, timeout, poll=0.2):
        """轮询 `m` 直到 predicate(state) 为真或超时；返回 (state, ok)。"""
        deadline = time.monotonic() + float(timeout)
        st = self.mode()
        while True:
            try:
                if predicate(st):
                    return st, True
            except Exception:  # pragma: no cover - 谓词异常按未命中处理
                pass
            if time.monotonic() >= deadline:
                return st, False
            time.sleep(poll)
            st = self.mode()

    def ensure_duty(self):
        """回到值班模式 + 番茄钟 IDLE（菜单关闭；不改变 muted）。"""
        st = self.mode()
        if st["menu"]:
            st = self.key("H")
        if st["phase"] != "IDLE":
            if st["mode"] != MODE_POMO:
                st = self.key("J")
            st = self.key("L")           # POMO 内 KEY1 长 = 放弃
        if st["mode"] != MODE_DUTY:
            st = self.key("J")
        return self.mode()

    def ensure_pomo(self):
        """进入番茄钟模式 + IDLE（菜单关闭）。"""
        st = self.mode()
        if st["menu"]:
            st = self.key("H")
        if st["mode"] != MODE_POMO:
            st = self.key("J")
        if st["phase"] != "IDLE":
            st = self.key("L")
        return self.mode()

    def menu_selected(self, st=None):
        """读取菜单选中行：优先用 `m` 的 sel 字段，缺字段时回退到帧推断。"""
        st = self.mode() if st is None else st
        if st.get("sel") is not None:
            return st["sel"], "m.sel"
        tmp = self.outdir / "_tmp_menu_infer.bin"
        self.dev.dump(tmp, timeout=self.dump_timeout)
        return lc3.infer_menu_selected(tmp.read_bytes()), "frame.infer"

    # -- frame evidence --------------------------------------------------
    def capture(self, name, page, **kw):
        """导出当前帧 → PNG → 像素断言；返回证据 dict（不抛断言异常）。"""
        bin_path = self.outdir / (name + ".bin")
        dump = self.dev.dump(bin_path, timeout=self.dump_timeout)
        png_path = bin_path.with_suffix(".png")
        info = fb.frame_to_png(bin_path, png_path)
        frame = bin_path.read_bytes()
        if page == "pomo":
            rep = lc3.check_pomo_frame(frame, kw["phase"],
                                       kw.get("remain_ms"),
                                       kw.get("total_ms"),
                                       kw.get("done"), kw.get("focus_s"))
        elif page == "menu":
            rep = lc3.check_menu_frame(frame, kw.get("selected"),
                                       kw.get("mode", MODE_DUTY),
                                       kw.get("done"), kw.get("focus_s"))
        else:
            rep = lc.check_frame(frame, kw.get("state", "IDLE"),
                                 kw.get("cards", 0))
        self._last_hb = time.monotonic()
        rec = {
            "name": name,
            "page": page,
            "bin": bin_path.name,
            "png": png_path.name,
            "bytes": dump["bytes"],
            "crc32": dump["crc32"],
            "frame_sha256": info["sha256"],
            "png_sha256": info["png_sha256"],
            "layout_ok": rep.ok,
            "layout_failed": rep.failed,
            "layout_checks": len(rep.checks),
            "layout": rep.to_dict(),
            "context": {k: v for k, v in kw.items()},
        }
        log("  frame %-24s page=%-4s layout=%s%s"
            % (name, page, "PASS" if rep.ok else "FAIL",
               "" if rep.ok else " failed=" + ",".join(rep.failed)))
        return rec
