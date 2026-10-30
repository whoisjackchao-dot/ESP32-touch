#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""run_g3.py - CARD-10..14：20 轮 3 卡栈 + 第 4 卡淘汰 + 4 个单卡特例。

每轮：
  x 清栈 → c×3 → 断言 CARDS=[ALPHA,BETA,GAMMA] → c 第 4 次 →
  断言 [DELTA,ALPHA,BETA] 且 GAMMA 被淘汰 → d 导出 → PNG → layout_checks。

特例（在 20 轮之后，先 x 再按预置序列打到第 4 张）：
  c#5 同名 BETA 置顶            → CARDS=[BETA,DELTA,ALPHA]
  c#6 空 title 卡（首项为空）    → CARDS=["",BETA,DELTA]，占位渲染
  c#7 长标题                    → CARDS=[LONG,"",BETA]，截断不越界
  c#8 CJK 标题                  → CARDS=[中文标题测试,LONG,""]
各导出一张 PNG 并跑 layout_checks。

前提（已在 pet_sim 中同样实现）：`x` 会重置 docs/08 §4 的预置卡序号，
因此每轮都从 ALPHA/BETA/GAMMA 开始。若真机 `x` 不重置序号，第 1 轮就会
在 CARDS 断言的证据里失败，脚本报错并停止判定 PASS。

用法::

    python run_g3.py                  # COM7 → evidence/g3.json
    python run_g3.py --simulate       # 无设备全链自测 → evidence/sim/
    python run_g3.py --rounds 2 --simulate   # 快速冒烟

