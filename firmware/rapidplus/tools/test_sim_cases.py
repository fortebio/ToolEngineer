#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Guard for the simulated-run dataset (tools/sim_cases.py, tools/simcases/, tools/run_sim_cases.py).

    python tools/test_sim_cases.py

What it pins - and what it deliberately cannot:
  1. Every well lands on its intended branch in the MIRROR, with the Ct band, climb count and
     flag it claims. A recipe that drifted (someone edited a threshold in define.h, someone
     retuned a well) shows up here before anyone burns it into a unit.
  2. The committed tools/simcases/ files are exactly what `gen` produces today. The .expect.json
     files carry the mirror numbers, so a mirror edit that changes a single verdict fails this too.
  3. Every threshold the mirror uses is actually READ from the source tree, not silently defaulted -
     a renamed #define would otherwise leave the pre-screen checking against yesterday's number.
  4. Hardware limits: <= 2000 B per slot message (recvData[2048]), raw in 10..60000 on every slot
     (the /reviewlast record scan treats slot-0 raw outside that range as "no record"), exactly ten
     wells per scenario, deterministic output.
  5. The serial parser survives the NetworkTask's "[dash] ..." line landing inside a JSON line, and
     takes the climb count from the "neutralised" line (the JSON's climbs_fixed is stale on the
     Serial path - sensor6035.cpp serialises before it assigns).
  6. `uploadResult` (the UART way onto the "Up Data" path, --upload in the runner) is still in the
     firmware, still dispatched, still refuses while the device is busy or off STA, and still lands
     on eUpLoadData - not escreenFinished, whose 'f' path would tag the payload "Auto" and whose
     WHITE reboots. And the runner reads the [up] markers the way Bluetooth.cpp prints them.
  7. --regrade of the committed evidence log still finds every scenario where it is. The first
     version located scenario 0 by "total getResult sections minus catalogue size", which is off
     by one whenever the log ends with the restore's review - every well then compared against
     the NEXT scenario's echo and came back STALE, and nobody noticed because the report just
     said "re-run on the unit".

