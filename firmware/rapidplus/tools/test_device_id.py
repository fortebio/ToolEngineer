#!/usr/bin/env python3
"""Guard: the device ID has exactly ONE store, and it is sanitised before anything reads it.

History. The ID used to be stored TWICE - id_device (EEPROM 170, read with EEPROM.readString,
no integrity check, mirrored into a global) and parameter.device_id (EEPROM 512, gated by
parameter.length) - with different compiled defaults. Everything that could go wrong did:

  * the Setting card WROTE one store and DISPLAYED the other, so it offered to "correct" an ID
    that was never in use;
  * the "device ID" key reached parameter.device_id from Serial/BT and POST /config without
    touching id_device, so the machine showed one ID and uploaded another;
  * slot 170 on a unit from an older layout returned EEPROM noise - and an over-long value
    stopped WiFi.softAP() and rebooted the machine through the QR encoder.

Fixed 2026-07-30 by DELETING the id_device global: parameter.device_id (the protoID macro) is
the only store. That removes the whole class of bug, but only while nobody adds a copy back and
while the load order still puts the store ahead of its first reader. That is what this pins.

Run: python tools/test_device_id.py       (host-side, no hardware)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
READ = lambda *p: (ROOT.joinpath(*p)).read_text(encoding="utf-8", errors="replace")
DEF, JS, CSS = READ("src", "define.h"), READ("data", "script.js"), READ("data", "style.css")
DASH, FS = READ("src", "webDashboard.cpp"), READ("src", "ForteSetting.cpp")
BT, MAIN = READ("src", "Bluetooth.cpp"), READ("src", "main.cpp")

fail = []


def code_only(text):
    """Reduce to actual C++ code: drop comments, raw strings and ordinary string literals.

    All three matter. The guard must not trip on a comment explaining the bug; "id_device" is a
    legitimate JSON *key* in the upload payload (the cloud's field name, not a C++ identifier);
    and src/index.h holds the legacy HTML page as a R"rawliteral(...)" whose JavaScript declares
    its own `id_device` variable - JS in a string, nothing to do with the firmware's storage."""
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r'R"([^(]*)\(.*?\)\1"', '""', text, flags=re.S)  # raw strings first
    return re.sub(r'"(?:[^"\\]|\\.)*"', '""', text)


# ---- 1. THE invariant: no id_device identifier survives anywhere in src/ ------------------
for name in sorted(p.name for p in (ROOT / "src").glob("*.cpp")) + sorted(
    p.name for p in (ROOT / "src").glob("*.h")
):
    code = code_only(READ("src", name))
    for m in re.finditer(r"\bid_device\b", code):
        fail.append(
            f"src/{name}: `id_device` is back (~line {code[:m.start()].count(chr(10)) + 1} of the "
            "stripped file). The ID must have ONE store - use protoID "
            "(_ForteSetting.parameter.device_id). A second copy is the bug, not the fix."
        )

# ---- 2. Slot 170 is a READ-ONLY legacy source ----------------------------------------------
# It is not "dead" any more: ForteSetting::begin() migrates the v2.4.2 device ID out of it exactly
# once, because that slot - not parameter.device_id - held the identity the cloud already knows.
# READING it is required. WRITING it recreates the second store and breaks downgrade to v2.4.2.
for name in ("Bluetooth.cpp", "ForteSetting.cpp", "webDashboard.cpp", "main.cpp"):
    code = code_only(READ("src", name))
    if re.search(r"EEPROM\.(write\w*|put)\s*\(\s*ADDR_ID_DEVICE_BASE", code):
        fail.append(
            f"src/{name} WRITES ADDR_ID_DEVICE_BASE - slot 170 is read-only legacy input; writing "
            "it is the second ID store coming back"
        )
    if re.search(r"EEPROM\.readString\s*\(\s*ADDR_(ID_DEVICE_BASE|CHECK_ID_DEVICE)", code):
        fail.append(
            f"src/{name} reads the legacy ID slot with EEPROM.readString(): that scans for a NUL "
            "to the end of the 4096 B buffer, not to the 40 B slot boundary. Read 40 bytes into a "
            "local and force the terminator"
        )

