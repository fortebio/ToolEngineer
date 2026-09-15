#!/usr/bin/env python3
"""Mock ESP32 SSE server for testing the RAPID live dashboard WITHOUT hardware.

Serves the real data/ client files (index.html, script.js, style.css) and streams,
on /events, two SSE event types exactly like the firmware's AsyncEventSource:

  event: home          -> home screen state (temps, status, notify, buttons, actions)
  event: new_readings  -> live chart point, scalar per channel {"i":n,"#1":num,...}

The run is DRIVEN BY THE WEB, like the real device:

  heater  --(timed)-->  waitamp  --(RED "Start" press)-->  amplification
     ^                                                          |
     |                                                     (all rounds)
     +---------------- (WHITE "Next test" press) <---- finished

waitamp HOLDS until Start is pressed (firmware: ewaitampTube waits for the user),
and finished HOLDS until white is pressed (escreenFinished -> escreenRestart). That
makes the slot-naming gate and the post-run chart actually testable.

  Run:        python tools/sse_test_server.py          # open http://localhost:8000
  Full scale: python tools/sse_test_server.py --full   # a real 40-minute run's data
  Real run:   python tools/sse_test_server.py --slots tools/slots.txt --reboot
                                                       # replay a captured run, chart ready
                                                       # immediately in the Result tab
  Self-check: python tools/sse_test_server.py selftest
  E2E:        node tools/test_full_run.js              # against --full
  Test hook:  POST /__reset                            # abandon the run, back to idle
                                                       # (no device counterpart)

The new_readings payload is the scalar-per-channel form because the client does
Number(jsonValue[key]) per series. The firmware events.send() must push these same
shapes (NOT the /getdata array form, which would plot as NaN).
"""
import json
import math
import sys
import time
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from urllib.parse import urlparse, parse_qs

DATA_DIR = Path(__file__).resolve().parent.parent / "data"
CHANNELS = 10

# --full reproduces a REAL run's DATA SCALE and x-axis: amplification_time = 120
# rounds x timePerLoop = 20 s = a 40 minute run (src/define.h). The wall clock is
# compressed so the procedure takes ~15 s, but /curve payload size, chart point
# count and axis values are exactly what the device produces.
FULL = "--full" in sys.argv

# --slots <file>: replay a REAL run instead of the synthetic sigmoids. One line per slot,
# comma-separated CALIBRATED readings - exactly the shape /curve serves. Fed in through
# reading_at(), so the live SSE playback and the stored curve both show it and the chart is
# exercised the same way it is on the machine. Row/round counts come from the file, so a run
# of any length works; missing rows are flat at 0 (an unused slot).
def _slots_arg():
    for i, a in enumerate(sys.argv):
        if a == "--slots" and i + 1 < len(sys.argv):
            return sys.argv[i + 1]
        if a.startswith("--slots="):
            return a.split("=", 1)[1]
    return None


def _load_slots(path):
    rows = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        vals = [float(v) for v in line.replace('"', "").split(",") if v.strip()]
        if vals:
            rows.append(vals)
    if not rows:
        raise SystemExit(f"--slots {path}: no data rows (one comma-separated line per slot)")
    return rows


SLOTS_PATH = _slots_arg()
SLOTS = _load_slots(SLOTS_PATH) if SLOTS_PATH else None

AMP_ROUNDS = (
    max(len(r) for r in SLOTS) if SLOTS else (120 if FULL else 44)
)                                              # rounds in one amplification
REPORT_INTERVAL_MS = 20000 if (FULL or SLOTS) else 1200  # ms/round reported -> client x axis
TICK_SEC = 0.08 if FULL else 1.2               # wall-clock seconds per round
T_HEAT_SEC = 4.0 if FULL else 8.0              # lysis heating duration
HOME_PERIOD = 0.25 if FULL else 1.0            # 'home' push cadence (device: 1 s)

# Manual button presses from the web UI: btn -> monotonic() expiry (chip lights ~2 s).
PRESSED = {}

# ---- Setting tab -----------------------------------------------------------
# Live config, same shape as JsonPara/pass_file_initial_Full.json (= what paraToJson()
# emits and JsonDataConfig() parses). POST /config merges the keys present, like the
# device does.
CONFIG = json.loads((Path(__file__).resolve().parent.parent /
                     "JsonPara" / "pass_file_initial_Full.json")
                    .read_text(encoding="utf-8").rstrip("@"))
CONFIG.setdefault("top heater PWM", [[40, 100], [40, 100]])
# The device derives BOTH /curve intervalMs and "time per loop" from one parameter
# (parameter.timePerLoop), and runs exactly "amplification time" rounds. The client sizes
# the LIVE chart from those two keys (plannedRunMin), so the mock must describe its own
# run here or the live chart is scaled to a 40-minute run while the fast mock plays 44
# rounds of 1.2 s.
CONFIG["amplification time"] = AMP_ROUNDS
CONFIG["time per loop"] = REPORT_INTERVAL_MS

FAKE_WIFI = [
    {"ssid": "FBT-Office", "rssi": -48, "open": False},
    {"ssid": "FBT-Lab-5G", "rssi": -61, "open": False},
    {"ssid": "TP-Link_2.4G", "rssi": -73, "open": False},
    {"ssid": "FreeWifi", "rssi": -85, "open": True},
]
_scan_started = None      # monotonic when a scan began (mock the async scan)

# Mirrors ForteSetting cfgSeq/cfgState. A POST only QUEUES: the device applies it on
# SettingTask ~10ms later and DROPS it if a run started in between, so the outcome has
# to be published on the home event or the web would report "Saved" for a write that
# never happened. APPLY_DELAY reproduces that window - without it the mock would apply
# instantly and the client's wait-for-outcome path would never be exercised.
_cfg_seq = 0
_cfg_state = "none"       # none | pending | applied | busy
_cfg_pending = None       # (kind, payload) waiting to be drained
APPLY_DELAY = 0.15
_calib_step = ""          # "" | preheatStart | preheating | ... (mock wizard)
_calib_slot = 0

