#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""run_g6.py - G6：合成键自动跑（两键短/长 + 菜单 + 静音持久化）。

依据 `docs/10-m3-development.md` §4（M3 串口命令）、§7（G6 用例）与
`docs/contracts/C07-keymap.md` §3/§4（键表与菜单项）。

每项默认 **20 次**，每次动作后读 `m` 断言（`MODE ...` 行，字段见 docs/10 §4；
真机 2026-10-31 追加 sel/key1/key2/pwr，工具两种格式都解析）。判定门槛：
每项 **≥19/20** PASS（`--iterations` 可改，门槛 = N-1）。

覆盖项：
  duty_key1_short   值班 KEY1 短：只记 SEND 日志，模式/相位/muted/done 不变
  duty_key1_long    值班 KEY1 长：muted 0↔1 翻转；随后 R 重启断言静音仍在
  duty_key2_short   值班 KEY2 短：进 POMO；再按一次回 DUTY
  duty_key2_long    值班 KEY2 长：开菜单 menu=1；再长按关闭
  pomo_key1_short   POMO 内 KEY1 短：IDLE→FOCUS→FOCUS_PAUSED→FOCUS
  pomo_key1_long    POMO 内 KEY1 长：→ IDLE 且 done 不变
  pomo_key2_short   POMO 内 KEY2 短：回 DUTY
  menu_key1_short   菜单内 KEY1 短：选中循环 0..4（sel 字段 / 帧推断）
  menu_key2_long    菜单内 KEY2 长：关闭菜单（POMO 下开/关，模式不变）
  menu_exec_mute    菜单 MUTE 项：muted 翻转，留在菜单
  menu_exec_back    菜单 BACK 项：关闭菜单
  menu_exec_wifi    菜单 WIFI (M5)：关闭菜单 + 日志含 M5
  menu_exec_bt      菜单 BT (M5)：关闭菜单 + 日志含 M5

帧证据：`g6_duty.png`、`g6_pomo_idle/focus/paused.png`、`g6_menu_sel0..4.png`
均由 `d` 导出 → PNG → `layout_checks_m3.py`（POMO/MENU）或 `layout_checks.py`
（值班页）做像素断言。

用法::

    python run_g6.py                        # 真机 COM7 → evidence/g6.json
    python run_g6.py --simulate             # 无设备全链路自测 → evidence/sim/
    python run_g6.py --iterations 2 --simulate   # 快速冒烟

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

DESC = {
    "duty_key1_short": "值班 KEY1 短：只记 SEND 日志，模式/相位/muted/done 不变",
    "duty_key1_long": "值班 KEY1 长：muted 0<->1 翻转",
    "duty_key2_short": "值班 KEY2 短：DUTY->POMO，再按回 DUTY",
    "duty_key2_long": "值班 KEY2 长：开菜单 menu=1，再长按关闭",
    "pomo_key1_short": "POMO KEY1 短：IDLE->FOCUS->FOCUS_PAUSED->FOCUS",
    "pomo_key1_long": "POMO KEY1 长：->IDLE 且 done 不变",
    "pomo_key2_short": "POMO KEY2 短：回 DUTY",
    "menu_key1_short": "菜单 KEY1 短：选中循环 0..4",
    "menu_key2_long": "菜单 KEY2 长：关闭菜单",
    "menu_exec_mute": "菜单 MUTE 项：muted 翻转并留在菜单",
    "menu_exec_back": "菜单 BACK 项：关闭菜单",
    "menu_exec_wifi": "菜单 WIFI (M5)：关闭菜单 + 日志 M5",
    "menu_exec_bt": "菜单 BT (M5)：关闭菜单 + 日志 M5",
}
ITEM_ORDER = list(DESC.keys())


# --------------------------------------------------------------------------
# item implementations：返回 (ok, detail)；状态断言全部来自 `m`
# --------------------------------------------------------------------------

def _fmt(st, keys=("mode", "menu", "phase", "muted", "done", "focus_s")):
    return " ".join("%s=%s" % (k, st.get(k)) for k in keys)


def item_duty_key1_short(r, i):
    r.ensure_duty()
    before = r.mode()
    after = r.key("K")
    ok = (after["mode"] == MODE_DUTY and after["menu"] == 0
          and after["phase"] == "IDLE"
          and after["muted"] == before["muted"]
          and after["done"] == before["done"])
    return ok, "before[%s] after[%s]" % (_fmt(before), _fmt(after))


