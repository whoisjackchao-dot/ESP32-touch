#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""run_g1.py - UI-10 / UI-11 / UI-12：6 态各 4 次 = 24 次观测 + 帧断言。

每次观测：注入事件 → 等待（DONE 去抖路径 1.75 s，其余 0.4 s）→ `p` 断言
state / frame / cards → 每态第 1 次观测 `d` 导出帧 → PNG → layout_checks
（含头顶标记断言）。

6 态注入方式（与 docs/08 §4 命令一致）：
  IDLE            3 然后 0（TASK_DONE → AGENT_IDLE，确定性进入 IDLE）
  WORKING         1
  INPUT_REQUIRED  2
  DONE            奇数轮 1→0 等 1.75 s（done 下降沿去抖路径）；偶数轮 3 直进
  FAILED          4
  OFFLINE         5（观测后立即 u 恢复并断言不是 OFFLINE）

跑之前先 `x` 清栈、`5`→`u` 校准链路（避免残留 OFFLINE）。结束时再发 `u`。

用法（设备就绪后）::

    python run_g1.py                          # COM7，evidence/g1.json
    python run_g1.py --port COM7 --wait-ready m2:READY
    python run_g1.py --simulate               # 无设备全链路自测 → evidence/sim/

退出码：0 PASS；1 FAIL；2 设备/端口打不开。
"""

from __future__ import annotations

import argparse
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
from pet_serial import (  # noqa: E402
    DEFAULT_BAUD, DEFAULT_PORT, FrameDumpError, PetSerial, PetSerialError,
    SerialOpenError, SerialTimeout)

STATE_ORDER = ("IDLE", "WORKING", "INPUT_REQUIRED", "DONE", "FAILED", "OFFLINE")

# STATUS 的 frame 期望值（pet_state_view 映射；见上游 pet_state.c）：
#   IDLE 会眨眼/小发呆 → 0..1；WORKING=4；INPUT_REQUIRED=5；DONE=2；FAILED=3；
#   OFFLINE=1（EYES_CLOSED；2026-10-31 Lead 真机确认）。
EXPECTED_FRAMES = {
    "IDLE": (0, 1),
    "WORKING": (4,),
    "INPUT_REQUIRED": (5,),
    "DONE": (2,),
    "FAILED": (3,),
    "OFFLINE": (1,),
}

DONE_EDGE_WAIT = 1.75  # 1500 ms 去抖 + 余量
GENERIC_WAIT = 0.4     # 主循环 125 ms 节拍 + 余量


# --------------------------------------------------------------------------
# small helpers
# --------------------------------------------------------------------------

def now_iso():
    return datetime.now().astimezone().isoformat(timespec="milliseconds")


def log(msg):
    print(msg, flush=True)


def write_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    tmp.replace(path)


def write_json(path, obj):
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(obj, ensure_ascii=False, indent=2),
                   encoding="utf-8")
    tmp.replace(path)


def open_device(args):
    if args.simulate:
        from pet_sim import PetSim
        dev = PetSim(verbose=args.verbose)
        dev.open()
        return dev
    dev = PetSerial(args.port, args.baud, timeout=args.timeout,
                    verbose=args.verbose)
    dev.open()
    return dev


def inject_state(dev, state, round_idx, wait):
    """按态注入命令；返回实际发送的字符序列（证据用）。"""
    steps = []

    def send(chars, delay):
        dev.send(chars)
        steps.extend(list(chars))
        time.sleep(delay)

    if state == "IDLE":
        send("3", 0.12)
        send("0", wait)
    elif state == "WORKING":
        send("1", wait)
    elif state == "INPUT_REQUIRED":
        send("2", wait)
    elif state == "DONE":
        if round_idx % 2 == 1:
            send("1", 0.12)
            send("0", DONE_EDGE_WAIT)  # done 下降沿 → 1500 ms 去抖 → DONE
        else:
            send("3", wait)            # TASK_DONE 直进
    elif state == "FAILED":
        send("4", wait)
    elif state == "OFFLINE":
        send("5", 0.3)
    else:  # pragma: no cover
        raise ValueError("unknown state " + state)
    return steps


# --------------------------------------------------------------------------
# evidence formatting
# --------------------------------------------------------------------------

def summary_markdown(evidence):
    rows = evidence["observations"]
    out = []
    out.append("# G1（UI-10）24 次观测摘要")
    out.append("")
    out.append("- 时间：%s" % evidence["started"])
    out.append("- 模式：%s（串口 %s @ %d）"
               % (evidence["mode"], evidence["port"], evidence["baud"]))
    out.append("- 结论：**%s** — 观测 %d/%d PASS，布局断言 %d/%d PASS，"
               "6 态 PNG 哈希互异：%s"
               % (evidence["result"],
                  evidence["summary"]["passed_observations"],
                  evidence["summary"]["observations"],
                  evidence["summary"]["layout_pass"],
                  evidence["summary"]["layout_total"],
                  "是" if evidence["summary"]["hashes_distinct"] else "否"))
    out.append("")
    out.append("| # | 时间 | 注入 | 期望态 | 实测态 | frame | cards | "
               "state/frame/cards | 布局断言 | PNG sha256(前16) | crc32 |")
    out.append("|---|------|------|--------|--------|-------|-------|"
               "-------------------|----------|-------------------|-------|")
    for r in rows:
        hash16 = (r.get("state_png_sha256") or "-")[:16]
        layout = ("PASS" if r.get("layout_ok")
                  else ("FAIL" if r.get("layout_ok") is False else "-"))
        flags = "%s/%s/%s" % (
            "P" if r["state_ok"] else "F",
            "P" if r["frame_ok"] else "F",
            "P" if r["cards_ok"] else "F")
        out.append("| %d | %s | `%s` | %s | %s | %d | %d | %s | %s | %s | %s |"
                   % (r["index"], r["time"], "".join(r["injected"]) or "-",
                      r["expected_state"], r["actual_state"], r["frame"],
                      r["cards"], flags, layout, hash16,
                      r.get("crc32") or "-"))
    out.append("")
    out.append("## 每态导出帧")
    out.append("")
    out.append("| 态 | PNG | 帧 CRC32 | 帧 sha256 | PNG sha256 |")
    out.append("|----|-----|----------|-----------|------------|")
    for state in STATE_ORDER:
        item = evidence["frames"].get(state)
        if not item:
            out.append("| %s | - | - | - | - |" % state)
            continue
        out.append("| %s | %s | %s | %s | %s |"
                   % (state, item["png"], item["crc32"],
                      item["frame_sha256"][:16], item["png_sha256"][:16]))
    if evidence.get("error"):
        out.append("")
        out.append("## 错误")
        out.append("")
        out.append("```")
        out.append(str(evidence["error"]))
        out.append("```")
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------
# main run
# --------------------------------------------------------------------------

def run(dev, args, outdir):
    evidence = {
        "tool": "run_g1.py",
        "version": 1,
        "started": now_iso(),
        "finished": None,
        "mode": dev.describe().get("mode", "serial"),
        "port": dev.describe().get("port"),
        "baud": dev.describe().get("baud"),
        "expected_frames": {k: list(v) for k, v in EXPECTED_FRAMES.items()},
        "obs_per_state": args.obs_per_state,
        "observations": [],
        "frames": {},
        "preflight": {},
        "summary": {},
        "result": "FAIL",
        "error": None,
    }
    total = len(STATE_ORDER) * args.obs_per_state
    obs = []

    def record_failure(msg):
        log("ERROR: " + msg)
        evidence["error"] = msg

    try:
        if args.wait_ready:
            line, _skipped = dev.wait_for_line(args.wait_ready,
                                               args.wait_ready_timeout)
            evidence["preflight"]["wait_ready"] = {"spec": args.wait_ready,
                                                   "line": line}

        # --- 预检：清卡栈 + 链路 5→u（避免残留 OFFLINE） -----------------
        dev.flush_input()
        dev.send("x")
        time.sleep(0.2)
        st = dev.status()
        evidence["preflight"]["clear_cards"] = {
            "state": st["state"], "cards": st["cards"],
            "ok": st["cards"] == 0}
        if st["cards"] != 0:
            record_failure("清栈后 cards=%d（期望 0）；请检查 `x` 命令"
                           % st["cards"])

        dev.send("5")
        time.sleep(0.25)
        down = dev.status()
        evidence["preflight"]["link_down"] = {
            "injected": "5", "state": down["state"],
            "ok": down["state"] == "OFFLINE"}
        dev.send("u")
        time.sleep(0.3)
        up = dev.status()
        evidence["preflight"]["link_up"] = {
            "injected": "u", "state": up["state"],
            "ok": up["state"] != "OFFLINE"}
        evidence["preflight"]["ok"] = (
            evidence["preflight"]["clear_cards"]["ok"]
            and evidence["preflight"]["link_down"]["ok"]
            and evidence["preflight"]["link_up"]["ok"])
        log("preflight: clear_cards=%s link_down=%s link_up=%s"
            % (evidence["preflight"]["clear_cards"]["ok"],
               evidence["preflight"]["link_down"]["ok"],
               evidence["preflight"]["link_up"]["ok"]))

        # --- 24 次观测 ---------------------------------------------------
        index = 0
        for rnd in range(1, args.obs_per_state + 1):
            for state in STATE_ORDER:
                index += 1
                steps = inject_state(dev, state, rnd, args.wait)
                st = dev.status()
                actual = st["state"]
                frame = st["frame"]
                cards = st["cards"]
                row = {
                    "index": index,
                    "round": rnd,
                    "time": now_iso(),
                    "injected": steps,
                    "expected_state": state,
                    "actual_state": actual,
                    "frame": frame,
                    "frame_expected": list(EXPECTED_FRAMES[state]),
                    "state_ok": actual == state,
                    "frame_ok": frame in EXPECTED_FRAMES[state],
                    "frame_strict": frame in EXPECTED_FRAMES[state],
                    "cards": cards,
                    "cards_ok": cards == 0,
                    "status_line": st["status_line"],
                    "cards_line": st["cards_line"],
                    "bin": None,
                    "png": None,
                    "crc32": None,
                    "png_sha256": None,
                    "layout_ok": None,
                    "layout": None,
                }
                if args.relax_frame and not row["frame_ok"]:
                    row["frame_ok"] = True
                    row["frame_relaxed"] = True

                if rnd == 1:
                    bin_path = outdir / ("g1_obs%02d_%s.bin"
                                         % (index, state.lower()))
                    dump = dev.dump(bin_path, timeout=args.dump_timeout)
                    png_path = bin_path.with_suffix(".png")
                    info = fb.frame_to_png(bin_path, png_path)
                    rep = lc.check_frame(bin_path.read_bytes(), state, cards)
                    row.update({
                        "bin": bin_path.name,
                        "png": png_path.name,
                        "crc32": dump["crc32"],
                        "png_sha256": info["png_sha256"],
                        "layout_ok": rep.ok,
                        "layout": rep.to_dict(),
                    })
                    evidence["frames"][state] = {
                        "bin": bin_path.name,
                        "png": png_path.name,
                        "crc32": dump["crc32"],
                        "frame_sha256": info["sha256"],
                        "png_sha256": info["png_sha256"],
                        "layout_ok": rep.ok,
                        "layout_failed": rep.failed,
                    }
                    if not rep.ok:
                        record_failure("%s 帧布局断言失败：%s"
                                       % (state, ", ".join(rep.failed)))

                if state == "OFFLINE":
                    dev.send("u")
                    time.sleep(0.3)
                    rec = dev.status()
                    row["recovery"] = {
                        "injected": "u", "state": rec["state"],
                        "ok": rec["state"] != "OFFLINE"}
                    if not row["recovery"]["ok"]:
                        record_failure("OFFLINE 后 `u` 未恢复（仍 %s）"
                                       % rec["state"])

                obs.append(row)
                evidence["observations"] = obs
                flags = "state=%s frame=%s cards=%s" % (
                    "OK" if row["state_ok"] else "FAIL",
                    "OK" if row["frame_ok"] else "FAIL",
                    "OK" if row["cards_ok"] else "FAIL")
                layout = ""
                if row["layout_ok"] is not None:
                    layout = " layout=%s" % ("PASS" if row["layout_ok"]
                                             else "FAIL")
                log("[%02d/%d] round %d %-14s -> %-14s frame=%d cards=%d %s%s"
                    % (index, total, rnd, state, actual, frame, cards,
                       flags, layout))

    except (SerialTimeout, FrameDumpError, PetSerialError, OSError) as exc:
        record_failure("%s: %s" % (type(exc).__name__, exc))

    finally:
        # 结束时确保链路在线（避免残留 OFFLINE）
        final = {}
        try:
            dev.send("u")
            time.sleep(0.2)
            st = dev.status()
            final = {"injected": "u", "state": st["state"],
                     "ok": st["state"] != "OFFLINE"}
        except PetSerialError as exc:  # pragma: no cover
            final = {"ok": False, "error": str(exc)}
        evidence["preflight"]["final_link_up"] = final

        # --- 汇总 -------------------------------------------------------
        ev_obs = evidence["observations"]
        state_hashes = {}
        for state, item in evidence["frames"].items():
            state_hashes[state] = item["png_sha256"]
        for row in ev_obs:
            row["state_png_sha256"] = state_hashes.get(row["expected_state"])

        present = [s for s in STATE_ORDER if s in evidence["frames"]]
        hashes = [state_hashes[s] for s in present]
        distinct = len(present) == len(STATE_ORDER) and len(set(hashes)) == len(hashes)

        layout_frames = [item for item in evidence["frames"].values()]
        passed_obs = sum(1 for r in ev_obs
                         if r["state_ok"] and r["frame_ok"] and r["cards_ok"])
        layout_pass = sum(1 for item in layout_frames if item["layout_ok"])

        evidence["summary"] = {
            "observations": total,
            "recorded_observations": len(ev_obs),
            "passed_observations": passed_obs,
            "failed_observations": len(ev_obs) - passed_obs,
            "state_counts": {
                s: sum(1 for r in ev_obs if r["expected_state"] == s)
                for s in STATE_ORDER},
            "state_pass": {
                s: sum(1 for r in ev_obs
                       if r["expected_state"] == s and r["state_ok"]
                       and r["frame_ok"] and r["cards_ok"])
                for s in STATE_ORDER},
            "layout_total": len(layout_frames),
            "layout_pass": layout_pass,
            "layout_fail": len(layout_frames) - layout_pass,
            "hashes_distinct": distinct,
            "png_sha256": state_hashes,
        }
        ok = (len(ev_obs) == total
              and passed_obs == total
              and len(layout_frames) == len(STATE_ORDER)
              and layout_pass == len(layout_frames)
              and distinct
              and evidence["preflight"].get("ok", False)
              and final.get("ok", False)
              and evidence["error"] is None)
        evidence["result"] = "PASS" if ok else "FAIL"
        evidence["finished"] = now_iso()

        # 写证据（即使失败也写，便于定位）
        outdir.mkdir(parents=True, exist_ok=True)
        write_json(outdir / "g1.json", evidence)
        write_text(outdir / "g1_summary.md", summary_markdown(evidence))

        log("")
        log("summary: observations %d/%d PASS, layout %d/%d PASS, "
            "hash distinct=%s, preflight=%s, final_online=%s"
            % (passed_obs, total, layout_pass, len(layout_frames), distinct,
               evidence["preflight"].get("ok"), final.get("ok")))
        if evidence["error"]:
            log("error: %s" % evidence["error"])
        log("evidence: %s" % (outdir / "g1.json"))
        log("summary : %s" % (outdir / "g1_summary.md"))
        log("RESULT %s" % evidence["result"])
    return evidence


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    ap = argparse.ArgumentParser(
        prog="run_g1.py",
        description="UI-10：6 态 × 4 次 = 24 次观测 + 帧缓冲/版式断言")
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    ap.add_argument("--timeout", type=float, default=3.0,
                    help="单次协议等待超时（默认 %(default)s s）")
    ap.add_argument("--dump-timeout", type=float, default=30.0)
    ap.add_argument("--obs-per-state", type=int, default=4,
                    help="每态观测次数（验收=4，共 24 次）")
    ap.add_argument("--wait", type=float, default=GENERIC_WAIT,
                    help="注入后等待秒数（默认 %(default)s）")
    ap.add_argument("--outdir", default=None, help="证据目录")
    ap.add_argument("--wait-ready", nargs="?", const="m2:READY", default=None,
                    metavar="SPEC", help="先等启动日志（默认 m2:READY）")
    ap.add_argument("--ready-timeout", type=float, default=20.0)
    ap.add_argument("--relax-frame", action="store_true",
                    help="frame 与期望表不符时记为 warning 而非 FAIL")
    ap.add_argument("--simulate", action="store_true",
                    help="用 pet_sim 模拟设备（无硬件；证据写 evidence/sim）")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)
    args.wait_ready_timeout = args.ready_timeout

    if args.outdir:
        outdir = Path(args.outdir)
    else:
        outdir = HERE / "evidence" / ("sim" if args.simulate else "")

    try:
        dev = open_device(args)
    except SerialOpenError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        print("提示：设备未连接时可先跑离线自测："
              "python layout_checks.py --selftest  /  python run_g1.py --simulate",
              file=sys.stderr)
        return 2
    try:
        evidence = run(dev, args, outdir)
    finally:
        dev.close()
    return 0 if evidence["result"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
