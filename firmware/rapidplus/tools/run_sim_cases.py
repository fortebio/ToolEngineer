#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Send the simulated runs to a REAL unit over UART and grade its verdicts.

    python tools/run_sim_cases.py COM7                       # every scenario, report under docs/reports/simcases/
    python tools/run_sim_cases.py COM7 --only S06            # one scenario (substring of its id)
    python tools/run_sim_cases.py COM7 --file my.cal.txt     # any calibrated file (one slot per line); graded only if
                                                             #   a sibling .expect.json exists
    python tools/run_sim_cases.py COM7 --keep                # leave the last scenario in the unit (look at it on the web)
    python tools/run_sim_cases.py COM7 --dry-run             # no port: show what would be sent
    python tools/run_sim_cases.py COM7 --regrade <serial.log> # grade an earlier run's log again (no unit needed)
    python tools/run_sim_cases.py COM7 --restore-from <log>  # put back the run the first getResult of that log captured

The unit must be IDLE on the start screen. Nothing here uploads: `getResult` reviews the
stored record on the TFT (escreenReview -> screen_Result('r')), and only the 'f' path posts.

PROTOCOL (ForteSetting.cpp)
  ParaRead                      -> one JSON line ending in '@': slopes, origins, thresholds, loops, ms/round
  {"Slot":[v0,...]}<n>#          -> start_amplification_simulation(): RAW counts into sensor67Value[n] + EEPROM.
                                   readCommand() needs 1 s of line silence to see the end of a message, so the
                                   next slot goes out only after the unit has echoed "Json data received".
  getResult                     -> resultOutput(): the stored record is re-analysed by bResultGet(), which prints
                                   per slot "Outcome check: <word>" and one JSON line (outcome, Ct, increase,
                                   shape_flag, climbs_fixed, ...). Repeatable without a reboot.