退出码：0 PASS；1 FAIL；2 设备/端口打不开。
"""

from __future__ import annotations

import argparse
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

CARD_SEQ3 = ["ALPHA", "BETA", "GAMMA"]
CARD_SEQ4 = ["DELTA", "ALPHA", "BETA"]
CARD_STATES = ("IDLE", "WORKING", "INPUT_REQUIRED", "DONE", "FAILED", "OFFLINE")

LONG_TITLE = "THIS-IS-A-VERY-LONG-TITLE-0123456789"
CJK_TITLE = "中文标题测试"

SPECIALS = (
    {"name": "special5_beta_dedup", "label": "c#5 同名 BETA 置顶",
     "presses": 5, "expect": ["BETA", "DELTA", "ALPHA"],
     "notes": "同名（title+project）更新并移到 cards[0]"},
    {"name": "special6_untitled", "label": "c#6 空 title 占位",
     "presses": 6, "expect": ["", "BETA", "DELTA"], "first_empty_ok": True,
     "notes": "空 title 必须接收并渲染 (untitled)；固件 CARDS 行用 `-` 打印空 title，"
              "断言把首项 `-` 归一化为空串"},
    {"name": "special7_long", "label": "c#7 长标题截断",
     "presses": 7, "expect": [LONG_TITLE, "", "BETA"],
     "notes": "行 2 宽 ≤180，溢出尾 `..`，不覆盖时长区"},
    {"name": "special8_cjk", "label": "c#8 CJK 标题",
     "presses": 8, "expect": [CJK_TITLE, LONG_TITLE, ""],
     "notes": "16×16 CJK，顶边 y+12"},
)


# --------------------------------------------------------------------------
# helpers (与 run_g1 保持一致的小工具)
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


def press(dev, chars, gap=0.15):
    """逐个发送命令字符（每个间隔 gap，等主循环 125 ms 节拍处理）。"""
    sent = []
    for ch in chars:
        dev.send(ch)
        sent.append(ch)
        if gap:
            time.sleep(gap)
    return sent


def expect_titles(status, expected, note, row, failures):
    titles = list(status["titles"])
    ok = titles == list(expected) and status["cards"] == len(expected)
    row["assertions"].append({"what": note, "expected": list(expected),
                              "actual": titles, "ok": ok,
                              "status_line": status["status_line"],
                              "cards_line": status["cards_line"]})
    if not ok:
        failures.append("%s: CARDS=%s 期望 %s" % (note, titles,
                                                 list(expected)))
    return ok


# --------------------------------------------------------------------------
# evidence formatting
# --------------------------------------------------------------------------

def summary_markdown(evidence):
    out = []
    out.append("# G3（CARD-10..14）卡栈观测摘要")
    out.append("")
    out.append("- 时间：%s" % evidence["started"])
    out.append("- 模式：%s（串口 %s @ %d）"
               % (evidence["mode"], evidence["port"], evidence["baud"]))
    out.append("- 结论：**%s** — 卡栈轮次 %d/%d PASS，特例 %d/%d PASS，"
               "布局断言 %d/%d PASS"
               % (evidence["result"], evidence["summary"]["rounds_pass"],
                  evidence["summary"]["rounds"],
                  evidence["summary"]["specials_pass"],
                  evidence["summary"]["specials"],
                  evidence["summary"]["layout_pass"],
                  evidence["summary"]["layout_total"]))
    out.append("")
    out.append("## 20 轮卡栈")
    out.append("")
    out.append("| 轮 | 时间 | 清栈后 cards | c×3 CARDS | c#4 CARDS | 淘汰 | "
               "state | 布局 | PNG | crc32 |")
    out.append("|----|------|--------------|-----------|-----------|------|"
               "-------|------|-----|-------|")
    for r in evidence["rounds"]:
        out.append("| %d | %s | %s | %s | %s | %s | %s | %s | %s | %s |"
                   % (r["round"], r["time"], r.get("cards_before", "-"),
                      "|".join(r.get("after3", []) or []),
                      "|".join(r.get("after4", []) or []),
                      r.get("eliminated", "-"), r.get("state", "-"),
                      "PASS" if r.get("layout_ok") else "FAIL",
                      r.get("png") or "-", r.get("crc32") or "-"))
    out.append("")
    out.append("## 单卡特例")
    out.append("")
    out.append("| 特例 | 时间 | CARDS | 期望 | 断言 | 布局 | PNG | 说明 |")
    out.append("|------|------|-------|------|------|------|-----|------|")
    for r in evidence["specials"]:
        out.append("| %s | %s | %s | %s | %s | %s | %s | %s |"
                   % (r["label"], r["time"],
                      "|".join(r.get("titles", []) or []),
                      "|".join(r.get("expected", []) or []),
                      "PASS" if r.get("ok") else "FAIL",
                      "PASS" if r.get("layout_ok") else "FAIL",
                      r.get("png") or "-", r.get("notes", "")))
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
        "tool": "run_g3.py",
        "version": 1,
        "started": now_iso(),
        "finished": None,
        "mode": dev.describe().get("mode", "serial"),
        "port": dev.describe().get("port"),
        "baud": dev.describe().get("baud"),
        "rounds": [],
        "specials": [],
        "preflight": {},
        "summary": {},
        "result": "FAIL",
        "error": None,
    }
    failures = []
    layout_total = 0
    layout_pass = 0

    def record_failure(msg):
        failures.append(msg)
        log("FAIL: " + msg)

    try:
        if args.wait_ready:
            line, _ = dev.wait_for_line(args.wait_ready, args.ready_timeout)
            evidence["preflight"]["wait_ready"] = {"spec": args.wait_ready,
                                                   "line": line}

        # --- 预检：清栈 + 归位到 IDLE（保证后面帧断言的环境确定） ----------
        dev.flush_input()
        dev.send("x")
        time.sleep(0.2)
        st = dev.status()
        evidence["preflight"]["clear_cards"] = {
            "cards": st["cards"], "ok": st["cards"] == 0}
        dev.send("3")  # TASK_DONE → DONE
        time.sleep(0.15)
        dev.send("0")  # AGENT_IDLE（DONE → IDLE）
        time.sleep(0.25)
        st = dev.status()
        evidence["preflight"]["idle"] = {"state": st["state"],
                                         "ok": st["state"] == "IDLE"}
        log("preflight: clear_cards=%s idle_state=%s"
            % (evidence["preflight"]["clear_cards"]["ok"], st["state"]))

        # --- 20 轮卡栈 ---------------------------------------------------
        for round_idx in range(1, args.rounds + 1):
            t0 = now_iso()
            row = {
                "round": round_idx,
                "time": t0,
                "injected": [],
                "assertions": [],
                "cards_before": None,
                "after3": [],
                "after4": [],
                "eliminated": None,
                "state": None,
                "crc32": None,
                "png": None,
                "png_sha256": None,
                "layout_ok": False,
                "ok": False,
            }
            dev.send("n")  # 心跳：刷新链路，避免 15 s 失联误判
            dev.send("x")
            time.sleep(0.2)
            before = dev.status()
            row["cards_before"] = before["cards"]
            if before["cards"] != 0:
                record_failure("round %d: 清栈后 cards=%d"
                               % (round_idx, before["cards"]))

            row["injected"].extend(press(dev, "ccc", gap=0.15))
            after3 = dev.status()
            row["after3"] = list(after3["titles"])
            row["state"] = after3["state"]
            ok3 = expect_titles(after3, CARD_SEQ3, "c×3", row, failures)

            row["injected"].extend(press(dev, "c", gap=0.2))
            after4 = dev.status()
            row["after4"] = list(after4["titles"])
            row["state"] = after4["state"]
            ok4 = expect_titles(after4, CARD_SEQ4, "c#4", row, failures)
            eliminated = "GAMMA" not in after4["titles"]
            row["eliminated"] = "GAMMA" if eliminated else "none"
            if not eliminated:
                record_failure("round %d: 第 4 卡后 GAMMA 未被淘汰 CARDS=%s"
                               % (round_idx, after4["titles"]))
            state_ok = after4["state"] == "IDLE"
            if not state_ok:
                record_failure("round %d: 状态 %s（期望 IDLE）"
                               % (round_idx, after4["state"]))

            bin_path = outdir / ("g3_round%02d.bin" % round_idx)
            dump = dev.dump(bin_path, timeout=args.dump_timeout)
            png_path = bin_path.with_suffix(".png")
            info = fb.frame_to_png(bin_path, png_path)
            rep = lc.check_frame(bin_path.read_bytes(), after4["state"],
                                 after4["cards"], after4["titles"])
            layout_total += 1
            layout_pass += 1 if rep.ok else 0
            row.update({
                "crc32": dump["crc32"],
                "png": png_path.name,
                "png_sha256": info["png_sha256"],
                "layout_ok": rep.ok,
                "layout_failed": rep.failed,
            })
            if not rep.ok:
                record_failure("round %d: 布局断言失败 %s"
                               % (round_idx, ", ".join(rep.failed)))

            row["ok"] = bool(before["cards"] == 0 and ok3 and ok4
                             and eliminated and state_ok and rep.ok)
            evidence["rounds"].append(row)
            log("[%02d/%d] x=%d c×3=%s c#4=%s elim=%s state=%s layout=%s"
                % (round_idx, args.rounds, before["cards"],
                   "|".join(row["after3"]), "|".join(row["after4"]),
                   row["eliminated"], row["state"],
                   "PASS" if rep.ok else "FAIL"))

        # --- 4 个单卡特例 ------------------------------------------------
        dev.send("n")
        dev.send("x")
        time.sleep(0.2)
        press(dev, "cccc", gap=0.15)  # 预置卡 1..4 → [DELTA,ALPHA,BETA]
        base = dev.status()
        if base["titles"] != CARD_SEQ4:
            record_failure("特例基线 CARDS=%s 期望 %s"
                           % (base["titles"], CARD_SEQ4))

        for spec in SPECIALS:
            row = {
                "label": spec["label"],
                "name": spec["name"],
                "time": now_iso(),
                "expected": list(spec["expect"]),
                "notes": spec["notes"],
                "titles": [],
                "cards": None,
                "state": None,
                "crc32": None,
                "png": None,
                "png_sha256": None,
                "layout_ok": False,
                "ok": False,
                "assertions": [],
            }
            dev.send("c")
            time.sleep(0.3)
            st = dev.status()
            raw_titles = list(st["titles"])
            # 固件 CARDS 行把空 title 打印成 `-`（main.c print_status）；
            # 断言一律把 `-` 归一化为空串（本序列中不存在真实 title 为 `-` 的卡）。
            titles = ["" if t == "-" else t for t in raw_titles]
            view = dict(st)
            view["titles"] = titles
            row["titles"] = raw_titles
            row["titles_normalized"] = titles
            row["cards"] = st["cards"]
            row["state"] = st["state"]
            ok_titles = expect_titles(view, spec["expect"], spec["label"],
                                      row, failures)
            if spec.get("first_empty_ok"):
                first_empty = bool(titles) and titles[0] == ""
                row["assertions"].append({
                    "what": "首项空 title", "expected": "",
                    "actual": raw_titles[0] if raw_titles else None,
                    "ok": first_empty})
                if not first_empty:
                    record_failure("c#6 空 title 卡消失（CARDS 首项=%r）"
                                   % (raw_titles[0] if raw_titles else None))
            bin_path = outdir / ("g3_%s.bin" % spec["name"])
            dump = dev.dump(bin_path, timeout=args.dump_timeout)
            png_path = bin_path.with_suffix(".png")
            info = fb.frame_to_png(bin_path, png_path)
            rep = lc.check_frame(bin_path.read_bytes(), st["state"],
                                 st["cards"], st["titles"])
            layout_total += 1
            layout_pass += 1 if rep.ok else 0
            row.update({
                "crc32": dump["crc32"],
                "png": png_path.name,
                "png_sha256": info["png_sha256"],
                "layout_ok": rep.ok,
                "layout_failed": rep.failed,
            })
            if not rep.ok:
                record_failure("%s: 布局断言失败 %s"
                               % (spec["label"], ", ".join(rep.failed)))
            row["ok"] = bool(ok_titles and st["cards"] == len(spec["expect"])
                             and rep.ok)
            evidence["specials"].append(row)
            log("[special] %-22s CARDS=%-45s layout=%s"
                % (spec["label"], "|".join(row["titles"]),
                   "PASS" if rep.ok else "FAIL"))

    except (SerialTimeout, FrameDumpError, PetSerialError, OSError) as exc:
        record_failure("%s: %s" % (type(exc).__name__, exc))

    finally:
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

        rounds_pass = sum(1 for r in evidence["rounds"] if r.get("ok"))
        specials_pass = sum(1 for r in evidence["specials"] if r.get("ok"))
        evidence["summary"] = {
            "rounds": args.rounds,
            "rounds_recorded": len(evidence["rounds"]),
            "rounds_pass": rounds_pass,
            "rounds_fail": len(evidence["rounds"]) - rounds_pass,
            "specials": len(SPECIALS),
            "specials_pass": specials_pass,
            "specials_fail": len(evidence["specials"]) - specials_pass,
            "layout_total": layout_total,
            "layout_pass": layout_pass,
            "layout_fail": layout_total - layout_pass,
            "failures": failures,
        }
        ok = (len(evidence["rounds"]) == args.rounds
              and rounds_pass == args.rounds
              and specials_pass == len(SPECIALS)
              and layout_pass == layout_total
              and evidence["preflight"].get("clear_cards", {}).get("ok")
              and evidence["preflight"].get("idle", {}).get("ok")
              and final.get("ok", False)
              and not failures)
        evidence["result"] = "PASS" if ok else "FAIL"
        evidence["finished"] = now_iso()

        outdir.mkdir(parents=True, exist_ok=True)
        write_json(outdir / "g3.json", evidence)
        write_text(outdir / "g3_summary.md", summary_markdown(evidence))

        log("")
        log("summary: rounds %d/%d, specials %d/%d, layout %d/%d, "
            "failures=%d, final_online=%s"
            % (rounds_pass, args.rounds, specials_pass, len(SPECIALS),
               layout_pass, layout_total, len(failures), final.get("ok")))
        if failures:
            for item in failures[:10]:
                log("  - " + item)
        log("evidence: %s" % (outdir / "g3.json"))
        log("summary : %s" % (outdir / "g3_summary.md"))
        log("RESULT %s" % evidence["result"])
    return evidence


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    ap = argparse.ArgumentParser(
        prog="run_g3.py",
        description="CARD-10..14：20 轮 3 卡栈 + 第 4 卡淘汰 + 4 个特例")
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    ap.add_argument("--timeout", type=float, default=3.0)
    ap.add_argument("--dump-timeout", type=float, default=30.0)
    ap.add_argument("--rounds", type=int, default=20, help="轮次（验收=20）")
    ap.add_argument("--outdir", default=None)
    ap.add_argument("--wait-ready", nargs="?", const="m2:READY", default=None,
                    metavar="SPEC", help="先等启动日志（默认 m2:READY）")
    ap.add_argument("--ready-timeout", type=float, default=20.0)
    ap.add_argument("--simulate", action="store_true",
                    help="用 pet_sim 模拟设备（无硬件；证据写 evidence/sim）")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)
    args.ready_timeout = args.ready_timeout

    if args.outdir:
        outdir = Path(args.outdir)
    else:
        outdir = HERE / "evidence" / ("sim" if args.simulate else "")

    try:
        dev = open_device(args)
    except SerialOpenError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        print("提示：设备未连接时可先跑离线自测："
              "python run_g3.py --simulate", file=sys.stderr)
        return 2
    try:
        evidence = run(dev, args, outdir)
    finally:
        dev.close()
    return 0 if evidence["result"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