_beg = FS.index("void ForteSetting::begin()")
begin_body = code_only(FS[_beg : FS.index("EEPROM.end();", _beg)])
if "FirmwareVer" in begin_body:
    fail.append(
        "ForteSetting::begin() gates a migration on FirmwareVer: that global changes every "
        "release, so a unit jumping 2.4.2 -> 2.4.4 skips it. Gate on the data condition"
    )
if "ADDR_CHECK_ID_DEVICE" not in begin_body:
    fail.append(
        "the slot-170 device-ID migration is gone from begin(): upgrading from v2.4.2 silently "
        "re-keys the machine in the Google Sheet / ERP"
    )
if "readBool" in begin_body:
    fail.append(
        "the migration stamp is read with readBool(): a virgin EEPROM byte is 0xFF, which reads "
        "as TRUE, so every fresh unit would skip the migration. Use EEPROM.read()"
    )

# The placeholder list is what stops a fleet-wide shared identity: the v2.4.2 portal PRE-FILLED
# its ID box with "RPL" and saved it, so slot 170 holds "RPL" on every unit whose operator only
# went in for WiFi. Adopting that as a serial is silent and unrecoverable.
if "idIsPlaceholder" not in FS:
    fail.append("idIsPlaceholder() is gone - \"RPL\"/\"proto 0\" would be adopted as real serials")
else:
    # Comments stripped but STRING LITERALS KEPT - code_only() blanks literals, which is exactly
    # what this check is looking for. Comments still go, so prose naming "RPL" cannot satisfy it.
    ph = re.sub(r"//[^\n]*", "", FS[FS.index("static bool idIsPlaceholder") :][:600])
    for lit in ('"RPL"', '"proto 0"'):
        if lit not in ph:
            fail.append(f"idIsPlaceholder() no longer rejects {lit} - see the v2.4.2 portal default")
    if "strcmp" not in ph:
        fail.append(
            "idIsPlaceholder() no longer compares with strcmp - a PREFIX match would reject real "
            "serials like \"RPL03010\""
        )
if re.search(r'char\s+device_id\s*\[\s*\d+\s*\]\s*=\s*"[^"]+"', DEF):
    fail.append(
        "parastructure.device_id has a non-empty compiled default again: a default that looks "
        "like a serial is one the machine will upload under. Empty -> sanitiseDeviceId says UNSET"
    )

# ---- 3. The store is sanitised, and every entry point uses the same check -----------------
if "void ForteSetting::sanitiseDeviceId()" not in FS:
    fail.append("ForteSetting::sanitiseDeviceId() is gone - the ID store has no trust boundary")
else:
    body = FS[FS.index("void ForteSetting::sanitiseDeviceId()") :]
    body = code_only(body[: body.index("\n}")])
    if "sizeof(parameter.device_id) - 1] = '\\0'" not in body:
        fail.append(
            "sanitiseDeviceId() no longer NUL-terminates first. begin() fills the struct with a "
            "raw EEPROM.get(); an unterminated char[10] makes every later read walk into slopes[]"
        )
    if "0x20" not in body and "isprint" not in body:
        fail.append("sanitiseDeviceId() no longer rejects non-printable bytes")

    fsc = code_only(FS)
    if not re.search(r"sanitiseDeviceId\(\);", fsc[fsc.index("void ForteSetting::begin()") :]):
        fail.append("ForteSetting::begin() does not call sanitiseDeviceId() - EEPROM noise reaches every reader")
    # Serial/BT enter JsonDataConfig() with nothing else validating them (CLAUDE.md Setting #4).
    jdc = fsc[fsc.index("bool ForteSetting::JsonDataConfig()") : fsc.index("void ForteSetting::begin()")]
    if "sanitiseDeviceId()" not in jdc:
        fail.append(
            "JsonDataConfig() does not re-sanitise after applying \"device ID\": Serial/BT reach "
            "it with no validation at all, and that field feeds the SoftAP SSID and the QR payload"
        )
    else:
        # The invariant is "the sanitise only runs when the caller actually sent the key",
        # not one spelling of it: ArduinoJson 7 deprecated containsKey() and the 42 gates in
        # this file moved to !doc["k"].isNull(). Accept either, reject neither being present.
        _jdc_raw = FS[FS.index("bool ForteSetting::JsonDataConfig()") : FS.index("void ForteSetting::begin()")]
        if not re.search(r'(containsKey\("device ID"\)|\["device ID"\]\.isNull\(\))', _jdc_raw):
            fail.append(
                'the JsonDataConfig() sanitise is not gated on "device ID" being present - it '
                "would re-sanitise (and queue a reboot) on every unrelated config write"
            )

