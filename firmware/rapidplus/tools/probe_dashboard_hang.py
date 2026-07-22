#!/usr/bin/env python3
"""
Reproduce & measure the "Up Data hangs the dashboard" bug on a REAL device.

The failure is invisible from the machine's LCD ("Upload Failed" then the web UI
just stops) and hard to reason about by eye. This turns it into hard data: it
polls the live dashboard and an open SSE stream (exactly what a browser holds)
and prints the precise instant the dashboard stops answering and whether/when it
comes back -- so you can SEE the hang, time it, and confirm a fix removed it.

The bug: WiFi.begin() from the DisplayTask deadlocks async_tcp (see
docs/history/2026-07-20-updata-wifi-reconnect-hang.md). The symptom: the device
still answers ping / completes a TCP SYN, but HTTP requests and the SSE stream go
silent forever.

HOW TO USE (semi-automatic -- the trigger is a physical action, like the real bug)
  1. Find the device IP (serial log line "[dash] dashboard on http://.../").
  2. Run:   python tools/probe_dashboard_hang.py 192.168.1.10
     (add a number to auto-stop:  python tools/probe_dashboard_hang.py 192.168.1.10 60)
  3. Confirm it prints "ALIVE" steadily.
  4. Trigger the bug the way a user would, ONE of:
       - press "Up Data" on the machine (hold RED -> RED) while WiFi is flaky, or
       - pull power on the WiFi AP/router for a few seconds so STA drops.
  5. Watch the timeline. Ctrl+C for the summary.

Reading it:
  - BEFORE the permanent fix: after the trigger you see "ALIVE -> DEAD" and it
    NEVER returns to ALIVE (SSE also stops) -> the deadlock reproduced.
  - AFTER the fix (no app-side WiFi.begin): the dashboard stays ALIVE the whole
    time, or blips DEAD only while the AP is physically down and returns to ALIVE
    on its own within seconds -> fixed.

Only Python stdlib (urllib, socket, threading), like the repo's other test tools.
"""
import socket
import sys
import threading
import time
import urllib.request

HOME_TIMEOUT = 2.0     # a healthy /home answers in ~20ms; 2s = generous
POLL_EVERY = 0.4       # seconds between /home polls
SSE_SILENT_HANG = 6.0  # no SSE bytes for this long while polling -> SSE considered hung
                       # (device pushes a "home" event every ~1s)


class Probe:
    def __init__(self, ip, duration=None):
        self.ip = ip
        self.duration = duration    # auto-stop after N seconds (None = until Ctrl+C)
        self.t0 = time.monotonic()
        self.http_alive = None      # None=unknown, True/False
        self.transitions = []       # (t, "ALIVE"/"DEAD", detail)
        self.dead_total = 0.0
        self._dead_since = None
        self.last_sse = None        # monotonic time of last SSE byte
        self.sse_events = 0
        self.stop = threading.Event()

    def _t(self):
        return time.monotonic() - self.t0

    def _mark(self, alive, detail):
        if alive == self.http_alive:
            return
        first = self.http_alive is None  # unknown -> first reading, not a transition
        was_dead = self._dead_since is not None
        # accumulate dead time on recovery
        if alive and was_dead:
            self.dead_total += time.monotonic() - self._dead_since
            self._dead_since = None
        if not alive:
            self._dead_since = time.monotonic()
        self.http_alive = alive
        state = "ALIVE" if alive else "DEAD"
        # Only log real state changes as transitions (not the first-ever reading).
        if not first:
            self.transitions.append((self._t(), state, detail))
        arrow = "  <<< recovered" if (alive and was_dead) else ""
        print(f"[{self._t():7.1f}s]  {'-> ' + state:>8}   {detail}{arrow}")

    # ---- /home poller (the "is the web server answering" signal) -------------
    def poll_home(self):
        url = f"http://{self.ip}/home"
        while not self.stop.is_set():
            t = self._t()
            try:
                start = time.monotonic()
                with urllib.request.urlopen(url, timeout=HOME_TIMEOUT) as r:
                    r.read(64)
                rtt = (time.monotonic() - start) * 1000
                self._mark(True, f"/home 200 in {rtt:.0f}ms")
            except Exception as e:
                self._mark(False, f"/home DID NOT ANSWER ({type(e).__name__}: {e})")
            self.stop.wait(POLL_EVERY)

    # ---- SSE reader (what a browser tab holds open) --------------------------
    def read_sse(self):
        while not self.stop.is_set():
            try:
                s = socket.create_connection((self.ip, 80), timeout=5)
                req = (f"GET /events HTTP/1.1\r\nHost: {self.ip}\r\n"
                       "Accept: text/event-stream\r\nConnection: keep-alive\r\n\r\n")
                s.sendall(req.encode())
                s.settimeout(1.0)
                self.last_sse = time.monotonic()
                while not self.stop.is_set():
                    try:
                        chunk = s.recv(2048)
                        if not chunk:
                            break
                        self.last_sse = time.monotonic()
                        self.sse_events += chunk.count(b"event:")
                    except socket.timeout:
                        pass
                s.close()
            except Exception:
                time.sleep(1.0)

    def sse_state(self):
        if self.last_sse is None:
            return "connecting"
        gap = time.monotonic() - self.last_sse
        return "flowing" if gap < SSE_SILENT_HANG else f"SILENT {gap:.0f}s"

    def run(self):
        print(f"Probing dashboard at http://{self.ip}/  (Ctrl+C for summary)")
        print("Steady 'ALIVE' means the web server answers. Trigger the bug now.\n")
        threads = [
            threading.Thread(target=self.poll_home, daemon=True),
            threading.Thread(target=self.read_sse, daemon=True),
        ]
        for th in threads:
            th.start()
        try:
            while self.duration is None or self._t() < self.duration:
                time.sleep(5)
                print(f"[{self._t():7.1f}s]  status: http={'ALIVE' if self.http_alive else 'DEAD'}"
                      f"  sse={self.sse_state()}  (sse events seen: {self.sse_events})")
        except KeyboardInterrupt:
            pass
        finally:
            self.stop.set()
            self.summary()

    def summary(self):
        # close out any ongoing dead spell
        if self._dead_since is not None:
            self.dead_total += time.monotonic() - self._dead_since
        total = self._t()
        print("\n" + "=" * 60)
        print("SUMMARY")
        print(f"  observed:      {total:.1f}s")
        print(f"  dashboard dead: {self.dead_total:.1f}s "
              f"({100*self.dead_total/total:.0f}% of the time)" if total else "")
        print(f"  transitions:   {len(self.transitions)}")
        for t, state, detail in self.transitions:
            print(f"    [{t:7.1f}s] {state:5}  {detail}")
        print(f"  SSE events seen: {self.sse_events}  (final SSE state: {self.sse_state()})")
        print("=" * 60)
        # Verdict heuristic
        dead_count = sum(1 for _, s, _ in self.transitions if s == "DEAD")
        if dead_count == 0:
            print("VERDICT: dashboard stayed ALIVE the whole time -> no hang observed.")
        elif self.http_alive:
            print("VERDICT: dashboard went DEAD then RECOVERED on its own -> transient,\n"
                  "         consistent with the permanent fix (core auto-reconnect).")
        else:
            print("VERDICT: dashboard went DEAD and DID NOT RECOVER -> the permanent hang\n"
                  "         reproduced. Needs a power cycle. This is the bug.")


def main():
    ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.10"
    dur = float(sys.argv[2]) if len(sys.argv) > 2 else None
    Probe(ip, dur).run()


if __name__ == "__main__":
    main()
