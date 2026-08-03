#!/usr/bin/env python3
"""
Guard the three OTA invariants that no host test can exercise and that fail SILENTLY on a
device: the machine simply never updates, and nobody finds out until a unit needs a fix.

All three were real bugs caught in review of the Pha 1 diff
(docs/plan/2026-07-28-ota-fleet-upgrade-243.md, items 8-9):

1. eUpdateOTA must be in the NOT-busy allowlist of isBusy() (webDashboard.cpp).
   updateFirmware() re-checks dashboardDeviceBusy() before downloading, and dashboardLoop()
   gates the deferred reboot on it. With the OTA prompt counted as busy, pressing RED on
   that prompt aborts its own download, and a downloaded image is never activated because
   the screen it sits on never becomes idle. Nothing runs behind that prompt - it is only
   reachable from the boot-time checkFirmware().

2. httpUpdate.rebootOnUpdate(false) must be set before httpUpdate.update().
   Otherwise the library calls ESP.restart() inside update() (HTTPUpdate.cpp:353) and the
   "don't reboot into a running assay" gate never executes.

3. The ?md5= parameter must be lowercased before Update.setMD5().
   Update.end() compares it case-sensitively against MD5Builder's always-lowercase digest,
   so an uppercase hash (what PowerShell's Get-FileHash prints) would be accepted by the
   validator and then rejected after the whole 2.3 MB image had been uploaded.

    python tools/test_ota_guards.py     # exit 0 = pass
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
fails = []


def uncommented(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return "\n".join(re.sub(r"//.*", "", ln) for ln in text.splitlines())


dash = uncommented((SRC / "webDashboard.cpp").read_text(encoding="utf-8", errors="replace"))
ota = uncommented((SRC / "updateOTA.cpp").read_text(encoding="utf-8", errors="replace"))

# 1. eUpdateOTA is not busy - but only matters while updateFirmware() gates on it, so check
#    the pair together rather than asserting a lone constant.
# Item 9 REQUIRES this call; treating its absence as "nothing to check" would let a single
# deletion switch off two of the three guards below.
#
# Look INSIDE updateFirmware() and require the call to precede httpUpdate.update(). A plain
# "is the string anywhere in the file" test was already defeated once: the helper
# showStartScreenIfIdle() also calls dashboardDeviceBusy(), so deleting the real re-check
# left the guard green - a guard that guards its own bystander.
fn = re.search(r"void updateFirmware\(void\)\s*\{(.*?)\n\}", ota, re.S)
if not fn:
    fails.append("updateOTA.cpp: updateFirmware() not found - this guard is checking nothing")
body = fn.group(1) if fn else ""
i_busy, i_update = body.find("dashboardDeviceBusy()"), body.find("httpUpdate.update(")
gates_on_busy = i_busy >= 0 and i_update >= 0 and i_busy < i_update
if not gates_on_busy:
    fails.append("updateOTA.cpp: the dashboardDeviceBusy() re-check before httpUpdate.update() "
                 "is gone (plan item 9) - OTA would download and reboot into a running assay")
    gates_on_busy = True  # keep checking the rest; they only make sense together
m = re.search(r"static bool isBusy\(e_statuslcd s\)\s*\{(.*?)\n\}", dash, re.S)
if not m:
    fails.append("webDashboard.cpp: isBusy() not found")
elif gates_on_busy:
    allow = m.group(1).split("return false")[0]
    if "eUpdateOTA" not in allow:
        fails.append("webDashboard.cpp: eUpdateOTA is missing from the not-busy allowlist in "
                     "isBusy(), so updateFirmware()'s busy check aborts the on-device OTA and "
                     "the deferred reboot never fires")

# 2/3. the two calls that make the OTA paths behave as intended
if gates_on_busy:
    order = ota.find("rebootOnUpdate(false)"), ota.find("httpUpdate.update(")
    if order[0] < 0:
        fails.append("updateOTA.cpp: httpUpdate.rebootOnUpdate(false) is gone - the library "
                     "restarts inside update() and the busy gate before the reboot is dead code")
    elif order[1] >= 0 and order[0] > order[1]:
        fails.append("updateOTA.cpp: rebootOnUpdate(false) is set AFTER httpUpdate.update()")

if "setMD5(" in dash:
    md5 = dash[dash.find('hasParam("md5")'):dash.find("setMD5(")]
    if "toLowerCase()" not in md5:
        fails.append("webDashboard.cpp: the ?md5= value is not lowercased before Update.setMD5() "
                     "- an uppercase digest (PowerShell Get-FileHash) fails only after the whole "
                     "image has been uploaded")

for f in fails:
    print("FAIL " + f)
print(("FAILED (%d)" % len(fails)) if fails else "ok - OTA invariants hold")
sys.exit(1 if fails else 0)