def item_duty_key1_long(r, i):
    r.ensure_duty()
    before = r.mode()
    after = r.key("L")
    expected = 0 if before["muted"] else 1
    ok = (after["muted"] == expected and after["mode"] == MODE_DUTY
          and after["menu"] == 0 and after["phase"] == "IDLE")
    return ok, "muted %d->%d expect %d; %s" % (
        before["muted"], after["muted"], expected, _fmt(after))


def item_duty_key2_short(r, i):
    r.ensure_duty()
    st1 = r.key("J")
    ok1 = st1["mode"] == MODE_POMO and st1["menu"] == 0
    st2 = r.key("J")
    ok2 = st2["mode"] == MODE_DUTY
    return ok1 and ok2, "->%s ->%s" % (_fmt(st1), _fmt(st2))


def item_duty_key2_long(r, i):
    r.ensure_duty()
    st1 = r.key("H")
    ok1 = st1["menu"] == 1 and st1["mode"] == MODE_DUTY
    st2 = r.key("H")
    ok2 = st2["menu"] == 0
    return ok1 and ok2, "open[%s] close[%s]" % (_fmt(st1), _fmt(st2))


def item_pomo_key1_short(r, i):
    st0 = r.ensure_pomo()
    done0 = st0["done"]
    st1 = r.key("K")
    ok1 = st1["phase"] == "FOCUS"
    st2 = r.key("K")
    ok2 = st2["phase"] == "FOCUS_PAUSED"
    st3 = r.key("K")
    ok3 = st3["phase"] == "FOCUS"
    st4 = r.key("L")
    ok4 = st4["phase"] == "IDLE" and st4["done"] == done0
    return (ok1 and ok2 and ok3 and ok4,
            "IDLE->%s->%s->%s->abandon[%s] done %d->%d" % (
                st1["phase"], st2["phase"], st3["phase"], st4["phase"],
                done0, st4["done"]))


def item_pomo_key1_long(r, i):
    st0 = r.ensure_pomo()
    done0 = st0["done"]
    st1 = r.key("K")
    st2 = r.key("L")
    ok = (st1["phase"] == "FOCUS" and st2["phase"] == "IDLE"
          and st2["mode"] == MODE_POMO and st2["done"] == done0)
    return ok, "FOCUS -> abandon[%s] done %d->%d" % (
        st2["phase"], done0, st2["done"])


def item_pomo_key2_short(r, i):
    r.ensure_pomo()
    st1 = r.key("J")
    ok1 = st1["mode"] == MODE_DUTY
    st2 = r.key("J")
    ok2 = st2["mode"] == MODE_POMO
    return ok1 and ok2, "->%s ->%s" % (_fmt(st1), _fmt(st2))


def item_menu_key1_short(r, i, iterations=20, evidence_cache=None):
    """菜单内 KEY1 短：打开一次菜单，连续按 K，断言 sel 循环 0->1->..->4->0。"""
    cache = evidence_cache if evidence_cache is not None else {}
    if not cache.get("menu_open"):
        r.ensure_duty()
        st = r.key("H")
        cache["menu_open"] = True
        cache["open_state"] = st
        cache["open_ok"] = (st["menu"] == 1 and st.get("sel") == 0)
        if cache["open_ok"]:
            rec = r.capture("g6_menu_sel0", "menu", selected=0,
                            mode=st["mode"], muted=st["muted"],
                            done=st["done"], focus_s=st["focus_s"])
            cache.setdefault("frames", []).append(rec)
    expected = i % 5                 # 已按 i 次，选中行 = i mod 5
    st2 = r.key("K")
    sel, src = r.menu_selected(st2)
    ok = st2["menu"] == 1 and sel == expected and cache.get("open_ok")
    if 1 <= i <= 4:
        rec = r.capture("g6_menu_sel%d" % expected, "menu",
                        selected=expected, mode=st2["mode"],
                        muted=st2["muted"], done=st2["done"],
                        focus_s=st2["focus_s"])
        cache.setdefault("frames", []).append(rec)
    if i >= iterations:              # 收尾关闭菜单
        r.key("H")
        cache["menu_open"] = False
    return ok, "K#%d -> sel=%s expect %d (%s) open_sel0=%s" % (
        i, sel, expected, src, cache.get("open_ok"))