# Result table (fake). Names persist in memory like the device's /slotnames.json,
# sample labels like /slotsamples.json.
SLOT_NAMES = [""] * 10
SLOT_SAMPLES = [""] * 10
FAKE_CT = [22.3, None, None, 28.9, None, 19.5, None, None, None, None]
FAKE_RESULT = ["P", "N", "N", "S", "N", "P", "E", "N", "B", "N"]

if SLOTS:
    # Replaying a REAL run: derive the verdicts from the curves instead of keeping the
    # canned ones. Otherwise the table calls a flat channel "Positive" while the chart next
    # to it shows a flat line - fine as a layout fixture, wrong in a screenshot that teaches
    # someone how to read a result. This is a DEMO heuristic, not the device's algorithm
    # (bResultGet); the device stays the authority on real hardware.
    def _verdict(row):
        # Same baseline window the chart draws against (script.js BASELINE_START_MIN 2,
        # BASELINE_RANGE_MIN 4). Averaging from round 0 instead would fold the optics'
        # warm-up climb into the baseline and every channel would look like it took off.
        per_min = 60000.0 / REPORT_INTERVAL_MS
        b0, b1 = int(2 * per_min), int(6 * per_min)
        if not row or len(row) <= b1 or b1 <= b0:
            return ("N", None)
        base = sum(row[b0:b1]) / (b1 - b0)
        adj = [v - base for v in row]
        rise = max(adj[b1:])
        if rise < 12:
            return ("N", None)
        idx = next((i for i in range(b1, len(adj)) if adj[i] >= rise * 0.2), None)
        ct = round(idx / per_min, 1) if idx is not None else None
        return ("P" if rise >= 40 else "S", ct)

    FAKE_RESULT, FAKE_CT = [], []
    for _i in range(CHANNELS):
        _r, _ct = _verdict(SLOTS[_i] if _i < len(SLOTS) else None)
        FAKE_RESULT.append(_r)
        FAKE_CT.append(_ct)

# ---- Run state machine (shared by every client, like the real device) --------
# Amplification (RED) flow:  idle --red--> waitname --red--> heater --(timed)-->
#                            waitamp --red--> amplification --> finished --white--> idle
# Lysis (GREEN) flow:        idle --green--> heater (names at waitamp, no waitname gate)
_naming = False                # RED at idle -> waitname (name slots BEFORE heating)
_lysis_start = None            # monotonic when RED started the lysis incubation (None = not yet)
T_LYSIS_SEC = 16.0             # stands in for lysisDuration (600 s on the device)
_amp_flow = True               # which leg is heating: True = eheating67, False = epreheating80.
                               # The two legs put DIFFERENT text on the same "heater" phase
                               # (fillStatus splits them), so the mock has to know which one.
_run_start = None              # monotonic when the heater phase began (None = not yet)
_amp_start = None              # monotonic when Start was pressed (None = not started)
_ran_before = False            # a run has completed since boot -> device holds a curve

# --reboot boots as if the device just power-cycled with a run still in EEPROM: the RAM
# result cache is empty (/slots ready=false), but /reviewlast reloads the stored run.
# Lets the Result-tab "review after reboot" path be tested without power-cycling hardware.
_stored = "--reboot" in sys.argv
# Mock OTA state. "idle" until the web asks for a check, then pretends a newer build exists.
_ota = {"state": "idle", "checked": False}
# Mock saved-WiFi list (device: /wifi.json on LittleFS). Front entry = preferred.
_saved_wifi = ["FBT-Office", "Lab-2G"]
_wifi_trial = None  # {"result":"failed","ssid":..} after a wrong-password save
# Which network the machine is actually joined to. Normally the front of the list, but the
# device CAN sit on another one: if the preferred network is absent at boot, main.cpp falls
# back to a saved net WITHOUT rewriting EEPROM or the list order. Settable (POST
# /wifilist?current=) so a test can reproduce that state - it is where the "no way back to
# row 0" UI bug lived. None = the normal "connected == front" case.
_wifi_current = None


def _wifi_live():
    """The SSID the machine is joined to. ONE source for both /home net.ssid and /wifilist
    current+active - on the device both read WiFi.SSID(), so letting them drift here would
    make the mock show two 'connected' rows that hardware never produces."""
    if _wifi_current in _saved_wifi:
        return _wifi_current
    return _saved_wifi[0] if _saved_wifi else ""
_reviewed = False              # /reviewlast reloaded the stored EEPROM run


_err_table = False   # escreenErrorResult: RED on the finished screen, WHITE leaves


def run_state():
    """(phase, rounds_done) mirroring the device. rounds_done == COUNTER (0 outside amp)."""
    global _ran_before
    now = time.monotonic()
    if _naming:
        return ("waitname", 0)          # HOLDS until RED confirms -> heating
    if _run_start is None:
        return ("idle", 0)              # escreenStart: waits for a button
    if _amp_start is None:
        # ---- lysis leg -----------------------------------------------------
        # The device does NOT go preheat -> waitamp on this path: it stops twice for a
        # person (drop the tube in, take the hot tube out). Collapsing those two screens
        # is how the guide ended up documenting only half the machine.
        if not _amp_flow:
            if _lysis_start is not None:
                if now - _lysis_start < T_LYSIS_SEC:
                    return ("lysisrun", 0)   # eheatLysis: the incubation countdown
                return ("waitphase2", 0)
            if now - _run_start < T_HEAT_SEC:
                return ("heater", 0)         # epreheating80
            return ("waitlysis", 0)          # ewaitLysisTube: HOLDS until RED
        # ---- amplification leg ---------------------------------------------
        if now - _run_start < T_HEAT_SEC:
            return ("heater", 0)
        return ("waitamp", 0)           # HOLDS until the RED "Start" press
    n = int((now - _amp_start) / TICK_SEC)
    if n < AMP_ROUNDS:
        return ("amplification", n)
    if n < AMP_ROUNDS + 2:
        # COUNTER hits AMP_ROUNDS while sensor6035 saves to EEPROM; dashboardLoop still
        # streams the final round in that window (else the mock fakes a 1-round gap).
        return ("amplification", AMP_ROUNDS)
    _ran_before = True
    # escreenFinished --RED--> escreenErrorResult. Its OWN phase, not "error": that one is
    # a real fault (errprocess), this is the operator asking to look at the table.
    if _err_table:
        return ("errortable", AMP_ROUNDS)
    return ("finished", AMP_ROUNDS)


