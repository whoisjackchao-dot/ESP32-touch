"""M5a 设备事件入口 H 测试（C05 / USB-Serial-JTAG）。

用法：
  python tools/host/pet_event.py --port COM7
  python tools/host/pet_event.py --port COM7 --out evidence/m5a

场景（断言设备 STATUS/CARDS/MODE 与帧导出）：
  1) task.start + 单卡字段 → WORKING + CARDS
  2) tasks 3 项 → CARDS top-first
  3) agent.input_required → INPUT_REQUIRED（导出 PNG）
  4) task.done → DONE（导出 PNG）
  5) task.error → FAILED（导出 PNG）
  6) agent.idle（从 WORKING）→ 1.6 s 后 DONE
  7) 非法 JSON / 未知事件 / version 2 → 不崩、状态不变
  8) 心跳停 16 s → OFFLINE；恢复心跳 → 回原态
  9) 事件→状态延迟 ≤1 s（G12 口径）
输出 JSON + 摘要 + PNG。
"""
import argparse
import json
import os
import re
import sys
import threading
import time
import zlib

import serial

STATUS_RE = re.compile(r"STATUS state=(\S+) frame=(\d+) cards=(\d+)")
CARDS_RE = re.compile(r"CARDS (.*)")
MODE_RE = re.compile(r"MODE mode=(\S+) menu=(\d) sel=(\d+) phase=(\S+)")
W, H = 240, 240


class Device:
    def __init__(self, port, baud=115200):
        # write_timeout 放宽：USB-Serial-JTAG 偶发调度停顿会把 50 ms 写超时打满
        self.ser = serial.Serial(port, baud, timeout=0.05, write_timeout=2.0)
        self.buf = b""
        self.lines = []
        self._stop = threading.Event()
        self._pause_beat = threading.Event()
        self._pause_rx = threading.Event()
        self._beat = threading.Thread(target=self._heartbeat, daemon=True)
        self._rx_stop = threading.Event()
        self._rx = threading.Thread(target=self._reader, daemon=True)
        self._rx.start()
        self._beat.start()

    def _reader(self):
        while not self._rx_stop.is_set():
            if self._pause_rx.is_set():
                time.sleep(0.01)
                continue
            try:
                chunk = self.ser.read(4096)
            except Exception:
                return
            if chunk:
                self.buf += chunk
                while b"\n" in self.buf:
                    line, self.buf = self.buf.split(b"\n", 1)
                    self.lines.append(line.rstrip(b"\r").decode("utf-8", "replace"))

    def _heartbeat(self):
        while not self._stop.is_set():
            if not self._pause_beat.is_set():
                try:
                    self.ser.write(b'{"version":1,"event":"device.heartbeat"}\n')
                except Exception:
                    pass
            self._stop.wait(3.0)

    def write_line(self, obj):
        data = (json.dumps(obj, ensure_ascii=False) + "\n").encode("utf-8")
        self.ser.write(data)
        return time.time()

    def write_raw(self, data: bytes):
        self.ser.write(data)
        return time.time()

    def drain(self, sec=0.3):
        time.sleep(sec)
        out, self.lines = self.lines, []
        return out

    def query(self, cmd="p", timeout=1.0):
        # 清掉旧行，避免上一次 dump/心跳遗留影响解析
        self.lines = []
        self.ser.write(cmd.encode())
        end = time.time() + timeout
        while time.time() < end:
            time.sleep(0.02)
            for line in list(self.lines):
                m = STATUS_RE.search(line)
                if m:
                    return {"state": m.group(1), "frame": int(m.group(2)),
                            "cards": int(m.group(3))}
                m = MODE_RE.search(line)
                if m:
                    return {"mode": m.group(1), "menu": int(m.group(2)),
                            "sel": int(m.group(3)), "phase": m.group(4)}
        return None

    def query_cards(self, timeout=1.0):
        self.lines = []
        self.ser.write(b"p")
        end = time.time() + timeout
        cards = None
        while time.time() < end:
            time.sleep(0.02)
            for line in list(self.lines):
                m = CARDS_RE.search(line)
                if m is None and line.startswith("CARDS "):
                    m = re.match(r"CARDS (.*)", line)
                if m:
                    cards = m.group(1).strip()
            if cards is not None:
                return cards
        return cards

    def wait_state(self, want, timeout=1.5):
        t0 = time.time()
        while time.time() - t0 < timeout:
            st = self.query("p", timeout=0.2)
            if st and st.get("state") == want:
                return round(time.time() - t0, 3), st
            time.sleep(0.03)
        return None, self.query("p", timeout=0.2)

    def dump(self, path):
        # dump 是二进制流：必须暂停心跳与读线程、清空缓冲，否则会交错
        self._pause_beat.set()
        self._pause_rx.set()
        try:
            self.lines = []
            self.buf = b""
            self.ser.reset_input_buffer()
            time.sleep(0.05)
            self.lines = []
            self.ser.write(b"d")
            hdr = b""
            end = time.time() + 3
            while time.time() < end and not hdr.endswith(b"\n"):
                hdr += self.ser.read(1)
            if not hdr.startswith(b"FBBIN"):
                return False
            n = int(hdr.decode("utf-8", "replace").split()[3])
            data = b""
            end = time.time() + 5
            while len(data) < n and time.time() < end:
                data += self.ser.read(n - len(data))
            tail = b""
            end = time.time() + 2
            while b"FBEND" not in tail and time.time() < end:
                tail += self.ser.read(64)
            crc = zlib.crc32(data) & 0xFFFFFFFF
            ok = len(data) == n and f"{crc:08X}" in tail.decode("utf-8", "replace")
            if ok:
                write_png(path, data)
            return ok
        finally:
            time.sleep(0.05)
            self._pause_rx.clear()
            self._pause_beat.clear()

    def close(self):
        self._stop.set()
        self._rx_stop.set()
        try:
            self.ser.close()
        except Exception:
            pass


