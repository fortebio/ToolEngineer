#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Run every guard in this repo with one command.

    python tools/check.py              # everything that needs no mock and no hardware
    python tools/check.py --mock       # also the guards that need sse_test_server.py
    python tools/check.py --list       # show what exists and what it needs; run nothing
    python tools/check.py -k wifi      # only guards whose filename contains "wifi"

Twenty-five guards were written for this firmware and until 2026-08-21 nothing ran
them, so they only fired when somebody remembered. Two had been silently broken for
days. This is that somebody.

Guards are DISCOVERED from tools/test_* rather than listed here, so a branch that has
fewer of them (or a new one nobody added to a list) is handled correctly. NEEDS below
only records which ones cannot run unattended.

Exit code is non-zero if any guard fails, so CI and hooks can gate on it.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")

# Guards that cannot run unattended. Everything else is treated as static.
#   "mock"   - needs tools/sse_test_server.py (some start their own)
#   "device" - needs real hardware; never run here
NEEDS = {
    "test_chart_ticks.js":      "mock",
    "test_chart_scale.js":      "mock",
    "test_setting_a11y.js":     "mock",
    "test_no_hscroll.js":       "mock",
    "test_wifi_e2e.js":         "mock",
    "test_review_reboot.js":    "mock",
    "test_error_table.js":      "mock",
    "test_home_error_table.js": "mock",
    "test_full_run.js":         "mock",
}

# One line each, so --list is useful without opening the files.
WHAT = {
    "test_no_runtime_wifi_begin.py": "WiFi.begin() only at boot (async_tcp deadlock)",
    "test_phase0_guards.py":         "no strcpy into parameter, -Wformat still on",
    "test_web_assets.py":            "embedded UI complete, no LittleFS",
    "test_ota_guards.py":            "OTA: rebootOnUpdate(false), eUpdateOTA not busy",
    "test_no_method_branch.py":      "no handler compares req->method() (GOTCHA 3)",
    "test_status_coverage.py":       "fillStatus/fillActions cover the same states",
    "test_qr_payload.py":            "QR payload <= 53 B, SSID read from the radio",
    "test_device_id.py":             "one device-id store, touch inputs >= 16 px",
    "test_tft_widths.py":            "no TFT string overflows 320 px",
    "test_i18n_keys.py":             "every device key exists in both languages",
    "test_config_migration.py":      "define.h / migration / self-check agree",
    "test_upload_targets.py":        "every upload reaches all three endpoints",
    "test_profile_minutes.js":       "Profile card shows minutes, stores seconds/loops",
    "test_readcmd_overflow.cpp":     "readCommand cannot overflow recvData[2048]",
    "test_curve_length.cpp":         "/reviewlast scans for the real run length",
    "test_wifi_store.cpp":           "saved-WiFi list contract",
    "test_wifi_bars.cpp":            "WiFi bars: thresholds match web, bitmaps nest",
    "test_json_key_present.cpp":     "JSON keys present in the upload payload",
    "test_chart_ticks.js":           "chart Y axis always 10 ticks, floor 200",
    "test_chart_scale.js":           "curve shape does not change with screen/orientation",
    "test_setting_a11y.js":          "Setting tab labels, focus, disabled styling",
    "test_no_hscroll.js":            "no screen scrolls sideways, 320-412 px",
    "test_wifi_e2e.js":              "WiFi list/pick/connect/forget end to end",
    "test_review_reboot.js":         "review last run after reboot",
    "test_error_table.js":           "error table replaces the chart",
    "test_home_error_table.js":      "RED after a run swaps Home to the error table",
    "test_full_run.js":              "full run: heat -> name -> amplify -> result",
}


def discover():
    """Every tools/test_* on this branch, as (name, tier, description)."""
    out = []
    for path in sorted(glob.glob(os.path.join(TOOLS, "test_*"))):
        name = os.path.basename(path)
        if not name.endswith((".py", ".js", ".cpp")):
            continue
        out.append((name, NEEDS.get(name, "static"), WHAT.get(name, "")))
    return out


