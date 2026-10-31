#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""pet_serial.py - ESP32 桌面宠物 M2/M3 调试串口驱动（pyserial 封装，可被其它脚本 import）。

协议见 docs/08-m2-development.md §4 + docs/10-m3-development.md §4
（USB-Serial-JTAG，默认 COM7，115200 8N1）：

  M2 单字符命令 1 2 3 4 0 5 u n c x p d（不需要回车，\\r\\n 忽略）
  p -> STATUS state=<...> frame=<..> cards=<..> fps=<..> heap=<..>
       CARDS <title>|<title>|<title>      （仅 cards>0）
  d -> FBBIN 240 240 115200\\n + 115200 B RGB565-LE + \\nFBEND <crc32-ieee-hex>\\n

  M3 追加命令（docs/10 §4）：
    K/L/J/H 合成 KEY1 短 / KEY1 长 / KEY2 短 / KEY2 长
    m       打印一行 MODE 状态（mode/menu/phase/remain_ms/total_ms/done/
            focus_s/muted/day/has_day）
    y       模拟跨日（has_day ? set_day(day+1) : set_day(1)），并落盘
    R       软重启（G5 重启持久化用）；本驱动 `reboot()` 会关端口→重连→等 READY
    t       临时缩短本段时长（focus 6 s / break 3 s，G5 演示）

  真机 2026-10-31 的 `m` 行比 docs/10 §4 多 4 个字段（Lead 追加，已实测）：
    ... muted=<0|1> day=<n|-> has_day=<0|1> sel=<0..4> key1=<0|1> key2=<0|1> pwr=<0|1>
  `sel` 是菜单选中行，key1/key2/pwr 是原始按下状态；`parse_mode_line()` 兼容
  “有/没有这些字段”两种情况（缺失时对应值为 None）。

CLI::

    python pet_serial.py --status
    python pet_serial.py --mode
    python pet_serial.py --send "1pc"
    python pet_serial.py --dump out.bin
    python pet_serial.py --wait m2:READY --wait-timeout 20
    python pet_serial.py --reboot --mode
    python pet_serial.py --list-ports

保证：
  * 日志行与协议行混在一起时自动跳过日志（--verbose 可打印被跳过的行）；
  * 一切等待都有超时，超时抛 SerialTimeout（CLI 退出码 3），绝不无限挂起；
  * `d` 导出：发命令后**立即**读（不 sleep，避免设备突发 115200 B 时丢字节），
    先读 FBBIN 头行、再精确读 115200 字节、再读 FBEND 行；校验 CRC32-IEEE，
    失败自动重试（默认共 3 次尝试）并给出明确错误；不按 "FBEND" 字节序列截断；
  * 导出文件先写临时文件再原子替换，避免半截帧。

退出码：0 成功；2 打不开串口；3 协议超时 / CRC / 导出失败。
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path

try:  # pyserial 5.x/3.5 均可；缺失时不阻塞 fb/layout_checks 的离线自测
    import serial  # type: ignore
    from serial.tools import list_ports  # type: ignore
except ImportError:  # pragma: no cover - 仅在环境缺 pyserial 时触发
    serial = None
    list_ports = None

DEFAULT_PORT = "COM7"
DEFAULT_BAUD = 115200
WIDTH = 240
HEIGHT = 240
FRAME_BYTES = WIDTH * HEIGHT * 2  # 115200
MAX_FRAME_BYTES = 1 << 20

STATE_NAMES = ("IDLE", "WORKING", "INPUT_REQUIRED", "DONE", "FAILED", "OFFLINE")
STATUS_RE = re.compile(
    r"STATUS\s+state=(\w+)\s+frame=(\d+)\s+cards=(\d+)"
    r"(?:\s+fps=(\d+))?(?:\s+heap=(\d+))?")
FBBIN_RE = re.compile(r"FBBIN\s+(\d+)\s+(\d+)\s+(\d+)")
FBEND_RE = re.compile(r"FBEND\s+([0-9a-fA-F]{1,8})")

