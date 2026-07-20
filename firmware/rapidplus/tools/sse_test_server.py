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
  Self-check: python tools/sse_test_server.py selftest
  E2E:        node tools/test_full_run.js              # against --full

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

AMP_ROUNDS = 120 if FULL else 44               # rounds in one amplification
REPORT_INTERVAL_MS = 20000 if FULL else 1200   # ms/round reported -> client x axis
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

# Result table (fake). Names persist in memory like the device's /slotnames.json.
SLOT_NAMES = [""] * 10
FAKE_CT = [22.3, None, None, 28.9, None, 19.5, None, None, None, None]
FAKE_RESULT = ["P", "N", "N", "S", "N", "P", "E", "N", "B", "N"]

# ---- Run state machine (shared by every client, like the real device) --------
# Amplification (RED) flow:  idle --red--> waitname --red--> heater --(timed)-->
#                            waitamp --red--> amplification --> finished --white--> idle
# Lysis (GREEN) flow:        idle --green--> heater (names at waitamp, no waitname gate)
_naming = False                # RED at idle -> waitname (name slots BEFORE heating)
_run_start = None              # monotonic when the heater phase began (None = not yet)
_amp_start = None              # monotonic when Start was pressed (None = not started)
_ran_before = False            # a run has completed since boot -> device holds a curve


def run_state():
    """(phase, rounds_done) mirroring the device. rounds_done == COUNTER (0 outside amp)."""
    global _ran_before
    now = time.monotonic()
    if _naming:
        return ("waitname", 0)          # HOLDS until RED confirms -> heating
    if _run_start is None:
        return ("idle", 0)              # escreenStart: waits for a button
    if _amp_start is None:
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
    return ("finished", AMP_ROUNDS)


def press(btn):
    """A web /control press, driving the run like the device's buttons do."""
    global _amp_start, _run_start, _naming
    PRESSED[btn] = time.monotonic() + 2.0
    phase, _ = run_state()
    if btn == "ampname" and phase == "idle":
        _naming = True                  # web Amplification --> ewaitname (name first)
    elif btn == "red" and phase == "idle":
        _run_start = time.monotonic()   # PHYSICAL Amplification --> heat directly
    elif btn == "red" and phase == "waitname":
        _naming = False                 # ewaitname --red(confirm)--> eheating67
        _run_start = time.monotonic()
    elif btn == "green" and phase == "idle":
        _run_start = time.monotonic()   # escreenStart --green(Lysis)--> heating
    elif btn == "red" and phase == "waitamp":
        _amp_start = time.monotonic()   # ewaitampTube --red--> eoptoreading
    elif btn == "white" and phase == "waitname":
        _naming = False                 # ewaitname --white--> escreenStart (cancel)
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
    if phase == "idle":
        lysis, amp, top = 25.0, 25.0, 25.0
    elif phase == "heater":
        lysis, amp, top = 45.0, 25.0, 60.0
    elif phase == "waitamp":
        lysis, amp, top = 65.0, 63.0, 105.0
    elif phase == "amplification":
        lysis, amp, top = 64.5, 63.0, 105.0
    else:
        lysis, amp, top = 64.0, 40.0, 105.0
    wig = 0.3 * math.sin(rounds / 3.0 + prog)

    # actions = what each button does in this state (fillActions in webDashboard.cpp)
    if phase == "idle":
        # escreenStart: the device sits here after boot / after Next test
        status = {"phase": "idle", "title": "Idle",
                  "subtitle": "Waiting for a run to start."}
        actions = {"green": "Lysis", "red": "Amplification", "white": ""}
    elif phase == "waitname":
        # ewaitname: name the slots BEFORE heating; RED confirms + starts preheat
        status = {"phase": "waitname", "title": "Name the samples",
                  "subtitle": "Pick a disease per slot, then start"}
        actions = {"green": "", "red": "Confirm & heat", "white": "Return"}
    elif phase == "heater":
        status = {"phase": "heater", "title": "Lysis heating",
                  "subtitle": "Warming sample to 65 C"}
        actions = {"green": "", "red": "", "white": "Return"}
    elif phase == "waitamp":
        status = {"phase": "waitamp", "title": "Insert amplification tube",
                  "subtitle": "Name slots, then start"}
        actions = {"green": "", "red": "Start", "white": "Return"}
    elif phase == "amplification":
        remain = (AMP_ROUNDS - rounds) * REPORT_INTERVAL_MS / 1000.0 / 60.0
        status = {"phase": "amplification", "title": "Amplification",
                  "subtitle": f"~{remain:.1f} min remaining"}
        actions = {"green": "", "red": "", "white": "Return"}
    else:
        status = {"phase": "finished", "title": "Run complete",
                  "subtitle": "Results ready"}
        actions = {"green": "", "red": "Errors", "white": "Next test"}

    # Mirrors webDashboard.cpp isBusy(): settings are locked unless the device is idle.
    # Of the mock's phases only "finished" is idle; calibrating counts as busy too.
    status["busy"] = bool(_calib_step) or phase in (
        "waitname", "heater", "waitamp", "amplification")
    status["calib"] = _calib_step

    return {
        "device": CONFIG.get("device ID", "RAPIDPlus"),
        "company": "Fortebiotech",
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
        if self.path.startswith("/rename"):
            return self._rename()
        if self.path.startswith("/curve"):
            return self._curve()
        if self.path.startswith("/config"):
            return self._json(CONFIG)
        if self.path.startswith("/wifiscan"):
            return self._wifiscan()
        if self.path.startswith("/calib"):
            return self._calib()
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
        n = curve_count(phase, rounds, _ran_before)
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
        if self.path.startswith("/calib"):
            return self._calib()
        self.send_error(404)

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
        ready = phase != "amplification"
        slots = [{"name": SLOT_NAMES[i],
                  "ct": FAKE_CT[i] if ready else None,
                  "result": FAKE_RESULT[i] if ready else ""}
                 for i in range(10)]
        return self._json({"ready": ready, "slots": slots})

    def _rename(self):
        q = parse_qs(urlparse(self.path).query)
        try:
            slot = int((q.get("slot") or ["-1"])[0])
        except ValueError:
            slot = -1
        name = (q.get("name") or [""])[0]
        if 0 <= slot < 10:
            SLOT_NAMES[slot] = name
            print(f"[rename] slot {slot} -> {name!r}")
            return self._json({"ok": True, "slot": slot, "name": name})
        return self._json({"ok": False, "error": "bad slot"})

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
    srv = ThreadingHTTPServer(("0.0.0.0", 8000), Handler)
    print("Mock ESP32 SSE server on http://localhost:8000  (Ctrl+C to stop)")
    print(f"  scale: {AMP_ROUNDS} rounds x {REPORT_INTERVAL_MS} ms "
          f"= {AMP_ROUNDS * REPORT_INTERVAL_MS / 60000:.0f} min run"
          f"{'  [--full]' if FULL else ''}")
    print("  the run waits at 'waitamp' until you press Start (red) on the web")
    srv.serve_forever()