def _close_menu(r):
    st = r.mode()
    if st["menu"]:
        st = r.key("H")
    return st


def item_menu_key2_long(r, i):
    st0 = r.ensure_pomo()          # 在 POMO 下开菜单，关闭后应回 POMO
    st1 = r.key("H")
    ok1 = st1["menu"] == 1 and st1["mode"] == MODE_POMO
    st2 = r.key("H")
    ok2 = st2["menu"] == 0 and st2["mode"] == MODE_POMO
    st3 = r.key("J")               # 回 DUTY，保持后续用例干净
    ok3 = st3["mode"] == MODE_DUTY
    return ok1 and ok2 and ok3, "open[%s] close[%s] back[%s]" % (
        _fmt(st1), _fmt(st2), _fmt(st3))


def item_menu_exec_mute(r, i):
    r.ensure_duty()
    before = r.mode()
    st1 = r.key("H")
    st2 = r.key("J")
    expected = 0 if before["muted"] else 1
    ok = (st2["muted"] == expected and st2["menu"] == 1
          and st2["mode"] == MODE_DUTY)
    _close_menu(r)
    return ok, "muted %d->%d expect %d; menu stays=%d" % (
        before["muted"], st2["muted"], expected, st2["menu"])


def _goto_menu_item(r, index):
    st = r.key("H")
    if st["menu"] != 1:
        return st, False
    for _ in range(index):
        st = r.key("K")
    sel, src = r.menu_selected(st)
    return st, sel == index


def item_menu_exec_back(r, i):
    r.ensure_duty()
    st, ok_sel = _goto_menu_item(r, 4)   # BACK = 第 5 项
    st2 = r.key("J")
    ok = ok_sel and st2["menu"] == 0 and st2["mode"] == MODE_DUTY
    return ok, "sel=%s -> execute BACK -> menu=%d mode=%s" % (
        st.get("sel"), st2["menu"], st2["mode"])


def _exec_m5_item(r, index, label):
    r.ensure_duty()
    _st, ok_sel = _goto_menu_item(r, index)
    r.flush_input()
    r.key_quiet("J", wait=0.5)
    logs = r.drain(0.6)
    st = r.mode()
    m5 = [line for line in logs if "M5" in line.upper()]
    ok = ok_sel and st["menu"] == 0 and st["mode"] == MODE_DUTY and bool(m5)
    return ok, "sel_ok=%s menu=%d logs(M5)=%s" % (
        ok_sel, st["menu"], (m5[:2] if m5 else "none"))


def item_menu_exec_wifi(r, i):
    return _exec_m5_item(r, 2, "WIFI")


def item_menu_exec_bt(r, i):
    return _exec_m5_item(r, 3, "BT")


ITEMS = {
    "duty_key1_short": item_duty_key1_short,
    "duty_key1_long": item_duty_key1_long,
    "duty_key2_short": item_duty_key2_short,
    "duty_key2_long": item_duty_key2_long,
    "pomo_key1_short": item_pomo_key1_short,
    "pomo_key1_long": item_pomo_key1_long,
    "pomo_key2_short": item_pomo_key2_short,
    "menu_key1_short": item_menu_key1_short,
    "menu_key2_long": item_menu_key2_long,
    "menu_exec_mute": item_menu_exec_mute,
    "menu_exec_back": item_menu_exec_back,
    "menu_exec_wifi": item_menu_exec_wifi,
    "menu_exec_bt": item_menu_exec_bt,
}


# --------------------------------------------------------------------------
# summary markdown
# --------------------------------------------------------------------------

