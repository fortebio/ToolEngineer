#!/usr/bin/env python3
"""
Phase 0 guards for the fleet upgrade to v2.4.3 (docs/plan/2026-07-28-ota-fleet-upgrade-243.md).

Three static invariants, all host-side, no hardware:

1. No strcpy() into a `parameter.*` field. Those are char[10] slots at the very top of
   parastructure (define.h:116-118); an overrun walks straight over slopes/origins/kpid
   and then commits the wreck to EEPROM. JsonDataConfig() is reachable from Serial/BT
   (no validation at all) as well as POST /config, so the bound has to live at the copy.

2. validateConfig() (webDashboard.cpp) length-checks "PCB version" and "para version".
   The comment used to say "checked below" while no check existed anywhere.

3. No credential literal in committed source: Bluetooth.cpp uses the SECRET_* macros and
   secrets.example.h ships PASTE_* placeholders only. secrets.h itself is gitignored.

    python tools/test_phase0_guards.py     # exit 0 = pass
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)


def uncommented(text):
    """Drop // and /* */ comments so a key or a call named in prose cannot satisfy a check."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return "\n".join(re.sub(r"//.*", "", ln) for ln in text.splitlines())


# 1. strcpy into parameter.* - both the bare form (inside ForteSetting) and the
#    _ForteSetting.parameter.* alias every other translation unit has to use.
for f in list(SRC.rglob("*.cpp")) + list(SRC.rglob("*.h")):
    for i, line in enumerate(uncommented(f.read_text(encoding="utf-8", errors="replace")).splitlines(), 1):
        if re.search(r"\bstrcpy\s*\(\s*(?:_ForteSetting\s*\.\s*)?parameter\s*\.", line):
            fails.append(f"{f.relative_to(ROOT)}:{i}: strcpy into parameter.* - use strlcpy(dst, src, sizeof dst)")

# 2. char[10] config keys are length-checked before they are queued. Match the CONDITION
#    of the if that guards the strlen check, not its body: a body-wide match would go green
#    on a comment that merely mentions "PCB version" while the key itself fell through again.
dash = uncommented((SRC / "webDashboard.cpp").read_text(encoding="utf-8", errors="replace"))
m = re.search(r'if \((k ==[^\n{]*?)\)\s*\n\s*\{\s*\n\s*if \(!v\.is<const char \*>\(\) \|\| strlen\(v\.as<const char \*>\(\)\) > 9', dash)
check(m is not None, "webDashboard.cpp: no '<= 9 chars' branch found in validateConfig()")
if m:
    cond = m.group(1)
    for key in ('"PCB version"', '"para version"', '"device ID"', '"units"'):
        check(key in cond, f"webDashboard.cpp: {key} is not in the '<= 9 chars' branch of validateConfig()")

# 3. secrets stay out of committed source
bt = (SRC / "Bluetooth.cpp").read_text(encoding="utf-8", errors="replace")
for macro in ("SECRET_GAS_URL", "SECRET_INGEST_URL", "SECRET_INGEST_TOKEN", "SECRET_ERP_URL", "SECRET_ERP_TOKEN"):
    check(macro in bt, f"Bluetooth.cpp: endpoint/token no longer uses {macro} - literal creeping back in?")
check(not re.search(r'"https://script\.google\.com/macros/s/AKfy', bt), "Bluetooth.cpp: hardcoded GAS deployment URL")

check("src/secrets.h" in (ROOT / ".gitignore").read_text(encoding="utf-8", errors="replace").split(),
      ".gitignore no longer ignores src/secrets.h - the real tokens would get committed")

example = (SRC / "secrets.example.h").read_text(encoding="utf-8", errors="replace")
for i, line in enumerate(example.splitlines(), 1):
    m = re.match(r'#define (SECRET_\w+) "(.*)"', line)
    if m and not ("PASTE_" in m.group(2) or m.group(2).startswith("https://your-")):
        fails.append(f"src/secrets.example.h:{i}: {m.group(1)} looks like a real value - this file IS committed")

# 4. -Wformat stays on: it is a compile-time detector for a whole crash class, and losing it is
#    silent (docs/history/2026-07-30-printf-loadprohibited-wformat.md).
ini = (ROOT / "platformio.ini").read_text(encoding="utf-8", errors="replace")
check(
    re.search(r"^build_flags\s*=.*-Wformat", ini, re.M) is not None,
    "platformio.ini: -Wformat is gone from build_flags. printf format mismatches (an int read "
    "as a char* = LoadProhibited) would compile silently again",
)

for f in fails:
    print("FAIL " + f)
print(("FAILED (%d)" % len(fails)) if fails else "ok - phase 0 guards hold")
sys.exit(1 if fails else 0)