# M3 `m` 行（docs/10 §4）：MODE mode=<DUTY|POMO> menu=<0|1> phase=<...>
# remain_ms=<n> total_ms=<n> done=<n> focus_s=<n> muted=<0|1> day=<n|-> has_day=<0|1>
MODE_MARK = "MODE"
MODE_KEYS = ("mode", "menu", "phase", "remain_ms", "total_ms", "done",
             "focus_s", "muted", "day", "has_day")
# M3 真机（2026-10-31 Lead）在 docs/10 §4 之外追加了 sel/key1/key2/pwr 字段；
# 解析时兼容“有/没有”两种情况：有就填进返回 dict，没有就是 None。
MODE_OPTIONAL_KEYS = ("sel", "key1", "key2", "pwr")
MODE_PHASES = ("IDLE", "FOCUS", "FOCUS_PAUSED", "BREAK", "BREAK_PAUSED",
               "FOCUS_DONE", "BREAK_DONE")
MODE_MODES = ("DUTY", "POMO")
_KV_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=(\S+)")


class PetSerialError(RuntimeError):
    """串口/协议错误基类。"""


class SerialOpenError(PetSerialError):
    """打不开串口（设备未连接、被占用、驱动缺失）。"""


class SerialTimeout(PetSerialError):
    """等待协议行/字节超时。"""


class FrameDumpError(PetSerialError):
    """帧导出失败（头/长度/CRC/写盘）。"""


# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------

def parse_wait_spec(spec):
    """`m2:READY` -> (tag="m2", pattern="READY")；无冒号 -> (None, spec)。"""
    text = str(spec)
    if ":" in text:
        tag, _, pat = text.partition(":")
        tag = tag.strip()
        pat = pat.strip()
        if tag and pat:
            return tag, pat
    return None, text


def frame_crc32(frame):
    import zlib
    return "%08x" % (zlib.crc32(frame) & 0xFFFFFFFF)


def parse_mode_line(line):
    """解析 M3 的 `MODE ...` 单行；不是 MODE 行返回 None。

    返回 dict（字段名见 ``MODE_KEYS``，数值已转 int；day 为 None 表示 `-`），
    并带 ``raw``（原始行）与 ``fields``（原始文本值）。字段缺失/非法抛 ValueError。
    """
    if not line or MODE_MARK not in line:
        return None
    if "mode=" not in line:
        return None
    raw = {}
    for key, val in _KV_RE.findall(line):
        raw[key] = val
    missing = [k for k in MODE_KEYS if k not in raw]
    if missing:
        raise ValueError("MODE line missing field(s) %s: %r"
                         % (", ".join(missing), line))

    def _int(name):
        try:
            return int(raw[name], 10)
        except ValueError:
            raise ValueError("MODE line field %s=%r is not an integer: %r"
                             % (name, raw[name], line))

    mode = raw["mode"].upper()
    phase = raw["phase"].upper()
    if mode not in MODE_MODES:
        raise ValueError("MODE line mode=%r not in %s: %r"
                         % (raw["mode"], MODE_MODES, line))
    if phase not in MODE_PHASES:
        raise ValueError("MODE line phase=%r not in %s: %r"
                         % (raw["phase"], MODE_PHASES, line))
    day_txt = raw["day"].strip()
    day = None if day_txt in ("-", "", "null") else _int("day")
    out = {
        "mode": mode,
        "menu": _int("menu"),
        "phase": phase,
        "remain_ms": _int("remain_ms"),
        "total_ms": _int("total_ms"),
        "done": _int("done"),
        "focus_s": _int("focus_s"),
        "muted": _int("muted"),
        "day": day,
        "has_day": _int("has_day"),
        "fields": raw,
        "raw": line,
    }
    for key in MODE_OPTIONAL_KEYS:
        if key in raw:
            try:
                out[key] = int(raw[key], 10)
            except ValueError:
                raise ValueError("MODE line field %s=%r is not an integer: %r"
                                 % (key, raw[key], line))
        else:
            out[key] = None
    return out