It cannot tell whether the FIRMWARE agrees with any of it. Only `run_sim_cases.py COM7` can, and
the point of this guard is that when that run fails, the dataset is not the suspect.
"""
import io
import json
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import sim_cases as sc          # noqa: E402
import run_sim_cases as rsc     # noqa: E402

fails = []


def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        fails.append(msg)


def main():
    loops = sc.PARAMS["amplification_time"]
    interval = sc.PARAMS["timePerLoop"] // 1000
    print("firmware numbers: loops %d x %d s, min_increase %g, min_sharpness %g, margin %g, MIN_CALLABLE_CT %g"
          % (loops, interval, sc.PARAMS["min_increase"], sc.PARAMS["min_sharpness"],
             sc.PARAMS["detection_margin_time"], sc.CONSTS["MIN_CALLABLE_CT"]))

    # 3. thresholds really come from the tree
    define_h = open(os.path.join(ROOT, "src", "define.h"), encoding="utf-8", errors="replace").read()
    for key in ("min_increase", "min_sharpness", "min_slight_positive_time", "detect_shape",
                "detection_margin_time", "arm_percentile", "transition_percentile", "sg_order",
                "sg_window", "baseline_start", "baseline_range", "amplification_time", "timePerLoop"):
        check(re.search(r"^\s*(?:double|uint8_t|bool|ulong|uint16_t)\s+%s\s*=" % key, define_h, re.M) is not None,
              "define.h declares %s (mirror reads it from there)" % key)
    algo_src = "".join(open(os.path.join(ROOT, "src", p), encoding="utf-8", errors="replace").read()
                       for p in ("Alg/Algo.h", "Alg/Algo.cpp", "sensor6035.cpp"))
    for key in sc.DEFAULT_CONSTS:
        check(re.search(r"^\s*#define\s+%s\s+[0-9.]+" % key, algo_src, re.M) is not None,
              "#define %s found in Alg/Algo.h, Alg/Algo.cpp or sensor6035.cpp" % key)

    # 1. every well on its intended branch (quiet unless something is off)
    buf = io.StringIO()
    old = sys.stdout
    sys.stdout = buf
    try:
        n, problems = sc.screen(loops, interval)
    finally:
        sys.stdout = old
    check(n >= 100, "pre-screen covered %d wells (>= 100)" % n)
    check(not problems, "every well lands on its intended branch in the mirror (%d off)" % len(problems))
    for p in problems:
        print("         %s slot %d: %s" % p)

    # 4. hardware limits + determinism + ten wells
    scen = sc.all_scenarios(loops)
    check(len(scen) >= 9, "%d scenarios in the catalogue" % len(scen))
    worst = 0
    for s in scen:
        t1 = sc.materialise(s, loops, interval)
        t2 = sc.materialise(s, loops, interval)
        check(t1 == t2, "%s: deterministic" % s["id"])
        check(len(s["wells"]) == 10 and len(t1) == 10, "%s: exactly ten wells" % s["id"])
        for i, tr in enumerate(t1):
            raw = sc.to_raw(tr, 1.74, 0)   # the steepest slope seen on RPL01015
            payload = '{"Slot":[' + ",".join(str(v) for v in raw) + "]}" + str(i) + "#"
            worst = max(worst, len(payload))
            unclamped = [int(round(v * 1.74)) for v in tr]
            if not (10 <= min(unclamped) and max(unclamped) <= 60000):
                check(False, "%s slot %d: raw %d..%d leaves 10..60000 (would be clamped -> not the curve designed)"
                      % (s["id"], i + 1, min(unclamped), max(unclamped)))
    check(worst <= 2000, "largest slot message %d B <= 2000 (recvData[2048])" % worst)
    # a 40-minute unit must still fit
    worst130 = 0
    for s in sc.build_scenarios():
        for i, tr in enumerate(sc.materialise(s, 130, interval)):
            raw = sc.to_raw(tr, 1.74, 0)
            worst130 += 0
            worst130 = max(worst130, len('{"Slot":[' + ",".join(str(v) for v in raw) + "]}" + str(i) + "#"))
    check(worst130 <= 2000, "largest slot message at 130 rounds %d B <= 2000" % worst130)

    # 2. committed files are current
    tmp = tempfile.mkdtemp(prefix="simcases-")
    buf = io.StringIO()
    sys.stdout = buf
    try:
        sc.write_outputs(loops, interval, [1.4] * 10, [0.0] * 10, tmp)
    finally:
        sys.stdout = old
    stale = []
    for name in sorted(os.listdir(tmp)):
        if not name.endswith((".cal.txt", ".expect.json", ".raw.txt", "README.md")):
            continue
        a = os.path.join(tmp, name)
        b = os.path.join(sc.OUT_DIR, name)
        if not os.path.isfile(b):
            stale.append(name + " (missing)")
            continue
        # autocrlf is on in this repo: a fresh checkout hands back CRLF while gen writes LF.
        norm = lambda path: open(path, "rb").read().replace(b"\r\n", b"\n")  # noqa: E731
        if norm(a) != norm(b):
            stale.append(name)
    check(not stale, "tools/simcases/ matches `sim_cases.py gen` (%s)" % (", ".join(stale) if stale else "all current"))
    if stale:
        print("         run: python tools/sim_cases.py gen")

    # 5. serial parser
    doc = {"outcome": {"outcome": "Positive", "transition_time": {"x": 8.67, "y": 1.2, "i": 26},
                       "plateau_point": {"x": 15, "y": 90, "i": 45}, "increase": 86.4, "suspect_score": 0,
                       "arm_width": 2.3, "window_rate": 10, "rise_width": 4, "shape_flag": 0,
                       "climbs_fixed": 0, "climb_first_i": -1},
           "peak_features": {"main_peak": {"x": 10, "y": 23.9, "i": 30}, "right_arm": {"x": 11, "y": 12, "i": 33},
                             "left_arm": {"x": 9, "y": 11, "i": 27}},
           "time_data": [0, 0.33], "raw_data": [300.1, 301.2], "processed_data": [0, 0], "differential_data": [0, 0]}
    j = json.dumps(doc, separators=(",", ":"))
    cut = j.index('"raw_data"') + 5
    text = ("Slot 1: neutralised 1 vertical climb(s)\nSlot 1:\nOutcome check: Positive\nIndex Rising data : 7.0\n"
            "Index Break data : 0.0\nIndex Transition time: 8.666667\n" + j[:cut] +
            "[dash] heap free=87528 maxAlloc=51188 clients=0 ap=0\n" + j[cut:] + "\n\n"
            "<PIDControl.cpp>:<123> a debug line\r\nSlot 2:\nOutcome check: Break\nIndex Rising data : 0.0\n"
            "Index Break data : 13.0\nIndex Transition time: -0.333333\n" + j.replace('"Positive"', '"Break"') + "\n")
    res = rsc.parse_results(text)
    check(res.get(0, {}).get("letter") == "P" and abs(res[0].get("ct", 0) - 8.67) < 1e-9 and len(res[0].get("raw_data", [])) == 2,
          "parser re-joins a JSON line split by a [dash] log line")
    check(res.get(0, {}).get("climbs") == 1 and res.get(1, {}).get("climbs") == 0,
          "parser takes the climb count from the 'neutralised' line, not the stale JSON field")
    check(res.get(1, {}).get("letter") == "B", "parser maps 'Break' to B")

    # 6. uploadResult: the firmware side and the runner's reading of it
    fs = open(os.path.join(ROOT, "src", "ForteSetting.cpp"), encoding="utf-8", errors="replace").read()
    body = re.search(r"bool ForteSetting::uploadResult\(\)\s*\{(.*?)\n\}", fs, re.S)
    check(body is not None, "ForteSetting.cpp defines uploadResult()")
    if body:
        b = body.group(1)
        check("dashboardDeviceBusy()" in b and "refused" in b, "uploadResult() refuses while dashboardDeviceBusy()")
        check("WL_CONNECTED" in b and "dashboardIsAP()" in b, "uploadResult() refuses without an STA link")
        check("type_infor = eUpLoadData" in b and "escreenFinished" not in b,
              "uploadResult() lands on eUpLoadData (type_Upload Manual, WHITE returns without a reboot)")
        check(b.index("type_infor = eUpLoadData") < b.index("changeScreen = true"),
              "uploadResult() writes the state before the redraw flag")
    check(re.search(r"else if \(uploadResult\(\)\)", fs) is not None, "loop() dispatches uploadResult()")
    bt = open(os.path.join(ROOT, "src", "Bluetooth.cpp"), encoding="utf-8", errors="replace").read()
    for lit in ('"[up] %s POST OK in %u ms, code=%d (try %u)\\n"',
                '"[up] %s POST FAIL in %u ms, code=%d (%s) try %u/%u%s\\n"',
                '"ERP server feedback: %s\\n"', '"[up] %s -> GAS + ingest + ERP (%u B)\\n"'):
        check(lit in bt, "Bluetooth.cpp still prints %s (the runner keys on it)" % lit)
    up_text = ("[up] uploadResult refused: device busy\n")
    u = rsc.parse_upload(up_text, 0.2)
    check(u["refused"] == "device busy" and not u["ok"], "parse_upload: a refusal is reported, not counted as sent")
    up_text = ("Slot 1:\nOutcome check: Positive\nIndex Rising data : 7.0\n\nSlot 2:\nOutcome check: Break\n\n"
               "[up] begin: suspend dashboard + build JSON\n[up] data -> GAS + ingest + ERP (24812 B)\n"
               "[up] GAS POST begin (free=90000 intLargest=60000 try 1/3)\n"
               "[up] GAS POST FAIL in 60012 ms, code=-11 (read Timeout) try 1/3\n"
               "[up] GAS POST begin (free=90000 intLargest=60000 try 2/3)\n"
               "[up] GAS POST OK in 6123 ms, code=302 (try 2)\n"
               "[up] ingest POST OK in 2301 ms, code=200 (try 1)\nEngineer server feedback: {\"ok\":true}\n"
               "[up] ERP POST FAIL in 1900 ms, code=422 (Unprocessable) try 1/3 [no retry]\nERP server feedback: {\"detail\":\"x\"}\n")
    u = rsc.parse_upload(up_text, 71.0)
    check(u["targets"].get("GAS", {}).get("ok") and u["targets"]["GAS"]["tries"] == 2 and u["targets"]["GAS"]["code"] == 302,
          "parse_upload: a retry that succeeds counts as OK with its try number")
    check(u["targets"].get("ERP", {}).get("code") == 422 and not u["targets"]["ERP"]["ok"] and not u["ok"],
          "parse_upload: a non-retryable FAIL is final and makes the upload INCOMPLETE")
    check(u["bytes"] == 24812 and u["letters"] == ["P", "B"] and u["feedback"]["ingest"] == '{"ok":true}',
          "parse_upload: payload size, upload-path letters and server feedback are read")

    # 7. regrade of the committed evidence log: scenario 0 is found after the backup read
    evidence = os.path.join(ROOT, "docs", "reports", "simcases", "2026-09-15-1201-RPL01015.serial.log")
    if os.path.isfile(evidence):
        import types
        out = tempfile.mkdtemp(prefix="simcases-regrade-")
        ns = types.SimpleNamespace(regrade=evidence, only=None, out=out)
        buf = io.StringIO()
        sys.stdout = buf
        try:
            rsc.regrade(ns, "guard", "guard")
        finally:
            sys.stdout = old
        rep_json = [n for n in os.listdir(out) if n.endswith(".json")]
        counts = {}
        if rep_json:
            with open(os.path.join(out, rep_json[0]), "r", encoding="utf-8") as f:
                for sc_ in json.load(f)["scenarios"]:
                    for w in sc_["wells"]:
                        counts[w["status"]] = counts.get(w["status"], 0) + 1
        check(counts.get("PASS", 0) >= 90 and counts.get("FAIL", 0) == 0 and counts.get("NOREPLY", 0) == 0,
              "regrade of the 12:01 evidence log: %s (>= 90 PASS, 0 FAIL/NOREPLY; all-STALE means the section offset is wrong)"
              % ", ".join("%s %d" % kv for kv in sorted(counts.items())))
    else:
        check(False, "evidence log %s is missing" % os.path.relpath(evidence, ROOT))

    print("\n%s: %d check(s) failed" % ("FAIL" if fails else "PASS", len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
