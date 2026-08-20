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
    # (?<!:) so the // inside an https:// literal survives. Without it every URL in the file
    # was truncated at its scheme, and the "no GitHub URL" check below could not see a URL at
    # all - a guard blinded by its own helper, the same self-blinding test_qr_payload.py had
    # to fix and CLAUDE.md writes up. Verified: re-adding a raw githubusercontent.com literal
    # to HOSTS[] passes the old helper and fails this one.
    return "\n".join(re.sub(r"(?<!:)//.*", "", ln) for ln in text.splitlines())


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

# 4. v2.4.4 moved OTA off GitHub onto the ingest server. Every failure below is SILENT on a
#    device - a 401 looks exactly like "no update available" from the operator's chair.
if "raw.githubusercontent.com" in ota:
    fails.append("updateOTA.cpp: the GitHub OTA URL is back. Two sources means a machine can be "
                 "offered a build nobody chose on the server - pick one")

chk = re.search(r"static bool otaCheckHost\([^)]*\)\s*\{(.*?)\n\}", ota, re.S)
if not chk:
    fails.append("updateOTA.cpp: otaCheckHost() not found - this guard is checking nothing")
else:
    cb = chk.group(1)
    # Both requests need the Bearer, and they get it from ONE helper. Grepping for the header
    # string alone would go green on a build where only the /ota/check call has it and every
    # download 401s after the prompt has already been shown.
    if "addBearer" not in cb:
        fails.append("updateOTA.cpp: otaCheckHost() no longer sends the Bearer - GET /ota/check "
                     "returns 401, which this code reports as a failed check and nothing else")
    if "addBearer" not in (re.search(r"httpUpdate\.update\([^;]*", ota, re.S) or
                           re.match("", "")).group(0):
        fails.append("updateOTA.cpp: httpUpdate.update() lost its request callback - the .bin GET "
                     "goes out unauthenticated, so every accepted update fails at download")
    # A 200 is not an answer until it parses. Dropping this check makes a Cloudflare Access
    # login page (or any HTML error body) read as update=false -> "up to date" -> return true,
    # which ALSO stops the two-host loop before the fallback is ever tried. Silent on-device.
    if "deserializeJson" not in cb or "DeserializationError" not in cb:
        fails.append("updateOTA.cpp: otaCheckHost() no longer checks the deserializeJson() "
                     "result - an HTML error page from the edge is read as \"up to date\" AND "
                     "silences the fallback host, which is the one thing it exists for")
    # A check that lands mid-download must not clear OTA_UPDATING (the only flag holding
    # dashboardDeviceBusy() true) nor reassign fwUrl, which httpUpdate holds by reference for
    # the whole ~2 minute stream.
    if "OTA_UPDATING" not in cb:
        fails.append("updateOTA.cpp: otaCheckHost() no longer refuses to publish while an "
                     "install is in flight - it can clear OTA_UPDATING mid-write (reopening the "
                     "reboot gate) and reassign fwUrl under httpUpdate's live reference")
    # The version scheme. EXACT, not substring: indexOf() treats any name that EXTENDS the
    # running version ("fbt_v2.4.4_rc1.bin", and the app's own dialog suggests exactly those)
    # as already-installed, so the fleet would decline a fix silently and forever.
    if 'name != "fbt_" + FirmwareVer + ".bin"' not in cb:
        fails.append("updateOTA.cpp: the exact file-name comparison against FirmwareVer is gone. "
                     "A substring test (indexOf) silently declines every name that extends the "
                     "running version - see docs/history/2026-08-17-ota-github-to-server.md")
    # The .bin URL must keep coming from the server's reply. Rebuilding it from a constant
    # would send the download to the primary host even when the FALLBACK answered the check.
    if 'json["url"]' not in cb:
        fails.append("updateOTA.cpp: fwUrl is no longer taken from the /ota/check reply - a "
                     "download would not follow the host that actually answered")

# 5. Two front doors, tried in order. OTA is the only way to reach a fielded machine, so a
#    build that knows one host turns a single outage into 109 units nobody can fix. Losing
#    the fallback is invisible until the day it is needed.
loop = re.search(r"void checkFirmware\(bool promptOnDevice\)\s*\{(.*?)\n\}", ota, re.S)
if not loop:
    fails.append("updateOTA.cpp: checkFirmware() not found - this guard is checking nothing")