def press(btn):
    """A web /control press, driving the run like the device's buttons do."""
    global _amp_start, _run_start, _naming, _amp_flow, _lysis_start, _err_table
    PRESSED[btn] = time.monotonic() + 2.0
    phase, _ = run_state()
    # Leaving the idle screen INTO a run starts a new cycle, and the device wipes the slot
    # labels there (dashboardLoop -> dashboardClearSlotLabels): they belong to one run, and a
    # run nobody named must not upload under the previous run's disease names. Mirrored here
    # so the mock does not hide the bug it was written to reproduce. WHITE (QR) is not a run.
    if phase == "idle" and btn in ("ampname", "red", "green"):
        SLOT_NAMES[:] = [""] * 10
        SLOT_SAMPLES[:] = [""] * 10
    if btn == "ampname" and phase == "idle":
        _naming = True                  # web Amplification --> ewaitname (name first)
    elif btn == "red" and phase == "idle":
        _amp_flow = True
        _run_start = time.monotonic()   # PHYSICAL Amplification --> heat directly
    elif btn == "red" and phase == "waitname":
        _naming = False                 # ewaitname --red(confirm)--> eheating67
        _amp_flow = True
        _run_start = time.monotonic()
    elif btn == "green" and phase == "idle":
        _amp_flow = False               # escreenStart --green(Lysis)--> epreheating80
        _lysis_start = None
        _run_start = time.monotonic()
    elif btn == "red" and phase == "waitlysis":
        _lysis_start = time.monotonic() # ewaitLysisTube --red--> eheatLysis
    elif btn == "green" and phase == "waitphase2":
        # ewaitphase2 --green--> eheating67. From here both legs run the same procedure.
        _amp_flow = True
        _lysis_start = None
        _run_start = time.monotonic()
    elif btn == "white" and phase in ("waitlysis", "lysisrun", "waitphase2"):
        _run_start = None               # "Return" -> back to the start screen
        _lysis_start = None
    elif btn == "red" and phase == "waitamp":
        _amp_start = time.monotonic()   # ewaitampTube --red--> eoptoreading
    elif btn == "white" and phase == "waitname":
        _naming = False                 # ewaitname --white--> escreenStart (cancel)
    elif btn == "red" and phase == "finished":
        _err_table = True               # escreenFinished --red--> escreenErrorResult
    elif btn == "white" and phase == "errortable":
        _err_table = False              # --white--> ebuttonrestart -> back to idle
        _naming = False
        _run_start = None
        _amp_start = None
    elif btn == "white" and phase == "finished":
        _naming = False                 # escreenFinished --white--> back to idle
        _run_start = None
        _amp_start = None


def drain_cfg():
    """SettingTask's drainPending(): apply, or drop if the device turned busy."""
    global _cfg_pending, _cfg_state
    if _cfg_pending is None:
        return
    at, kind, payload = _cfg_pending
    if time.monotonic() - at < APPLY_DELAY:
        return  # still queued - this is the window the POST cannot wait for
    _cfg_pending = None
    phase, _ = run_state()
    if bool(_calib_step) or phase in ("waitname", "heater", "waitamp", "amplification"):
        _cfg_state = "busy"   # dropped on purpose: never change setpoints mid-run
        print("[cfg] device busy - queued settings dropped")
        return
    if kind == "config":
        def merge(dst, src):
            for k, v in src.items():
                if isinstance(v, dict) and isinstance(dst.get(k), dict):
                    merge(dst[k], v)
                else:
                    dst[k] = v
        merge(CONFIG, payload)
        print("[cfg] applied: %s" % sorted(payload.keys()))
    elif kind == "id":
        CONFIG["device ID"] = payload
        print("[cfg] device id: %r" % payload)
    elif kind == "wifi":
        print("[cfg] WiFi saved: %r (would reboot)" % payload)
    _cfg_state = "applied"


def queue_cfg(kind, payload):
    global _cfg_seq, _cfg_state, _cfg_pending
    if _cfg_pending is not None:
        return None
    _cfg_seq += 1
    _cfg_state = "pending"
    _cfg_pending = (time.monotonic(), kind, payload)
    return _cfg_seq


def curve_count(phase, rounds, ran_before):
    """What GET /curve reports. Mirrors firmware handleCurve:
        n = getCurrentLoop();
        if (n == 0 && type_infor != eoptoreading) n = getLastRunLoops();

    Two traps this reproduces:
      1. sensor6035::clear() is effectively never called per-run (boot only), so
         lastRunLoops survives into the NEXT run. At heater/waitamp of run B the
         device still serves run A's curve - which the Result tab legitimately wants
         (the machine really does still hold run A), but the Home live chart must not
         preload it.
      2. COUNTER is ALSO 0 for the first round of a new amplification. Falling back
         there would hand run A's curve to a run that just started, so the fallback is
         gated on 'not amplifying' -> a fresh run correctly reports 0.
    """
    if phase == "amplification":
        return rounds  # live COUNTER, including 0 on the first round
    return AMP_ROUNDS if ran_before else 0  # retained lastRunLoops


# ---- Amplification chart curves -------------------------------------------
def _profiles():
    import random
    random.seed(42)  # deterministic so demo + selftest reproduce
    out = []
    for _ in range(CHANNELS):
        negative = random.random() < 0.25
        out.append((
            random.uniform(40, 90),                          # baseline fluorescence
            0.0 if negative else random.uniform(300, 550),   # amplitude
            random.uniform(0.18, 0.45) * AMP_ROUNDS,         # takeoff round
            random.uniform(0.4, 0.9) * (44.0 / AMP_ROUNDS),  # steepness (same shape)
        ))
    return out