# --------------------------------------------------------------------------
# driver
# --------------------------------------------------------------------------

class PetSerial:
    """阻塞式、行/字节混合读取的 M2 调试串口客户端。"""

    def __init__(self, port=DEFAULT_PORT, baud=DEFAULT_BAUD, timeout=2.0,
                 verbose=False, log=None):
        self.port = port
        self.baud = baud
        self.timeout = float(timeout)
        self.verbose = bool(verbose)
        self._log = log
        self._ser = None
        self._buf = bytearray()
        self._skipped = []

    # -- lifecycle -------------------------------------------------------
    def open(self):
        if serial is None:
            raise SerialOpenError(
                "pyserial is not installed for this interpreter; run: "
                "C:\\Python314\\python.exe -m pip install pyserial")
        try:
            self._ser = serial.Serial(
                port=self.port, baudrate=self.baud,
                bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE, timeout=0.02, write_timeout=5)
        except Exception as exc:  # serial.SerialException / OSError
            raise SerialOpenError(
                "cannot open %s @ %d 8N1: %s (device connected? port busy? "
                "close idf.py monitor?)" % (self.port, self.baud, exc)) from exc
        self._buf.clear()
        try:
            self._ser.reset_input_buffer()
        except Exception:
            pass
        return self

    def close(self):
        if self._ser is not None:
            try:
                self._ser.close()
            finally:
                self._ser = None

    def __enter__(self):
        return self.open()

    def __exit__(self, *exc_info):
        self.close()

    @property
    def is_open(self):
        return self._ser is not None

    def describe(self):
        return {"port": self.port, "baud": self.baud, "mode": "serial"}

    # -- low level -------------------------------------------------------
    def _debug(self, line):
        if self._log is not None:
            self._log(line)
        elif self.verbose:
            print("LOG> " + line)

    def flush_input(self):
        """丢弃尚未读取的输入（发命令前清掉旧日志/旧响应，避免错位）。"""
        self._buf.clear()
        if self._ser is not None:
            try:
                self._ser.reset_input_buffer()
            except Exception:
                pass

    def _pump(self):
        if self._ser is None:
            raise PetSerialError("serial port is not open")
        try:
            waiting = self._ser.in_waiting
            chunk = self._ser.read(waiting) if waiting else self._ser.read(1)
        except Exception as exc:
            raise PetSerialError("serial read failed on %s: %s"
                                 % (self.port, exc)) from exc
        if chunk:
            self._buf += chunk

    def send(self, data):
        """发送字符串/字节（不自动加换行；协议是单字符命令）。"""
        if self._ser is None:
            raise PetSerialError("serial port is not open")
        if isinstance(data, str):
            raw = data.encode("ascii", "strict")
        else:
            raw = bytes(data)
        if not raw:
            return
        try:
            self._ser.write(raw)
            self._ser.flush()
        except Exception as exc:
            raise PetSerialError("serial write failed on %s: %s"
                                 % (self.port, exc)) from exc

    def send_command(self, chars):
        """发送命令串（如 "1pc"）；返回发送的字符列表。"""
        self.send(chars)
        return list(chars)

    # -- line handling ---------------------------------------------------
    def read_line(self, timeout=None):
        """读一行（去掉 \\r\\n）；超时返回 None。"""
        if self._ser is None:
            raise PetSerialError("serial port is not open")
        deadline = time.monotonic() + (self.timeout if timeout is None
                                       else float(timeout))
        while True:
            idx = self._buf.find(b"\n")
            if idx >= 0:
                raw = bytes(self._buf[:idx])
                del self._buf[:idx + 1]
                return raw.decode("utf-8", "replace").rstrip("\r")
            if time.monotonic() >= deadline:
                return None
            self._pump()

    def wait_for_line(self, spec, timeout=None):
        """等待匹配 `spec` 的行；`m2:READY` 表示行内同时含 tag 与模式。

        返回 (line, skipped_lines)。超时抛 SerialTimeout。
        """
        tag, pattern = parse_wait_spec(spec)
        try:
            rx = re.compile(pattern)
        except re.error:
            rx = re.compile(re.escape(pattern))
        deadline = time.monotonic() + (self.timeout if timeout is None
                                       else float(timeout))
        skipped = []
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise SerialTimeout(
                    "timeout waiting for %r (%.1fs); skipped %d log line(s). "
                    "First skipped: %r"
                    % (spec, timeout if timeout is not None else self.timeout,
                       len(skipped), skipped[:3]))
            line = self.read_line(remaining)
            if line is None:
                raise SerialTimeout(
                    "timeout waiting for %r (%.1fs); skipped %d log line(s). "
                    "First skipped: %r"
                    % (spec, timeout if timeout is not None else self.timeout,
                       len(skipped), skipped[:3]))
            if tag is not None:
                matched = (tag in line) and bool(rx.search(line))
            else:
                matched = bool(rx.search(line))
            if matched:
                return line, skipped
            skipped.append(line)
            self._debug(line)

    # -- status ----------------------------------------------------------
    def status(self, timeout=None, retries=1):
        """发 `p`，解析 STATUS[+CARDS]。返回 dict；失败重试一次。"""
        last_err = None
        for attempt in range(retries + 1):
            try:
                return self._status_once(timeout)
            except (SerialTimeout, PetSerialError) as exc:
                last_err = exc
                if attempt < retries:
                    self.flush_input()
                    time.sleep(0.15)
        raise SerialTimeout("status via `p` failed after %d attempt(s): %s"
                            % (retries + 1, last_err)) from last_err

    def _status_once(self, timeout=None):
        self.flush_input()
        self.send("p")
        deadline = time.monotonic() + (self.timeout if timeout is None
                                       else float(timeout))
        line = None
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise SerialTimeout("no STATUS line after `p`")
            candidate = self.read_line(remaining)
            if candidate is None:
                raise SerialTimeout("no STATUS line after `p`")
            m = STATUS_RE.search(candidate)
            if m:
                line = candidate
                break
            self._debug(candidate)
        state = m.group(1).upper()
        if state not in STATE_NAMES:
            raise PetSerialError("unknown state %r in STATUS line %r"
                                 % (state, line))
        out = {
            "state": state,
            "frame": int(m.group(2)),
            "cards": int(m.group(3)),
            "fps": int(m.group(4)) if m.group(4) is not None else None,
            "heap": int(m.group(5)) if m.group(5) is not None else None,
            "titles": [],
            "status_line": line,
            "cards_line": None,
        }
        if out["cards"] > 0:
            cards_line = None
            cards_deadline = time.monotonic() + 0.75
            while True:
                remaining = cards_deadline - time.monotonic()
                if remaining <= 0:
                    break
                candidate = self.read_line(remaining)
                if candidate is None:
                    break
                if "CARDS" in candidate:
                    cards_line = candidate
                    break
                self._debug(candidate)
            if cards_line is None:
                raise SerialTimeout(
                    "STATUS reports cards=%d but no CARDS line arrived"
                    % out["cards"])
            rest = cards_line.split("CARDS", 1)[1]
            if rest.startswith(" "):
                rest = rest[1:]
            out["titles"] = rest.split("|")
            out["cards_line"] = cards_line
        return out

    # -- M3 mode / pomodoro state ---------------------------------------
    def mode(self, timeout=None, retries=1):
        """发 `m`，读回并解析一行 `MODE ...`（docs/10 §4）。失败重试一次。"""
        last_err = None
        for attempt in range(retries + 1):
            try:
                return self._mode_once(timeout)
            except (SerialTimeout, PetSerialError, ValueError) as exc:
                last_err = exc
                if attempt < retries:
                    self.flush_input()
                    time.sleep(0.15)
        raise SerialTimeout("mode via `m` failed after %d attempt(s): %s"
                            % (retries + 1, last_err)) from last_err

    def _mode_once(self, timeout=None):
        self.flush_input()
        self.send("m")
        deadline = time.monotonic() + (self.timeout if timeout is None
                                       else float(timeout))
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise SerialTimeout("no MODE line after `m`")
            candidate = self.read_line(remaining)
            if candidate is None:
                raise SerialTimeout("no MODE line after `m`")
            if MODE_MARK in candidate and "mode=" in candidate:
                return parse_mode_line(candidate)
            self._debug(candidate)

    def drain(self, duration=0.3):
        """读取并返回 `duration` 秒内到达的所有行（用于抓设备日志，如 M5 提示）。"""
        lines = []
        deadline = time.monotonic() + float(duration)
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            line = self.read_line(remaining)
            if line is None:
                break
            lines.append(line)
        return lines

    # -- M3 soft reboot ---------------------------------------------------
    def reboot(self, ready_spec="READY", wait_timeout=20.0, settle=0.6,
               max_open_attempts=40):
        """发 `R` 软重启 → 关端口 → 重连（带重试）→ 等启动 READY 行。

        设备 `R` 调 `esp_restart()`：USB-Serial/JTAG 会重新枚举，故必须先关掉
        旧句柄再重开；重开失败按 ``settle`` 间隔重试。返回 dict 证据。
        """
        if self._ser is None:
            raise PetSerialError("serial port is not open")
        self.flush_input()
        self.send("R")
        time.sleep(0.1)          # 让 write 落到设备，随后立即重启
        self.close()
        time.sleep(float(settle))
        last_err = None
        last_vent = time.monotonic() + float(wait_timeout)
        attempts = 0
        while attempts < max_open_attempts and time.monotonic() < last_vent:
            attempts += 1
            try:
                self.open()
                break
            except SerialOpenError as exc:
                last_err = exc
                time.sleep(0.25)
        if self._ser is None:
            raise SerialOpenError(
                "device did not come back after `R` (%d open attempts): %s"
                % (attempts, last_err))
        # 先等启动 READY；若在重连窗口里已经错过（USB 枚举慢），退化为确认 `m` 能应答。
        ready_line = None
        ready_caught = True
        skipped = []
        try:
            ready_line, skipped = self.wait_for_line(ready_spec, wait_timeout)
        except SerialTimeout:
            ready_caught = False
            deadline = time.monotonic() + float(wait_timeout)
            while True:
                try:
                    self.mode(timeout=2.0, retries=0)
                    break
                except (SerialTimeout, PetSerialError, ValueError) as exc:
                    if time.monotonic() >= deadline:
                        raise SerialTimeout(
                            "device reopened but neither %r nor `m` responded "
                            "after `R`: %s" % (ready_spec, exc)) from exc
                    time.sleep(0.3)
        return {"command": "R", "ready_spec": ready_spec,
                "ready_line": ready_line, "ready_caught": ready_caught,
                "skipped": skipped, "open_attempts": attempts,
                "reopened": True}

    # -- frame dump ------------------------------------------------------
    def dump(self, path, timeout=30.0, retries=2):
        """发 `d` 导出当前帧到 path；校验 115200 B + CRC32；失败最多重试 2 次。"""
        last_err = None
        for attempt in range(retries + 1):
            try:
                return self._dump_once(path, timeout, attempt + 1)
            except (SerialTimeout, FrameDumpError, PetSerialError) as exc:
                last_err = exc
                if attempt < retries:
                    self.flush_input()
                    time.sleep(0.25)
        raise FrameDumpError(
            "frame dump to %s failed after %d attempt(s): %s"
            % (path, retries + 1, last_err)) from last_err

    def _dump_once(self, path, timeout, attempt):
        target = Path(path)
        self.flush_input()
        self.send("d")
        deadline = time.monotonic() + float(timeout)

        header = None
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise SerialTimeout("no FBBIN header after `d` "
                                    "(waited %.1fs)" % timeout)
            candidate = self.read_line(remaining)
            if candidate is None:
                continue
            m = FBBIN_RE.search(candidate)
            if m:
                header = candidate
                break
            self._debug(candidate)
        width, height, nbytes = (int(m.group(1)), int(m.group(2)),
                                 int(m.group(3)))
        if (width, height, nbytes) != (WIDTH, HEIGHT, FRAME_BYTES):
            raise FrameDumpError(
                "unexpected FBBIN header %r (want %d %d %d)"
                % (header, WIDTH, HEIGHT, FRAME_BYTES))
        frame = self._read_exact(nbytes, deadline)

        footer = None
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise SerialTimeout("no FBEND footer after frame payload")
            candidate = self.read_line(remaining)
            if candidate is None:
                continue
            m = FBEND_RE.search(candidate)
            if m:
                footer = candidate
                break
            self._debug(candidate)
        device_crc = m.group(1).lower()
        actual_crc = frame_crc32(frame)
        if device_crc != actual_crc:
            raise FrameDumpError(
                "CRC32 mismatch: device FBEND=%s, host computed=%s "
                "(retrying)" % (device_crc, actual_crc))

        # atomic write
        tmp = target.with_name(target.name + ".part")
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            tmp.write_bytes(frame)
            tmp.replace(target)
        except OSError as exc:
            try:
                tmp.unlink()
            except OSError:
                pass
            raise FrameDumpError("cannot write %s: %s" % (target, exc)) from exc

        return {
            "path": str(target),
            "bytes": len(frame),
            "width": WIDTH,
            "height": HEIGHT,
            "crc32": actual_crc,
            "device_crc32": device_crc,
            "crc_ok": True,
            "attempts": attempt,
            "header": header,
            "footer": footer,
        }

    def _read_exact(self, nbytes, deadline):
        if nbytes <= 0 or nbytes > MAX_FRAME_BYTES:
            raise FrameDumpError("refusing payload of %d bytes" % nbytes)
        while len(self._buf) < nbytes:
            if time.monotonic() >= deadline:
                raise SerialTimeout(
                    "frame payload timed out: got %d/%d bytes"
                    % (len(self._buf), nbytes))
            self._pump()
        frame = bytes(self._buf[:nbytes])
        del self._buf[:nbytes]
        return frame


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def _read_some_lines(dev, duration, out):
    deadline = time.monotonic() + duration
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        line = dev.read_line(remaining)
        if line is None:
            break
        out.append(line)
        print("LINE> " + line)


