#!/usr/bin/env python3
"""
Guard the config migration in ForteSetting::begin() - the code that makes a v2.4.3a threshold
actually reach a machine.

Why this needs a guard at all. begin() replaces the compiled parastructure with the EEPROM copy
whenever the stored length matches, and sizeof(parastructure) has not moved since v2.4.2. So on
every unit that has ever been configured, editing a default in define.h changes NOTHING. That is
not a cosmetic drift: removed_by_new_gate() compares the RUNTIME min_increase/min_sharpness pair
against LEGACY_MIN_*, so a machine still holding 20/5 has both sides equal and the review gate can
never fire. Both builds would run, report normally, and have the feature silently switched off.

Every check below is something that failed silently in exactly that way, or would:

1. The revision byte is read with EEPROM.read() and 0xFF is normalised to 0 BEFORE the compare.
   A virgin byte reads 255. Under `if (rev < CONFIG_REV_THRESHOLDS)` that is 255 < 1 = false, so
   the migration would skip on precisely the machines that need it. This is the readBool() trap
   from the slot-170 device-ID migration wearing different clothes.

2. It is stamp-gated, not value-gated. 120 rounds and min_increase 20 are legitimate settings
   somebody may have chosen; re-forcing them on every boot would fight an operator forever. (The
   kpid3 seed beside it can afford a value compare because {0,0,0} is impossible for a real PID.)

3. It never gates on FirmwareVer. That global changes every release, so a unit jumping
   2.4.2 -> 2.4.4 would skip the migration. Gate on data. Same rule as CLAUDE.md Setting #6.

4. slopes, origins and led_power are copied out before the edit and compared after, and a
   mismatch abandons the write. These are per-machine (slopes run 0.50-3.50 across the fleet);
   resetting them to the compiled placeholder is worse than not migrating, because the machine
   keeps working and every number it reports is wrong.

5. The migration lives inside the valid-config branch, so a fresh or erased EEPROM is never
   written a partial config.

6. baseline_start + baseline_range == detection_margin_time. The baseline window must CLOSE where
   the earliest legitimate Ct opens, or an early amplifier has its zero measured against its own
   rise. The migration writes all three, so the three must agree with each other.

7. The self-check reads the LIVE parameter struct, not the compiled defaults - reading the
   defaults is the mistake the whole exercise exists to catch - and it is served from a handler
   that never opens EEPROM (AsyncTCP must not, CLAUDE.md Setting #2).

    python tools/test_config_migration.py     # exit 0 = pass
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
fails = []


def uncommented(text):
    """Strip comments so prose ABOUT a rule is never mistaken for the rule."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return "\n".join(re.sub(r"//.*", "", ln) for ln in text.splitlines())


setting_raw = (SRC / "ForteSetting.cpp").read_text(encoding="utf-8", errors="replace")
setting = uncommented(setting_raw)
define = uncommented((SRC / "define.h").read_text(encoding="utf-8", errors="replace"))
dash = uncommented((SRC / "webDashboard.cpp").read_text(encoding="utf-8", errors="replace"))

# The migration block: from the revision read to the end of begin().
m = re.search(r"EEPROM\.read\(ADDR_CONFIG_REV\)(.*?)\n\}", setting, re.S)
block = m.group(1) if m else ""
if not block:
    fails.append("cannot find the ADDR_CONFIG_REV migration in ForteSetting.cpp at all")

# ---- 1. the 0xFF normalisation, and that it happens BEFORE the compare -----------------------
if not re.search(r"ADDR_CONFIG_REV\s*\)\s*;", setting):
    fails.append("ADDR_CONFIG_REV must be read with EEPROM.read(), never readBool() "
                 "(a virgin 0xFF reads TRUE and skips the migration forever)")
norm = re.search(r"==\s*0xFF\s*\)?\s*\n?\s*\w+\s*=\s*0\s*;", block, re.I)
cmp_ = re.search(r"<\s*CONFIG_REV_THRESHOLDS", block)
if not norm:
    fails.append("the revision byte is not normalised from 0xFF to 0 - a never-stamped unit "
                 "reads 255 and `255 < 1` is false, so it would never migrate")
elif cmp_ and norm.start() > cmp_.start():
    fails.append("0xFF is normalised AFTER the `< CONFIG_REV_THRESHOLDS` compare, which is the "
                 "same bug with an extra line of code in front of it")
