#!/usr/bin/env python3
"""dash_mock.py — mock máy Rapid4P cho web dashboard (main/web/dashboard.html), không cần phần cứng.

    python scripts\\dash_mock.py [port=8765]

Phục vụ dashboard.html + /api/state với vòng đời giả: idle → prepare → measuring (3×N bước, 1 s)
→ result (giữ 15 s) → idle. POST /api/control?btn=measure|back đổi pha như máy thật;
POST /api/threshold lưu trong RAM. Học theo firmware/rapidplus/tools/sse_test_server.py.
"""
import json, os, sys, time, random
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HTML = open(os.path.join(os.path.dirname(__file__), "..", "main", "web", "dashboard.html"), "rb").read()
SICK = [("PC", "Chứng Dương"), ("EHP", "EHP"), ("EMS", "EMS"), ("WSSV", "WSSV"), ("TPD", "TPD")]
thr = {k: 600 for k, _ in SICK}
N = 5   # số khe mock (máy thật báo qua sensors.total)
S = {"phase": "idle", "t0": time.time(), "step": 0, "r": [[0, 0, 0] for _ in range(N)], "has": False}


def tick():
    now = time.time()
    if S["phase"] == "measuring":
        step = int(now - S["t0"])
        if step >= 3 * N:
            S["phase"] = "result"; S["t0"] = now; S["has"] = True
        else:
            S["step"] = step
            rnd, slot = divmod(step, N)
            if S["r"][slot][rnd] == 0 and step > 0:
                pr, ps = divmod(step - 1, N)
                if S["r"][ps][pr] == 0: S["r"][ps][pr] = random.randint(200, 1100)
    elif S["phase"] == "result" and now - S["t0"] > 15:
        S["phase"] = "idle"


def state():
    tick()
    busy = S["phase"] == "measuring"
    rnd, slot = divmod(S["step"], N)
    if S["phase"] == "result":
        for i in range(N):
            for k in range(3):
                if S["r"][i][k] == 0: S["r"][i][k] = random.randint(200, 1100)
    slots = []
    for i in range(N):
        vals = S["r"][i]; avg = sum(vals) // 3 if all(vals) else 0
        slots.append({"r": vals, "avg": avg, "positive": avg >= thr["PC"], "ok": i != N - 1, "calibrated": i < 2})
    return {
        "device": {"id": "R4P-0001", "fw": "v0.1.0", "ip": "127.0.0.1", "heap": 213000, "uptime_s": int(time.time() - 1e3), "lang": "Tiếng Việt"},
        "ui": {"screen": 4 if busy else 0, "name": "measuring" if busy else "start"},
        "phase": S["phase"],
        "sensors": {"alive": N - 1, "total": N},
        "measure": {"busy": busy, "sick": "PC", "sick_label": "Chứng Dương", "sample": "PRAWN Vannamei", "sample_label": "Tôm Thẻ",
                    "round": rnd + 1 if busy else 0, "rounds": 3, "slot": slot if busy else 0,
                    "progress": S["step"] * 100 // (3 * N) if busy else (100 if S["phase"] == "result" else 0),
                    "has_result": S["has"], "threshold": thr["PC"], "slots": slots},
        "upload": {"pending": 1 if S["has"] else 0, "sent": 3},
        "thresholds": {k: {"label": l, "value": thr[k]} for k, l in SICK},
    }


class H(BaseHTTPRequestHandler):
    def _json(self, obj, code=200):
        b = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)

    def do_GET(self):
        p = urlparse(self.path)
        if p.path == "/":
            self.send_response(200); self.send_header("Content-Type", "text/html; charset=utf-8"); self.send_header("Content-Length", str(len(HTML))); self.end_headers(); self.wfile.write(HTML)
        elif p.path == "/api/state":
            self._json(state())
        else:
            self.send_response(404); self.end_headers()

    def do_POST(self):
        p = urlparse(self.path); q = parse_qs(p.query)
        if p.path == "/api/control":
            btn = q.get("btn", [""])[0]
            if btn == "measure":
                if S["phase"] in ("idle", "result"): S["phase"] = "prepare"; S["r"] = [[0, 0, 0] for _ in range(N)]
                elif S["phase"] == "prepare": S["phase"] = "measuring"; S["t0"] = time.time(); S["step"] = 0
            elif btn == "back":
                S["phase"] = "idle"
            self._json({"ok": True})
        elif p.path == "/api/threshold":
            k = q.get("sick", [""])[0]; v = int(q.get("value", ["0"])[0])
            if k in thr and 0 <= v <= 9999: thr[k] = v; self._json({"ok": True})
            else: self._json({"ok": False, "err": "range"}, 400)
        else:
            self.send_response(404); self.end_headers()

    def log_message(self, *a): pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    print(f"mock Rapid4P dashboard: http://127.0.0.1:{port}/")
    ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