def write_png(path, data):
    import struct

    rows = []
    for y in range(H):
        row = bytearray([0])
        for x in range(W):
            i = (y * W + x) * 2
            v = data[i] | (data[i + 1] << 8)
            r5 = (v >> 11) & 0x1F
            g6 = (v >> 5) & 0x3F
            b5 = v & 0x1F
            row += bytes([(r5 * 255 + 15) // 31, (g6 * 255 + 31) // 63,
                          (b5 * 255 + 15) // 31])
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    open(path, "wb").write(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--out", default=os.path.join("tools", "host", "evidence", "m5a"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    dev = Device(args.port)
    results = []

    def check(name, cond, detail=""):
        results.append({"name": name, "pass": bool(cond), "detail": detail})
        print(("PASS " if cond else "FAIL ") + name + (" | " + detail if detail else ""))

    try:
        dev.drain(0.5)
        # 归位：回值班页/关菜单/清卡
        dev.write_raw(b"x")
        dev.write_raw(b"R")
        time.sleep(1.8)
        boot = " ".join(dev.drain(1.0))
        check("boot_m5a_ready", "m5a: READY" in boot or "READY" in boot, boot[-120:])

        # 1) task.start + 单卡
        dev.write_line({"version": 1, "event": "task.start", "title": "M5A-CARD",
                        "project": "esp32-touch", "agent": "DSH", "elapsed_s": 12})
        lat, st = dev.wait_state("WORKING")
        check("task_start_working", st is not None and st.get("state") == "WORKING",
              "latency=" + str(lat) + "s " + str(st))
        check("g12_latency_le_1s", lat is not None and lat <= 1.0, "latency=" + str(lat))
        check("cards_text", dev.query_cards() == "M5A-CARD", dev.query_cards() or "None")
        check("dump_working", dev.dump(os.path.join(args.out, "m5a_working.png")))

        # 2) tasks 3 卡（top-first）
        dev.write_line({"version": 1, "event": "task.start", "tasks": [
            {"title": "C-TOP", "project": "p1", "elapsed_s": 3, "status": "working", "agent": "DSH"},
            {"title": "C-MID", "project": "p2", "elapsed_s": 8, "status": "input_required"},
            {"title": "C-OLD", "project": "p3", "elapsed_s": 99, "status": "working"},
        ]})
        time.sleep(0.4)
        check("tasks3_order", dev.query_cards() == "C-TOP|C-MID|C-OLD", dev.query_cards() or "None")

        # 3) input_required
        dev.write_line({"version": 1, "event": "agent.input_required"})
        lat, st = dev.wait_state("INPUT_REQUIRED")
        check("input_required", st is not None and st.get("state") == "INPUT_REQUIRED", str(st))
        check("dump_input", dev.dump(os.path.join(args.out, "m5a_input_required.png")))

        # 4) done
        dev.write_line({"version": 1, "event": "task.done"})
        lat, st = dev.wait_state("DONE")
        check("task_done", st is not None and st.get("state") == "DONE", str(st))
        check("dump_done", dev.dump(os.path.join(args.out, "m5a_done.png")))

        # 5) failed
        dev.write_line({"version": 1, "event": "task.error"})
        lat, st = dev.wait_state("FAILED")
        check("task_error", st is not None and st.get("state") == "FAILED", str(st))

        # 6) idle edge → DONE
        dev.write_line({"version": 1, "event": "task.start"})
        dev.wait_state("WORKING")
        dev.write_line({"version": 1, "event": "agent.idle"})
        time.sleep(1.7)
        st = dev.query("p")
        check("idle_edge_done", st is not None and st.get("state") == "DONE", str(st))

        # 7) 非法输入不崩
        before = dev.query("p")
        dev.write_raw(b'{"version":1,"event":"no.such.event"}\n')
        dev.write_raw(b'{not json at all}\n')
        dev.write_raw(b'{"version":2,"event":"task.start"}\n')
        dev.write_raw(b'{"version":1,"event":"task.start","title":123}\n')
        time.sleep(0.4)
        after = dev.query("p")
        with open(os.path.join(args.out, "m5a_invalid.log"), "w", encoding="utf-8") as f:
            f.write("\n".join(dev.drain(0.3)))
        check("invalid_no_crash", after is not None and after.get("state") == before.get("state"),
              f"{before} -> {after}")

        # 8) 心跳/失联
        # 8a) tasks:[] 清空卡栈（C05 §4）
        dev.write_line({"version": 1, "event": "device.heartbeat", "tasks": []})
        time.sleep(0.4)
        cleared = dev.query_cards()
        check("tasks_empty_clears", cleared in (None, ""), str(cleared))

        dev.write_line({"version": 1, "event": "task.start"})
        dev.wait_state("WORKING")
        dev._stop.set()  # 停心跳
        time.sleep(16.5)
        st = dev.query("p")
        check("offline_after_15s", st is not None and st.get("state") == "OFFLINE", str(st))
        # 恢复心跳：ANNOUNCEMENT 回失联前状态（WORKING）
        dev._stop.clear()
        dev._beat = threading.Thread(target=dev._heartbeat, daemon=True)
        dev._beat.start()
        time.sleep(1.0)
        st = dev.query("p")
        check("heartbeat_restore", st is not None and st.get("state") == "WORKING", str(st))

        summary = {"port": args.port, "passed": sum(1 for r in results if r["pass"]),
                   "total": len(results), "results": results}
        with open(os.path.join(args.out, "m5a_event.json"), "w", encoding="utf-8") as f:
            json.dump(summary, f, ensure_ascii=False, indent=1)
        lines = ["# M5a 设备事件入口 H 测试", "",
                 f"- port: {args.port}", f"- 结果：{summary['passed']}/{summary['total']} PASS", "",
                 "| 检查 | 结果 | 明细 |", "|---|---|---|"]
        for r in results:
            lines.append(f"| {r['name']} | {'PASS' if r['pass'] else 'FAIL'} | {r['detail']} |")
        open(os.path.join(args.out, "m5a_event_summary.md"), "w", encoding="utf-8").write("\n".join(lines) + "\n")
        print(f"RESULT {'PASS' if summary['passed'] == summary['total'] else 'FAIL'} "
              f"({summary['passed']}/{summary['total']})")
        return 0 if summary["passed"] == summary["total"] else 1
    finally:
        dev.close()


if __name__ == "__main__":
    sys.exit(main())