def cmd_list_ports():
    if list_ports is None:
        print("ERROR: pyserial is not installed", file=sys.stderr)
        return 2
    ports = list(list_ports.comports())
    if not ports:
        print("no serial ports found")
        return 0
    for item in ports:
        print("%-8s %s" % (item.device, item.description))
    return 0


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    ap = argparse.ArgumentParser(
        prog="pet_serial.py",
        description="ESP32 桌面宠物 M2 调试串口（COM7 @115200 8N1）")
    ap.add_argument("--port", default=DEFAULT_PORT, help="串口名（默认 %(default)s）")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                    help="波特率（默认 %(default)s）")
    ap.add_argument("--timeout", type=float, default=3.0,
                    help="单次等待超时秒数（默认 %(default)s）")
    ap.add_argument("--send", help='发送命令串，如 "1pc"')
    ap.add_argument("--status", action="store_true", help="发 `p` 并解析状态")
    ap.add_argument("--mode", action="store_true",
                    help="发 `m` 并解析 M3 MODE 行")
    ap.add_argument("--reboot", nargs="?", const="READY", metavar="SPEC",
                    default=None,
                    help="发 `R` 软重启：重开串口并等 READY 行（默认 SPEC=READY）")
    ap.add_argument("--dump", metavar="FILE.bin",
                    help="发 `d` 导出 115200 B 帧（校验 CRC，失败重试一次）")
    ap.add_argument("--wait", metavar="SPEC",
                    help='等待日志/协议行，如 "m2:READY"（tag:pattern）')
    ap.add_argument("--wait-timeout", type=float, default=15.0,
                    help="--wait 超时秒数（默认 %(default)s）")
    ap.add_argument("--read-time", type=float, default=0.5,
                    help="--send 后继续读取响应的秒数（默认 %(default)s）")
    ap.add_argument("--dump-timeout", type=float, default=30.0,
                    help="--dump 总超时秒数（默认 %(default)s）")
    ap.add_argument("--json", action="store_true", help="机器可读输出")
    ap.add_argument("--verbose", action="store_true", help="打印被跳过的日志行")
    ap.add_argument("--list-ports", action="store_true", help="列出串口后退出")
    args = ap.parse_args(argv)

    if args.list_ports:
        return cmd_list_ports()

    result = {"port": args.port, "baud": args.baud}
    dev = PetSerial(args.port, args.baud, timeout=args.timeout,
                    verbose=args.verbose)
    try:
        dev.open()
    except SerialOpenError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 2

    lines = []
    try:
        if args.wait:
            line, skipped = dev.wait_for_line(args.wait, args.wait_timeout)
            result["wait"] = {"spec": args.wait, "line": line,
                              "skipped": skipped}
            print("WAIT HIT> " + line)

        if args.reboot is not None:
            rb = dev.reboot(args.reboot, wait_timeout=args.wait_timeout)
            result["reboot"] = rb
            if not args.json:
                print("REBOOT ready_caught=%s attempts=%d> %s"
                      % (rb["ready_caught"], rb["open_attempts"],
                         rb["ready_line"] or "(missed, `m` ok)"))

        if args.mode:
            st = dev.mode(timeout=args.timeout)
            result["mode_state"] = st
            if not args.json:
                extra = ""
                if st.get("sel") is not None:
                    extra = " sel=%d key1=%s key2=%s pwr=%s" % (
                        st["sel"], st.get("key1"), st.get("key2"),
                        st.get("pwr"))
                print("MODE mode=%s menu=%d phase=%s remain_ms=%d total_ms=%d "
                      "done=%d focus_s=%d muted=%d day=%s has_day=%d%s"
                      % (st["mode"], st["menu"], st["phase"], st["remain_ms"],
                         st["total_ms"], st["done"], st["focus_s"],
                         st["muted"],
                         "-" if st["day"] is None else st["day"],
                         st["has_day"], extra))

        if args.send:
            dev.send_command(args.send)
            _read_some_lines(dev, args.read_time, lines)

        if args.status:
            status = dev.status(timeout=args.timeout)
            result["status"] = status
            if not args.json:
                print("STATUS state=%s frame=%d cards=%d fps=%s heap=%s"
                      % (status["state"], status["frame"], status["cards"],
                         status["fps"], status["heap"]))
                if status["cards_line"]:
                    print(status["cards_line"])

        if args.dump:
            dump = dev.dump(args.dump, timeout=args.dump_timeout)
            result["dump"] = dump
            if not args.json:
                print("DUMP %s: %d bytes crc32=%s (device %s, attempt %d)"
                      % (dump["path"], dump["bytes"], dump["crc32"],
                         dump["device_crc32"], dump["attempts"]))
    except (SerialTimeout, FrameDumpError, PetSerialError) as exc:
        result["error"] = str(exc)
        if args.json:
            print(json.dumps(result, ensure_ascii=False, indent=2))
        print("ERROR: %s" % exc, file=sys.stderr)
        return 3
    finally:
        dev.close()

    result["lines"] = lines
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