if not cmp_:
    fails.append("the migration does not gate on `< CONFIG_REV_THRESHOLDS`")

# ---- 2. stamp-gated, not value-gated ---------------------------------------------------------
for field in ("amplification_time", "min_increase", "min_sharpness"):
    if re.search(r"parameter\.%s\s*==" % field, block):
        fails.append("migration compares parameter.%s by VALUE - it must be stamp-gated, or it "
                     "overwrites a deliberate operator setting on every boot" % field)

# ---- 3. never gated on FirmwareVer -----------------------------------------------------------
if "FirmwareVer" in block:
    fails.append("migration references FirmwareVer - gate on DATA, or a unit jumping "
                 "2.4.2 -> 2.4.4 skips it entirely")

# ---- 4. calibration is snapshotted, compared, and a mismatch abandons the write ---------------
for field in ("slopes", "origins", "led_power"):
    if not re.search(r"memcpy\([^)]*parameter\.%s" % field, block):
        fails.append("migration does not snapshot parameter.%s before editing" % field)
    if not re.search(r"memcmp\([^)]*parameter\.%s" % field, block):
        fails.append("migration does not verify parameter.%s survived the edit" % field)
put = re.search(r"EEPROM\.put\(PARAMETERPOS", block)
guard = re.search(r"if\s*\(\s*calibIntact\s*\)", block)
if not guard:
    fails.append("no calibIntact guard - the EEPROM write is not conditional on calibration "
                 "having survived")
elif put and put.start() < guard.start():
    fails.append("EEPROM.put runs BEFORE the calibIntact guard, so a config that clobbered "
                 "calibration would already be saved by the time it is noticed")

# ---- 5. inside the valid-config branch -------------------------------------------------------
begin = setting[setting.find("void ForteSetting::begin()"):]
valid = begin.find("paraEEPROM.length == sizeof(parameter)")
rev = begin.find("EEPROM.read(ADDR_CONFIG_REV)")
disp = begin.find("paraDisplay(parameter)")
if not (0 <= valid < rev < disp):
    fails.append("the migration is not inside begin()'s valid-config branch - a fresh or erased "
                 "EEPROM would be written a partial config")

# ---- 6. the baseline window closes where the detection margin opens ---------------------------
vals = {}
for field in ("baseline_start", "baseline_range", "detection_margin_time"):
    hit = re.search(r"parameter\.%s\s*=\s*([0-9.]+)\s*;" % field, block)
    if not hit:
        fails.append("migration does not set parameter.%s - all three move together or the "
                     "baseline window stops closing where the earliest valid Ct opens" % field)
    else:
        vals[field] = float(hit.group(1))
if len(vals) == 3:
    if vals["baseline_start"] + vals["baseline_range"] != vals["detection_margin_time"]:
        fails.append("migration writes baseline_start %g + baseline_range %g != "
                     "detection_margin_time %g - an early amplifier would have its zero measured "
                     "against its own rise" % (vals["baseline_start"], vals["baseline_range"],
                                               vals["detection_margin_time"]))

# The two gates must actually clear the legacy pair, or removed_by_new_gate() is inert.
algo = uncommented((SRC / "Alg" / "Algo.h").read_text(encoding="utf-8", errors="replace"))
for field, macro in (("min_increase", "LEGACY_MIN_INCREASE"),
                     ("min_sharpness", "LEGACY_MIN_SHARPNESS")):
    setv = re.search(r"parameter\.%s\s*=\s*([0-9.]+)\s*;" % field, block)
    legacy = re.search(r"#define\s+%s\s+([0-9.]+)" % macro, algo)
    if setv and legacy and float(setv.group(1)) <= float(legacy.group(1)):
        fails.append("migration sets %s to %s, which does not exceed %s (%s) - the review gate "
                     "compares the two and can never fire"
                     % (field, setv.group(1), macro, legacy.group(1)))

