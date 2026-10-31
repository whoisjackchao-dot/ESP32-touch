#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""run_g5.py - G5：番茄钟完整/暂停/放弃/继续 + R 重启持久化 + y 跨日清零。

依据 `docs/10-m3-development.md` §4（m/y/R/t 命令）、§7（G5 用例）与
`pet_pomo.c`（状态机）：
  `t` 把本段时长临时改为 focus 6 s / break 3 s（默认 25/5 仍是发布值）。

流程（每步读 `m` 断言；基线用**增量**，因为 NVS 里可能已有当次会话的统计）：
  1. `t` → (下一段) focus=6 s
  2. KEY2 短 → POMO；KEY1 短启动 → FOCUS（total_ms=6000）
  3. 跑 1 s → KEY1 短暂停 → FOCUS_PAUSED；再等 1 s 断言 remain_ms **不变**
  4. KEY1 短继续 → FOCUS；等到点 → FOCUS_DONE，done +1、focus_s +6
  5. KEY1 短启动 BREAK（total_ms=3000）→ KEY1 长放弃 → IDLE，done 不变
  6. 再跑一段并完成 → done +2、focus_s +12
  7. KEY2 短回 DUTY → `R` 软重启 → 重连断言 done/focus_s/muted 持久（NVS）
  8. `y` 模拟跨日 → done=0、focus_s=0、has_day=1、day+1；再 `R` 断言清零已落盘

帧证据：`g5_pomo_idle.png`（进度 0）、`g5_pomo_focus_paused.png`、
`g5_pomo_done.png`（进度 1000）、`g5_pomo_break.png`（运行中，进度不断言）；
`d` 导出 → PNG → `layout_checks_m3.py` 像素断言。

用法::

    python run_g5.py                        # 真机 COM7 → evidence/g5.json
    python run_g5.py --simulate             # 无设备全链路自测 → evidence/sim/
    python run_g5.py --reset --wait-ready   # 先 R 重启并等 READY 再跑

注意（M2 契约）：设备 15 s 无有效事件会进 OFFLINE；本脚本用 `n` 心跳保活。
退出码：0 PASS；1 FAIL；2 设备/端口打不开。
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from m3_common import (  # noqa: E402
    MODE_DUTY, MODE_POMO, Runner, log, now_iso, open_device, write_json,
    write_text)
from pet_serial import (  # noqa: E402
    DEFAULT_BAUD, DEFAULT_PORT, PetSerialError, SerialOpenError,
    SerialTimeout)

G5_FOCUS_MS = 6000
G5_BREAK_MS = 3000


# --------------------------------------------------------------------------
# summary markdown
# --------------------------------------------------------------------------

def summary_markdown(ev):
    out = ["# G5（番茄钟完成/暂停/放弃/继续 + 重启持久化 + 跨日清零）跑测摘要", ""]
    out.append("- 时间：%s → %s" % (ev["started"], ev["finished"]))
    out.append("- 模式：%s（%s @ %s）"
               % (ev["mode"], ev["port"], ev["baud"]))
    out.append("- **结论：%s** — 步骤 %d/%d PASS；帧布局断言 %d/%d PASS"
               % (ev["result"], ev["summary"]["steps_pass"],
                  ev["summary"]["steps_total"],
                  ev["summary"]["layout_pass"],
                  ev["summary"]["layout_total"]))
    b = ev.get("baseline") or {}
    out.append("- 起始基线：done=%s focus_s=%s muted=%s total_ms=%s "
               "has_day=%s day=%s"
               % (b.get("done"), b.get("focus_s"), b.get("muted"),
                  b.get("total_ms"), b.get("has_day"), b.get("day")))
    if ev.get("error"):
        out += ["", "## 错误", "", "```", str(ev["error"]), "```"]
    out += ["", "## 步骤", "", "| # | 步骤 | 判定 | 证据 |",
            "| --- | --- | --- | --- |"]
    for idx, row in enumerate(ev["steps"], 1):
        out.append("| %d | `%s` | %s | %s |"
                   % (idx, row["step"], "PASS" if row["ok"] else "**FAIL**",
                      row["detail"].replace("|", "\\|")))
    out += ["", "## 帧证据（d → PNG → 像素断言）", "",
            "| 帧 | 页面 | PNG | CRC32 | layout | 备注 |",
            "| --- | --- | --- | --- | --- | --- |"]
    for name, rec in ev["frames"].items():
        note = ""
        ctx = rec.get("context") or {}
        if rec["page"] == "pomo":
            exp = rec["layout"].get("expected_progress")
            note = "phase=%s progress=%s" % (
                ctx.get("phase"),
                "—" if exp is None else "%.3f" % exp)
            if ctx.get("total_ms") is None:
                note += "（运行中帧，进度不断言）"
        out.append("| %s | %s | `%s` | %s | %s | %s |"
                   % (name, rec["page"], rec["png"], rec["crc32"],
                      "PASS" if rec["layout_ok"] else
                      "**FAIL** (" + ",".join(rec["layout_failed"]) + ")",
                      note))
    failed = [row for row in ev["steps"] if not row["ok"]]
    if failed:
        out += ["", "## 失败明细", ""]
        for row in failed:
            out.append("- `%s`：%s" % (row["step"], row["detail"]))
    out += ["", "## 协议/环境备注", ""]
    for note in ev.get("protocol_notes", []):
        out.append("- " + note)
    out.append("")
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------
# main flow
# --------------------------------------------------------------------------

