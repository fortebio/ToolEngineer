#!/usr/bin/env python3
"""Guard: the QR payload can never overflow ricmoo/QRCode's stack buffer.

Why this exists (2026-07-29). screen_QR() encodes with version 3 / ECC_LOW, whose byte-mode
capacity is 53 bytes. The library does NOT enforce that: encodeDataCodewords() never compares
the text length against the capacity, and bb_appendBits() has no bounds check, so it writes
straight past codewordBytes[71] - a VLA on DisplayTask's stack. The failure mode is not a bad
QR, it is a panic + reset the instant the screen opens.

There are TWO payloads, and both are bounded here:
  - SoftAP: "WIFI:T:nopass;S:" + <the broadcast SSID> + ";;" (18 fixed bytes). Note the
    indirection (2026-07-30): screen_QR() reads WiFi.softAPSSID() - the driver's copy - not
    dashboardApName(). The arithmetic still holds because the ONLY string ever handed to
    WiFi.softAP() is dashboardApName()'s clamped output, which section 2 pins.
  - STA: "http://" + dashboardHostname() + ".local/" (2026-08-04, was the IP). Section 1b.

Run: python tools/test_qr_payload.py       (host-side, no hardware)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DASH = (ROOT / "src" / "webDashboard.cpp").read_text(encoding="utf-8", errors="replace")
LCD = (ROOT / "src" / "displayLCD.cpp").read_text(encoding="utf-8", errors="replace")
BT = (ROOT / "src" / "Bluetooth.cpp").read_text(encoding="utf-8", errors="replace")

# Version 3, ECC_LOW, byte mode. Changing the version in screen_QR() must change this too.
QR_V3_LOW_BYTES = 53
WIFI_WRAPPER = len("WIFI:T:nopass;S:") + len(";;")  # 18

fail = []


def body(src, sig):
    """Source text of a function, from its signature to the next line-start '}'."""
    i = src.index(sig)
    j = src.index("\n}", i)
    return src[i:j]


def code_only(text):
    """Drop /* */ and // comments, but NOT the contents of string literals.

    Every check below must look at code, not prose - a guard that trips on a comment explaining
    the bug is a guard someone deletes. Both comment forms matter: the doc-headers here are
    /* */ blocks and they legitimately name dashboardApName() while explaining why the code no
    longer calls it.

    String-aware since 2026-08-04, and that is not a refinement. A regex `//[^\\n]*` cuts
    `payload = "http://" + dashboardHostname() + ".local/";` down to `payload = "http:` - the
    URL scheme's own slashes read as a comment - so section 1b silently saw no payload at all
    and reported the code as missing. The failure mode of the alternative is worse: a check
    written against a literal containing `//` would pass because the guard blinded itself, and
    nothing would say so. The prefix-literal scan below needs literals kept for the same reason.
    """
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":  # string/char literal: copy it whole, escapes included
            out.append(c)
            i += 1
            while i < n:
                ch = text[i]
                out.append(ch)
                i += 1
                if ch == "\\" and i < n:
                    out.append(text[i])
                    i += 1
                elif ch == c:
                    break
            continue
        if c == "/" and i + 1 < n:
            if text[i + 1] == "/":
                while i < n and text[i] != "\n":
                    i += 1
                continue
            if text[i + 1] == "*":
                j = text.find("*/", i + 2)
                i = n if j < 0 else j + 2
                continue
        out.append(c)
        i += 1
    return "".join(out)


# ---- 1. dashboardApName() is the single source of truth, and it clamps -------------------
try:
    ap = body(DASH, "String dashboardApName()")
except ValueError:
    fail.append("dashboardApName() is gone - the AP SSID has no single source of truth again")
    ap = ""

prefix = re.search(r'"([A-Za-z]+-)"\s*\+\s*id', ap)
if not prefix:
    fail.append('dashboardApName() no longer builds "<PREFIX>-" + id')
clamp = re.search(r"id\.length\(\)\s*>\s*(\d+)", ap)
if not clamp:
    fail.append("dashboardApName() does not clamp id.length() - a garbage EEPROM id gets through")

max_id = int(clamp.group(1)) if clamp else 999
pfx = len(prefix.group(1)) if prefix else 0
worst = WIFI_WRAPPER + pfx + max_id
if worst > QR_V3_LOW_BYTES:
    fail.append(
        f"worst-case AP payload is {worst} B > {QR_V3_LOW_BYTES} B capacity "
        f"(wrapper {WIFI_WRAPPER} + prefix {pfx} + id {max_id}): "
        "lower the id clamp or raise the QR version"
    )

# ---- 1b. The STA payload is bounded too, and both address forms stay on screen ------------
# Added 2026-08-04, when the QR moved from the IP to the mDNS name. The IP bounded itself - an
# IPv4 string cannot exceed 15 characters - while a hostname is built from the device ID, so
# the overflow-into-DisplayTask's-stack path from section 1 just gained a second entrance.
#
# The second assertion matters more. ".local" resolves only if the CLIENT speaks mDNS (iOS,
# macOS and Windows 10+ do; older Android does not), and nothing on the device can detect that,
# so the screen has to carry an address that always works next to the one it encodes.
# Collapsing the caption to .local as well would leave those phones with no way in at all - and
# it would read like a tidy-up, which is why it is pinned here rather than left to a comment.
try:
    host = body(DASH, "String dashboardHostname()")
except ValueError:
    fail.append("dashboardHostname() is gone - the STA QR payload has no bound")
    host = ""

hclamp = re.search(r"out\.length\(\)\s*<\s*(\d+)", host)
if not hclamp:
    fail.append(
        "dashboardHostname() does not clamp out.length() - a long device ID reaches the encoder"
    )
max_host = int(hclamp.group(1)) if hclamp else 999
worst_sta = len("http://") + max_host + len(".local/")
if worst_sta > QR_V3_LOW_BYTES:
    fail.append(
        f"worst-case STA payload is {worst_sta} B > {QR_V3_LOW_BYTES} B capacity "
        f"(http:// + hostname {max_host} + .local/): lower the hostname clamp or raise the version"
    )

qr_body = code_only(body(LCD, "void displayCLD::screen_QR()"))
if not re.search(
    r'payload\s*=\s*"http://"\s*\+\s*dashboardHostname\(\)\s*\+\s*"\.local/"', qr_body
):
    fail.append(
        "screen_QR() no longer encodes http://<dashboardHostname()>.local/ on STA - the bound "
        "above is for THAT string, and dashboardHostname() is the label MDNS.begin() registered"
    )
if "localIP()" not in qr_body:
    fail.append(
        "screen_QR() no longer prints the IP. .local only resolves on a client that speaks mDNS, "
        "so the printed IP is the only way in for a phone that does not - it cannot be dropped"
    )

# ---- 2. The builder feeds the RADIO; everyone else ASKS the radio -------------------------
# Rewritten 2026-07-30, and the rule got stronger rather than weaker.
#
# The old rule was "dashboardStartAP() AND screen_QR() must both call dashboardApName()". That
# enforces agreement between a builder and a builder - which is not the property that matters.
# WiFi.softAP() LATCHES the SSID into the radio and this core has no API to change it in place,
# so the moment the device ID changes, "what the name should be" and "what is on the air"
# diverge, and a QR built from the builder tells the phone to join a network that is not there.
# The operator could not rejoin the hotspot without a power cycle.
#
# So: the builder produces the string softAP() latches, and nothing else. Whoever REPORTS the
# name reads WiFi.softAPSSID(). Four assertions pin that call graph.

ap_start = code_only(body(DASH, "void dashboardStartAP()"))
if "dashboardApName()" not in ap_start:
    fail.append(
        "dashboardStartAP() does not call dashboardApName() - the name that goes on the air "
        "must come from the one clamped builder"
    )

# (a) EXACTLY ONE WiFi.softAP() in the whole firmware, and it is that one. This is what keeps
#     section 1's arithmetic true now that the QR reads the driver instead of the builder: the
#     only string ever put on the air is the clamped builder output. It also pins the decision
#     NOT to re-raise the AP live - IDF's behaviour towards already-associated stations under a
#     live esp_wifi_set_config is undocumented, and dashboardStartAP() is not idempotent (it
#     also does releaseBluetoothStack(), WiFi.mode() and dnsServer.start()).
softap_sites = [
    f"src/{p.name}"
    for p in sorted((ROOT / "src").glob("*.cpp"))
    for _ in re.finditer(r"WiFi\.softAP\s*\(", code_only(p.read_text(encoding="utf-8", errors="replace")))
]
if softap_sites != ["src/webDashboard.cpp"] or "WiFi.softAP(" not in ap_start:
    fail.append(
        f"WiFi.softAP() must be called exactly once, inside dashboardStartAP(); found {softap_sites}"
    )

# (b) The builder has exactly ONE call site. A fourth consumer is how this bug class returns -
#     same reasoning as the whole-file literal scan below. Headers excluded: the prototype
#     `String dashboardApName();` matches the call regex too.
callers = 0
for p in sorted((ROOT / "src").glob("*.cpp")):
    code = code_only(p.read_text(encoding="utf-8", errors="replace"))
    if p.name == "webDashboard.cpp":
        code = code.replace(code_only(body(DASH, "String dashboardApName()")), "")
    callers += len(re.findall(r"dashboardApName\s*\(\s*\)", code))
if callers != 1:
    fail.append(
        f"dashboardApName() has {callers} call sites outside its own body, expected 1 "
        "(dashboardStartAP). Anything that REPORTS the SSID must read WiFi.softAPSSID(): the "
        "builder answers 'what should it be', not 'what is on the air'."
    )

# (c) The two reporters read the radio, and do not rebuild the name.
for name, src, sig in (
    ("screen_QR", LCD, "void displayCLD::screen_QR()"),
    ("buildHomeJson", DASH, "static String buildHomeJson()"),
):
    b = code_only(body(src, sig))
    if "softAPSSID()" not in b:
        fail.append(
            f"{name}() no longer reads WiFi.softAPSSID(): between a device-ID change and the "
            "deferred reboot it would report a name the radio is not broadcasting"
        )
    if "dashboardApName()" in b:
        fail.append(f"{name}() calls dashboardApName() again - that is exactly the stale-SSID bug")

for label, src, allowed_in in (
    ("src/webDashboard.cpp", DASH, "String dashboardApName()"),
    ("src/displayLCD.cpp", LCD, None),
    ("data/script.js", (ROOT / "data" / "script.js").read_text(encoding="utf-8", errors="replace"), None),
):
    code = code_only(src)
    if allowed_in:  # blank out the one function that is allowed to own the literal
        owner = body(src, allowed_in)
        code = code.replace(code_only(owner), "")
    for m in re.finditer(r'"(RAPID|FBT)-', code):
        line = code[: m.start()].count("\n") + 1
        fail.append(
            f'{label}: an SSID prefix literal "{m.group(1)}-" outside dashboardApName() '
            f"(~line {line} of the comment-stripped file). Call dashboardApName() instead - "
            "every hand-built copy has drifted so far."
        )

# ---- 3. screen_QR() gates the encoder on length AND return value -------------------------
qr = body(LCD, "void displayCLD::screen_QR()")
init = re.search(r"qrcode_initText\s*\([^;]*?\)", qr, re.S)
if not init:
    fail.append("qrcode_initText() call not found in screen_QR()")
else:
    version = re.search(r"qrcode_initText\s*\(\s*&qr\s*,\s*\w+\s*,\s*(\d+)", qr)
    if version and version.group(1) != "3":
        fail.append(
            f"screen_QR() encodes at version {version.group(1)}, but this test's capacity "
            f"constant is for version 3 - update QR_V3_LOW_BYTES"
        )
    if not re.search(r"payload\.length\(\)\s*<=\s*\w+", qr):
        fail.append("screen_QR() no longer bounds payload.length() before encoding (REBOOT PATH)")
    if not re.search(r"qrcode_initText\s*\([^;]*?\)\s*==\s*0", qr, re.S):
        fail.append("screen_QR() ignores qrcode_initText()'s return value")

# ---- 4. The device ID that feeds this payload is bounded ----------------------------------
# Owned by tools/test_device_id.py, deliberately NOT re-checked here. It used to be, against
# loadSettingDevice(); when the ID moved to ForteSetting::sanitiseDeviceId() this file kept
# asserting the old location and went red for a bug that no longer existed. Two guards on one
# invariant means one of them is always the stale one - so this file owns the QR arithmetic
# (worst case above) and that file owns where the ID gets its bound.

if fail:
    print("FAIL - QR payload guard")
    for f in fail:
        print("  -", f)
    sys.exit(1)
print(
    f"ok - QR payload bounded: AP {worst} B, STA {worst_sta} B <= {QR_V3_LOW_BYTES} B, "
    "no prefix drift, IP still printed as the non-mDNS fallback"
)