def summary_markdown(ev):
    out = ["# G6（合成键 / 菜单 / 静音持久化）跑测摘要", ""]
    out.append("- 时间：%s → %s" % (ev["started"], ev["finished"]))
    out.append("- 模式：%s（%s @ %s）"
               % (ev["mode"], ev["port"], ev["baud"]))
    out.append("- 每项次数：%d；门槛：每项 >=%d/%d PASS；**结论：%s**"
               % (ev["iterations"], ev["min_pass"], ev["iterations"],
                  ev["result"]))
    s = ev["summary"]
    out.append("- 项汇总：%d/%d 项达标；总迭代 %d，PASS %d，FAIL %d"
               % (s["items_ok"], s["items_total"], s["iterations_total"],
                  s["iterations_pass"], s["iterations_fail"]))
    out.append("- 帧布局断言：%d/%d PASS" % (s["layout_pass"], s["layout_total"]))
    if ev.get("error"):
        out += ["", "## 错误", "", "```", str(ev["error"]), "```"]
    out += ["", "## 每项结果", "",
            "| 项 | 说明 | 次数 | PASS | FAIL | 达标 |",
            "| --- | --- | --- | --- | --- | --- |"]
    for item in ev["items"]:
        out.append("| `%s` | %s | %d | %d | %d | %s |"
                   % (item["name"], item["description"], item["iterations"],
                      item["pass"], item["fail"],
                      "PASS" if item["ok"] else "**FAIL**"))
    out += ["", "## 静音持久化（R 重启）", ""]
    p = ev["persistence"].get("mute", {})
    out.append("- 重启前 muted=%s（额外翻转 %s 次使 muted=1）；重启后 muted=%s → %s"
               % (p.get("pre_reboot_muted"), p.get("extra_toggles"),
                  p.get("post_reboot", {}).get("muted"),
                  "PASS" if p.get("ok") else "**FAIL**"))
    out.append("- 重启后 `m`：`%s`"
               % (p.get("post_reboot", {}).get("raw", "-")))
    out += ["", "## 帧证据（d → PNG → 像素断言）", "",
            "| 帧 | 页面 | PNG | CRC32 | layout |",
            "| --- | --- | --- | --- | --- |"]
    for name, rec in ev["frames"].items():
        out.append("| %s | %s | `%s` | %s | %s |"
                   % (name, rec["page"], rec["png"], rec["crc32"],
                      "PASS" if rec["layout_ok"] else
                      "**FAIL** (" + ",".join(rec["layout_failed"]) + ")"))
    failed = [(item["name"], row) for item in ev["items"]
              for row in item["rows"] if not row["ok"]]
    if failed:
        out += ["", "## 失败明细（如实保留）", ""]
        for name, row in failed[:40]:
            out.append("- `%s` #%d：%s" % (name, row["iteration"],
                                           row["detail"]))
        if len(failed) > 40:
            out.append("- …（其余 %d 条见 g6.json）" % (len(failed) - 40))
    out += ["", "## 协议/环境备注", ""]
    for note in ev.get("protocol_notes", []):
        out.append("- " + note)
    out.append("- 日志采样（发送日志）：%s"
               % "; ".join(ev.get("send_log_sample", [])) if
               ev.get("send_log_sample") else "- 日志采样（发送日志）：未取得")
    out.append("")
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------
# main flow
# --------------------------------------------------------------------------