PROFILES = _profiles()


def reading_at(rnd):
    """One amplification round across all channels -> {'#1': val, ... '#10': val}.
    rnd is the 0-based round index within the amplification phase (the x-axis base)."""
    if SLOTS:
        # The single place the run's numbers come from, so --slots reaches BOTH the SSE
        # playback and /curve. A row shorter than the run holds its last value rather than
        # dropping out - a gap would render as a break in the line, not as "no data".
        out = {}
        for i in range(CHANNELS):
            row = SLOTS[i] if i < len(SLOTS) else None
            out[f"#{i + 1}"] = (row[rnd] if rnd < len(row) else row[-1]) if row else 0.0
        return out
    out = {}
    for i, (base, amp, mid, k) in enumerate(PROFILES):
        y = base + amp / (1 + math.exp(-k * (rnd - mid)))
        out[f"#{i + 1}"] = round(y, 1)
    return out


# ---- Home screen state -----------------------------------------------------
def _ramp(v, start, end, t0, t1):
    if v <= t0:
        return start
    if v >= t1:
        return end
    return start + (end - start) * (v - t0) / (t1 - t0)


def home_payload(phase, rounds):
    """The 'home' SSE event for a given run state (pure -> testable)."""
    prog = rounds / float(AMP_ROUNDS) if AMP_ROUNDS else 0
    # Setpoints come from the live config, like fillStatus reads p.amplifTemp / p.lysisTemp:
    # editing them in the Setting tab has to move the text the operator reads.
    AMP_SETPOINT = float(CONFIG.get("amplification temperature", 65.8))
    LYSIS_SETPOINT = float(CONFIG.get("lysis temperature", 82))
    # The lids are NOT configurable: HOTLID23_TEMP is a compile-time macro (define.h:373),
    # so it does not come from /config like the other two. The mock used to report 105 C
    # here, which is not a temperature this machine ever targets.
    HOTLID_SETPOINT = 75.0
    if phase in ("idle", "waitname"):
        # ewaitname is BEFORE any heating: naming is the gate that STARTS the preheat, so
        # everything is still at room temperature here.
        lysis, amp, top = 25.0, 25.0, 25.0
    elif phase == "heater":
        # Which blocks are hot depends on the leg: the lysis leg heats the lysis block, the
        # amplification leg heats the two amp blocks and the lids.
        if _amp_flow:
            lysis, amp, top = 25.5, 55.0, 62.0
        else:
            # Lysis preheat drives heater1 only - setpid1startpreHeat80() never arms the
            # lids; those wait for setPreheat67() on the amplification leg. Reporting a
            # warm lid here contradicted the guide's own caption.
            lysis, amp, top = 45.0, 25.0, 25.0
    elif phase in ("waitlysis", "lysisrun", "waitphase2"):
        # Only heater1 runs on this leg - the amplification blocks and the lids stay cold
        # until the green press at ewaitphase2 starts the 65.8 C preheat.
        lysis = LYSIS_SETPOINT if phase != "waitphase2" else LYSIS_SETPOINT - 1.5
        amp, top = 25.0, 25.0
    elif phase == "waitamp":
        # The lysis block is only hot if the run came through the lysis leg; the
        # amplification-only run never touches it.
        lysis, amp, top = (25.5 if _amp_flow else 65.0), AMP_SETPOINT, HOTLID_SETPOINT
    elif phase == "amplification":
        lysis, amp, top = (25.5 if _amp_flow else 64.5), AMP_SETPOINT, HOTLID_SETPOINT
    else:
        lysis, amp, top = (26.0 if _amp_flow else 64.0), 40.0, 70.0
    wig = 0.3 * math.sin(rounds / 3.0 + prog)

    # actions = what each button does in this state (fillActions in webDashboard.cpp)
    if phase == "idle":
        # escreenStart: the device sits here after boot / after Next test
        status = {"phase": "idle", "title": "Idle",
                  "subtitle": "Waiting for a run to start."}
        # WHITE is not idle here: on escreenStart it opens the dashboard QR screen.
        actions = {"green": "Lysis", "red": "Amplification", "white": "QR / Web"}
    elif phase == "waitname":
        # ewaitname: name the slots BEFORE heating; RED confirms + starts preheat
        status = {"phase": "waitname", "title": "Name the samples",
                  "subtitle": "Pick a disease per slot, then start"}
        actions = {"green": "", "red": "Confirm & heat", "white": "Return"}
    elif phase == "heater":
        # Mirror fillStatus's two heater screens verbatim - the amplification leg reports the
        # amp blocks and the lids, the lysis leg reports the lysis block against its setpoint.
        if _amp_flow:
            status = {"phase": "heater",
                      "title": f"Heating to {AMP_SETPOINT:.1f} C",
                      "subtitle": (f"Blocks {round(amp + wig, 1):.1f}/{round(amp - wig, 1):.1f} C, "
                                   f"lids {round(top + wig, 1):.1f}/{round(top - wig, 1):.1f} C")}
        else:
            status = {"phase": "heater", "title": "Heating lysis block",
                      "subtitle": f"{round(lysis + wig, 1):.1f} / {LYSIS_SETPOINT:.1f} C"}
        actions = {"green": "", "red": "", "white": "Return"}
    elif phase == "waitlysis":
        # NOT "idle": RED means "Start lysis" here, and the web rewrites a red press at idle
        # into the naming gate. One phase per meaning (fillStatus, ewaitLysisTube).
        status = {"phase": "waitlysis", "title": "Insert lysis tube",
                  "subtitle": f"Block at {round(lysis + wig, 1):.1f} C - press Start lysis"}
        actions = {"green": "", "red": "Start lysis", "white": "Return"}
    elif phase == "lysisrun":
        # eheatLysis reports phase "heater" to the web; only the title differs.
        frac = 1.0
        if _lysis_start is not None:
            frac = max(0.0, 1.0 - (time.monotonic() - _lysis_start) / T_LYSIS_SEC)
        secs = int(frac * float(CONFIG.get("lysis duration", 600)))
        status = {"phase": "heater", "title": "Lysis running",
                  "subtitle": f"~{(secs + 59) // 60} min left, "
                              f"block {round(lysis + wig, 1):.1f} C"}
        actions = {"green": "", "red": "", "white": "Return"}
    elif phase == "waitphase2":
        # The machine is waiting on a PERSON with a hot tube in the block.
        status = {"phase": "waitphase2", "title": "Remove lysis tube",
                  "subtitle": "Take the tube out and close the lid, "
                              "then press Amplification (green)."}
        actions = {"green": "Amplification", "red": "", "white": "Return"}
    elif phase == "waitamp":
        status = {"phase": "waitamp", "title": "Insert amplification tube",
                  "subtitle": "Name slots, then start"}
        actions = {"green": "", "red": "Start", "white": "Return"}
    elif phase == "amplification":
        # fillStatus: whole minutes rounded UP, plus the round counter - the clock and the
        # acquisition are independent, so both are shown.
        left = int((AMP_ROUNDS - rounds) * REPORT_INTERVAL_MS / 1000.0)
        sub = (f"~{(left + 59) // 60} min left" if left else "Finishing the last rounds")
        status = {"phase": "amplification", "title": "Amplification",
                  "subtitle": f"{sub} - round {rounds}/{AMP_ROUNDS}"}
        actions = {"green": "", "red": "", "white": "Return"}
    elif phase == "errortable":
        # escreenErrorResult: the operator asked to SEE the machine's error table.
        # Its OWN phase, never "finished" - Home swaps the chart for that table on it, and
        # folding it into the else branch is exactly how this went unnoticed: the mock kept
        # reporting "finished" while its own state machine had already moved on.
        status = {"phase": "errortable", "title": "Sensor error table",
                  "subtitle": "Per-channel errors for the last run. White returns."}
        actions = {"green": "", "red": "", "white": "Next test"}
    else:
        status = {"phase": "finished", "title": "Run complete",
                  "subtitle": "Results ready."}
        actions = {"green": "", "red": "Errors Table", "white": "Next test"}

    # Mirrors webDashboard.cpp isBusy(): settings are locked unless the device is idle.
    # Of the mock's phases only "finished" is idle; calibrating counts as busy too.
    status["busy"] = bool(_calib_step) or phase in (
        "waitname", "heater", "waitamp", "amplification",
        "waitlysis", "lysisrun", "waitphase2")
    status["calib"] = _calib_step

    return {
        "device": CONFIG.get("device ID", "RAPIDPlus"),
        "company": "Fortebiotech",
        "net": {"ap": False, "ssid": _wifi_live(), "ip": "192.168.1.25"},
        "temps": {
            "lysis": round(lysis + wig, 1),
            "ampLeft": round(amp + wig, 1),
            "ampRight": round(amp - wig, 1),
            "topLeft": round(top + wig, 1),
            "topRight": round(top - wig, 1),
        },
        "status": status,
        "actions": actions,
        "notify": {"show": phase == "finished",
                   "title": "Amplification finished",
                   "subtitle": "Remove the cartridge to finish the run"},
        "buttons": {"red": False, "green": False, "white": False},
        # outcome of the last queued settings write (ForteSetting cfgSeq/cfgState)
        "cfg": {"seq": _cfg_seq, "state": _cfg_state},
    }


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=str(DATA_DIR), **kw)

    def log_message(self, *a):  # quieter console
        pass

    def do_GET(self):
        if self.path.startswith("/events"):
            return self._sse()
        if self.path.startswith("/readings"):
            phase, rounds = run_state()
            return self._json(reading_at(max(0, rounds - 1)))
        if self.path.startswith("/home"):
            phase, rounds = run_state()
            return self._json(home_payload(phase, rounds))
        if self.path.startswith("/control"):
            return self._control()
        if self.path.startswith("/slots"):
            return self._slots()
        if self.path.startswith("/errors"):
            return self._errors()
        if self.path.startswith("/rename"):
            return self._rename()
        if self.path.startswith("/curve"):
            return self._curve()
        if self.path.startswith("/config"):
            return self._json(CONFIG)
        if self.path.startswith("/wifiscan"):
            return self._wifiscan()
        # The device cannot tell GET from POST here: the route is registered with
        # WebServer.h's HTTP_POST (== 3) while AsyncWebServer matches bitwise against its own
        # flags (GET == 1), so 3 & 1 lets a bare GET into the POST handler. Mirror that, or
        # the mock hides the very hole ?go=1 exists to close.
        if self.path.startswith("/reviewlast"):
            return self._reviewlast()
        if self.path.startswith("/calib"):
            return self._calib()
        if self.path.startswith("/ota"):
            return self._ota_get()
        if self.path.startswith("/wifilist"):
            return self._wifilist_get()
        return super().do_GET()  # static files from data/

    def _wifiscan(self):
        # Mirror the device's ASYNC scan: 202 while running, 200 with the list after.
        global _scan_started
        now = time.monotonic()
        if _scan_started is None:
            _scan_started = now
            return self._json({"scanning": True})
        if now - _scan_started < 1.5:
            return self._json({"scanning": True})
        _scan_started = None  # next GET starts a fresh scan, like scanDelete()
        return self._json({"scanning": False, "networks": FAKE_WIFI})

    def _calib(self):
        # Mock of the device's wizard: the web only drives buttons, the device advances
        # its own state machine. Preheat is shortened from 5 min to 4 s here.
        global _calib_step, _calib_slot
        q = parse_qs(urlparse(self.path).query)
        a = (q.get("action") or [""])[0]
        order = ["preheatStart", "preheating", "select", "slot", "mode",
                 "measure", "complete", "save"]
        if a == "start":
            if _calib_step:
                return self._json({"ok": False, "error": "already calibrating"})
            _calib_step = "preheatStart"
        elif a == "cancel":
            if not _calib_step:  # mirror the firmware guard: cancel only while calibrating,
                return self._json({"ok": False, "error": "not calibrating"})  # else it would abort a run
            _calib_step = ""
        elif a == "slot":
            if _calib_step != "slot":
                return self._json({"ok": False, "error": "not on the slot screen"})
            _calib_slot = int((q.get("n") or ["0"])[0])
        elif a in ("next", "measure"):
            if not _calib_step:
                return self._json({"ok": False, "error": "not calibrating"})
            i = order.index(_calib_step)
            if _calib_step == "save":
                _calib_step = ""       # saved -> back to idle
            else:
                _calib_step = order[i + 1]
        else:
            return self._json({"ok": False, "error": "unknown action"})
        print(f"[calib] {a} -> {_calib_step or 'idle'} (slot {_calib_slot})")
        return self._json({"ok": True})

    def _curve(self):
        # The whole run the device currently holds, so a late/reconnected browser can
        # redraw everything. j = round index, matches new_readings "i".
        phase, rounds = run_state()
        # After a --reboot, the reloaded run (_reviewed) is served just like a run that
        # completed this session (_ran_before) - both mean "the device holds a curve".
        n = curve_count(phase, rounds, _ran_before or _reviewed)
        series = [[reading_at(j)[f"#{ch + 1}"] for j in range(n)] for ch in range(CHANNELS)]
        return self._json({"count": n, "intervalMs": REPORT_INTERVAL_MS, "series": series})

    def do_POST(self):
        if self.path.startswith("/control"):
            return self._control()
        if self.path.startswith("/rename"):
            return self._rename()
        if self.path.startswith("/config"):
            return self._config_post()
        # match the exact path: the firmware's AsyncWebServer routes by URL, so
        # startswith("/wifi") would wrongly swallow a POST to /wifiscan and make the
        # mock disagree with the device it is meant to mirror.
        if urlparse(self.path).path == "/wifi":
            return self._wifi_post()
        if self.path.startswith("/deviceid"):
            return self._deviceid_post()
        if self.path.startswith("/reviewlast"):
            return self._reviewlast()
        if self.path.startswith("/calib"):
            return self._calib()
        if self.path.startswith("/wifilist"):
            return self._wifilist_post()
        if self.path.startswith("/otaupload"):
            return self._otaupload()
        if self.path.startswith("/ota"):
            return self._ota_post()
        if self.path == "/__reset":
            return self._reset()
        self.send_error(404)

    def _reset(self):
        """TEST HOOK, no device counterpart: abandon any run in progress and go back to the
        idle screen of the current mode. A guard that drives a real run through /control
        (test_chart_scale.js, section 6) leaves the mock mid-amplification for the next
        two minutes otherwise, and every guard run after it against the same mock then sees
        a busy device. The stored run (--reboot) and its review state are kept, exactly as
        the EEPROM record survives a power cycle."""
        global _naming, _run_start, _amp_start, _lysis_start, _err_table
        _naming = False
        _run_start = None
        _amp_start = None
        _lysis_start = None
        _err_table = False
        PRESSED.clear()
        print("[reset] back to idle (test hook)")
        return self._json({"ok": True, "phase": run_state()[0]})

    def _otaupload(self):
        """Mirror POST /otaupload: swallow the multipart body, then report success.
        The device flashes it and reboots; here we only exercise the UI path."""
        if self._busy():
            return self._json({"ok": False, "error": "device busy"})
        n = int(self.headers.get("Content-Length") or 0)
        remaining = n
        while remaining > 0:                     # drain, else the browser sees a reset
            remaining -= len(self.rfile.read(min(65536, remaining)) or b"")
        _ota["state"] = "updating"
        return self._json({"ok": True, "restarting": True})

    def _wifilist_get(self):
        live = _wifi_live()
        nets = []
        for s in _saved_wifi:
            e = {"ssid": s, "saved": True}
            if s == live:
                e["active"] = True
            nets.append(e)
        out = {"max": 5, "current": live, "nets": nets}
        if _wifi_trial:
            out["trial"] = _wifi_trial
        return self._json(out)

    def _wifilist_post(self):
        global _wifi_current
        body = self._body()
        # keep_blank_values: "current=" (empty) is the UNPIN command, and the default
        # parse_qs drops empty values entirely, so the reset silently did nothing.
        q = parse_qs(body, keep_blank_values=True)
        remove = q.get("remove", [""])[0]
        connect = q.get("connect", [""])[0]
        if "current" in q:  # test hook: pin/unpin which net the machine is joined to
            _wifi_current = q["current"][0] or None
            return self._json({"ok": True, "current": _wifi_current or ""})
        if remove:
            if remove in _saved_wifi:
                _saved_wifi.remove(remove)
                return self._json({"ok": True})
            return self._json({"ok": False, "error": "not saved"})
        if connect:
            if self._busy():
                return self._json({"ok": False, "error": "device busy"})
            if connect not in _saved_wifi:
                return self._json({"ok": False, "error": "not saved"})
            # promote to preferred (front), like the device does before rebooting
            _saved_wifi.remove(connect)
            _saved_wifi.insert(0, connect)
            _wifi_current = None  # rebooted onto it: connected == front again
            return self._json({"ok": True, "restarting": True, "seq": 1})
        return self._json({"ok": False, "error": "missing remove|connect"})

    def _ota_get(self):
        """Mirror GET /ota (webDashboard.cpp handleOtaStatus)."""
        out = {
            "version": "v2.4.3",
            "versionCode": 18,
            "state": _ota["state"],
            "hasUpdate": _ota["state"] == "available",
            "busy": _ota["state"] == "updating",
            "checked": _ota["checked"],
            "checkFailed": _ota.get("failed", False),
            "online": True,
        }
        if _ota["checked"]:
            out["newVersion"] = "v2.4.4"
            out["newVersionCode"] = 19
            out["notes"] = "Mock release notes: QR screen, viewer cap, UI contrast pass."
        return self._json(out)

    def _ota_post(self):
        """Mirror POST /ota?action=check|update. The check is instant here; on the device
        it is a blocking HTTPS GET queued onto SettingTask (PEND_OTACHECK)."""
        action = (parse_qs(urlparse(self.path).query).get("action") or [""])[0]
        if self._busy():
            return self._json({"ok": False, "error": "device busy"})
        if action == "check":
            _ota["checked"] = True
            _ota["state"] = "available"   # pretend GitHub offers a newer build
            return self._json({"ok": True, "queued": True})
        if action == "update":
            if _ota["state"] != "available":
                return self._json({"ok": False, "error": "no update available"})
            _ota["state"] = "updating"
            return self._json({"ok": True, "started": True})
        return self._json({"ok": False, "error": "unknown action"})

    def _reviewlast(self):
        # Reload the last completed run from EEPROM for the Result tab (device: PEND_REVIEW
        # on SettingTask). Refuse while busy - it would overwrite the live sensor buffer.
        # Mirror the device's ?go=1 requirement: without it a bare GET would trigger the
        # reload (AsyncWebServer matches methods bitwise, so GET reaches a POST route).
        global _reviewed
        if "go=" not in urlparse(self.path).query:
            return self._json({"ok": False, "error": "use POST /reviewlast?go=1"})
        if self._busy():
            return self._json({"ok": False, "error": "device busy"})
        if _stored:
            _reviewed = True   # the stored run is now cached -> /slots ready, /curve serves it
        return self._json({"ok": True, "queued": True})

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n).decode("utf-8") if n else ""

    def _busy(self):
        phase, _ = run_state()
        return bool(_calib_step) or phase in ("waitname", "heater", "waitamp", "amplification")

    def _config_post(self):
        # Mirror the device: reject while busy, then QUEUE. The apply happens later on
        # SettingTask (drain_cfg) and can still be dropped - hence only "queued" + seq.
        if self._busy():
            return self._json({"ok": False, "error": "device busy - cannot change settings now"})
        try:
            patch = json.loads(self._body())
        except Exception:
            return self._json({"ok": False, "error": "bad json"})
        seq = queue_cfg("config", patch)
        if seq is None:
            return self._json({"ok": False, "error": "busy, retry"})
        return self._json({"ok": True, "queued": True, "seq": seq})

    def _wifi_post(self):
        if self._busy():
            return self._json({"ok": False, "error": "device busy"})
        q = parse_qs(self._body())
        s = (q.get("ssid") or [""])[0]
        p = (q.get("pass") or [""])[0]
        if not (1 <= len(s) <= 32):
            return self._json({"ok": False, "error": "ssid must be 1..32 chars"})
        if len(p) > 54:  # EEPROM slot limit, not WPA2's 63
            return self._json({"ok": False, "error": "password max 54 chars (EEPROM slot limit)"})
        # Mirror the device's trial-then-commit: a save does NOT commit here. "wrongpass"
        # stands in for a password that fails the boot-test -> trial:failed, list unchanged.
        global _wifi_trial
        if p == "wrongpass":
            _wifi_trial = {"result": "failed", "ssid": s, "reason": "auth"}
        else:
            _wifi_trial = None
            _saved_wifi[:] = [s] + [x for x in _saved_wifi if x != s]  # commit to front
        seq = queue_cfg("wifi", s)
        if seq is None:
            return self._json({"ok": False, "error": "busy, retry"})
        return self._json({"ok": True, "queued": True, "restarting": True, "seq": seq})

    def _deviceid_post(self):
        if self._busy():
            return self._json({"ok": False, "error": "device busy"})
        q = parse_qs(self._body())
        i = (q.get("id") or [""])[0]
        if not (1 <= len(i) <= 9):
            return self._json({"ok": False, "error": "id must be 1..9 chars (char[10] in EEPROM)"})
        seq = queue_cfg("id", i)
        if seq is None:
            return self._json({"ok": False, "error": "busy, retry"})
        return self._json({"ok": True, "queued": True, "seq": seq})

    def _slots(self):
        # Mirror the firmware: while a run is amplifying, the cached results belong to
        # the PREVIOUS run but /curve already serves the new one -> hide them so the
        # Result tab never shows a table and a chart from different runs.
        phase, _ = run_state()
        # ready gates on results being AVAILABLE, not just on phase: after a --reboot the
        # RAM cache is empty until /reviewlast reloads the stored run (_reviewed).
        available = _ran_before or _reviewed or not _stored
        ready = phase != "amplification" and available
        slots = [{"name": SLOT_NAMES[i],
                  "sample": SLOT_SAMPLES[i],
                  "ct": FAKE_CT[i] if ready else None,
                  "result": FAKE_RESULT[i] if ready else ""}
                 for i in range(10)]
        return self._json({"ready": ready, "slots": slots})

    def _errors(self):
        """Mirror GET /errors (webDashboard.cpp handleErrors).

        Same `ready` gate as _slots on purpose: on the device both read one snapshot taken at
        the same instant, so a mock that let them disagree would hide exactly the desync the
        firmware is built to prevent.

        Two slots are seeded with the device's own 4-digit encoding
        (module*1000 + type*100 + step*10 + slot) so the table has something to render; the
        rest report no error.
        """
        phase, _ = run_state()
        available = _ran_before or _reviewed or not _stored
        ready = phase != "amplification" and available
        seeded = {2: "[Sensor Light]- no data from sensor 1st reading ",
                  7: "[Sensor Light]- no data from sensor 1st reading "}
        slots = []
        for i in range(10):
            if ready and i in seeded:
                slots.append({"code": 0 * 1000 + 1 * 100 + 0 * 10 + i, "text": seeded[i]})
            else:
                slots.append({"code": None})
        return self._json({"ready": ready, "slots": slots})

    def _rename(self):
        q = parse_qs(urlparse(self.path).query)
        try:
            slot = int((q.get("slot") or ["-1"])[0])
        except ValueError:
            slot = -1
        if not (0 <= slot < 10):
            return self._json({"ok": False, "error": "bad slot"})
        # name (disease) and sample are independent, like the firmware /rename.
        if "name" in q:
            SLOT_NAMES[slot] = q["name"][0]
            print(f"[rename] slot {slot} name -> {SLOT_NAMES[slot]!r}")
        if "sample" in q:
            SLOT_SAMPLES[slot] = q["sample"][0]
            print(f"[rename] slot {slot} sample -> {SLOT_SAMPLES[slot]!r}")
        return self._json({"ok": True, "slot": slot,
                           "name": SLOT_NAMES[slot], "sample": SLOT_SAMPLES[slot]})

    def _control(self):
        # Web button press -> light the chip AND drive the run, like the device.
        btn = (parse_qs(urlparse(self.path).query).get("btn") or [""])[0]
        if btn in ("red", "green", "white", "ampname"):
            press(btn)
            print(f"[control] press: {btn} -> phase {run_state()[0]}")
            return self._json({"ok": True, "btn": btn})
        return self._json({"ok": False, "error": "unknown btn"})

    def _json(self, obj):
        body = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _sse(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        sent = 0        # rounds already streamed for the current run
        last_home = 0.0
        try:
            while True:
                now = time.monotonic()
                drain_cfg()  # SettingTask applies queued settings on its own tick
                phase, rounds = run_state()

                if now - last_home >= HOME_PERIOD:
                    last_home = now
                    home_obj = home_payload(phase, rounds)
                    for btn in ("red", "green", "white"):
                        if PRESSED.get(btn, 0) > now:
                            home_obj["buttons"][btn] = True
                    self.wfile.write(
                        f"event: home\ndata: {json.dumps(home_obj)}\n\n".encode())
                    self.wfile.flush()

                # new_readings only during amplification. Stream EVERY completed round
                # exactly once, in order - the device sends one per round and never
                # skips, so catch up if this loop fell behind rather than dropping
                # points (a dropped point here would fake a gap the device never has).
                if phase == "amplification":
                    while sent < rounds:
                        obj = reading_at(sent)
                        obj["i"] = sent  # round index -> x base, same as /curve
                        self.wfile.write(
                            f"event: new_readings\ndata: {json.dumps(obj)}\n\n".encode())
                        sent += 1
                    self.wfile.flush()
                else:
                    sent = 0
                time.sleep(0.03)
        except (BrokenPipeError, ConnectionResetError):
            pass  # browser closed the tab / EventSource reconnecting


def selftest():
    # chart curve: in range, amplifying channels rise, JSON-safe
    r0, rlate = reading_at(0), reading_at(int(AMP_ROUNDS * 0.8))
    json.loads(json.dumps(r0))
    assert set(r0) == {f"#{i + 1}" for i in range(CHANNELS)}
    for i, (base, amp, _mid, _k) in enumerate(PROFILES):
        key = f"#{i + 1}"
        assert r0[key] >= base - 1
        if amp > 0:
            assert rlate[key] > r0[key]

    # home payloads: keys present, phases map to the firmware's fillStatus strings
    h_heat = home_payload("heater", 0)
    h_wait = home_payload("waitamp", 0)
    h_run = home_payload("amplification", 10)
    h_done = home_payload("finished", AMP_ROUNDS)
    json.loads(json.dumps(h_heat))
    assert set(h_heat["temps"]) == {"lysis", "ampLeft", "ampRight", "topLeft", "topRight"}
    assert h_heat["status"]["phase"] == "heater"
    assert h_wait["status"]["phase"] == "waitamp"     # naming stage
    assert h_wait["actions"]["red"] == "Start"        # web locks this until confirmed
    assert h_run["status"]["phase"] == "amplification"
    assert h_done["status"]["phase"] == "finished"    # fillStatus escreenFinished
    assert h_done["actions"]["white"] == "Next test"  # this press clears the Home chart
    assert h_done["notify"]["show"] is True
    assert h_heat["notify"]["show"] is False
    assert set(h_heat["buttons"]) == {"red", "green", "white"}
    assert all(v is False for v in h_run["buttons"].values())  # lit only on press

    # /curve = live COUNTER, else the retained last-run length (firmware handleCurve)
    assert curve_count("amplification", 6, True) == 6    # live run wins over fallback
    assert curve_count("finished", 0, True) == AMP_ROUNDS  # finished run stays readable
    assert curve_count("waitamp", 0, False) == 0        # first ever run: nothing yet
    # TRAP 1: clear() never runs per-run, so at waitamp of run B the device still
    # serves run A. Home must not preload this; Result legitimately shows it.
    assert curve_count("waitamp", 0, True) == AMP_ROUNDS
    assert curve_count("heater", 0, True) == AMP_ROUNDS
    # TRAP 2: COUNTER is 0 on the FIRST round of a new run too. The fallback must NOT
    # fire there, or a just-started run opens showing the previous run's curve.
    assert curve_count("amplification", 0, True) == 0

    print("selftest OK" + ("  [--full: 120 rounds x 20 s = 40 min run]" if FULL else ""))
    print("  scale:", AMP_ROUNDS, "rounds,", REPORT_INTERVAL_MS, "ms/round ->",
          f"{AMP_ROUNDS * REPORT_INTERVAL_MS / 60000:.0f} min run")
    print("  wait:", h_wait["status"], "red action:", h_wait["actions"]["red"])
    print("  done:", h_done["status"], "notify:", h_done["notify"]["show"])


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "selftest":
        selftest()
        sys.exit()
    # Port: first bare number on the command line, else 8000. Lets a second instance run
    # alongside the first (e.g. compare two UI revisions side by side).
    port = next((int(a) for a in sys.argv[1:] if a.isdigit()), 8000)
    srv = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print(f"Mock ESP32 SSE server on http://localhost:{port}  (Ctrl+C to stop)")
    print(f"  scale: {AMP_ROUNDS} rounds x {REPORT_INTERVAL_MS} ms "
          f"= {AMP_ROUNDS * REPORT_INTERVAL_MS / 60000:.0f} min run"
          f"{'  [--full]' if FULL else ''}")
    if SLOTS:
        print(f"  slots: replaying {SLOTS_PATH} - {len(SLOTS)} slot(s) x {AMP_ROUNDS} rounds")
    print("  the run waits at 'waitamp' until you press Start (red) on the web")
    srv.serve_forever()