WHAT GETS SENT
  Every scenario is re-materialised for THIS unit: its loop count, its ms/round, and its own slopes/origins
  (raw = cal*slope + origin, rounded to the integer the ADC would have produced). The committed
  tools/simcases/*.raw.txt use nominal slopes and are for send_slots.py; do not send those here.

THE RECORD IS OVERWRITTEN - and put back
  Injection replaces the unit's stored last run. Before the first scenario the runner asks `getResult` once and
  keeps the ten raw_data arrays it gets back; after the last scenario (or on Ctrl+C) it injects them again.
  Caveat, stated rather than hidden: bResultGet() prints raw_data AFTER neutralise_climbs(), so a stored run
  that carried a repaired step is put back repaired. climbs_fixed is checked and the report says so if it
  happened. --keep skips the restore; --no-backup skips the read.

GRADING (per well, from <id>.expect.json)
  PASS        letter in expect+accept; Ct inside its band when one is given; climbs_fixed / shape_flag as expected
  KNOWN-WEAK  letter == `known` (a documented weakness of the current algorithm) - neither pass nor fail
  FAIL        anything else
  INFO        expect is "?" - reported, never graded
The mirror column is tools/sim_cases.py's PC re-implementation, printed beside the unit for diagnosis only.

Requires: pip install pyserial
"""
import argparse
import datetime
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sim_cases as sc  # noqa: E402

ROOT = sc.ROOT
REPORT_DIR = os.path.join(ROOT, "docs", "reports", "simcases")
LETTER = {"Positive": "P", "Negative": "N", "Slight Positive": "S", "Error": "E", "Break": "B", "Flagged": "F"}


# --------------------------------------------------------------------------- serial
class Unit(object):
    def __init__(self, port, baud, log):
        import serial  # pyserial
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = baud
        self.ser.timeout = 0.1
        self.ser.dtr = False   # a DTR/RTS pulse resets the ESP32
        self.ser.rts = False
        self.ser.open()
        self.log = log
        time.sleep(0.3)
        self.ser.reset_input_buffer()

    def close(self):
        self.ser.close()

    def send(self, payload):
        self.log.write("\n>>> %s\n" % (payload if len(payload) < 120 else payload[:100] + "...(%d B)" % len(payload)))
        self.ser.write(payload.encode("ascii"))
        self.ser.flush()

    def read_until(self, predicate, timeout):
        """Collect output until predicate(text) is true or the timeout elapses. Returns text."""
        buf = bytearray()
        deadline = time.time() + timeout
        while time.time() < deadline:
            chunk = self.ser.read(self.ser.in_waiting or 1)
            if chunk:
                buf += chunk
                text = buf.decode("utf-8", "replace")
                if predicate(text):
                    # let the tail of the line arrive
                    time.sleep(0.15)
                    buf += self.ser.read(self.ser.in_waiting or 0)
                    break
        text = buf.decode("utf-8", "replace")
        self.log.write(text)
        self.log.flush()
        return text

    def para_read(self):
        self.send("ParaRead")
        text = self.read_until(lambda t: "@" in t and "There is para" in t or "No para" in t, 8.0)
        m = re.search(r"(\{.*\})@", text, re.S)
        if "No para" in text:
            raise RuntimeError("unit reports no parameters in EEPROM - cannot know its slopes; abort")
        if not m:
            raise RuntimeError("no ParaRead answer (wrong port? unit busy? not 115200?). Got:\n" + text[-400:])
        return json.loads(m.group(1))

    def inject_slot(self, idx, raw, timeout=6.0):
        payload = '{"Slot":[' + ",".join(str(v) for v in raw) + "]}" + str(idx) + "#"
        if len(payload) > 2000:
            raise RuntimeError("slot %d payload is %d B, over the 2 KB recvData buffer" % (idx, len(payload)))
        self.send(payload)
        text = self.read_until(lambda t: "Json data received" in t or "Error parsing" in t or "too long" in t
                               or "not supported" in t, timeout)
        if "Error parsing" in text or "too long" in text or "not supported" in text:
            raise RuntimeError("unit rejected slot %d: %s" % (idx, text.strip()[-200:]))
        if "Json data received" not in text:
            print("    [!] slot %d: no acknowledgement within %.0fs (sending on)" % (idx, timeout))
        time.sleep(0.4)  # EEPROM.put + commit before the next message lands
        return len(payload)

    def get_result(self, timeout=45.0):
        """-> (slots dict idx->result, raw text). Waits for 10 'Outcome check' lines + their JSON."""
        self.ser.reset_input_buffer()
        self.send("getResult")
        text = self.read_until(lambda t: t.count('"outcome":{"outcome"') >= 10 and t.count("Slot 10:") >= 1, timeout)
        # a short grace period: the last JSON line may still be streaming
        text += self.read_until(lambda t: False, 1.0)
        return parse_results(text), text


_NUM_CHARS = set("0123456789+-.eE")
_NUM_RE = re.compile(r"^-?\d+(\.\d+)?([eE][-+]?\d+)?$")
_LITERALS = ("true", "false", "null", "NaN", "Infinity")


class _JsonPrefix(object):
    """Consumes the longest prefix of a line that continues a JSON document of the shape
    bResultGet() prints: objects, FLAT arrays of numbers, strings, numbers, literals.

    Why this exists: serializeJson() writes the record in small chunks on the DisplayTask while
    other tasks print whole lines - "[dash] heap ..." (NetworkTask), "[stack]" plus a multi-line
    stack report, "[len] set(90) was=90 APPLIED", "finish one round maintenance" (SensorTask),
    HEADER_FORMAT "<file>:<line> ..." lines. They land anywhere, including between two digits of
    a number, and only the intruder ends with a newline. So each physical line is
    <json piece><intruder> or <intruder alone>, and the JSON is the concatenation of the pieces.
    The piece ends where the next character cannot continue the document: a letter outside a
    string, a '[' inside an array (nothing here nests), a token that is not a number/literal.
    Measured on the first run: 9 of 120 slot records were split this way, by four different
    prints. A regex per known print would be a list that is wrong the day a fifth is added."""

    def __init__(self):
        self.stack = []
        self.expect = "value"
        self.in_str = False
        self.esc = False
        self.kind = "str"
        self.partial = ""
        self.done = False

    def _after_value(self):
        if not self.stack:
            self.done = True
        else:
            self.expect = "comma_or_end"

    def _end_partial(self):
        tok, self.partial = self.partial, ""
        if _NUM_RE.match(tok) or tok in _LITERALS or tok == "-Infinity":
            self._after_value()
            return True
        return False

    def feed(self, s):
        """-> number of characters of s consumed as JSON; the rest of s is an intruder."""
        i = 0
        while i < len(s) and not self.done:
            c = s[i]
            if self.in_str:
                if self.esc:
                    self.esc = False
                elif c == "\\":
                    self.esc = True
                elif c == '"':
                    self.in_str = False
                    if self.kind == "key":
                        self.expect = "colon"
                    else:
                        self._after_value()
                elif not (c.isalnum() or c in "_ "):
                    return i      # keys and outcome words are [A-Za-z0-9_ ]: anything else is an intruder
                i += 1
                continue
            if self.partial:
                if (self.partial[0] in "-0123456789" and c in _NUM_CHARS) or \
                        (self.partial[0].isalpha() and c.isalpha()):
                    self.partial += c
                    i += 1
                    continue
                n = len(self.partial)
                if not self._end_partial():
                    return i - n
            if c in " \t\r":
                i += 1
                continue
            e = self.expect
            if e == "value_or_end" and c == "]":
                self.stack.pop()
                self._after_value()
                i += 1
                continue
            if e in ("value", "value_or_end"):
                if c == "{":
                    self.stack.append("{")
                    self.expect = "key"
                elif c == "[":
                    if self.stack and self.stack[-1] == "[":
                        return i          # nested array: not in this document, so an intruder
                    self.stack.append("[")
                    self.expect = "value_or_end"
                elif c == '"':
                    self.in_str, self.kind = True, "str"
                elif c in "-0123456789" or c.isalpha():
                    self.partial = c
                else:
                    return i
                i += 1
                continue
            if e == "key":
                if c == '"':
                    self.in_str, self.kind = True, "key"
                elif c == "}":
                    self.stack.pop()
                    self._after_value()
                else:
                    return i
                i += 1
                continue
            if e == "colon":
                if c != ":":
                    return i
                self.expect = "value"
                i += 1
                continue
            if e == "comma_or_end":
                top = self.stack[-1] if self.stack else ""
                if c == ",":
                    self.expect = "key" if top == "{" else "value"
                elif (c == "}" and top == "{") or (c == "]" and top == "["):
                    self.stack.pop()
                    self._after_value()
                else:
                    return i
                i += 1
                continue
            return i
        return i


# Prints that land INSIDE a JSON line WITHOUT a newline of their own, so the prefix scanner
# cannot tell where they end. main.cpp's stack report is eight separate Serial.print calls
# ("[stack]", " Control=2128/4096", ..., " | unused=27288 B\n") and any of them can fall
# between two digits. Their shapes are fixed by the source, so they are removed verbatim
# first; everything that ends in a newline is left to _JsonPrefix.
_KNOWN_FRAGMENTS = re.compile(
    r"\[stack\]"
    r"| (?:Control|Sensor|Display|Network|Input|Setting)=\d{1,6}/(?:2048|4096|6144|8192|10240|12288|16384)"
    r"| \| unused=\d+ B\r?\n?"
    r"|\[len\][^\n]*\n?"
    r"|\[dash\][^\n]*\n?"
    r"|finish one round maintenance\r?\n?"
    r"|<[A-Za-z0-9_.]+>:<\d+>[^\n]*\n?")


def parse_results(text):
    """Per-slot verdicts out of the bResultGet() serial print (see _JsonPrefix and
    _KNOWN_FRAGMENTS for the interleaving it survives)."""
    text = _KNOWN_FRAGMENTS.sub("", text)
    out = {}
    cur = None
    scanner = None
    pieces = []

    def finish_json():
        doc = None
        try:
            doc = json.loads("".join(pieces))
        except ValueError:
            pass
        if doc is None:
            return
        oc = doc.get("outcome", {})
        out[cur].update(
            json=True,
            word=oc.get("outcome", out[cur].get("word", "")),
            ct=float(oc.get("transition_time", {}).get("x", -1)),
            ct_i=int(oc.get("transition_time", {}).get("i", -1)),
            increase=float(oc.get("increase", -1)),
            sharpness=float(doc.get("peak_features", {}).get("main_peak", {}).get("y", -1)),
            flag=int(oc.get("shape_flag", 0)),
            climb_first=int(oc.get("climb_first_i", -1)),
            window_rate=float(oc.get("window_rate", -1)),
            rise_width=float(oc.get("rise_width", -1)),
            arm_width=float(oc.get("arm_width", -1)),
            suspect=int(oc.get("suspect_score", 0)),
            raw_data=doc.get("raw_data", []),
        )

    for raw_line in text.splitlines():
        line = raw_line.strip()
        if scanner is not None:
            n = scanner.feed(raw_line.rstrip("\r\n"))
            if n:
                pieces.append(raw_line[:n])
            if scanner.done:
                finish_json()
                scanner = None
                pieces = []
            elif re.match(r"Slot \d+:", line):
                scanner = None   # the next slot started: this record is lost, keep the letter
                pieces = []
            else:
                continue
        if not line or line.startswith("[") or line.startswith("<"):
            continue
        # "Slot N: neutralised K vertical climb(s)" is printed by bResultGet() BEFORE the
        # "Slot N:" header. It is the only trustworthy climb count on this path: the JSON is
        # serialised (sensor6035.cpp:418) before outcome.climbs_fixed is assigned (:427), so the
        # JSON always says 0 / -1 here. The upload path assigns first, so the payload is right.
        m = re.match(r"Slot (\d+): neutralised (\d+) vertical climb", line)
        if m:
            out.setdefault(int(m.group(1)) - 1, {})["climbs_line"] = int(m.group(2))
            continue
        m = re.match(r"Slot (\d+):\s*$", line)
        if m:
            cur = int(m.group(1)) - 1
            out.setdefault(cur, {})
            pending = ""
            continue
        if cur is None:
            continue
        m = re.match(r"Outcome check:\s*(.+)$", line)
        if m:
            out[cur]["word"] = m.group(1).strip()
            continue
        m = re.match(r"Index Transition time:\s*([-0-9.]+)", line)
        if m:
            out[cur]["ct_line"] = float(m.group(1))
            continue
        if line.startswith('{"outcome"'):
            scanner = _JsonPrefix()
            pieces = []
            n = scanner.feed(raw_line.rstrip("\r\n").lstrip())
            pieces.append(raw_line.lstrip()[:n])
            if scanner.done:
                finish_json()
                scanner = None
                pieces = []
    for r in out.values():
        r["letter"] = LETTER.get(r.get("word", ""), (r.get("word", "?")[:1] or "?"))
        r["climbs"] = r.get("climbs_line", 0)   # see the note above; the JSON value is stale on this path
        if "ct" not in r and "ct_line" in r:
            r["ct"] = r["ct_line"]
    return out


# --------------------------------------------------------------------------- unit parameters
def unit_params(pj):
    """ParaRead JSON -> (P dict for the mirror, slopes, origins, ident)."""
    op = pj.get("parameters", {})
    cal = pj.get("opto calibration", {})
    P = dict(sc.PARAMS)
    P.update(
        min_increase=float(op.get("min increase", P["min_increase"])),
        min_sharpness=float(op.get("min sharpness", P["min_sharpness"])),
        min_slight_positive_time=float(op.get("min slight positive time", P["min_slight_positive_time"])),
        detect_shape=bool(op.get("detect shape", P["detect_shape"])),
        detection_margin_time=float(op.get("detection margin time", P["detection_margin_time"])),
        arm_percentile=float(op.get("arm percentile", P["arm_percentile"])),
        transition_percentile=float(op.get("transition percentile", P["transition_percentile"])),
        sg_order=int(op.get("sg order", P["sg_order"])),
        sg_window=int(op.get("sg window", P["sg_window"])),
        baseline_start=int(op.get("baseline start", P["baseline_start"])),
        baseline_range=int(op.get("baseline range", P["baseline_range"])),
        amplification_time=int(pj.get("amplification time", P["amplification_time"])),
        timePerLoop=int(pj.get("time per loop", P["timePerLoop"])),
    )
    slopes = [float(v) for v in cal.get("slopes", [1.0] * 10)]
    origins = [float(v) for v in cal.get("origins", [0.0] * 10)]
    ident = dict(device=str(pj.get("device ID", "?")), para_version=str(pj.get("para version", "?")),
                 pcb=str(pj.get("PCB version", "?")))
    return P, slopes, origins, ident


# --------------------------------------------------------------------------- grading
def grade(well, r, mirror):
    """-> (status, reasons). status in PASS / FAIL / KNOWN-WEAK / INFO / NOREPLY."""
    if not r or "letter" not in r:
        return "NOREPLY", ["unit printed nothing for this slot"]
    exp = well.get("expect", "?")
    if exp == "?":
        return "INFO", []
    allowed = set(exp) | set(well.get("accept", []))
    L = r["letter"]
    why = []
    if L in allowed:
        if "ct" in well and L in "PSF":
            lo, hi = well["ct"]
            if not (lo <= r.get("ct", -1) <= hi):
                why.append("Ct %.2f outside %.1f..%.1f" % (r.get("ct", -1), lo, hi))
        if "climbs" in well and "climbs" in r and r["climbs"] != well["climbs"]:
            why.append("climbs_fixed %d, wanted %d" % (r["climbs"], well["climbs"]))
        if "flag" in well and L == "F" and "flag" in r and r["flag"] != well["flag"]:
            why.append("shape_flag %d, wanted %d" % (r["flag"], well["flag"]))
        return ("PASS" if not why else "FAIL"), why
    if well.get("known") and L == well["known"]:
        return "KNOWN-WEAK", ["documented: current algorithm answers %s, intent is %s" % (L, exp)]
    return "FAIL", ["letter %s, wanted %s%s" % (L, exp, ("/" + "".join(well.get("accept", []))) if well.get("accept") else "")]


def fmt(v, w=6, d=2):
    try:
        return ("%" + str(w) + "." + str(d) + "f") % float(v)
    except (TypeError, ValueError):
        return " " * (w - 1) + "-"


# --------------------------------------------------------------------------- report
def write_report(path_md, path_json, ident, P, slopes, origins, results, backup_note, started):
    n_pass = sum(1 for s in results for w in s["wells"] if w["status"] == "PASS")
    n_fail = sum(1 for s in results for w in s["wells"] if w["status"] == "FAIL")
    n_known = sum(1 for s in results for w in s["wells"] if w["status"] == "KNOWN-WEAK")
    n_info = sum(1 for s in results for w in s["wells"] if w["status"] in ("INFO", "NOREPLY", "STALE"))
    L = []
    L.append("# Kết quả chạy kịch bản mô phỏng trên máy %s — %s" % (ident["device"], started))
    L.append("")
    L.append("Máy: **%s** (para %s, PCB %s). Thuật toán trên máy: min_increase %g · min_sharpness %g · "
             "margin %g · slight %g · baseline %d+%d · SG %d/%d · %d vòng × %d ms." % (
                 ident["device"], ident["para_version"], ident["pcb"], P["min_increase"], P["min_sharpness"],
                 P["detection_margin_time"], P["min_slight_positive_time"], P["baseline_start"],
                 P["baseline_range"], P["sg_window"], P["sg_order"], P["amplification_time"], P["timePerLoop"]))
    L.append("Slopes: %s · origins: %s." % (", ".join("%.3f" % s for s in slopes), ", ".join("%g" % o for o in origins)))
    L.append("")
    L.append("**PASS %d · FAIL %d · KNOWN-WEAK %d · INFO/NOREPLY/STALE %d** trên %d giếng." % (
        n_pass, n_fail, n_known, n_info, n_pass + n_fail + n_known + n_info))
    if backup_note:
        L.append("")
        L.append(backup_note)
    L.append("")
    L.append("Cột *máy* là điều máy in ra qua `getResult`; cột *mirror* là bản PC (`tools/sim_cases.py`) trên "
             "cùng dữ liệu, chỉ để chẩn đoán. Hai cột lệch nhau nghĩa là mirror sai hoặc firmware đổi — "
             "máy là phép đo.")
    L.append("")
    for s in results:
        L.append("## %s — %s" % (s["id"], s["title"]))
        L.append("")
        L.append("| slot | kỳ vọng | máy | Ct | inc | sharp | climb | flag | mirror | Ct | trạng thái | ghi chú |")
        L.append("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
        for w in s["wells"]:
            r = w.get("unit") or {}
            m = w.get("mirror") or {}
            exp = w["expect"] + (("/" + "".join(w["accept"])) if w.get("accept") else "")
            mark = {"PASS": "✅", "FAIL": "❌", "KNOWN-WEAK": "⚠", "INFO": "ℹ", "NOREPLY": "∅", "STALE": "⏳"}[w["status"]]
            L.append("| %d | %s | **%s** | %s | %s | %s | %s | %s | %s | %s | %s %s | %s%s |" % (
                w["slot"], exp, r.get("letter", "-"), fmt(r.get("ct"), 5, 2).strip(), fmt(r.get("increase"), 5, 1).strip(),
                fmt(r.get("sharpness"), 5, 1).strip(), r.get("climbs", "-"), r.get("flag", "-"),
                m.get("letter", "-"), fmt(m.get("ct"), 5, 2).strip(), mark, w["status"], w["note"],
                (" — " + "; ".join(w["why"])) if w["why"] else ""))
        L.append("")
    with open(path_md, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")
    with open(path_json, "w", encoding="utf-8", newline="\n") as f:
        json.dump(dict(device=ident, params=P, slopes=slopes, origins=origins, started=started,
                       scenarios=results), f, indent=1)


def grade_scenario(s, traces, res, P, slopes, origins, stale=None):
    """Grade one scenario's unit answers (res: slot index -> parsed record) and print the rows.
    The mirror column is computed on the DEVICE VIEW of the trace (integer raw, float32 divide)
    so it sees the same numbers the unit did. stale[i] marks wells whose data in a re-graded log
    no longer matches the catalogue; they are reported, never graded."""
    srec = dict(id=s["id"], title=s["title"], wells=[])
    for i, w in enumerate(s["wells"]):
        m = sc.analyse_slot(sc.device_view(traces[i], slopes[i], origins[i]), P, sc.CONSTS)
        r = res.get(i)
        if stale and stale[i]:
            status, why = "STALE", ["catalogue data changed since this log; re-run on the unit"]
        else:
            status, why = grade(w, r, m)
        rec = dict(slot=i + 1, expect=w.get("expect", "?"), note=w.get("note", ""), status=status, why=why,
                   mirror=dict(letter=m["letter"], ct=round(m["ct"], 2), increase=round(m["increase"], 1),
                               sharpness=round(m["peak_y"], 2), climbs=m["climbs"], flag=m["flag"]),
                   unit={k: v for k, v in (r or {}).items() if k != "raw_data"})
        for k in ("accept", "ct", "climbs", "flag", "known"):
            if k in w:
                rec[k] = w[k]
        srec["wells"].append(rec)
        rr = r or {}
        print("  %-10s slot %2d  want %-6s unit %s ct %s inc %s sharp %s climbs %s flag %s | mirror %s ct %s  %s%s" % (
            status, i + 1, w.get("expect", "?") + (("/" + "".join(w["accept"])) if w.get("accept") else ""),
            rr.get("letter", "-"), fmt(rr.get("ct"), 5), fmt(rr.get("increase"), 6, 1), fmt(rr.get("sharpness"), 5, 1),
            rr.get("climbs", "-"), rr.get("flag", "-"), m["letter"], fmt(m["ct"], 5), w.get("note", ""),
            ("  <-- " + "; ".join(why)) if why else ""))
    return srec


def regrade(a, stamp, started):
    """Grade an earlier serial log again, without the unit. The log's ParaRead gives the unit's
    numbers; its getResult sections are matched to the catalogue IN ORDER (the first one is the
    backup read, the last one the restore's review, when present). Useful after a parser fix or
    an expectation edit - the unit's answers do not change, only the reading of them."""
    with open(a.regrade, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    m = re.search(r'(\{"device ID".*?\})@', text, re.S)
    if not m:
        print("no ParaRead JSON in %s" % a.regrade)
        return 2
    P, slopes, origins, ident = unit_params(json.loads(m.group(1)))
    loops, interval = P["amplification_time"], int(round(P["timePerLoop"] / 1000.0))
    scenarios = [s for s in sc.all_scenarios(loops) if not a.only or a.only.lower() in s["id"].lower()]
    # all_secs[0] is everything before the first getResult; all_secs[j] (j >= 1) is the output of
    # getResult number j followed by whatever was sent next (the next scenario's injections, or an
    # upload). A getResult that no injection precedes is the backup read, not a scenario: that
    # decides where scenario 0 starts. Counting "extra sections" instead is ambiguous - a run with
    # --restore-from has a trailing restore read and no backup read, a plain run has both - and
    # the earlier formula (total minus catalogue) put scenario 0 one section late whenever a
    # restore read existed, so every well compared against the NEXT scenario's echo and was STALE.
    all_secs = text.split(">>> getResult")
    lead = 0 if '{"Slot":[' in all_secs[0] else 1
    secs = [sec.split("\n>>> ")[0] for sec in all_secs[lead + 1:lead + 1 + len(scenarios)]]
    if len(secs) < len(scenarios):
        print("%s has %d getResult sections after the backup read, catalogue needs %d"
              % (a.regrade, len(secs), len(scenarios)))
        return 2
    print("regrading %s: unit %s, %d scenarios" % (a.regrade, ident["device"], len(scenarios)))
    # The unit echoes every injected message in full ('{"Slot":[...]}N'), and the injections for
    # scenario k sit in the log BEFORE its getResult - i.e. at the end of the previous section. A
    # well is graded only if what was sent then is byte-for-byte what the catalogue builds now.
    offset = lead      # index of the section holding scenario 0's injections
    results = []
    for k, (s, sec) in enumerate(zip(scenarios, secs)):
        traces = sc.materialise(s, loops, interval)
        # Other tasks' prints land inside the echo too (the stack report split " Control" from
        # "=2128/4096" around one of them on the first run), so an echo that does not read
        # cleanly proves nothing: only a CLEAN echo that differs marks the well stale.
        block = _KNOWN_FRAGMENTS.sub("", all_secs[offset + k])
        sent = {int(n): v for v, n in re.findall(r'^\{"Slot":\[([0-9,]+)\]\}(\d)\s*$', block, re.M)}
        stale = []
        for i, tr in enumerate(traces):
            now = ",".join(str(v) for v in sc.to_raw(tr, slopes[i], origins[i]))
            stale.append(i in sent and sent[i] != now)
        print("\n== %s  %s%s" % (s["id"], s["title"], ("  [%d well(s) STALE]" % sum(stale)) if any(stale) else ""))
        results.append(grade_scenario(s, traces, parse_results(sec), P, slopes, origins, stale))
    dev = re.sub(r"[^A-Za-z0-9_-]+", "_", ident["device"]) or "unit"
    md = os.path.join(a.out, "%s-%s-regrade.md" % (stamp, dev))
    js = os.path.join(a.out, "%s-%s-regrade.json" % (stamp, dev))
    note = "Chấm lại từ log `%s` (không nạp lại máy)." % os.path.basename(a.regrade)
    write_report(md, js, ident, P, slopes, origins, results, note, started)
    n_fail = sum(1 for s in results for w in s["wells"] if w["status"] == "FAIL")
    n_pass = sum(1 for s in results for w in s["wells"] if w["status"] == "PASS")
    n_known = sum(1 for s in results for w in s["wells"] if w["status"] == "KNOWN-WEAK")
    print("\nPASS %d  FAIL %d  KNOWN-WEAK %d  -> %s" % (n_pass, n_fail, n_known, md))
    return 1 if n_fail else 0


# --------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("port", help="COM port of the unit, e.g. COM7")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--only", default=None, help="substring of a scenario id")
    ap.add_argument("--file", default=None, help="a calibrated file (one slot per line) instead of the catalogue")
    ap.add_argument("--keep", action="store_true", help="leave the last scenario in the unit; skip the restore")
    ap.add_argument("--no-backup", action="store_true", help="do not read the stored run first")
    ap.add_argument("--restore-from", default=None, metavar="SERIAL_LOG",
                    help="put back the run captured by the FIRST getResult of an earlier serial log instead of "
                         "reading the unit (recovery: a previous run that could not restore)")
    ap.add_argument("--dry-run", action="store_true", help="materialise and print sizes; open no port")
    ap.add_argument("--regrade", default=None, metavar="SERIAL_LOG",
                    help="grade an earlier serial log again instead of talking to the unit (port is ignored)")
    ap.add_argument("--out", default=REPORT_DIR)
    a = ap.parse_args()

    started = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    stamp = datetime.datetime.now().strftime("%Y-%m-%d-%H%M")

    if a.dry_run:
        P = dict(sc.PARAMS)
        loops, interval = P["amplification_time"], P["timePerLoop"] // 1000
        for s in sc.all_scenarios(loops):
            if a.only and a.only.lower() not in s["id"].lower():
                continue
            traces = sc.materialise(s, loops, interval)
            sizes = [len('{"Slot":[' + ",".join(str(v) for v in sc.to_raw(t, 1.4, 0)) + "]}0#") for t in traces]
            print("%-36s %d slots x %d rounds, payload %d..%d B" % (s["id"], len(traces), loops, min(sizes), max(sizes)))
        return 0

    os.makedirs(a.out, exist_ok=True)
    if a.regrade:
        return regrade(a, stamp, started)
    import io
    head = io.StringIO()            # the log file is named after the unit, which ParaRead tells us
    unit = Unit(a.port, a.baud, head)
    print("opened %s @ %d, asking ParaRead ..." % (a.port, a.baud))
    pj = unit.para_read()
    P, slopes, origins, ident = unit_params(pj)
    dev = re.sub(r"[^A-Za-z0-9_-]+", "_", ident["device"]) or "unit"
    log_path = os.path.join(a.out, "%s-%s.serial.log" % (stamp, dev))
    log = open(log_path, "w", encoding="utf-8", newline="\n")
    log.write(head.getvalue())
    unit.log = log
    loops, interval_ms = P["amplification_time"], P["timePerLoop"]
    interval = interval_ms / 1000.0
    print("unit %s  para %s  %d rounds x %d ms  min_increase %g  min_sharpness %g  margin %g" % (
        ident["device"], ident["para_version"], loops, interval_ms, P["min_increase"], P["min_sharpness"],
        P["detection_margin_time"]))
    print("slopes %s" % " ".join("%.3f" % s for s in slopes))
    drift = [k for k in ("min_increase", "min_sharpness", "detection_margin_time", "min_slight_positive_time",
                         "baseline_start", "baseline_range", "sg_window", "sg_order")
             if P[k] != sc.PARAMS[k]]
    if drift:
        print("[!] unit thresholds differ from src/define.h: %s - expectations were designed for the source values"
              % ", ".join("%s=%s (src %s)" % (k, P[k], sc.PARAMS[k]) for k in drift))
    if interval_ms % 1000:
        print("[!] time per loop %d ms is not a whole second; time axis uses %.3f s" % (interval_ms, interval))

    # scenarios to send
    if a.file:
        rows = sc.load_cal_file(a.file)
        sid = os.path.basename(a.file).split(".")[0]
        exp_path = os.path.join(os.path.dirname(a.file), sid + ".expect.json")
        wells = [dict(cal=r[:loops] + [r[-1]] * max(0, loops - len(r)), expect="?", note="from file") for r in rows]
        if os.path.isfile(exp_path):
            with open(exp_path, "r", encoding="utf-8") as f:
                ex = json.load(f)
            for i, w in enumerate(ex.get("wells", [])[:len(wells)]):
                wells[i].update({k: w[k] for k in ("expect", "note", "ct", "accept", "climbs", "flag", "known") if k in w})
        scenarios = [dict(id=sid, title="file " + a.file, why="", wells=wells, real=True)]
    else:
        scenarios = [s for s in sc.all_scenarios(loops) if not a.only or a.only.lower() in s["id"].lower()]
    if not scenarios:
        print("no scenario matches --only %s" % a.only)
        return 2

    backup = None
    backup_note = ""
    if a.restore_from and not a.keep:
        with open(a.restore_from, "r", encoding="utf-8", errors="replace") as f:
            secs = f.read().split(">>> getResult")
        res = parse_results(secs[1]) if len(secs) > 1 else {}
        if len(res) == 10 and all("raw_data" in r and len(r["raw_data"]) >= loops for r in res.values()):
            backup = [[min(65535, max(0, int(round(v * slopes[i] + origins[i])))) for v in res[i]["raw_data"][:loops]]
                      for i in range(10)]
            repaired = [i + 1 for i in range(10) if res[i].get("climbs", 0)]
            backup_note = ("Record run cũ lấy từ log `%s` (getResult đầu tiên) và nạp trả lại sau khi chạy." % os.path.basename(a.restore_from) +
                           (" ⚠ Slot %s của run cũ có climb đã vá (`neutralised`) — bản trả lại là bản ĐÃ VÁ, "
                            "không phải raw gốc." % ", ".join(str(i) for i in repaired) if repaired else ""))
            print("stored run taken from %s (%s)" % (a.restore_from,
                  ("repaired climbs on slots %s" % repaired) if repaired else "no climb repairs"))
        else:
            print("[!] %s has no complete first getResult; nothing to restore from" % a.restore_from)
    elif not a.no_backup and not a.keep:
        print("reading the stored run (getResult) so it can be put back afterwards ...")
        res, _ = unit.get_result()
        if len(res) == 10 and all("raw_data" in r and len(r["raw_data"]) >= loops for r in res.values()):
            backup = [[min(65535, max(0, int(round(v * slopes[i] + origins[i])))) for v in res[i]["raw_data"][:loops]]
                      for i in range(10)]
            repaired = [i + 1 for i in range(10) if res[i].get("climbs", 0)]
            backup_note = ("Record run cũ đã được nạp trả lại sau khi chạy." +
                           (" ⚠ Slot %s của run cũ có climb đã vá (`climbs_fixed`>0) — bản trả lại là bản ĐÃ VÁ, "
                            "không phải raw gốc." % ", ".join(str(i) for i in repaired) if repaired else ""))
            print("  stored run captured (%s)" % ("repaired climbs on slots %s" % repaired if repaired else "no climb repairs"))
        else:
            backup_note = "⚠ Không đọc được record run cũ (getResult trả về không đủ 10 slot) — KHÔNG nạp trả lại được."
            print("  [!] could not capture the stored run; it will NOT be restored")

    results = []
    try:
        for s in scenarios:
            traces = sc.materialise(s, loops, int(round(interval)))
            print("\n== %s  %s" % (s["id"], s["title"]))
            t0 = time.time()
            for i, tr in enumerate(traces):
                raw = sc.to_raw(tr, slopes[i], origins[i])
                n = unit.inject_slot(i, raw)
                print("  slot %2d sent (%d B, raw %d..%d)" % (i + 1, n, min(raw), max(raw)))
            res, text = unit.get_result()
            dt = time.time() - t0
            print("  getResult: %d slots answered in %.1fs" % (len(res), dt))
            results.append(grade_scenario(s, traces, res, P, slopes, origins))
    except KeyboardInterrupt:
        print("\ninterrupted")
    finally:
        if backup and not a.keep:
            print("\nputting the stored run back ...")
            try:
                for i in range(10):
                    unit.inject_slot(i, backup[i])
                unit.get_result(timeout=30)
                print("  restored; the TFT shows the old run's review")
            except Exception as e:  # noqa: BLE001
                print("  [!] restore failed: %s" % e)
                backup_note += " ⚠ Nạp trả lại THẤT BẠI: %s" % e
        unit.close()
        log.close()

    if results:
        md = os.path.join(a.out, "%s-%s.md" % (stamp, dev))
        js = os.path.join(a.out, "%s-%s.json" % (stamp, dev))
        write_report(md, js, ident, P, slopes, origins, results, backup_note, started)
        n_fail = sum(1 for s in results for w in s["wells"] if w["status"] == "FAIL")
        n_pass = sum(1 for s in results for w in s["wells"] if w["status"] == "PASS")
        n_known = sum(1 for s in results for w in s["wells"] if w["status"] == "KNOWN-WEAK")
        print("\nPASS %d  FAIL %d  KNOWN-WEAK %d  -> %s\nserial log: %s" % (n_pass, n_fail, n_known, md, log_path))
        print("press WHITE on the unit to leave the review screen (it reboots).")
        return 1 if n_fail else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