def run(r, args, outdir):
    ev = {
        "tool": "run_g6.py",
        "version": 1,
        "started": now_iso(),
        "finished": None,
        "mode": r.describe().get("mode", "serial"),
        "port": r.describe().get("port"),
        "baud": r.describe().get("baud"),
        "iterations": args.iterations,
        "min_pass": max(1, args.iterations - 1),
        "protocol_notes": [
            "m 行实际格式（2026-10-31 真机）比 docs/10 §4 多 sel/key1/key2/pwr；"
            "工具两种格式都解析（缺字段时 sel=None，菜单选中回退为帧推断）。",
            "M2 契约：15 s 无有效事件进 OFFLINE；本跑测用 `n` 心跳保活。",
        ],
        "preflight": {},
        "items": [],
        "persistence": {},
        "frames": {},
        "send_log_sample": [],
        "summary": {},
        "result": "FAIL",
        "error": None,
    }

    def fail(msg):
        ev["error"] = (ev["error"] + "; " if ev["error"] else "") + msg
        log("ERROR: " + msg)

    try:
        # ---- 预检：等 READY（可选）、恢复默认时长、归位 ----
        if args.wait_ready:
            line, _skipped = r.dev.wait_for_line(args.wait_ready,
                                                 args.ready_timeout)
            ev["preflight"]["wait_ready"] = {"spec": args.wait_ready,
                                             "line": line}
            log("preflight: READY -> %s" % line)
        st = r.ensure_duty()
        # 若上一轮 G5 用过 `t`（6 s/3 s），20 次迭代里 FOCUS 会到点，必须先恢复 25/5。
        if st["total_ms"] != 1500000 and st["phase"] == "IDLE":
            log("preflight: total_ms=%d != 1500000，发 R 重启恢复默认时长"
                % st["total_ms"])
            rb = r.reboot()
            ev["preflight"]["restore_default_durations"] = {
                "reason_total_ms": st["total_ms"], "reboot": rb}
            st = r.ensure_duty()
        ev["preflight"]["initial"] = st
        ev["preflight"]["total_ms"] = st["total_ms"]
        ev["preflight"]["m_raw"] = st.get("raw")
        if st["total_ms"] != 1500000:
            fail("预检后 total_ms=%d，期望默认 1500000（`t` 缩短时长未恢复）"
                 % st["total_ms"])

        # ---- 值班页帧 ----
        r.heartbeat(True)
        stt = r.status()
        ev["frames"]["g6_duty"] = r.capture(
            "g6_duty", "duty", state=stt["state"], cards=stt["cards"])
        # ---- 番茄钟页帧（IDLE / FOCUS / PAUSED）----
        st = r.ensure_pomo()
        ev["frames"]["g6_pomo_idle"] = r.capture(
            "g6_pomo_idle", "pomo", phase=st["phase"],
            remain_ms=st["remain_ms"], total_ms=st["total_ms"],
            done=st["done"], focus_s=st["focus_s"])
        st = r.key("K")
        ev["frames"]["g6_pomo_focus"] = r.capture(
            "g6_pomo_focus", "pomo", phase=st["phase"],
            remain_ms=st["remain_ms"], total_ms=st["total_ms"],
            done=st["done"], focus_s=st["focus_s"])
        st = r.key("K")
        ev["frames"]["g6_pomo_paused"] = r.capture(
            "g6_pomo_paused", "pomo", phase=st["phase"],
            remain_ms=st["remain_ms"], total_ms=st["total_ms"],
            done=st["done"], focus_s=st["focus_s"])
        r.key("L")  # 放弃，回 IDLE
        r.ensure_duty()

        # ---- 13 项 × N 次 ----
        cache = {"frames": []}
        for order, name in enumerate(ITEM_ORDER, 1):
            fn = ITEMS[name]
            if name == "menu_key1_short":
                fn = (lambda rr, ii, base=fn, its=args.iterations,
                      c=cache: base(rr, ii, iterations=its,
                                    evidence_cache=c))
            rows = []
            log("[%02d/%d] %s (%d 次)"
                % (order, len(ITEM_ORDER), name, args.iterations))
            for i in range(1, args.iterations + 1):
                try:
                    ok, detail = fn(r, i)
                except (SerialTimeout, PetSerialError) as exc:
                    ok, detail = False, "exception: %s: %s" % (
                        type(exc).__name__, exc)
                rows.append({"iteration": i, "time": now_iso(),
                             "ok": bool(ok), "detail": detail})
            passed = sum(1 for row in rows if row["ok"])
            item = {
                "name": name,
                "description": DESC[name],
                "iterations": args.iterations,
                "pass": passed,
                "fail": args.iterations - passed,
                "pass_rate": round(passed / float(args.iterations), 4),
                "min_pass": ev["min_pass"],
                "ok": passed >= ev["min_pass"],
                "rows": rows,
            }
            ev["items"].append(item)
            log("        -> %d/%d PASS (%s)"
                % (passed, args.iterations, "OK" if item["ok"] else "FAIL"))

        # 菜单选中帧证据（item 内捕获，收进 frames）
        for rec in cache["frames"]:
            ev["frames"].setdefault(rec["name"], rec)

        # ---- 静音持久化：R 重启后 muted 仍为 1 ----
        st = r.mode()
        extra = 0
        while st["muted"] != 1 and extra < 3:
            st = r.key("L")
            extra += 1
        rec = {"extra_toggles": extra, "pre_reboot_muted": st["muted"]}
        try:
            rb = r.reboot()
            rec["reboot"] = rb
            st2 = r.mode()
            rec["post_reboot"] = {
                "muted": st2["muted"], "mode": st2["mode"],
                "phase": st2["phase"], "done": st2["done"],
                "has_day": st2["has_day"], "day": st2["day"],
                "raw": st2.get("raw"),
            }
            rec["ok"] = (st2["muted"] == 1)
        except (SerialTimeout, PetSerialError) as exc:
            rec["ok"] = False
            rec["error"] = "%s: %s" % (type(exc).__name__, exc)
        ev["persistence"]["mute"] = rec
        if not rec["ok"]:
            fail("静音重启持久化失败：%s" % rec.get("error", "muted != 1"))

        # ---- 发送日志采样（证据，不影响判定）----
        try:
            r.ensure_duty()
            r.flush_input()
            r.key_quiet("K", wait=0.4)
            logs = r.drain(0.5)
            ev["send_log_sample"] = logs[:5]
            lines = [ln for ln in logs
                     if "send" in ln.lower() or "KEY1" in ln]
            if not lines:
                ev["protocol_notes"].append(
                    "KEY1 短按（DUTY/SEND）日志未匹配 send/KEY1 字样；"
                    "采样：%r（记录差异，不改协议）" % (logs[:3],))
        except (SerialTimeout, PetSerialError) as exc:
            ev["protocol_notes"].append("日志采样失败：%s" % exc)

    except (SerialTimeout, PetSerialError, OSError) as exc:
        fail("%s: %s" % (type(exc).__name__, exc))

    finally:
        # ---- 汇总 ----
        items = ev["items"]
        total_iter = sum(it["iterations"] for it in items)
        pass_iter = sum(it["pass"] for it in items)
        frames = list(ev["frames"].values())
        layout_pass = sum(1 for f in frames if f["layout_ok"])
        ev["summary"] = {
            "items_total": len(ITEM_ORDER),
            "items_ok": sum(1 for it in items if it["ok"]),
            "iterations_total": total_iter,
            "iterations_pass": pass_iter,
            "iterations_fail": total_iter - pass_iter,
            "layout_total": len(frames),
            "layout_pass": layout_pass,
            "layout_fail": len(frames) - layout_pass,
        }
        ok = (len(items) == len(ITEM_ORDER)
              and all(it["ok"] for it in items)
              and ev["persistence"].get("mute", {}).get("ok")
              and len(frames) >= 5 + min(args.iterations, 4)
              and layout_pass == len(frames)
              and ev["error"] is None)
        ev["result"] = "PASS" if ok else "FAIL"
        ev["finished"] = now_iso()
        outdir.mkdir(parents=True, exist_ok=True)
        write_json(outdir / "g6.json", ev)
        write_text(outdir / "g6_summary.md", summary_markdown(ev))
        log("")
        log("summary: items %d/%d ok, iterations %d/%d PASS, layout %d/%d PASS, "
            "mute_persist=%s"
            % (ev["summary"]["items_ok"], ev["summary"]["items_total"],
               pass_iter, total_iter, layout_pass, len(frames),
               ev["persistence"].get("mute", {}).get("ok")))
        if ev["error"]:
            log("error: %s" % ev["error"])
        log("evidence: %s" % (outdir / "g6.json"))
        log("summary : %s" % (outdir / "g6_summary.md"))
        log("RESULT %s" % ev["result"])
    return ev


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    ap = argparse.ArgumentParser(
        prog="run_g6.py",
        description="G6：合成键自动跑（13 项 × N 次）+ 菜单/番茄钟键语义 + "
                    "静音持久化")
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    ap.add_argument("--timeout", type=float, default=3.0,
                    help="单次协议等待超时（默认 %(default)s s）")
    ap.add_argument("--dump-timeout", type=float, default=30.0)
    ap.add_argument("--iterations", type=int, default=20,
                    help="每项次数（验收=20，门槛 N-1）")
    ap.add_argument("--wait", type=float, default=0.25,
                    help="每个合成键后的等待秒数（默认 %(default)s）")
    ap.add_argument("--outdir", default=None, help="证据目录")
    ap.add_argument("--wait-ready", nargs="?", const="READY", default=None,
                    metavar="SPEC", help="先等启动日志（默认 READY）")
    ap.add_argument("--ready-timeout", type=float, default=20.0)
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
              "python run_g6.py --simulate --iterations 2", file=sys.stderr)
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
