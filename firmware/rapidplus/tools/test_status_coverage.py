#!/usr/bin/env python3
"""Guard: the web must not tell the operator "Idle" while the machine is waiting on them.

fillStatus() and fillActions() (src/webDashboard.cpp) are two lookup tables over ONE enum,
e_statuslcd. They drifted: fillActions grew a case for ewaitphase2 (green = "Amplification")
while fillStatus never got one, so the machine sat there with a hot lysis tube in the block
waiting for a person, and the dashboard said "Idle / Waiting for a run to start." next to a
labelled green button. 26 of 38 states were in that shape.

Three invariants, in increasing order of how badly they bite:

1. COVERAGE. Every state falls into fillStatus explicitly, except a named allowlist. The
   allowlist is the point: adding a state to displayCLD.h should force a decision about what
   the web says, not silently inherit "Idle".

2. AGREEMENT. If fillActions knows what a BUTTON DOES in a state, fillStatus must know what the
   state IS. Anything else is the exact drift above.

3. THE RED REWRITE - this is the one with teeth. data/script.js rewrites a red press into the
   naming gate whenever phase == "idle" (`if (btn === "red" && curPhase === "idle") btn =
   "ampname"`). So a state where RED means something else must NOT report "idle", or the web's
   red chip lights up and sends a request that has nothing to do with the screen the operator is
   looking at. That already shipped once as "red lights up but lysis never starts"
   (ewaitLysisTube) and is why that state has a phase of its own.

Run: python tools/test_status_coverage.py       (host-side, no hardware)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LCD_H = (ROOT / "src" / "displayCLD.h").read_text(encoding="utf-8", errors="replace")
DASH = (ROOT / "src" / "webDashboard.cpp").read_text(encoding="utf-8", errors="replace")
JS = (ROOT / "data" / "script.js").read_text(encoding="utf-8", errors="replace")

# States that may legitimately fall through to the default "Idle". Keep this list SHORT and
# justified - every entry is a promise that an operator cannot sit here and read the web.
ALLOW_DEFAULT = {
    "escreenStart": "the genuine idle screen - 'Idle' is the truth here",
    # These three are never assigned anywhere in src/ - verify with
    #   grep -rn "type_infor = <name>" src/
    # before believing this comment. displayLCD's own switch has no case for eheathotlid1
    # either, so it would not even draw anything.
    "ewaitingReadsensor": "never assigned in src/ - unreachable",
    "eheathotlid1": "never assigned in src/, and displayLCD has no case for it either",
    "eprepare": "never assigned in src/ - unreachable",
    "eSettingBluetooth": "dead: the BT stack is released at boot, this screen cannot be entered",
}

fail = []


def strip_comments(text):
    """Drop // and /* */ but keep string literals intact - the phase names live in literals."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            out.append(c)
            i += 1
            while i < n:
                ch = text[i]
                out.append(ch)
                i += 1
                if ch == "\\" and i < n:
                    out.append(text[i])
                    i += 1
                elif ch == '"':
                    break
            continue
        if c == "/" and i + 1 < n:
            if text[i + 1] == "/":
                while i < n and text[i] != "\n":
                    i += 1
                continue
            if text[i + 1] == "*":
                j = text.find("*/", i + 2)
                i = n if j < 0 else j + 2
                continue
        out.append(c)
        i += 1
    return "".join(out)


def func_body(src, sig, end_sig):
    i = src.index(sig)
    return strip_comments(src[i : src.index(end_sig, i)])


# ---- the enum ----------------------------------------------------------------------------
enum_blk = re.search(r"typedef enum\s*\{(.*?)\}\s*e_statuslcd", strip_comments(LCD_H), re.S)
if not enum_blk:
    print("FAIL - could not find the e_statuslcd enum in src/displayCLD.h")
    sys.exit(1)
STATES = [s.strip() for s in re.findall(r"\b(e[A-Za-z0-9_]+)\s*(?:,|$)", enum_blk.group(1))]
if len(STATES) < 20:
    fail.append(f"only parsed {len(STATES)} states out of the enum - the parser has gone blind")

status_body = func_body(DASH, "static void fillStatus", "static void fillActions")
actions_body = func_body(DASH, "static void fillActions", "\n}\n")

status_cases = set(re.findall(r"case\s+(e[A-Za-z0-9_]+)\s*:", status_body))
action_cases = set(re.findall(r"case\s+(e[A-Za-z0-9_]+)\s*:", actions_body))


def case_to_value(body, assign_re):
    """Map each case label to the value assigned before the next break, honouring fall-through
    (several `case X:` in a row share one body)."""
    out, pending = {}, []
    for line in body.splitlines():
        m = re.search(r"case\s+(e[A-Za-z0-9_]+)\s*:", line)
        if m:
            pending.append(m.group(1))
            continue
        v = assign_re.search(line)
        if v and pending:
            for s in pending:
                out[s] = v.group(1)
        if "break;" in line:
            pending = []
    return out


state_phase = case_to_value(status_body, re.compile(r'phase\s*=\s*"([^"]*)"'))
state_red = case_to_value(actions_body, re.compile(r'red\s*=\s*"([^"]+)"'))

# ---- 1. coverage --------------------------------------------------------------------------
for s in STATES:
    if s in status_cases or s in ALLOW_DEFAULT:
        continue
    fail.append(
        f"{s} has no case in fillStatus, so the web reports 'Idle' while the machine is in it. "
        f"Add a case, or add it to ALLOW_DEFAULT here with the reason it cannot be seen."
    )
stale = [s for s in ALLOW_DEFAULT if s not in STATES]
if stale:
    fail.append(f"ALLOW_DEFAULT names states that no longer exist: {stale}")
covered = [s for s in ALLOW_DEFAULT if s in state_phase]
if covered:
    fail.append(f"ALLOW_DEFAULT is stale - these now set their own phase: {covered}")

# ---- 2. the two tables agree --------------------------------------------------------------
for s in sorted(action_cases - status_cases):
    if s in ALLOW_DEFAULT:
        continue
    fail.append(
        f"{s}: fillActions labels its buttons but fillStatus has no case - the web offers a "
        f"labelled button under the words 'Idle / Waiting for a run to start.'"
    )

# ---- 3. no red-meaning state may report "idle" --------------------------------------------
if 'btn === "red" && curPhase === "idle"' not in JS.replace("\n", " "):
    fail.append(
        "data/script.js no longer rewrites red at phase 'idle' - if that rule moved, this "
        "check is pinning a hazard that has changed shape. Re-read it before editing."
    )
for s, label in sorted(state_red.items()):
    # escreenStart is the BASE CASE, not a violation: it is the idle screen, red there really
    # does mean "start the amplification naming gate", and that is the behaviour script.js's
    # rewrite exists to produce. Every OTHER state reporting idle is borrowing it by accident.
    if s == "escreenStart":
        continue
    ph = state_phase.get(s, "idle")  # no explicit phase = falls through to the default
    if ph == "idle":
        fail.append(
            f'{s}: RED means "{label}" here, but fillStatus reports phase "idle", and the client '
            f"turns a red press at idle into the naming gate. Give this state its own phase."
        )

if fail:
    print("FAIL - web status coverage")
    for f in fail:
        print("  -", f)
    sys.exit(1)
print(
    f"ok - {len(status_cases)}/{len(STATES)} states named on the web "
    f"({len(ALLOW_DEFAULT)} allowed to default), fillActions and fillStatus agree, "
    f"no red-meaning state reports idle"
)