def documented_but_absent():
    """Guards CLAUDE.md promises that this branch does not have.

    A guard the team believes exists, but does not, is worse than no guard at all.
    """
    doc = os.path.join(ROOT, "CLAUDE.md")
    if not os.path.exists(doc):
        return []
    with open(doc, encoding="utf-8", errors="replace") as fh:
        named = set(re.findall(r"tools/(test_[a-z0-9_]+\.(?:py|js|cpp))", fh.read()))
    return sorted(n for n in named if not os.path.exists(os.path.join(TOOLS, n)))


def runner_for(name):
    if name.endswith(".py"):
        return [sys.executable]
    if name.endswith(".js"):
        return ["node"] if shutil.which("node") else None
    if name.endswith(".cpp"):
        return ["g++"] if shutil.which("g++") else None
    return None


def run_one(name, timeout=300):
    path = os.path.join(TOOLS, name)
    r = runner_for(name)
    if r is None:
        return "SKIP", "toolchain not installed"
    try:
        if name.endswith(".cpp"):
            exe = os.path.join(TOOLS, "_" + name[:-4] + (".exe" if os.name == "nt" else ""))
            c = subprocess.run(["g++", "-O2", "-std=c++17", path, "-o", exe],
                               capture_output=True, text=True, timeout=timeout)
            if c.returncode != 0:
                tail = (c.stderr or c.stdout).strip().splitlines()
                return "FAIL", (tail[-1][:200] if tail else "compile failed")
            p = subprocess.run([exe], capture_output=True, text=True, timeout=timeout)
            try:
                os.remove(exe)
            except OSError:
                pass
        else:
            p = subprocess.run(r + [path], capture_output=True, text=True,
                               timeout=timeout, cwd=ROOT)
        if p.returncode == 0:
            return "PASS", ""
        lines = [l for l in ((p.stdout or "") + (p.stderr or "")).splitlines() if l.strip()]
        return "FAIL", (lines[-1][:200] if lines else "exit %d" % p.returncode)
    except subprocess.TimeoutExpired:
        return "FAIL", "timed out after %ds" % timeout
    except Exception as e:                                          # noqa: BLE001
        return "FAIL", str(e)[:200]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mock", action="store_true",
                    help="also run guards that need tools/sse_test_server.py")
    ap.add_argument("--list", action="store_true", help="list guards, run nothing")
    ap.add_argument("-k", metavar="TEXT", default="", help="only guards matching TEXT")
    args = ap.parse_args()

    found = discover()
    absent = documented_but_absent()

    if args.list:
        print("%-32s %-8s %s" % ("guard", "needs", "what it protects"))
        for name, tier, why in found:
            print("%-32s %-8s %s" % (name, tier, why))
        for name in absent:
            print("%-32s %-8s %s" % (name, "-", "DOCUMENTED IN CLAUDE.md BUT ABSENT"))
        return 0

    sel = [g for g in found if args.k.lower() in g[0].lower()]
    if not args.mock:
        sel = [g for g in sel if g[1] == "static"]

    print("running %d guard(s)%s\n"
          % (len(sel), "" if args.mock else "   [static only; --mock adds the rest]"))
    t0 = time.time()
    results = []
    for name, tier, why in sel:
        sys.stdout.write("  %-32s " % name)
        sys.stdout.flush()
        status, detail = run_one(name)
        results.append((status, name, why, detail))
        print(status + (("  - " + detail) if detail else ""))

    bad = [r for r in results if r[0] == "FAIL"]
    skip = [r for r in results if r[0] == "SKIP"]
    print("\n%d passed, %d failed, %d skipped, %d documented-but-absent   (%.1fs)"
          % (len(results) - len(bad) - len(skip), len(bad), len(skip), len(absent),
             time.time() - t0))
    if absent:
        print("\nDOCUMENTED IN CLAUDE.md BUT NOT IN THIS BRANCH:")
        for name in absent:
            print("   %-32s %s" % (name, WHAT.get(name, "")))
    if bad:
        print("\nFAILED:")
        for _, name, why, detail in bad:
            print("   %-32s %s" % (name, why))
            print("   %-32s %s" % ("", detail))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