# ---- 6b. the SAME value in all three places ---------------------------------------------------
# min_sharpness lives in define.h (the compiled default), in the migration (what a fielded unit
# is given) and in kExpectMinSharpness (what the self-check grades against). Any one of them can
# be edited alone, and two of the three failure modes are silent: a stale migration means fielded
# machines never get the new value, and a stale yardstick means the self-check reports PASS while
# measuring against a number nobody uses.
for field, const in (("min_sharpness", "kExpectMinSharpness"),
                     ("min_increase", "kExpectMinIncrease")):
    d = re.search(r"double\s+%s\s*=\s*([0-9.]+)\s*;" % field, define)
    m_ = re.search(r"parameter\.%s\s*=\s*([0-9.]+)\s*;" % field, block)
    e_ = re.search(r"%s\s*=\s*([0-9.]+)\s*;" % const, setting)
    vals = {"define.h": d, "migration": m_, const: e_}
    missing = [k for k, v in vals.items() if not v]
    if missing:
        fails.append("cannot find %s in: %s" % (field, ", ".join(missing)))
        continue
    got = {k: float(v.group(1)) for k, v in vals.items()}
    if len(set(got.values())) != 1:
        fails.append("%s disagrees across its three homes: %s"
                     % (field, ", ".join("%s=%g" % kv for kv in sorted(got.items()))))

# The revision stamp must move whenever the migrated values do, or already-stamped units keep the
# old ones forever. Can only be checked against the record kept in define.h's comment block.
rev = re.search(r"#define\s+CONFIG_REV_THRESHOLDS\s+(\d+)", define)
raw_define = (SRC / "define.h").read_text(encoding="utf-8", errors="replace")
if rev:
    documented = re.findall(r"^//\s*rev\s+(\d+)\s", raw_define, re.M)
    if not documented:
        fails.append("CONFIG_REV_THRESHOLDS has no `// rev N` history in define.h - the next "
                     "person cannot tell whether a bump is needed")
    elif max(int(x) for x in documented) != int(rev.group(1)):
        fails.append("CONFIG_REV_THRESHOLDS is %s but the highest documented rev is %s"
                     % (rev.group(1), max(int(x) for x in documented)))

# ---- 7. the self-check reads the live struct and never touches EEPROM from AsyncTCP -----------
chk = setting[setting.find("String ForteSetting::configSelfCheckJson()"):]
chk = chk[:chk.find("void ForteSetting::configSelfCheckLog()")]
if not chk:
    fails.append("configSelfCheckJson() is missing")
else:
    if "EEPROM." in chk:
        fails.append("configSelfCheckJson() opens EEPROM - it is served from AsyncTCP, which "
                     "must never touch the shared 4096-byte buffer (CLAUDE.md Setting #2)")
    if "parameter." not in chk:
        fails.append("configSelfCheckJson() does not read the live parameter struct - checking "
                     "the compiled defaults is exactly the mistake it exists to catch")
    for want in ("lysisDuration", "amplification_time", "min_increase", "min_sharpness"):
        if want not in chk:
            fails.append("configSelfCheckJson() does not report %s" % want)
    # timePerLoop, not a hardcoded 20000: a unit on a different round length must not be told
    # its calling time is fine when it is running a different protocol.
    if "timePerLoop" not in chk:
        fails.append("configSelfCheckJson() does not use the machine's own timePerLoop to turn "
                     "rounds into minutes")

if "/selfcheck" not in dash:
    fails.append("webDashboard.cpp does not serve GET /selfcheck")

# ---- the address must not collide with its neighbours ----------------------------------------
addr = re.search(r"#define\s+ADDR_CONFIG_REV\s+(\d+)", define)
if not addr:
    fails.append("ADDR_CONFIG_REV is not defined in define.h")
else:
    a = int(addr.group(1))
    others = {n: int(v) for n, v in re.findall(r"#define\s+(ADDR_\w+)\s+(\d+)", define)
              if n != "ADDR_CONFIG_REV"}
    # ADDR_CHECK_UPDATE holds an int (4 bytes); ADDR_ERROR_NUMBER_UNIT starts the error block.
    if not (others.get("ADDR_CHECK_UPDATE", 0) + 4 <= a < others.get("ADDR_ERROR_NUMBER_UNIT", 0)):
        fails.append("ADDR_CONFIG_REV %d is not in the free gap between ADDR_CHECK_UPDATE+4 and "
                     "ADDR_ERROR_NUMBER_UNIT" % a)
    for name, v in others.items():
        if v == a:
            fails.append("ADDR_CONFIG_REV %d collides with %s" % (a, name))

if fails:
    print("FAIL config migration guard")
    for f in fails:
        print("  - " + f)
    sys.exit(1)
print("PASS config migration guard (%s at EEPROM %s, rev %s)"
      % ("stamp-gated", addr.group(1) if addr else "?",
         (re.search(r"#define\s+CONFIG_REV_THRESHOLDS\s+(\d+)", define) or [None, "?"])[1]))
