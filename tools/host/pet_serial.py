#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""pet_serial.py - ESP32 桌面宠物 M2 调试串口驱动（pyserial 封装，可被其它脚本 import）。

协议见 docs/08-m2-development.md §4（USB-Serial-JTAG，默认 COM7，115200 8N1）：

  单字符命令 1 2 3 4 0 5 u n c x p d（不需要回车，\\r\\n 忽略）
  p -> STATUS state=<...> frame=<..> cards=<..> fps=<..> heap=<..>
       CARDS <title>|<title>|<title>      （仅 cards>0）
  d -> FBBIN 240 240 115200\\n + 115200 B RGB565-LE + \\nFBEND <crc32-ieee-hex>\\n

CLI::

    python pet_serial.py --status
    python pet_serial.py --send "1pc"
    python pet_serial.py --dump out.bin
    python pet_serial.py --wait m2:READY --wait-timeout 20
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