def run(r, args, outdir):
    ev = {
        "tool": "run_g5.py",
        "version": 1,
        "started": now_iso(),
        "finished": None,
        "mode": r.describe().get("mode", "serial"),
        "port": r.describe().get("port"),
        "baud": r.describe().get("baud"),
        "short_durations": {"focus_ms": G5_FOCUS_MS,
                            "break_ms": G5_BREAK_MS},
        "protocol_notes": [
            "m 行实际格式（2026-10-31 真机）比 docs/10 §4 多 sel/key1/key2/pwr；"
            "工具两种格式都解析。",
            "M2 契约：15 s 无有效事件进 OFFLINE；本跑测用 `n` 心跳保活。",
            "G5 用 `t` 把本段改为 focus 6 s / break 3 s；`R` 重启后回到默认 25/5。",
            "统计断言用增量（NVS 里可能有上次会话残留的 done/focus_s）。",
        ],
        "baseline": {},
        "steps": [],
        "frames": {},
        "summary": {},
        "result": "FAIL",
        "error": None,
    }

    def check(name, ok, detail, state=None):
        row = {"step": name, "ok": bool(ok), "detail": detail,
               "time": now_iso()}
        if state is not None:
            row["state"] = {k: state.get(k) for k in
                            ("mode", "menu", "phase", "remain_ms", "total_ms",
                             "done", "focus_s", "muted", "day", "has_day")}
        ev["steps"].append(row)
        log("[%02d] %-26s %s | %s"
            % (len(ev["steps"]), name, "PASS" if ok else "FAIL", detail))
        return ok

    def fail(msg):
        ev["error"] = (ev["error"] + "; " if ev["error"] else "") + msg
        log("ERROR: " + msg)

    try:
        # ---- 预检 ----
        if args.wait_ready:
            line, _skipped = r.dev.wait_for_line(args.wait_ready,
                                                 args.ready_timeout)
            ev["baseline"]["wait_ready"] = {"spec": args.wait_ready,
                                            "line": line}
            log("preflight: READY -> %s" % line)
        if args.reset:
            rb = r.reboot()
            ev["baseline"]["reset"] = rb
            log("preflight: rebooted, ready=%s" % rb.get("ready_line"))
        st = r.ensure_duty()
        check("preflight_duty_idle",
              st["mode"] == MODE_DUTY and st["menu"] == 0
              and st["phase"] == "IDLE",
              "mode=%s menu=%d phase=%s" % (st["mode"], st["menu"],
                                            st["phase"]), st)
        ev["baseline"].update({
            "done": st["done"], "focus_s": st["focus_s"],
            "muted": st["muted"], "has_day": st["has_day"],
            "day": st["day"], "total_ms": st["total_ms"],
            "raw": st.get("raw"),
        })
        base_done = st["done"]
        base_focus = st["focus_s"]
        base_muted = st["muted"]

        # ---- 1. t：缩短本段时长 ----
        r.send("t")
        r.sleep(0.3)
        st_t = r.mode()
        ev["steps"].append({
            "step": "t_short_durations", "ok": True,
            "detail": "total_ms after `t` = %d（文档：下一段起 focus=%d ms；"
                      "下一段总时长在 focus_start 再断言）"
                      % (st_t["total_ms"], G5_FOCUS_MS),
            "time": now_iso(), "state": st_t})
        log("[%02d] %-26s PASS | total_ms=%d"
            % (len(ev["steps"]), "t_short_durations", st_t["total_ms"]))

        # ---- 2. 进 POMO + 启动 focus ----
        st = r.ensure_pomo()
        check("enter_pomo",
              st["mode"] == MODE_POMO and st["menu"] == 0
              and st["phase"] == "IDLE",
              "mode=%s menu=%d phase=%s"
              % (st["mode"], st["menu"], st["phase"]), st)
        ev["frames"]["g5_pomo_idle"] = r.capture(
            "g5_pomo_idle", "pomo", phase=st["phase"],
            remain_ms=st["remain_ms"], total_ms=st["total_ms"],
            done=st["done"], focus_s=st["focus_s"])

        st = r.key("K")
        check("focus_start",
              st["phase"] == "FOCUS" and st["total_ms"] == G5_FOCUS_MS
              and st["done"] == base_done,
              "phase=%s total_ms=%d (expect %d) done=%d"
              % (st["phase"], st["total_ms"], G5_FOCUS_MS, st["done"]), st)

        # ---- 3. 暂停 + 暂停期间 remain 不变 ----
        r.sleep(1.0)
        st = r.key("K")
        remain1 = st["remain_ms"]
        check("focus_pause",
              st["phase"] == "FOCUS_PAUSED" and 0 < remain1 < G5_FOCUS_MS,
              "phase=%s remain_ms=%d" % (st["phase"], remain1), st)
        ev["frames"]["g5_pomo_focus_paused"] = r.capture(
            "g5_pomo_focus_paused", "pomo", phase=st["phase"],
            remain_ms=st["remain_ms"], total_ms=st["total_ms"],
            done=st["done"], focus_s=st["focus_s"])
        r.sleep(1.0)
        st2 = r.mode()
        check("pause_remain_frozen",
              st2["phase"] == "FOCUS_PAUSED"
              and st2["remain_ms"] == remain1,
              "1 s 后 phase=%s remain_ms=%d（暂停前 %d，应不变）"
              % (st2["phase"], st2["remain_ms"], remain1), st2)

        # ---- 4. 继续 → 到点 ----
        st = r.key("K")
        check("focus_resume", st["phase"] == "FOCUS",
              "phase=%s" % st["phase"], st)
        st, ok = r.wait_state(lambda s: s["phase"] == "FOCUS_DONE",
                              args.focus_timeout)
        check("focus_done",
              ok and st["done"] == base_done + 1
              and st["focus_s"] == base_focus + 6
              and st["remain_ms"] == 0,
              "phase=%s done=%d (expect %d) focus_s=%d (expect %d) "
              "remain_ms=%d"
              % (st["phase"], st["done"], base_done + 1,
                 st["focus_s"], base_focus + 6, st["remain_ms"]), st)
        ev["frames"]["g5_pomo_done"] = r.capture(
            "g5_pomo_done", "pomo", phase=st["phase"],
            remain_ms=st["remain_ms"], total_ms=st["total_ms"],
            done=st["done"], focus_s=st["focus_s"])

        # ---- 5. BREAK 启动 → 放弃 ----
        st = r.key("K")
        check("break_start",
              st["phase"] == "BREAK" and st["total_ms"] == G5_BREAK_MS,
              "phase=%s total_ms=%d (expect %d)"
              % (st["phase"], st["total_ms"], G5_BREAK_MS), st)
        # BREAK 正在跑：导出的帧进度会在 dump 期间前进，故不做进度角断言
        ev["frames"]["g5_pomo_break"] = r.capture(
            "g5_pomo_break", "pomo", phase=st["phase"],
            remain_ms=None, total_ms=None,
            done=st["done"], focus_s=st["focus_s"])
        st = r.key("L")
        check("break_abandon",
              st["phase"] == "IDLE" and st["done"] == base_done + 1,
              "phase=%s done=%d（放弃不计完成，应仍 %d）"
              % (st["phase"], st["done"], base_done + 1), st)

        # ---- 6. 再跑一段并完成 ----
        st = r.key("K")
        check("segment2_start",
              st["phase"] == "FOCUS" and st["total_ms"] == G5_FOCUS_MS,
              "phase=%s total_ms=%d" % (st["phase"], st["total_ms"]), st)
        st, ok = r.wait_state(lambda s: s["phase"] == "FOCUS_DONE",
                              args.focus_timeout)
        check("segment2_done",
              ok and st["done"] == base_done + 2
              and st["focus_s"] == base_focus + 12,
              "phase=%s done=%d (expect %d) focus_s=%d (expect %d)"
              % (st["phase"], st["done"], base_done + 2,
                 st["focus_s"], base_focus + 12), st)

        # ---- 7. 回 DUTY → R 重启断言持久化 ----
        st = r.key("J")
        check("back_duty", st["mode"] == MODE_DUTY,
              "mode=%s" % st["mode"], st)
        rb = r.reboot()
        ev["baseline"]["reboot"] = rb
        st = r.mode()
        check("reboot_persist",
              st["mode"] == MODE_DUTY and st["phase"] == "IDLE"
              and st["done"] == base_done + 2
              and st["focus_s"] == base_focus + 12
              and st["muted"] == base_muted,
              "重启后 mode=%s phase=%s done=%d (expect %d) focus_s=%d "
              "(expect %d) muted=%d (expect %d)"
              % (st["mode"], st["phase"], st["done"], base_done + 2,
                 st["focus_s"], base_focus + 12, st["muted"], base_muted), st)

        # ---- 8. y 跨日清零 + 再重启验证落盘 ----
        prev = st
        expect_day = (prev["day"] + 1) if prev["has_day"] else 1
        r.send("y")
        r.sleep(0.4)
        st = r.mode()
        check("crossday_zero",
              st["done"] == 0 and st["focus_s"] == 0,
              "y 后 done=%d (expect 0) focus_s=%d (expect 0) has_day=%d "
              "day=%s" % (st["done"], st["focus_s"], st["has_day"], st["day"]),
              st)
        check("crossday_day",
              st["has_day"] == 1 and st["day"] == expect_day,
              "has_day=%d day=%s (expect 1/%d)"
              % (st["has_day"], st["day"], expect_day), st)
        rb2 = r.reboot()
        ev["baseline"]["reboot_after_crossday"] = rb2
        st = r.mode()
        check("crossday_persist",
              st["done"] == 0 and st["focus_s"] == 0,
              "重启后 done=%d (expect 0) focus_s=%d (expect 0) has_day=%d "
              "day=%s" % (st["done"], st["focus_s"], st["has_day"], st["day"]),
              st)

    except (SerialTimeout, PetSerialError, OSError) as exc:
        fail("%s: %s" % (type(exc).__name__, exc))

    finally:
        steps = ev["steps"]
        frames = list(ev["frames"].values())
        layout_pass = sum(1 for f in frames if f["layout_ok"])
        fatal = [row for row in steps if not row["ok"]]
        ev["summary"] = {
            "steps_total": len(steps),
            "steps_pass": len(steps) - len(fatal),
            "steps_fail": len(fatal),
            "layout_total": len(frames),
            "layout_pass": layout_pass,
            "layout_fail": len(frames) - layout_pass,
            "failed_steps": [row["step"] for row in fatal],
        }
        ok = (ev["error"] is None and not fatal and len(frames) == 4
              and layout_pass == len(frames))
        ev["result"] = "PASS" if ok else "FAIL"
        ev["finished"] = now_iso()
        outdir.mkdir(parents=True, exist_ok=True)
        write_json(outdir / "g5.json", ev)
        write_text(outdir / "g5_summary.md", summary_markdown(ev))
        log("")
        log("summary: steps %d/%d PASS, layout %d/%d PASS, failed=%s"
            % (ev["summary"]["steps_pass"], ev["summary"]["steps_total"],
               layout_pass, len(frames),
               ",".join(ev["summary"]["failed_steps"]) or "-"))
        if ev["error"]:
            log("error: %s" % ev["error"])
        log("evidence: %s" % (outdir / "g5.json"))
        log("summary : %s" % (outdir / "g5_summary.md"))
        log("RESULT %s" % ev["result"])
    return ev


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    ap = argparse.ArgumentParser(
        prog="run_g5.py",
        description="G5：番茄钟完成/暂停/放弃/继续 + 重启持久化 + 跨日清零")
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    ap.add_argument("--timeout", type=float, default=3.0,
                    help="单次协议等待超时（默认 %(default)s s）")
    ap.add_argument("--dump-timeout", type=float, default=30.0)
    ap.add_argument("--wait", type=float, default=0.3,
                    help="命令后等待秒数（默认 %(default)s）")
    ap.add_argument("--focus-timeout", type=float, default=10.0,
                    help="等待 FOCUS_DONE 的超时（默认 %(default)s s）")
    ap.add_argument("--outdir", default=None, help="证据目录")
    ap.add_argument("--wait-ready", nargs="?", const="READY", default=None,
                    metavar="SPEC", help="先等启动日志（默认 READY）")
    ap.add_argument("--ready-timeout", type=float, default=20.0)
    ap.add_argument("--reset", action="store_true",
                    help="开跑前先 `R` 软重启并等 READY")
    ap.add_argument("--simulate", action="store_true",
                    help="用 pet_sim 模拟设备（无硬件；证据写 evidence/sim）")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    if args.outdir:
        outdir = Path(args.outdir)
    else:
        outdir = HERE / "evidence" / ("sim" if args.simulate else "")

    try:
        dev = open_device(args)
    except SerialOpenError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        print("提示：设备未连接时可先跑离线自测："
              "python run_g5.py --simulate", file=sys.stderr)
        return 2
    try:
        runner = Runner(dev, outdir, wait=args.wait,
                        dump_timeout=args.dump_timeout,
                        verbose=args.verbose)
        ev = run(runner, args, outdir)
    finally:
        dev.close()
    return 0 if ev["result"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
