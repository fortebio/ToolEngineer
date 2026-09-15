#!/usr/bin/env python3
"""
Guard the invariant that fixes the "Up Data hangs the dashboard forever" bug:

    WiFi.begin() must be called ONLY from setup() (main.cpp), never from a task
    at runtime.

WHY THIS IS THE RIGHT TEST FOR THIS BUG
Pressing "Up Data" while STA had dropped ran WiFi.begin() from the DisplayTask
(screen_Result, displayLCD.cpp). In this build LWIP_TCPIP_CORE_LOCKING is off and
CONFIG_ASYNC_TCP_USE_WDT=0, so that call thrashes the WiFi/lwIP stack and drives
async_tcp + the tcpip thread into a timeout-free deadlock (both park on
portMAX_DELAY): the dashboard accepts TCP but never answers again until a power
cycle. See docs/history/2026-07-20-updata-wifi-reconnect-hang.md.

The permanent fix is: DON'T reconnect from app code. WiFi.setAutoReconnect(true)
(main.cpp) already makes the core re-associate a dropped STA in the background.
So the whole class of bug is prevented by one static rule: the only runtime
WiFi.begin() lives in setup(). This test enforces exactly that rule, so a future
edit that reintroduces an app-side WiFi.begin() (the mistake that caused this)
fails here instead of on a bench 40 minutes into a run.

It also asserts WiFi.setAutoReconnect(true) is still present, because deleting the
app-side reconnect is only safe while the core's background reconnect is enabled.

Runs on the host, no hardware:  python tools/test_no_runtime_wifi_begin.py
Exit 0 = pass, 1 = fail.
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src"

# WiFi.begin() is allowed ONLY here (the one-time, non-blocking boot bring-up).
BOOT_FILE = "main.cpp"


def strip_comments(text: str) -> str:
    """Remove /* ... */ and // ... comments so we only match real code.

    Good enough for this codebase (no "WiFi.begin(" inside string literals).
    Keeps newlines so reported line numbers stay correct.
    """
    # Block comments -> keep the newlines they span (so line numbers survive).
    def _blank(m):
        return "".join(ch if ch == "\n" else " " for ch in m.group(0))

    text = re.sub(r"/\*.*?\*/", _blank, text, flags=re.DOTALL)
    # Line comments.
    text = re.sub(r"//[^\n]*", "", text)
    return text


def find_calls(pattern: str):
    """Yield (file, lineno, line) for every non-comment match of `pattern`."""
    rx = re.compile(pattern)
    for path in sorted(SRC.rglob("*.cpp")) + sorted(SRC.rglob("*.h")):
        code = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for i, line in enumerate(code.splitlines(), 1):
            if rx.search(line):
                yield path.name, i, line.strip()


def main() -> int:
    failures = []

    # 1. WiFi.begin() must appear only in main.cpp (setup()).
    begins = list(find_calls(r"\bWiFi\.begin\s*\("))
    stray = [(f, ln, s) for (f, ln, s) in begins if f != BOOT_FILE]
    boot = [(f, ln, s) for (f, ln, s) in begins if f == BOOT_FILE]

    print(f"WiFi.begin() call sites found: {len(begins)}")
    for f, ln, s in begins:
        tag = "OK (boot)" if f == BOOT_FILE else "!! RUNTIME"
        print(f"  {tag:12} {f}:{ln}: {s}")

    if not boot:
        failures.append(
            f"expected the boot WiFi.begin() in {BOOT_FILE} (setup()); none found. "
            "Did the one-time STA bring-up get removed?"
        )
    for f, ln, s in stray:
        failures.append(
            f"{f}:{ln} calls WiFi.begin() at runtime (outside setup()). This is the "
            "exact cause of the permanent dashboard deadlock: WiFi.begin() from a task "
            "thrashes the WiFi/lwIP stack and, with CONFIG_ASYNC_TCP_USE_WDT=0, hangs "
            "async_tcp forever. Delete it and rely on WiFi.setAutoReconnect(true); the "
            "core reconnects a dropped STA in the background. "
            "(docs/history/2026-07-20-updata-wifi-reconnect-hang.md)"
        )

    # 2. The core's background reconnect must stay enabled (that's what makes it
    #    safe to have no app-side reconnect).
    autorec = list(find_calls(r"WiFi\.setAutoReconnect\s*\(\s*true\s*\)"))
    print(f"\nWiFi.setAutoReconnect(true) present: {'yes' if autorec else 'NO'}")
    for f, ln, s in autorec:
        print(f"  {f}:{ln}: {s}")
    if not autorec:
        failures.append(
            "WiFi.setAutoReconnect(true) is gone. With no app-side WiFi.begin() AND no "
            "auto-reconnect, a dropped STA never comes back. Restore it in setup()."
        )

    print()
    if failures:
        print("FAIL:")
        for msg in failures:
            print("  - " + msg)
        return 1
    print("PASS: the only WiFi.begin() is the boot one, and auto-reconnect is on.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