else:
    body = loop.group(1)
    # Inside the loop, not merely somewhere in the function: a fallback sitting in a branch
    # nothing reaches passes a bare substring test while the fleet has one front door.
    hosts = re.search(r"HOSTS\[\]\s*=\s*\{([^}]*)\}", body)
    if not hosts or "SECRET_OTA_CHECK_URL_FALLBACK" not in hosts.group(1):
        fails.append("updateOTA.cpp: the fallback host is not in the HOSTS[] table checkFirmware() "
                     "iterates - one Cloudflare incident then leaves the whole fleet unreachable, "
                     "and OTA is the only road back")
    if "otaCheckHost" not in body:
        fails.append("updateOTA.cpp: checkFirmware() no longer routes through otaCheckHost(), "
                     "so the per-host guards above are checking a function nobody calls")

# 6. The 6 h poll IS the release mechanism. Before it, checkFirmware() ran once per boot in a
#    ~2 s window against a 1-3 s DHCP - CLAUDE.md calls that the biggest reason OTA went quiet
#    in the field. And prompt=true is what makes a found build visible: the default is false,
#    so dropping the argument leaves the machine finding updates it shows to nobody.
if "postOtaCheck(true)" not in dash:
    fails.append("webDashboard.cpp: the periodic OTA poll is gone or no longer passes "
                 "promptOnDevice=true - the machine finds a build and shows nobody, so OTA "
                 "stops being a fleet mechanism (the default is false, ForteSetting.h)")
elif not re.search(r"OTA_POLL_MS", dash):
    fails.append("webDashboard.cpp: OTA_POLL_MS is gone - the poll interval is now a literal "
                 "nobody will find when it needs retuning")
else:
    # The poll must carry the REBOOT-grade gate, not the settings one: escreenFinished counts
    # as idle for settings, but it is the 30-90 s result-compute + upload window, and a check
    # landing there writes type_infor = eUpdateOTA, which unlocks the deferred restart.
    pollblk = dash[dash.find("OTA_POLL_MS"):dash.find("postOtaCheck(true)")]
    if "suspended" not in pollblk or "escreenFinished" not in pollblk:
        fails.append("webDashboard.cpp: the OTA poll lost its !suspended / != escreenFinished "
                     "gate - it can open a second TLS session against the end-of-run upload and "
                     "unlock the deferred reboot in the middle of screen_Result()")

if "setMD5(" not in dash:
    fails.append("webDashboard.cpp: Update.setMD5() is gone from /otaupload - ?md5= is still "
                 "accepted and hex-validated, so the browser reports success while nothing is "
                 "verified, and CLAUDE.md calls that the only real brick path")
elif True:

    md5 = dash[dash.find('hasParam("md5")'):dash.find("setMD5(")]
    if "toLowerCase()" not in md5:
        fails.append("webDashboard.cpp: the ?md5= value is not lowercased before Update.setMD5() "
                     "- an uppercase digest (PowerShell Get-FileHash) fails only after the whole "
                     "image has been uploaded")

# 7. The browser upload UI must COMPUTE and SEND ?md5=. Firmware has taken the parameter since
#    2026-07-24 and check 3 above verifies it is handled - but the shipped UI never sent it, so
#    for a year every browser install ran with NO integrity check at all while this file stayed
#    green. Handling a parameter nobody sends is not a check.
#    Update.end(true) sets _size = progress(), i.e. "however many bytes arrived IS the whole
#    image", so a .bin cut short by a dropped connection installs and the unit stops booting -
#    the only real brick path in the system, and the one a hand-flash campaign runs on.
#    Correctness of the digest itself is checked by tools/test_ota_md5.js against RFC 1321.
js = (ROOT / "data" / "script.js").read_text(encoding="utf-8", errors="replace")
if "/otaupload" not in js:
    fails.append("data/script.js: the /otaupload call is gone - this guard checks nothing")
else:
    if not re.search(r'"/otaupload\?md5="\s*\+\s*md5', js):
        fails.append("data/script.js: the firmware-upload UI no longer sends ?md5= - browser "
                     "installs go back to NO integrity check, and a .bin cut short in transit "
                     "boots into a brick (webDashboard.cpp Update.end(true))")
    if "md5Hex(" not in js:
        fails.append("data/script.js: md5Hex() is gone - the digest has to be COMPUTED from the "
                     "chosen file. Asking a person to type it was tried and rejected: a remote "
                     "customer cannot transcribe 32 hex characters, and a check people skip is "
                     "not a check")
    # Guard against a digest of the wrong bytes: it must be taken from the file being sent.
    if not re.search(r"md5Hex\(new Uint8Array\(fr\.result\)\)", js):
        fails.append("data/script.js: the digest is no longer computed from the file reader's "
                     "own bytes, so it may not describe what actually gets uploaded")

for f in fails:
    print("FAIL " + f)
print(("FAILED (%d)" % len(fails)) if fails else "ok - OTA invariants hold")
sys.exit(1 if fails else 0)
