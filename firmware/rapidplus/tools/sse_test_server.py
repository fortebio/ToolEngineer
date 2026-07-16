#!/usr/bin/env python3
"""Mock ESP32 SSE server for testing the RAPID live dashboard WITHOUT hardware.

Serves the real data/ client files (index.html, script.js, style.css) and streams,
on /events, two SSE event types exactly like the firmware's AsyncEventSource will:

  event: home          -> home screen state (temps, status, notify, buttons)
  event: new_readings  -> process chart, scalar per channel {"#1": num, ... "#10": num}

It walks a fake run lifecycle (Lysis heating -> Amplification -> finished + notify),
looping so every UI state is observable. Lets you validate the whole dashboard in a
browser before touching the ESP32.

  Run:        python tools/sse_test_server.py        # open http://localhost:8000
  Self-check: python tools/sse_test_server.py selftest

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
TICK_SEC = 1.2   # how often a reading is pushed
CYCLE = 80       # ticks per simulated run before it loops

# Lifecycle phase boundaries (in ticks)
T_HEAT_END = 12  # 0..12   Lysis heating
T_AMP_END = 55   # 12..55  Amplification
# 55..CYCLE       finished + notification shown

# Manual button presses from the web UI: btn -> monotonic() expiry time.
PRESSED = {}


# ---- Amplification chart curves -------------------------------------------
def _profiles():
    import random
    random.seed(42)  # deterministic so demo + selftest reproduce
    out = []
    for _ in range(CHANNELS):
        negative = random.random() < 0.25
        out.append((
            random.uniform(40, 90),                        # baseline fluorescence
            0.0 if negative else random.uniform(300, 550),  # amplitude
            random.uniform(8, 22),                         # takeoff cycle
            random.uniform(0.4, 0.9),                      # steepness
        ))
    return out


PROFILES = _profiles()


def reading_at(tick):
    """One time-point across all channels -> {'#1': val, ... '#10': val}."""
    out = {}
    for i, (base, amp, mid, k) in enumerate(PROFILES):
        y = base + amp / (1 + math.exp(-k * (tick - mid)))
        out[f"#{i + 1}"] = round(y, 1)
    return out


# ---- Home screen state -----------------------------------------------------
def _ramp(tick, start, end, t0, t1):
    if tick <= t0:
        return start
    if tick >= t1:
        return end
    return start + (end - start) * (tick - t0) / (t1 - t0)


def home_at(tick):
    heating = tick < T_HEAT_END
    running = T_HEAT_END <= tick < T_AMP_END
    finished = tick >= T_AMP_END

    lysis = 64.0 if finished else _ramp(tick, 25, 65, 0, T_HEAT_END)
    amp = 40.0 if finished else _ramp(tick, 25, 63, 8, 16)
    top = _ramp(tick, 25, 105, 0, 10)
    wig = 0.3 * math.sin(tick / 3.0)

    if heating:
        status = {"phase": "heater", "title": "Lysis heating",
                  "subtitle": "Warming sample to 65 C"}
    elif running:
        remain = (T_AMP_END - tick) * TICK_SEC / 60.0
        status = {"phase": "amplification", "title": "Amplification",
                  "subtitle": f"~{remain:.1f} min remaining"}
    else:
        status = {"phase": "idle", "title": "Run complete",
                  "subtitle": "Results ready"}

    return {
        "device": "RAPIDPlus",
        "company": "Fortebiotech",
        "temps": {
            "lysis": round(lysis + wig, 1),
            "ampLeft": round(amp + wig, 1),
            "ampRight": round(amp - wig, 1),
            "topLeft": round(top + wig, 1),
            "topRight": round(top - wig, 1),
        },
        "status": status,
        "notify": {"show": finished,
                   "title": "Amplification finished",
                   "subtitle": "Remove the cartridge to finish the run"},
        "buttons": {"red": False, "green": False, "white": False},
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
            return self._json(reading_at(0))
        if self.path.startswith("/home"):
            return self._json(home_at(0))
        if self.path.startswith("/control"):
            return self._control()
        return super().do_GET()  # static files from data/

    def do_POST(self):
        if self.path.startswith("/control"):
            return self._control()
        self.send_error(404)

    def _control(self):
        # Web UI button press -> light that button in the SSE stream for 2s so the
        # tester sees the loop close. The firmware maps btn -> handleShortPress_*().
        btn = (parse_qs(urlparse(self.path).query).get("btn") or [""])[0]
        if btn in ("red", "green", "white"):
            PRESSED[btn] = time.monotonic() + 2.0
            print(f"[control] press: {btn}")
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
        tick = 0
        try:
            while True:
                t = tick % CYCLE
                home_obj = home_at(t)
                now = time.monotonic()
                for btn in ("red", "green", "white"):
                    if PRESSED.get(btn, 0) > now:
                        home_obj["buttons"][btn] = True
                home = json.dumps(home_obj)
                readings = json.dumps(reading_at(t))
                self.wfile.write(f"event: home\ndata: {home}\n\n".encode())
                self.wfile.write(f"event: new_readings\ndata: {readings}\n\n".encode())
                self.wfile.flush()
                tick += 1
                time.sleep(TICK_SEC)
        except (BrokenPipeError, ConnectionResetError):
            pass  # browser closed the tab / EventSource reconnecting


def selftest():
    # chart curve: in range, amplifying channels rise, JSON-safe
    r0, r30 = reading_at(0), reading_at(30)
    json.loads(json.dumps(r0))
    assert set(r0) == {f"#{i + 1}" for i in range(CHANNELS)}
    for i, (base, amp, _mid, _k) in enumerate(PROFILES):
        key = f"#{i + 1}"
        assert r0[key] >= base - 1
        if amp > 0:
            assert r30[key] > r0[key]

    # home state: keys present, phases progress, JSON-safe
    h_heat, h_run, h_done = home_at(2), home_at(30), home_at(70)
    json.loads(json.dumps(h_heat))
    assert set(h_heat["temps"]) == {"lysis", "ampLeft", "ampRight", "topLeft", "topRight"}
    assert h_heat["status"]["phase"] == "heater"
    assert h_run["status"]["phase"] == "amplification"
    assert h_done["notify"]["show"] is True
    assert set(h_heat["buttons"]) == {"red", "green", "white"}
    assert all(v is False for v in h_run["buttons"].values())  # lit only on press
    assert h_heat["notify"]["show"] is False
    assert h_run["temps"]["lysis"] > h_heat["temps"]["lysis"]  # warmed up
    print("selftest OK")
    print("  heat:", h_heat["status"], h_heat["temps"])
    print("  done:", h_done["status"], "notify:", h_done["notify"]["show"])


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "selftest":
        selftest()
        sys.exit()
    srv = ThreadingHTTPServer(("0.0.0.0", 8000), Handler)
    print("Mock ESP32 SSE server on http://localhost:8000  (Ctrl+C to stop)")
    srv.serve_forever()