# ---- 4. Load order: the store is populated BEFORE its first reader ------------------------
# dashboardHostname() reads protoID, so _ForteSetting.begin() must precede WiFi.setHostname().
mc = code_only(MAIN)
try:
    if mc.index("_ForteSetting.begin()") > mc.index("WiFi.setHostname("):
        fail.append(
            "main.cpp calls WiFi.setHostname() BEFORE _ForteSetting.begin(): the hostname would "
            "be built from the compiled default, so DHCP advertises a different name than mDNS"
        )
    if mc.index("_displayCLD.begin()") > mc.index("_ForteSetting.begin()"):
        fail.append(
            "main.cpp calls _ForteSetting.begin() before _displayCLD.begin(): begin() paints "
            "ErrorDisplay() on a fresh EEPROM and would draw to an uninitialised TFT"
        )
except ValueError:
    fail.append("could not find _ForteSetting.begin() / _displayCLD.begin() / WiFi.setHostname() in main.cpp")

# ---- 5. All length limits still agree ------------------------------------------------------
limits = {}
m = re.search(r"char\s+device_id\s*\[\s*(\d+)\s*\]", DEF)
limits["parastructure.device_id char[N]"] = (int(m.group(1)) - 1) if m else None
m = re.search(r"id\.length\(\)\s*<\s*1\s*\|\|\s*id\.length\(\)\s*>\s*(\d+)", DASH)
limits["POST /deviceid"] = int(m.group(1)) if m else None
m = re.search(r'k == "device ID".*?strlen\(v\.as<const char \*>\(\)\) > (\d+)', DASH, re.S)
limits["POST /config validate"] = int(m.group(1)) if m else None
missing = [k for k, v in limits.items() if v is None]
if missing:
    fail.append("could not find the length limit for: " + ", ".join(missing))
elif len(set(limits.values())) != 1:
    fail.append(
        "device-ID length limits disagree: " + ", ".join(f"{k}={v}" for k, v in limits.items())
    )

# ---- 6. The Setting card shows the store it writes -----------------------------------------
rd = code_only(JS[JS.index("function renderDeviceId(form)") : JS.index("function renderDeviceId(form)") + 1200])
if "deviceIdNow" not in rd:
    fail.append("renderDeviceId() no longer shows deviceIdNow (the value the machine reports)")
if re.search(r"cfgCache\s*\[", rd):
    fail.append("renderDeviceId() reads cfgCache again - show the same value POST /deviceid writes")

# ---- 7. Touch: form controls >= 16px, and pinch-zoom NOT disabled --------------------------
touch = re.search(r"@media\s*\(hover:\s*none\)\s*and\s*\(pointer:\s*coarse\)\s*\{(.*?)\n\}", CSS, re.S)
if not touch or "font-size: 16px" not in touch.group(1):
    fail.append("no coarse-pointer rule setting form controls to 16px - iOS zooms on tap again")
else:
    for sel in (".sample-name", ".slot-name", ".disease-sel", ".f-in", ".f-sel"):
        if sel not in touch.group(1):
            fail.append(f"{sel} is missing from the 16px touch rule - it will still zoom on iOS")
if re.search(r"maximum-scale|user-scalable\s*=\s*no", READ("data", "index.html")):
    fail.append(
        "the viewport meta disables zoom. That trades the iOS input-zoom for taking pinch-zoom "
        "away from low-vision users - fix the font-size instead (this is a medical device)"
    )

if fail:
    print("FAIL - device ID guard")
    for f in fail:
        print("  -", f)
    sys.exit(1)
print(f"ok - one ID store (protoID, <= {next(iter(set(limits.values())))} chars), sanitised at load, touch inputs 16px")
