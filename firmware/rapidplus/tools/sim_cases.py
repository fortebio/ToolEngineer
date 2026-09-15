#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Realistic simulated runs for evaluating the result caller ON THE DEVICE.

    python tools/sim_cases.py list                     # the catalogue: scenarios, wells, intent
    python tools/sim_cases.py gen                      # tools/simcases/<id>.{cal.txt,raw.txt,expect.json} + README.md
    python tools/sim_cases.py gen --loops 120          # a unit still configured for 40 min
    python tools/sim_cases.py screen                   # MIRROR pre-screen: every well on its intended branch, with margin
    python tools/sim_cases.py screen S06 -v            # per-well numbers for one scenario
    python tools/sim_cases.py replay tools/slots.txt   # MIRROR verdicts on a real calibrated file

    python tools/run_sim_cases.py COM7                 # send every scenario to a real unit, read its verdicts

WHY
  Every algorithm change since v2.4.3a ends with "CHUA chay tren may that". The reason is
  practical: a real run costs 30 minutes of heater time per 10 wells, and a well with a
  KNOWN truth - a step artefact of exactly 12 units, a Ct of 2.7 min, a rise that begins
  before the detection margin - cannot be ordered from a reagent kit. This file manufactures
  those wells, shaped like the fleet's real captures, so a unit can be asked the same ten
  questions in 40 seconds instead of 30 minutes, and asked again after every change.

WHAT "REALISTIC" MEANS HERE (every number below is measured, not chosen)
  level      calibrated baseline 145..560 (tools/slots.txt); raw = level x slope with slopes
             1.3..1.55 and origins 0 on the units in sheet/test.json (fleet 0.50..3.50, docs
             2026-08-13). Raw counts are integers - the generator rounds like the ADC does.
  noise      ADDITIVE, ~1.5 raw counts per reading, independent of level (probe_sensor_noise.py:
             sigma = -0.0001*mean + 2.06, r = -0.03); 0.7..3.8 calibrated units once divided by
             the slope.
  warm-up    the first 3..8 rounds climb INTO the baseline from below (27 -> 57 -> 110 -> 134 ->
             147 on slots.txt slot 3; 311 -> 427 in 5 rounds in CLAUDE.md): exponential approach,
             amplitude 0..200, tau ~1.5 rounds. Never absent on a real unit, always absent on a
             textbook sigmoid - which is why textbook sigmoids prove nothing.
  sync step  a level shift ALL ten wells take at once early in the run (sheet/test.json: +90 raw
             at round 6 on every slot) - the "bac thang dong bo" probe_sensor_noise.py measures.
  drift      slow monotone creep of 0..25 units over 30 min on negatives (slots.txt 8 and 9),
             sometimes accelerating late (slot 3: +47 with a knee near 27 min).
  amplify    logistic rise; peak steepness A*k/4 is 20..60 units/min for shrimp LAMP (docs
             2026-08-21), 4..8 for ASF on the same hardware; after the plateau the trace keeps
             creeping (+25 over 25 min on slots.txt slot 6).
  artefacts  single-reading spikes, 2..3-reading dropouts, persistent offsets of 8..40 units,
             and a step inside the last five readings (docs 2026-08-15 / 2026-08-16).

TWO THINGS THIS FILE IS NOT
  1. Not the algorithm. The MIRROR section re-implements src/Alg/Algo.cpp plus the per-slot flow
     of sensor6035.cpp::bResultGet() so a recipe can be checked for MARGIN before it is burned
     into a unit - a well that lands 0.3 units from min_increase teaches nothing either way.
     The mirror is not evidence about the firmware. When the unit and the mirror disagree, the
     unit is the measurement and the mirror is what needs fixing. Thresholds are read from
     src/Alg/Algo.h and src/define.h at import so at least the NUMBERS cannot drift silently;
     the LOGIC can, and the only thing that catches that is running the unit.
  2. Not a label set. `expect` is the DESIGN INTENT of each well - what a reader who knows how
     the well was built would call it - not an engineer's reading of a field curve.

Stdlib only, on purpose: it has to run on the laptop that has the COM cable.
"""
import argparse
import json
import math
import os
import random
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "tools", "simcases")

# --------------------------------------------------------------------------- firmware numbers
# Defaults mirror define.h / Algo.h as of 2026-09-15. load_firmware_params() overrides them from
# the source tree when it is present, so a threshold edit in the firmware moves the pre-screen
# the same day. The device runner overrides the parastructure ones again from ParaRead.
DEFAULT_PARAMS = dict(
    min_increase=25.0, min_sharpness=8.0, min_slight_positive_time=22.0, detect_shape=True,
    detection_margin_time=4.0, arm_percentile=0.5, transition_percentile=0.4,
    sg_order=2, sg_window=4, baseline_start=2, baseline_range=2,
    amplification_time=90, timePerLoop=20000,
)
DEFAULT_CONSTS = dict(
    BREAK_JUMP_THRESHOLD=20.0, MIN_CALLABLE_CT=3.0,
    LEGACY_MIN_INCREASE=20.0, LEGACY_MIN_SHARPNESS=5.0,
    CLIMB_MIN_STEP=8.0, CLIMB_NOISE_MULT=4.0, CLIMB_PERSIST_LO=0.35, CLIMB_SETTLE_TOL=0.25,
    CLIMB_START_INDEX=7, CLIMB_MAX_PASSES=6, CLIMB_TAIL_MIN=6,
    JUMP_SETTLE_SKIP=3, JUMP_FLAT_WINDOW=6,
    BREAKING_START_INDEX=6, RISING_WINDOW=6,          # sensor6035.cpp
)
_PARAM_CAST = {
    "min_increase": float, "min_sharpness": float, "min_slight_positive_time": float,
    "detect_shape": lambda s: s.strip().lower() == "true", "detection_margin_time": float,
    "arm_percentile": float, "transition_percentile": float, "sg_order": int, "sg_window": int,
    "baseline_start": int, "baseline_range": int, "amplification_time": int,
    "timePerLoop": lambda s: int(eval(s.replace("ulong", ""), {"__builtins__": {}})),
}


def _read(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def load_firmware_params():
    """(params, consts) as the source tree currently states them; defaults where it is absent."""
    p = dict(DEFAULT_PARAMS)
    c = dict(DEFAULT_CONSTS)
    define_h = _read(os.path.join(ROOT, "src", "define.h"))
    for key, cast in _PARAM_CAST.items():
        m = re.search(r"^\s*(?:double|uint8_t|bool|ulong|uint16_t)\s+%s\s*=\s*([^;]+);" % key,
                      define_h, re.M)
        if m:
            try:
                p[key] = cast(m.group(1).split("//")[0].strip())
            except Exception:
                pass
    for src in ("Alg/Algo.h", "Alg/Algo.cpp", "sensor6035.cpp"):
        txt = _read(os.path.join(ROOT, "src", src))
        for key in c:
            m = re.search(r"^\s*#define\s+%s\s+([0-9.]+)" % key, txt, re.M)
            if m:
                c[key] = type(c[key])(float(m.group(1)))
    return p, c


PARAMS, CONSTS = load_firmware_params()


# --------------------------------------------------------------------------- synthesis
def _logistic(t, A, k, t0):
    x = -k * (t - t0)
    if x > 60:
        return 0.0
    if x < -60:
        return A
    return A / (1.0 + math.exp(x))


def synth_well(recipe, loops, interval_s, seed):
    """One calibrated trace. Deterministic for (recipe, loops, interval, seed).

    recipe keys (all optional, calibrated units unless stated):
      base       baseline level                         warm     warm-up amplitude (approach from below)
      warm_tau   warm-up time constant in ROUNDS        noise    sigma per reading
      drift      linear creep, units/min                drift2   quadratic creep, units/min^2
      knee       (t_min, units/min) extra slope after   sig      list of (A, k, t0_min) logistic rises
      post       creep after the FIRST sigmoid, u/min   steps    list of (index, delta) persistent offsets
      spikes     list of (index, delta, width) transient excursions   cm  scenario-level common-mode trace
      shape      'exp_saturate' (A, tau_min): rises from t=0, no lag - the "started before we looked" well
    """
    rng = random.Random(seed)
    dt = interval_s / 60.0
    base = recipe.get("base", 300.0)
    warm = recipe.get("warm", 0.0)
    warm_tau = recipe.get("warm_tau", 1.5)
    noise = recipe.get("noise", 1.5)
    drift = recipe.get("drift", 0.0)
    drift2 = recipe.get("drift2", 0.0)
    knee = recipe.get("knee")
    sigs = recipe.get("sig", [])
    post = recipe.get("post", 0.0)
    shape = recipe.get("shape")
    cm = recipe.get("cm")
    out = []
    for i in range(loops):
        t = i * dt
        y = base
        y -= warm * math.exp(-i / warm_tau)
        y += drift * t + drift2 * t * t
        if knee and t > knee[0]:
            y += knee[1] * (t - knee[0])
        for (A, k, t0) in sigs:
            y += _logistic(t, A, k, t0)
        if sigs and post:
            y += post * max(0.0, t - sigs[0][2])
        if shape:
            kind, A, tau = shape
            if kind == "exp_saturate":
                y += A * (1.0 - math.exp(-t / tau))
        if cm is not None:
            y += cm[i]
        out.append(y)
    for (idx, delta) in recipe.get("steps", []):
        for i in range(idx, loops):
            out[i] += delta
    for (idx, delta, width) in recipe.get("spikes", []):
        for i in range(idx, min(loops, idx + width)):
            out[i] += delta
    for i in range(loops):
        out[i] += rng.gauss(0.0, noise)
    return out


def common_mode(spec, loops, interval_s, seed):
    """Scenario-wide trace added to every well: shared steps, shared wobble, shared drift."""
    if not spec:
        return None
    rng = random.Random(seed)
    dt = interval_s / 60.0
    tr = [0.0] * loops
    for (idx, delta) in spec.get("steps", []):
        for i in range(idx, loops):
            tr[i] += delta
    wob = spec.get("wobble", 0.0)
    if wob:
        v = 0.0
        for i in range(loops):
            v = 0.85 * v + rng.gauss(0.0, wob)   # slow shared wander, not white
            tr[i] += v
    d = spec.get("drift", 0.0)
    for i in range(loops):
        tr[i] += d * i * dt
    return tr


def to_raw(cal, slope, origin):
    """The ADC path in reverse: (raw - origin) / slope = cal  ->  raw = cal*slope + origin, integer.
    Clamped to 10..60000 because /reviewlast's record scan treats slot-0 raw outside that range as
    "no record" (CLAUDE.md, curve-length scan)."""
    out = []
    for v in cal:
        r = int(round(v * slope + origin))
        out.append(min(60000, max(10, r)))
    return out


def device_view(cal, slope, origin):
    """What bResultGet() actually analyses for this trace on a unit with this slope: the integer
    raw count, then `(float(raw) - origin) / slope` in FLOAT32 (sensor6035.cpp:293), widened to
    double. Two rounding steps the design-side trace does not have, and a step that lands within
    ~0.3 units of a gate flips between them - measured: S07 slot 1's +15 step equalled
    4 x local range to the last float32 bit on RPL01015 and was not repaired."""
    import struct
    f32 = lambda x: struct.unpack("f", struct.pack("f", x))[0]  # noqa: E731
    sl, og = f32(slope), f32(origin)
    return [f32(f32(f32(float(r)) - og) / sl) for r in to_raw(cal, slope, origin)]


# Slopes the pre-screen evaluates every well at. The fleet spans 0.50..3.50 (docs 2026-08-13);
# these cover the units seen up close (RPL250701 1.30..1.55, RPL01015 1.29..1.74) plus both
# tails, so a recipe that holds on all of them holds on any unit it is likely to meet.
SCREEN_SLOPES = (1.0, 1.3, 1.5, 1.74, 2.2)


# --------------------------------------------------------------------------- MIRROR
# MIRROR of src/Alg/Algo.cpp + src/Alg/sgsmooth.cpp + sensor6035.cpp::bResultGet (per slot).
# Index conventions are kept as in C++ (-1 = not found, size_t wrap folded back to -1).
def _solve(a, b):
    n = len(b)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(m[r][c]))
        m[c], m[p] = m[p], m[c]
        for r in range(n):
            if r == c:
                continue
            f = m[r][c] / m[c][c]
            for k in range(c, n + 1):
                m[r][k] -= f * m[c][k]
    return [m[i][n] / m[i][i] for i in range(n)]


_SG_CACHE = {}


def sg_coeffs(width, deg):
    """Row p = weights evaluating the least-squares degree-`deg` fit of the window at window
    position p. Same construction as sgsmooth.cpp sg_coeff(), border rows included."""
    key = (width, deg)
    if key in _SG_CACHE:
        return _SG_CACHE[key]
    win = 2 * width + 1
    xs = [i - width for i in range(win)]
    ata = [[sum(x ** (i + j) for x in xs) for j in range(deg + 1)] for i in range(deg + 1)]
    rows = []
    for p in range(win):
        w = [0.0] * win
        for k in range(win):
            rhs = [xs[k] ** i for i in range(deg + 1)]
            coef = _solve(ata, rhs)
            w[k] = sum(coef[i] * (xs[p] ** i) for i in range(deg + 1))
        rows.append(w)
    _SG_CACHE[key] = rows
    return rows


def sg_smooth(v, width, deg):
    n = len(v)
    res = [0.0] * n
    if width < 1 or deg < 0 or n < 2 * width + 2:
        return res                      # sgsmooth.cpp: parameter error -> ALL ZEROS, silently
    win = 2 * width + 1
    if deg == 0:
        for i in range(width):
            scale = 1.0 / (i + 1)
            for j in range(i + 1):
                res[i] += scale * v[j]
                res[n - 1 - i] += scale * v[n - 1 - j]
        scale = 1.0 / win
        for i in range(n - win + 1):
            res[i + width] = sum(scale * v[i + j] for j in range(win))
        return res
    C = sg_coeffs(width, deg)
    for i in range(width):
        res[i] = sum(C[i][j] * v[j] for j in range(win))
        res[n - 1 - i] = sum(C[i][j] * v[n - 1 - j] for j in range(win))
    for i in range(n - win + 1):
        res[i + width] = sum(C[width][j] * v[i + j] for j in range(win))
    return res


def find_crossing_higher_than(a, crossing, start):
    for i in range(max(0, start), len(a)):
        if a[i] >= crossing:
            return i
    return -1


def find_crossing_lower_than(a, crossing, start):
    for i in range(max(0, start), len(a)):
        if a[i] <= crossing:
            return i
    return -1


def find_crossing_lower_than_reversed(a, crossing, start, end=0):
    i = start
    while i >= end:
        if a[i] <= crossing:
            return i
        i -= 1
    return -1


def mean_range(a, lo, hi):
    if not a:
        return 0.0
    s = 0.0
    for i in range(lo, hi):
        s += a[i]
    return s / float(hi - lo) if hi != lo else float("nan")


def baseline(time_data, raw, bstart, brange):
    d = find_crossing_higher_than(time_data, bstart, 0)
    stop = find_crossing_higher_than(time_data, bstart + brange, d) + 1
    bl = mean_range(raw, d, stop)
    return [v - bl for v in raw]


def differentiate(x, y):
    n = len(y)
    out = []
    for i in range(n):
        if i == 0:
            out.append((y[1] - y[0]) / (x[1] - x[0]))
        elif i == n - 1:
            out.append((y[i] - y[i - 1]) / (x[i] - x[i - 1]))
        else:
            out.append((y[i + 1] - y[i - 1]) / (x[i + 1] - x[i - 1]))
    return out


def argmax(a, start):
    mx, mi = 0.0, -1
    for i in range(max(0, start), len(a)):
        if a[i] > mx:
            mx, mi = a[i], i
    return mi


def mean_slope(a, start, window):
    s = 0.0
    for i in range(start + 1, start + window):
        s += a[i + 1] - a[i]
    return s / (window - 1)


def range_of(a, start, window):
    if start + window > len(a):
        return 0.0
    seg = a[start:start + window]
    return max(seg) - min(seg)


def check_jump(a, crossing, index, C):
    if index < 7 or index + C["JUMP_SETTLE_SKIP"] + C["JUMP_FLAT_WINDOW"] >= len(a):
        return False
    jump = a[index + 1] - a[index]
    if jump < crossing:
        return False
    if jump < range_of(a, index - 7, 8) * 2.5:
        return False
    if abs(mean_slope(a, index + C["JUMP_SETTLE_SKIP"], C["JUMP_FLAT_WINDOW"])) > 1.0:
        return False
    if a[index + 6] - a[index + 1] > crossing:
        return False
    return True


def check_break(a, crossing, start, C):
    for i in range(start, len(a)):
        if check_jump(a, crossing, i, C):
            return i
    return 0


def is_rising_trend(a, start, window):
    if start < 0 or start + window >= len(a):
        return False
    pos, total = 0, 0.0
    for i in range(start + 1, start + window + 1):
        d = a[i] - a[i - 1]
        total += d
        if d > 0:
            pos += 1
    return pos >= window - 1 and total > 0


def check_rising(a, start, window):
    for i in range(start, len(a)):
        if is_rising_trend(a, i, window):
            return i
    return 0


def _window_median(a, lo, hi):
    hi = min(hi, len(a))
    if lo >= hi:
        return 0.0
    buf = sorted(a[lo:hi][:8])
    n = len(buf)
    return buf[n // 2] if n & 1 else 0.5 * (buf[n // 2 - 1] + buf[n // 2])


def _max_abs_step(a):
    return max((abs(a[i + 1] - a[i]) for i in range(len(a) - 1)), default=0.0)


def _find_first_climb(a, C):
    S = C["CLIMB_START_INDEX"]
    if len(a) < S + 10:
        return None
    for i in range(S, len(a) - 1):
        step = a[i + 1] - a[i]
        mag = abs(step)
        if mag < C["CLIMB_MIN_STEP"]:
            continue
        seg = a[i - S:i + 1]
        if mag < C["CLIMB_NOISE_MULT"] * (max(seg) - min(seg)):
            continue
        if i + C["CLIMB_TAIL_MIN"] >= len(a):
            return (i, step, "tail")
        b0 = _window_median(a, i - 5, i + 1)
        b1 = _window_median(a, i + 3, i + 9)
        lvl = b1 - b0
        if abs(lvl) >= C["CLIMB_PERSIST_LO"] * mag:
            return (i, lvl, "offset")
        return (i, step, "spike")
    return None


def neutralise_climbs(a, C):
    """Returns (repaired, applied, first_index). `a` is not modified."""
    a = list(a)
    if len(a) < C["CLIMB_START_INDEX"] + 10:
        return a, 0, -1
    original = list(a)
    step_before = _max_abs_step(original)
    n, first = 0, -1
    for _ in range(C["CLIMB_MAX_PASSES"]):
        found = _find_first_climb(a, C)
        if not found:
            break
        i, amount, kind = found
        if kind == "tail":
            for k in range(i + 1, len(a)):
                a[k] = a[i]
        elif kind == "offset":
            b1 = _window_median(a, i + 3, i + 9)
            lvl = abs(amount)
            j = i + 1
            while j < len(a) and abs(a[j] - b1) > C["CLIMB_SETTLE_TOL"] * lvl:
                j += 1
            if j >= len(a):
                break
            for k in range(j, len(a)):
                a[k] -= amount
            if j > i + 1:
                y0, y1, span = a[i], a[j], j - i
                for k in range(i + 1, j):
                    a[k] = y0 + (y1 - y0) * (k - i) / span
        else:
            mag = abs(amount)
            k = i + 1
            while k < len(a) and abs(a[k] - a[i]) > 0.5 * mag:
                k += 1
            if k >= len(a):
                break
            y0, y1, span = a[i], a[k], k - i
            for m in range(i + 1, k):
                a[m] = y0 + (y1 - y0) * (m - i) / span
        if first < 0:
            first = i
        n += 1
    if n and _max_abs_step(a) > step_before:
        return original, 0, -1
    return a, n, first


def score_curve(time_data, raw, P, C, variant="AT"):
    """post_process_curve -> differentiate -> find_sigmoidal_feature -> predict_outcome."""
    bl = baseline(time_data, raw, P["baseline_start"], P["baseline_range"])
    proc = sg_smooth(bl, P["sg_window"], P["sg_order"])
    diff = differentiate(time_data, proc)
    r = dict(outcome="Negative", peak_i=-1, peak_y=-1.0, left_i=-1, right_i=-1,
             ct_i=-1, ct=-1.0, plateau_i=-1, increase=-1.0, flag=0)
    discard = find_crossing_higher_than(time_data, P["detection_margin_time"], 0)
    pk = argmax(diff, discard if discard >= 0 else len(diff))
    r["peak_i"] = pk
    if pk == -1:
        return r
    r["peak_y"] = diff[pk]
    r["left_i"] = find_crossing_lower_than_reversed(diff, diff[pk] * P["arm_percentile"], pk, discard - 1)
    r["right_i"] = find_crossing_lower_than(diff, diff[pk] * P["arm_percentile"], pk)
    # predict_outcome_core
    rate = 1.0
    ct_i = -1
    for _ in range(12):
        ct_i = find_crossing_lower_than_reversed(diff, diff[pk] * P["transition_percentile"] * rate, pk)
        if ct_i != -1:
            break
        rate *= 1.1
    if ct_i == -1:
        ct_i = 0
    r["ct_i"] = ct_i
    r["ct"] = time_data[ct_i]
    if r["right_i"] != -1:
        pl = find_crossing_lower_than(diff, diff[pk] * P["transition_percentile"], pk)
        if pl == -1:
            pl = len(proc) - 1
    else:
        pl = len(proc) - 1
    r["plateau_i"] = pl
    r["increase"] = proc[pl] - proc[ct_i]
    if r["increase"] > P["min_increase"] and diff[pk] > P["min_sharpness"]:
        if not P["detect_shape"] or r["left_i"] != -1:
            r["outcome"] = "Positive"
        elif r["left_i"] == -1:
            r["outcome"] = "Error"
        if r["outcome"] == "Positive":
            if r["ct"] < C["MIN_CALLABLE_CT"]:
                r["outcome"], r["flag"] = "Flagged", 2
            elif r["ct"] >= P["min_slight_positive_time"]:
                r["outcome"] = "Slight Positive"
    # removed_by_new_gate
    if r["flag"] == 0 and r["outcome"] == "Negative" and pk != -1 and r["increase"] >= 0:
        legacy = r["increase"] > C["LEGACY_MIN_INCREASE"] and diff[pk] > C["LEGACY_MIN_SHARPNESS"]
        now = r["increase"] > P["min_increase"] and diff[pk] > P["min_sharpness"]
        lag_ok = r["ct"] >= P["detection_margin_time"]
        if legacy and not now and lag_ok:
            r["flag"] = 1
            r["outcome"] = "Negative" if variant == "a" else "Flagged"
    return r


def analyse_slot(cal, P, C, variant="AT"):
    """sensor6035.cpp::bResultGet, one slot, calibrated input. THIS branch's trim rule
    (plain `+ breakIndex`, no settle skip, no RISING_WINDOW guard - v244-alg has those)."""
    loops = len(cal)
    dt = P["timePerLoop"] / 60000.0
    time_data = [i * dt for i in range(loops)]
    raw = [float(v) for v in cal]
    raw, climbs, climb_first = neutralise_climbs(raw, C)
    break_i = check_break(raw, C["BREAK_JUMP_THRESHOLD"], C["BREAKING_START_INDEX"], C)
    margin_i = int(P["detection_margin_time"] * 60000.0 / P["timePerLoop"]) if P["timePerLoop"] else 0
    rising_i = check_rising(raw, margin_i, C["RISING_WINDOW"])
    begin, end, valid = 0, loops, True
    if break_i:
        if rising_i:
            if break_i > rising_i:
                end = break_i
            else:
                begin = break_i
        else:
            valid = False
    if valid:
        r = score_curve(time_data[begin:end], raw[begin:end], P, C, variant)
    else:
        r = dict(outcome="Negative", peak_i=-1, peak_y=-1.0, left_i=-1, right_i=-1,
                 ct_i=-1, ct=-1.0, plateau_i=-1, increase=-1.0, flag=0)
    if not valid or (break_i and rising_i and r["outcome"] == "Negative"):
        r.update(outcome="Break", ct=0.0, ct_i=-1)
    r.update(break_i=break_i, rising_i=rising_i, climbs=climbs, climb_first=climb_first,
             trimmed=(begin, end))
    r["letter"] = r["outcome"][0]
    return r


# --------------------------------------------------------------------------- recipe helpers
CT_LAG = 2.064   # logistic: derivative falls to 40% of its peak 2.064/k min before t0


def positive(ct, A=100.0, k=1.0, base=300.0, post=0.8, **kw):
    """A shrimp-LAMP positive with the algorithm's Ct landing at `ct` (analytic, pre-smoothing)."""
    r = dict(base=base, sig=[(A, k, ct + CT_LAG / k)], post=post, warm=40.0, noise=1.5)
    r.update(kw)
    return r


def negative(base=300.0, **kw):
    r = dict(base=base, warm=40.0, noise=1.5)
    r.update(kw)
    return r


def well(recipe, expect, note, ct=None, accept=None, climbs=None, flag=None, known=None):
    """expect: the intended letter. accept: letters also tolerated on the unit (borderline by
    design, e.g. the S/P boundary). ct: (lo, hi) minutes the unit's Ct must fall in.
    known: what the CURRENT firmware is expected to answer when that differs from the intent -
    a documented weakness, kept in the set so a later fix shows up as the unit moving to
    `expect`. The report marks these KNOWN-WEAK; they never count as a pass or a fail."""
    w = dict(recipe=recipe, expect=expect, note=note)
    if known:
        w["known"] = known
    if ct is not None:
        w["ct"] = list(ct)
    if accept:
        w["accept"] = list(accept)
    if climbs is not None:
        w["climbs"] = climbs
    if flag is not None:
        w["flag"] = flag
    return w


def ct_band(ct, tol=1.0):
    return (round(ct - tol, 2), round(ct + tol, 2))


# --------------------------------------------------------------------------- the catalogue
def build_scenarios():
    """Ten wells per scenario, slot 0 first. Slot 0 must never be a dead channel: the
    /reviewlast record scan reads its raw value to decide the run exists (CLAUDE.md)."""
    S = []

    # S01 - the reference ladder. If this one fails nothing else is worth reading.
    S.append(dict(
        id="S01_positive_ladder",
        title="Dilution ladder: eight clean positives Ct 5..20 min, two NTC",
        why="Ct accuracy and the P/S boundary from the reagent side: a run the lab could "
            "in principle reproduce. Sharpness 25..38, increase 60..110 - well clear of every gate.",
        wells=[
            well(positive(5.0, A=140, k=1.5, base=397, warm=66), "P", "Ct 5.0, PC-like", ct=ct_band(5.0)),
            well(positive(7.0, A=110, k=1.2, base=430, warm=190), "P", "Ct 7.0, slots.txt slot 6", ct=ct_band(7.0)),
            well(positive(9.0, A=100, k=1.0, base=250, warm=30), "P", "Ct 9.0", ct=ct_band(9.0)),
            well(positive(11.0, A=100, k=1.0, base=320, warm=25), "P", "Ct 11.0", ct=ct_band(11.0)),
            well(positive(13.0, A=90, k=1.0, base=560, warm=5, noise=2.6), "P", "Ct 13.0, high level", ct=ct_band(13.0)),
            well(positive(15.0, A=80, k=1.0, base=146, warm=5, noise=2.8), "P", "Ct 15.0, low level", ct=ct_band(15.0)),
            well(positive(18.0, A=80, k=1.0, base=300, warm=40), "P", "Ct 18.0", ct=ct_band(18.0)),
            well(positive(20.0, A=80, k=1.0, base=300, warm=40), "P", "Ct 20.0, last P before S", ct=ct_band(20.0)),
            well(negative(base=444, warm=3, noise=1.6), "N", "NTC flat"),
            well(negative(base=186, warm=25, noise=3.8, drift=-0.02), "N", "NTC flat, noisy"),
        ]))

    # S02 - every way a negative looks on a real unit.
    S.append(dict(
        id="S02_negative_field",
        title="Ten negatives as the fleet actually records them",
        why="Nothing here amplifies. Each well is one negative behaviour measured on real "
            "captures: creep, late knee, big warm-up, sync step, high noise, a small bump.",
        cm=dict(steps=[(6, 64.0)]),   # sheet/test.json: +90 raw at round 6 on every slot (1.4 slope)
        wells=[
            well(negative(base=227, warm=15, noise=1.1), "N", "flat, sync step at round 6"),
            well(negative(base=375, warm=0, drift=0.7, noise=1.1), "N", "creep +21 over 30 min (slots.txt 9)"),
            well(negative(base=322, warm=0, drift=0.4, knee=(22.0, 1.4)), "N", "creep with a late knee (slots.txt 3)"),
            well(negative(base=316, warm=0, noise=0.9, drift=0.15), "N", "near-flat, low noise"),
            well(negative(base=148, warm=121, warm_tau=1.2, noise=2.2), "N", "warm-up 27 -> 148 (slots.txt 3)"),
            well(negative(base=302, warm=0, noise=3.8), "N", "noisy, sigma 3.8"),
            well(negative(base=395, warm=0, noise=0.7, drift=-0.3), "N", "slow downward drift"),
            well(negative(base=285, warm=0, spikes=[(45, 7.0, 6)]), "N", "2-min bump of +7 at 15 min"),
            well(negative(base=296, warm=0, drift2=0.02), "N", "quadratic creep, +18 by the end"),
            well(negative(base=558, warm=0, noise=2.6), "N", "high level 558, flat"),
        ]))

    # S03 - the Slight Positive boundary.
    S.append(dict(
        id="S03_slight_positive_boundary",
        title="Late risers straddling min_slight_positive_time = 22 min",
        why="S is decided by ONE number, the Ct, against 22.0. Wells sit at 20.5 / 21.3 / 22.7 / "
            "24 / 26 / 27.5 so both sides of the line are exercised; the 21.3 and 22.7 wells are "
            "close enough that either letter is tolerated on the unit (the design Ct is analytic, "
            "SG smoothing moves it by up to a round).",
        wells=[
            well(positive(20.5, A=100, k=1.0, base=310, warm=30), "P", "Ct 20.5", ct=ct_band(20.5), accept="PS"),
            well(positive(21.3, A=100, k=1.0, base=290, warm=30), "P", "Ct 21.3 - boundary", ct=ct_band(21.3), accept="PS"),
            well(positive(22.7, A=100, k=1.0, base=350, warm=30), "S", "Ct 22.7 - boundary", ct=ct_band(22.7), accept="SP"),
            well(positive(24.0, A=100, k=1.0, base=410, warm=30), "S", "Ct 24.0", ct=ct_band(24.0)),
            well(positive(26.0, A=100, k=1.0, base=260, warm=30), "S", "Ct 26.0, plateau not reached", ct=ct_band(26.0)),
            well(positive(27.5, A=120, k=1.0, base=330, warm=30), "S", "Ct 27.5, rise cut by end of run", ct=ct_band(27.5, 1.3), accept="SN"),
            well(positive(24.0, A=45, k=1.2, base=300, warm=30), "S", "Ct 24.0, weak (A 45)", ct=ct_band(24.0)),
            well(positive(19.0, A=100, k=1.0, base=300, warm=30), "P", "Ct 19.0, control P", ct=ct_band(19.0)),
            well(negative(base=470, warm=20, drift=0.5), "N", "creep, no rise"),
            well(negative(base=200, warm=60, noise=2.0), "N", "flat"),
        ]))

    # S04 - the two size gates and the F band between old and new thresholds.
    S.append(dict(
        id="S04_weak_and_threshold_band",
        title="Weak amplification around min_increase 25 / min_sharpness 8 and the v2.4.3 band",
        why="F (threshold band) exists for wells that were Positive under 20/5 and are not under "
            "25/8. Each well is placed on one side of one gate with a margin the pre-screen "
            "reports. ASF-like kinetics (sharpness 4..8, increase 40..70) are here because "
            "docs 2026-08-21 says they must read F on this build - that is the documented cost.",
        wells=[
            well(positive(10.0, A=60, k=1.4, base=300, warm=30), "P", "weak-but-clear: sharp 17, inc 45", ct=ct_band(10.0)),
            well(positive(10.0, A=42, k=1.5, base=320, warm=30), "P", "just above both gates: sharp ~12, inc ~31", ct=ct_band(10.0)),
            well(positive(10.0, A=25, k=1.6, base=280, warm=30, post=0.0, noise=1.0), "F", "increase in (20,25), sharp ~10 -> band", flag=1, accept="FN"),
            well(positive(10.0, A=70, k=0.30, base=340, warm=30, post=0.3, noise=1.0), "F", "ASF-like: sharp 5..8, inc ~55 -> band", flag=1, accept="FP"),
            well(positive(10.0, A=50, k=0.20, base=300, warm=30, post=0.0, noise=1.0), "N", "sharp ~3.5 fails legacy 5 too -> N", accept="NF"),
            well(positive(10.0, A=16, k=1.5, base=300, warm=30), "N", "increase ~12 fails legacy 20 too -> N"),
            well(positive(8.0, A=200, k=1.5, base=250, warm=30), "P", "strong control: sharp 75, inc 150", ct=ct_band(8.0)),
            well(positive(12.0, A=55, k=0.55, base=300, warm=30), "P", "slow but real: sharp ~7.5-8.5 -> BORDERLINE, either", accept="PF"),
            well(negative(base=330, warm=30, drift=0.9), "N", "creep +27 over the run, no knee"),
            well(negative(base=300, warm=30), "N", "flat"),
        ]))

    # S05 - the detection margin: MIN_CALLABLE_CT, the E branch, the early-rise F.
    S.append(dict(
        id="S05_early_rise_margin",
        title="Rises at and before the 4-min detection margin and the 3.0-min Ct floor",
        why="Three different answers live within two minutes of each other: P (Ct >= 3.0 with a "
            "lag phase), F/2 (real reaction, Ct < 3.0), E (no left arm - the rise was already "
            "under way when measurement began). This is the branch d7775b1 moved; it has never "
            "been exercised on a unit.",
        wells=[
            well(positive(4.0, A=140, k=1.5, base=380, warm=60), "P", "Ct 4.0 - on the margin", ct=ct_band(4.0), accept="PF"),
            well(positive(3.4, A=140, k=1.2, base=350, warm=60), "P", "Ct 3.4 - inside the ERP-corrected band 3.0..3.7", ct=ct_band(3.4), accept="PFE"),
            well(dict(base=300, warm=40, noise=1.5, sig=[(70, 1.6, 2.8), (90, 2.2, 5.6)], post=0.8), "F",
                 "two-stage rise: lead-in at 2.8, main rise at 5.6 -> Ct ~1.7 with a left arm -> F reason 2", flag=2, ct=(1.0, 2.9)),
            well(dict(base=320, warm=40, noise=1.5, shape=("exp_saturate", 220, 4.0)), "E",
                 "rising from t=0, saturating by 10 min: peak at the margin, no left arm", accept="EF"),
            well(dict(base=280, warm=40, noise=1.5, shape=("exp_saturate", 90, 3.0)), "E",
                 "same shape, smaller (A 90): increase ~20 -> may fall to N", accept="ENF"),
            well(positive(6.0, A=120, k=1.5, base=300, warm=150), "P", "Ct 6.0 under a big warm-up (150)", ct=ct_band(6.0)),
            well(positive(2.0, A=200, k=1.5, base=300, warm=40), "E", "Ct 2.0: still rising at the margin, left arm before it -> E", accept="EN"),
            well(dict(base=300, warm=40, noise=1.5, sig=[(70, 1.6, 3.2), (130, 2.2, 5.6)], post=0.8), "F",
                 "two-stage rise, Ct ~2.7: the last round under the 3.0 floor -> F reason 2", flag=2, ct=(2.0, 2.99), accept="FP"),
            well(negative(base=300, warm=200, warm_tau=2.5), "N", "warm-up only (200, tau 2.5 rounds)"),
            well(positive(2.0, A=120, k=2.5, base=300, warm=40), "N", "Ct 2.0, k 2.5: the whole rise is over before 4 min -> invisible, N", accept="NE"),
        ]))

    # S06 - electrical steps: break vs climb repair.
    S.append(dict(
        id="S06_breaks_and_climbs",
        title="Steps, spikes and dropouts on negatives: Break vs neutralised climb",
        why="BREAK_JUMP_THRESHOLD 20 splits a step into two fates: >= 20 on a flat trace is a "
            "Break; 8..20 is repaired by neutralise_climbs and the well is read as what is left. "
            "Each well is one row of that table, including the RPL01004 staircase that used to "
            "read Positive.",
        wells=[
            well(negative(base=300, warm=30, steps=[(30, 32.0)]), "N", "+32 at 10 min on a quiet trace: repaired as an offset, NOT a Break (repair runs first, no upper size)", climbs=1),
            well(negative(base=300, warm=30, noise=0.7, steps=[(30, 12.0)]), "N", "+12 at 10 min (sigma 0.7) -> offset repaired", climbs=1),
            well(negative(base=207, warm=30, noise=0.8, steps=[(15, 19.3)]), "N", "RPL01004 s2: +19.3 at 5 min, sigma 0.8 -> repaired, not P", climbs=1),
            well(negative(base=300, warm=30, spikes=[(30, 26.0, 1)]), "N", "+26 single-reading spike -> repaired", climbs=1),
            well(negative(base=300, warm=30, spikes=[(45, -40.0, 3)]), "N", "dropout -40 for 3 readings -> repaired", climbs=1),
            well(negative(base=300, warm=30, noise=0.8, steps=[(24, 18.0), (42, 18.0), (60, 18.0)]), "N", "staircase 3 x +18 -> repaired", climbs=3),
            well(negative(base=300, warm=30, steps=[(30, -30.0)]), "N", "-30 at 10 min: invisible to checkJump, repaired", climbs=1),
            well(negative(base=300, warm=30, steps=[(84, 25.0)]), "N", "+25 in the last 6 readings -> tail hold", climbs=1),
            well(negative(base=300, warm=30, noise=3.0, steps=[(40, 32.0)]), "B", "+32 on sigma 3: 2.5x < jump < 4x local range -> Break", accept="BN"),
            well(negative(base=300, warm=30, noise=4.5, steps=[(54, 40.0)]), "B", "+40 on sigma 4.5: dodges the climb gate (4x range) AND checkJump (post-step slope > 1.0) -> the scorer reads the smoothed edge as a rise", accept="BN", known="P"),
        ]))

    # S07 - the same artefacts on top of REAL amplification.
    S.append(dict(
        id="S07_artefacts_on_positives",
        title="Positives that also carry a step, spike, dropout or tail step",
        why="Repair exists so a well with real amplification underneath keeps a readable result "
            "instead of a Break, and a break inside the plateau trims the end instead of the "
            "reaction. Ct must survive every repair.",
        wells=[
            well(positive(9.0, A=100, k=1.0, base=300, warm=30, noise=0.8, steps=[(15, 18.0)]), "P", "offset +18 at 5 min, then P at Ct 9", ct=ct_band(9.0), climbs=1),
            well(positive(9.0, A=100, k=1.0, base=300, warm=30, spikes=[(60, -40.0, 2)]), "P", "dropout in the plateau", ct=ct_band(9.0), climbs=1),
            well(positive(9.0, A=100, k=1.0, base=300, warm=30, steps=[(85, 30.0)]), "P", "step in the last 5 readings: Ct must not slide to the end", ct=ct_band(9.0), climbs=1),
            well(positive(8.0, A=100, k=1.0, base=300, warm=30, steps=[(60, 40.0)]), "P", "+40 at 20 min after the plateau: end-trim, P kept", ct=ct_band(8.0), accept="PB"),
            well(positive(16.0, A=100, k=1.0, base=300, warm=30, steps=[(24, 40.0)]), "P", "+40 at 8 min BEFORE the rise: start-trim, P kept", ct=ct_band(16.0), accept="PB"),
            well(positive(9.0, A=100, k=1.0, base=300, warm=30, spikes=[(27, 25.0, 1)]), "P", "+25 spike on the rising flank", ct=ct_band(9.0), accept="PBF"),
            well(positive(9.0, A=100, k=1.0, base=300, warm=30, noise=4.0), "P", "sigma 4 noise on a real rise", ct=ct_band(9.0)),
            well(positive(9.0, A=100, k=1.0, base=300, warm=30, noise=0.8, steps=[(15, -18.0)]), "P", "-18 at 5 min then P", ct=ct_band(9.0), climbs=1),
            well(positive(9.0, A=100, k=1.0, base=300, warm=30), "P", "control P", ct=ct_band(9.0)),
            well(negative(base=300, warm=30), "N", "control N"),
        ]))

    # S08 - noise stress on flat wells and on one real rise.
    S.append(dict(
        id="S08_noise_stress",
        title="Flat wells at sigma 1 .. 6 calibrated units, plus positives under the same noise",
        why="min_sharpness 8.0 was set where pure noise never reaches (P(>8.0) = 0.000% at "
            "today's sigma ~2). Sigma 4..6 is 2-3x worse than any fleet unit; a flat well "
            "there may legitimately trip a gate, so those two only tolerate N or F.",
        wells=[
            well(negative(base=300, warm=20, noise=1.0), "N", "sigma 1.0"),
            well(negative(base=300, warm=20, noise=2.0), "N", "sigma 2.0 (fleet typical)"),
            well(negative(base=300, warm=20, noise=3.0), "N", "sigma 3.0"),
            well(negative(base=300, warm=20, noise=4.0), "N", "sigma 4.0", accept="NF"),
            well(negative(base=300, warm=20, noise=6.0), "N", "sigma 6.0 - stress", accept="NFB"),
            well(positive(10.0, A=100, k=1.0, base=300, warm=20, noise=2.0), "P", "P at sigma 2", ct=ct_band(10.0)),
            well(positive(10.0, A=100, k=1.0, base=300, warm=20, noise=4.0), "P", "P at sigma 4", ct=ct_band(10.0)),
            well(positive(10.0, A=45, k=1.5, base=300, warm=20, noise=4.0), "P", "weak P (A 45) at sigma 4", ct=ct_band(10.0), accept="PF"),
            well(negative(base=150, warm=20, noise=3.0), "N", "low level 150, sigma 3"),
            well(negative(base=560, warm=20, noise=3.0), "N", "high level 560, sigma 3"),
        ]))

    # S09 - common-mode: all ten wells move together.
    #
    # The sync step sits at 3.0 min (round 9), not at round 6 as in S02: that is where RPL01015's
    # own stored run had it (+45..58 units on every channel, read back on 2026-09-15), and round 9
    # is PAST CLIMB_START_INDEX (7), so neutralise_climbs repairs it on every well - the unit
    # reported "neutralised 1" x10 on that real run. Channels 1-3 take the step one round later
    # than 4-10, because the ten channels are read sequentially inside a round and the light
    # source moved between two of the reads. Both details are copied from that capture, and so
    # is warm=0: that run is FLAT from round 0 (the 15-min opto preheat had settled the optics).
    # It matters: with a warm-up tail inside the 8-point look-back window, 4 x range exceeds the
    # step, the climb gate refuses it, and checkJump then calls the same step a BREAK. Whether a
    # 3-min sync step is repaired or breaks the well depends on what the first rounds look like.
    S.append(dict(
        id="S09_common_mode",
        title="Shared wobble, and the 3.0-min synchronous step of a real unit, two wells positive",
        why="probe_sensor_noise.py measured 64% common component on one unit and 9% on "
            "another, and RPL01015's stored run carried a +45 step at 3.0 min on all ten channels "
            "that the climb repair took out ten times over. Calls are per well: the shared move must "
            "not become ten detections, must not hide the two real ones, and the repair must not "
            "move a Ct.",
        cm=dict(steps=[(50, 6.0)], wobble=1.2, drift=0.3),
        wells=[
            well(positive(8.0, A=110, k=1.2, base=300, warm=0, steps=[(10, 45.0)]), "P", "P, sync step one round late (channel 1-3)", ct=ct_band(8.0), climbs=1),
            well(negative(base=250, warm=0, steps=[(10, 48.0)]), "N", "shared only, step at round 10", climbs=1),
            well(negative(base=330, warm=0, steps=[(10, 42.0)]), "N", "shared only, step at round 10", climbs=1),
            well(negative(base=410, warm=0, steps=[(9, 45.0)]), "N", "shared only, step at round 9", climbs=1),
            well(positive(14.0, A=80, k=1.0, base=290, warm=0, steps=[(9, 45.0)]), "P", "P, sync step at round 9", ct=ct_band(14.0), climbs=1),
            well(negative(base=190, warm=0, steps=[(9, 58.0)]), "N", "shared only, step at round 9", climbs=1),
            well(negative(base=300, warm=0, steps=[(9, 58.0)]), "N", "shared only, step at round 9", climbs=1),
            well(negative(base=360, warm=0, steps=[(9, 45.0)]), "N", "shared only, step at round 9", climbs=1),
            well(negative(base=280, warm=0, steps=[(9, 52.0)]), "N", "shared only, step at round 9", climbs=1),
            well(negative(base=500, warm=0, steps=[(9, 45.0)]), "N", "shared only, step at round 9", climbs=1),
        ]))

    # S10 - the Ct floor against the left-arm clamp. d7775b1 lowered MIN_CALLABLE_CT to 3.0 so
    # that early risers report Positive, but find_sigmoidal_feature() still bounds the left-arm
    # search at discard_index - 1 (Algo.cpp:774), i.e. 3.67 min at 20 s/round. For a logistic rise
    # the 50%-of-peak arm sits 0.3/k min after the 40% crossing (the Ct), so an ordinary sigmoid
    # with Ct < ~3.4 has its arm before the clamp, fails detected_shape() and is called E - the
    # label the 11/09 change set out to abolish for real reactions. The mirror map (3 seeds x
    # 5 slopes, k 0.8..3.0, A 40..140) puts the E/P edge at reported Ct 3.33/3.67 for k >= 1.0;
    # F reason 2 is reachable only by wide rises (k <= 0.7) or two-stage shapes. Each well below
    # is 35/35 on the mirror (7 seeds x 5 slopes); `known` records what the unit answers today.
    PC = dict(A=140, k=1.5, base=380, warm=40)
    S.append(dict(
        id="S10_ct_floor_ladder",
        title="Ct floor 3.0 vs the 3.67-min left-arm clamp: Ct x steepness ladder of a positive control",
        why="MIN_CALLABLE_CT went 4.0 -> 3.0 (d7775b1) on 15 ERP wells with Ct 3.0..3.7 that reviewers "
            "flipped to P. But the left arm is still searched only from discard_index-1 (3.67 min), so "
            "a textbook sigmoid with Ct 3.0..3.33 fails the shape test and reads E, not P; below 3.0 it "
            "reads E, not F/2. This ladder measures exactly where the unit's E/P edge is, per steepness.",
        wells=[
            well(positive(2.67, **PC), "F", "PC-like, reported Ct 2.33-2.67: real reaction under the floor -> intent F/2, unit E (arm before 3.67)", flag=2, ct=(1.5, 2.99), known="E"),
            well(positive(3.0, **PC), "F", "PC-like, reported Ct 2.67: intent F/2, unit E", flag=2, ct=(1.5, 2.99), known="E"),
            well(positive(3.33, **PC), "P", "PC-like, reported Ct 3.00 - ON the floor: intent P, unit E (arm at ~3.2 min < clamp)", ct=(3.0, 3.67), known="E"),
            well(positive(3.67, **PC), "P", "PC-like, reported Ct 3.33: arm lands on index 11 -> P (first P of the ladder)", ct=(3.0, 3.67)),
            well(positive(4.0, **PC), "P", "PC-like, reported Ct 3.67-4.0", ct=(3.33, 4.33)),
            well(positive(4.33, **PC), "P", "PC-like, reported Ct 4.0", ct=(3.67, 4.33)),
            well(positive(3.6, A=140, k=0.6, base=380, warm=40), "P", "wide rise (k 0.6): arm 0.5 min after Ct, clears the clamp although Ct reads 3.0-4.0 (noisy on a slow rise)", ct=(3.0, 4.0)),
            well(positive(3.33, A=140, k=2.0, base=380, warm=40), "P", "steeper (k 2.0), reported Ct 3.00: intent P, unit E", ct=(3.0, 3.67), known="E"),
            well(positive(3.67, A=140, k=3.0, base=380, warm=40), "P", "very steep (k 3.0), reported Ct 3.33: intent P, unit E - the clamp bites up to 3.33 here", ct=(3.0, 3.67), known="E"),
            well(positive(4.0, A=140, k=3.0, base=380, warm=40), "P", "very steep (k 3.0), reported Ct 3.67: P", ct=(3.33, 4.0)),
        ]))

    # S11 - the same region with what the field adds to a curve.
    S.append(dict(
        id="S11_ct_floor_field",
        title="Around the 3.0 floor with field shapes: noise, big warm-up, the 3-min sync step, a spike, two-stage, optical transient",
        why="Positive controls are the fastest wells on the plate (12/15 of the ERP wells), so they meet "
            "every instrument artefact at the worst time: the 3.0-min synchronous step of RPL01015 lands "
            "right on their rise. Which artefacts move the verdict, which only move the Ct, and which "
            "turn an E into a P by accident.",
        wells=[
            well(positive(3.8, A=150, k=1.3, base=380, warm=60, noise=3.0), "P", "PC, sigma 3.0: noise does not move the E/P edge", ct=(3.0, 4.0)),
            well(positive(3.9, A=150, k=1.3, base=380, warm=250, warm_tau=2.5), "P", "PC under a 250-unit, tau 2.5 warm-up (large optical transient) - still P", ct=(3.0, 4.0)),
            well(positive(3.7, A=150, k=1.3, base=380, warm=60, post=1.5), "P", "PC with 1.5/min creep after the plateau", ct=(3.0, 4.0)),
            well(positive(3.7, A=150, k=1.3, base=380, warm=0, steps=[(9, 50.0)]), "P", "PC + RPL01015's +50 sync step at round 9 (flat start): repaired, Ct moves 3.33 -> 4.00", ct=(3.67, 4.33), climbs=1),
            well(positive(3.3, A=150, k=1.3, base=380, warm=0, steps=[(9, 50.0)]), "P", "same rise as S10 slot 3 (E without the step): the repair pushes Ct to 4.0 and opens the P branch by accident", ct=(3.67, 4.33), climbs=1),
            well(positive(4.0, A=150, k=1.3, base=380, warm=40, steps=[(9, 50.0)]), "F", "PC (Ct 4.0) + the same step but with a warm-up tail: the repair refuses, step+rise read as one -> F/2 with a bogus Ct 1.67 (flag right, number wrong); a noise-edge well, P Ct 4.0-4.33 in 25/35 mirror draws", flag=2, accept="P", climbs=0),
            well(positive(3.7, A=150, k=1.3, base=380, warm=0, spikes=[(10, 40.0, 2)]), "P", "PC + two-round +40 spike at 3.33 min: repaired, Ct pushed late (4.33-5.0)", ct=(4.0, 5.33)),
            well(dict(base=300, warm=40, noise=1.5, sig=[(50, 1.6, 3.4), (110, 2.2, 5.6)], post=0.8), "P", "weak lead-in (50) at 3.4 then the main rise at 5.6: the lead-in supplies the left arm -> P, Ct 3.33-3.67", ct=(3.0, 4.0)),
            well(positive(5.0, A=150, k=1.3, base=380, warm=40, steps=[(9, 50.0)]), "P", "un-repaired 3-min step ahead of a 5.0 rise: harmless, P Ct 4.67-5.0", ct=(4.33, 5.33)),
            well(negative(base=460, warm=300, warm_tau=3.0), "N", "optical warm-up transient alone, 300 units tau 3 rounds from 160 (derivative peak ~1 min) - the sub-3.0 noise source the 11/09 note names -> N"),
        ]))

    return S


# --------------------------------------------------------------------------- real replays
def load_cal_file(path):
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip().rstrip(",")
            if not line or line.startswith("#"):
                continue
            rows.append([float(v) for v in line.split(",") if v.strip()])
    return rows


def load_payload_json(path):
    """An uploaded payload (sheet/test.json shape): raw strings + its own slopes/origins."""
    with open(path, "r", encoding="utf-8") as f:
        d = json.load(f)
    if isinstance(d, list):
        d = d[0]
    sl, org = d["slopes"], d["origins"]
    rows = []
    for i, s in enumerate(d["amplification"]):
        raw = [float(v) for v in s.split(",") if v.strip()]
        rows.append([(v - org[i]) / sl[i] for v in raw])
    return rows, d


def real_scenarios(loops):
    """Two real captures, cut to `loops`. Expectations are the unit's own verdict where the
    payload carries one, else only the wells a reader can call without doubt."""
    S = []
    p = os.path.join(ROOT, "sheet", "test.json")
    if os.path.isfile(p):
        rows, d = load_payload_json(p)
        letters = [r.split("|")[-1].strip()[:1] for r in d.get("result", [])]
        S.append(dict(
            id="R01_real_all_negative_RPL250701",
            title="Real run RPL250701 30-07-2025 (v2.2.9), all ten wells Negative",
            why="Raw capture, its own slopes; the unit called N x10. Level 210..420 raw, +90 raw "
                "sync step at round 6, sigma ~1.5.",
            wells=[dict(cal=rows[i][:loops], expect=letters[i] if i < len(letters) else "N",
                        note="unit said %s" % (letters[i] if i < len(letters) else "?")) for i in range(10)],
            real=True))
    p = os.path.join(ROOT, "tools", "slots.txt")
    if os.path.isfile(p):
        rows = load_cal_file(p)
        exp = ["N", "N", "?", "N", "N", "P", "N", "?", "?", "N"]
        notes = ["flat 146", "flat 186, noisy", "creep +47 with late knee: reader cannot call it",
                 "flat 465 after warm-up", "flat 444", "clean P, algorithm Ct ~4.3 (40%-of-peak-slope), rise 107",
                 "flat 361 after warm-up", "creep +20: reader cannot call it",
                 "creep +21: reader cannot call it", "flat 558"]
        S.append(dict(
            id="R02_real_mixed_slots_txt",
            title="Real calibrated run tools/slots.txt (10 x 120), one clean positive",
            why="The run the web mock replays. Slot 6 is the reference positive; three creeping "
                "wells are reported, not asserted.",
            wells=[dict(cal=rows[i][:loops], expect=exp[i], note=notes[i],
                        **({"ct": [3.5, 5.5]} if i == 5 else {})) for i in range(10)],
            real=True))

        # R03 - the same real positive moved earlier one round at a time. The most realistic
        # Ct < 4 curve there is: a genuine rise (107 units, Ct 4.33 as the unit reads it) with
        # its own noise and warm-up, and nothing synthetic about its shape. Dropping the first n
        # samples and holding the last value pads the tail; the mirror puts the left arm on
        # index 13 / 12 / 11 for shifts 0 / 1 / 2 and at -1 (E) from shift 3 on.
        def shifted(row, n):
            r = row[n:] + [row[-1]] * n
            return r[:loops]
        pos_row = rows[5]
        shifts = [
            (0, "P", (4.0, 4.67), None, "slot 6 as captured: Ct 4.33"),
            (1, "P", (3.67, 4.33), None, "-0.33 min: Ct 4.0"),
            (2, "P", (3.33, 4.0), None, "-0.67 min: Ct 3.67, left arm exactly at the clamp (index 11)"),
            (3, "P", (3.0, 3.67), "E", "-1.0 min: Ct 3.33 - intent P (>= floor), unit E: arm before 3.67"),
            (4, "P", (3.0, 3.33), "E", "-1.33 min: Ct 3.00 - ON the floor, intent P, unit E"),
            (5, "F", (1.5, 2.99), "E", "-1.67 min: Ct 2.67 - intent F/2, unit E"),
            (6, "F", (1.5, 2.99), "E", "-2.0 min: Ct 2.33 - intent F/2, unit E"),
            (7, "F", (1.5, 2.99), "E", "-2.33 min: Ct 2.0 - intent F/2, unit E"),
        ]
        wells = []
        for n, exp_, band, known, note in shifts:
            w = dict(cal=shifted(pos_row, n), expect=exp_, note=note, ct=list(band))
            if exp_ == "F":
                w["flag"] = 2
            if known:
                w["known"] = known
            wells.append(w)
        wells.append(dict(cal=shifted(rows[0], 3), expect="N", note="slot 1 (flat 146) shifted 3: N"))
        wells.append(dict(cal=shifted(rows[9], 6), expect="N", note="slot 10 (flat 558) shifted 6: N"))
        S.append(dict(
            id="R03_real_positive_shifted",
            title="The real positive of tools/slots.txt moved earlier 0..7 rounds: Ct 4.33 -> 2.0 on a genuine shape",
            why="S10 says it with synthetic sigmoids; this says it with the one real positive in the repo. "
                "Two rounds earlier it is still P; from three rounds on (Ct 3.33 and below) the unit reads "
                "E - the 3.0 floor never reaches this curve.",
            wells=wells,
            real=True))
    return S


# --------------------------------------------------------------------------- materialise
def materialise(scenario, loops, interval_s):
    """-> list of 10 calibrated traces for this scenario at this time grid."""
    seed_base = sum(ord(c) for c in scenario["id"]) * 7919
    if scenario.get("real"):
        return [list(w["cal"]) for w in scenario["wells"]]
    cm = common_mode(scenario.get("cm"), loops, interval_s, seed_base + 99)
    traces = []
    for k, w in enumerate(scenario["wells"]):
        r = dict(w["recipe"])
        r["cm"] = cm
        traces.append(synth_well(r, loops, interval_s, seed_base + k))
    return traces


def all_scenarios(loops):
    return build_scenarios() + real_scenarios(loops)


def write_outputs(loops, interval_s, slopes, origins, out_dir=OUT_DIR):
    os.makedirs(out_dir, exist_ok=True)
    P = dict(PARAMS, timePerLoop=interval_s * 1000, amplification_time=loops)
    index = []
    for sc in all_scenarios(loops):
        traces = materialise(sc, loops, interval_s)
        cal_path = os.path.join(out_dir, sc["id"] + ".cal.txt")
        raw_path = os.path.join(out_dir, sc["id"] + ".raw.txt")
        exp_path = os.path.join(out_dir, sc["id"] + ".expect.json")
        with open(cal_path, "w", encoding="utf-8", newline="\n") as f:
            f.write("# %s - %s\n# calibrated units, one slot per line, %d rounds x %d s. "
                    "sse_test_server.py --slots <this> --reboot renders it.\n"
                    % (sc["id"], sc["title"], loops, interval_s))
            for tr in traces:
                f.write(",".join("%.1f" % v for v in tr) + "\n")
        with open(raw_path, "w", encoding="utf-8", newline="\n") as f:
            f.write("# %s - RAW counts for slopes %s origins %s. send_slots.py COM7 <this> --loops %d\n"
                    "# run_sim_cases.py rebuilds this from the UNIT's own slopes; use it instead when you can.\n"
                    % (sc["id"], ",".join("%.3f" % s for s in slopes), ",".join("%g" % o for o in origins), loops))
            for i, tr in enumerate(traces):
                f.write(",".join(str(v) for v in to_raw(tr, slopes[i], origins[i])) + "\n")
        mirror = [analyse_slot(device_view(tr, slopes[i], origins[i]), P, CONSTS) for i, tr in enumerate(traces)]
        exp = dict(id=sc["id"], title=sc["title"], why=sc["why"], loops=loops, interval_s=interval_s,
                   slopes=slopes, origins=origins, wells=[])
        for i, w in enumerate(sc["wells"]):
            e = {k: w[k] for k in ("expect", "note", "ct", "accept", "climbs", "flag", "known") if k in w}
            m = mirror[i]
            e["mirror"] = dict(letter=m["letter"], ct=round(m["ct"], 2), increase=round(m["increase"], 1),
                               sharpness=round(m["peak_y"], 2), climbs=m["climbs"], flag=m["flag"],
                               break_i=m["break_i"], rising_i=m["rising_i"])
            exp["wells"].append(e)
        with open(exp_path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(exp, f, indent=1)
        index.append((sc, mirror))
    write_readme(index, loops, interval_s, out_dir)
    return index


def write_readme(index, loops, interval_s, out_dir):
    lines = ["# Bộ kịch bản mô phỏng đánh giá kết quả (sinh từ `tools/sim_cases.py gen`)", "",
             "Sinh ở %d vòng × %d s. **Đừng sửa tay** — sửa recipe trong `tools/sim_cases.py` rồi chạy lại `gen`. "
             "Cột *mirror* là bản MIRROR trên PC, **không phải** máy; máy thật là `tools/run_sim_cases.py COM7`. "
             "Mỗi kịch bản có ba file: `.cal.txt` (đã calibrate, phát lại được bằng "
             "`sse_test_server.py --slots <file> --reboot`), `.raw.txt` (đếm thô ở slope danh nghĩa, cho "
             "`send_slots.py`), `.expect.json` (kỳ vọng từng giếng + số của mirror)." % (loops, interval_s), "",
             "Ký hiệu: **kỳ vọng** = chữ theo thiết kế giếng · **chấp nhận** = chữ khác cũng được (giếng cố ý sát "
             "biên) · *(máy hiện nay: X)* = điểm yếu đã biết, máy hôm nay trả X — không tính đậu/rớt.", ""]
    for sc, mirror in index:
        lines.append("## %s - %s" % (sc["id"], sc["title"]))
        lines.append("")
        lines.append(sc["why"])
        lines.append("")
        lines.append("| slot | kỳ vọng | chấp nhận | Ct (phút) | mirror | Ct | inc | sharp | climb | ghi chú |")
        lines.append("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
        for i, w in enumerate(sc["wells"]):
            m = mirror[i]
            band = "%.1f..%.1f" % tuple(w["ct"]) if "ct" in w else ""
            lines.append("| %d | **%s** | %s | %s | %s | %s | %s | %s | %d | %s |" % (
                i + 1, w["expect"], "".join(w.get("accept", "")) + (" (máy hiện nay: %s)" % w["known"] if w.get("known") else ""), band, m["letter"],
                ("%.1f" % m["ct"]) if m["ct"] >= 0 else "-",
                ("%.0f" % m["increase"]) if m["increase"] >= 0 else "-",
                ("%.1f" % m["peak_y"]) if m["peak_y"] >= 0 else "-", m["climbs"], w["note"]))
        lines.append("")
    with open(os.path.join(out_dir, "README.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


# --------------------------------------------------------------------------- screening
def screen(loops, interval_s, only=None, verbose=False, variant="AT"):
    """MIRROR pre-screen. Returns (n_wells, problems). A problem is a well whose mirror letter
    is neither `expect` nor in `accept`, whose Ct falls outside its band, or whose climb count
    differs - i.e. a recipe that does not build the well it claims to."""
    P = dict(PARAMS, timePerLoop=interval_s * 1000, amplification_time=loops)
    n, problems = 0, []
    for sc in all_scenarios(loops):
        if only and only.lower() not in sc["id"].lower():
            continue
        traces = materialise(sc, loops, interval_s)
        if verbose:
            print("\n== %s  %s" % (sc["id"], sc["title"]))
        for i, w in enumerate(sc["wells"]):
            n += 1
            ok = True
            why = []
            allowed = set(w["expect"]) | set(w.get("accept", "")) | set(w.get("known", ""))
            m = None
            for slope in SCREEN_SLOPES:
                mm = analyse_slot(device_view(traces[i], slope, 0.0), P, CONSTS, variant)
                if m is None:
                    m = mm
                bad = []
                if w["expect"] != "?" and mm["letter"] not in allowed:
                    bad.append("letter %s not in %s" % (mm["letter"], "".join(sorted(allowed))))
                if "ct" in w and mm["letter"] in "PSF" and not (w["ct"][0] <= mm["ct"] <= w["ct"][1]):
                    bad.append("ct %.2f outside %s" % (mm["ct"], w["ct"]))
                if "climbs" in w and mm["climbs"] != w["climbs"]:
                    bad.append("climbs %d != %d" % (mm["climbs"], w["climbs"]))
                if "flag" in w and mm["letter"] == "F" and mm["flag"] != w["flag"]:
                    bad.append("flag %d != %d" % (mm["flag"], w["flag"]))
                if bad:
                    ok = False
                    why.append("@slope %.2f: %s" % (slope, "; ".join(bad)))
            if verbose or not ok:
                print("  %s slot %2d  want %s%-4s got %s  ct %6.2f  inc %6.1f  sharp %6.2f  "
                      "climbs %d flag %d brk %d rise %d  %s%s" % (
                          "ok " if ok else "BAD", i + 1, w["expect"],
                          ("/" + "".join(w["accept"])) if w.get("accept") else "", m["letter"], m["ct"],
                          m["increase"], m["peak_y"], m["climbs"], m["flag"], m["break_i"],
                          m["rising_i"], w["note"], ("  <-- " + "; ".join(why)) if why else ""))
            if not ok:
                problems.append((sc["id"], i + 1, "; ".join(why)))
    return n, problems


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd")
    sub.add_parser("list", help="print the catalogue")
    g = sub.add_parser("gen", help="write tools/simcases/")
    g.add_argument("--loops", type=int, default=PARAMS["amplification_time"])
    g.add_argument("--interval", type=int, default=PARAMS["timePerLoop"] // 1000, help="seconds per round")
    g.add_argument("--slopes", default="1.4", help="one value or 10 comma-separated, for the .raw.txt files")
    g.add_argument("--origins", default="0")
    g.add_argument("--out", default=OUT_DIR)
    s = sub.add_parser("screen", help="MIRROR pre-screen of every recipe")
    s.add_argument("only", nargs="?", default=None)
    s.add_argument("--loops", type=int, default=PARAMS["amplification_time"])
    s.add_argument("--interval", type=int, default=PARAMS["timePerLoop"] // 1000)
    s.add_argument("--variant", choices=("AT", "a"), default="AT")
    s.add_argument("-v", "--verbose", action="store_true")
    r = sub.add_parser("replay", help="MIRROR verdicts on a real calibrated file (one slot per line)")
    r.add_argument("path")
    r.add_argument("--loops", type=int, default=PARAMS["amplification_time"])
    r.add_argument("--interval", type=int, default=PARAMS["timePerLoop"] // 1000)
    a = ap.parse_args()

    if a.cmd == "list" or a.cmd is None:
        print("firmware numbers in use: min_increase %g  min_sharpness %g  margin %g  MIN_CALLABLE_CT %g  "
              "break %g  climb %g  loops %d x %d s" % (
                  PARAMS["min_increase"], PARAMS["min_sharpness"], PARAMS["detection_margin_time"],
                  CONSTS["MIN_CALLABLE_CT"], CONSTS["BREAK_JUMP_THRESHOLD"], CONSTS["CLIMB_MIN_STEP"],
                  PARAMS["amplification_time"], PARAMS["timePerLoop"] // 1000))
        for sc in all_scenarios(PARAMS["amplification_time"]):
            print("\n%s  %s" % (sc["id"], sc["title"]))
            for i, w in enumerate(sc["wells"]):
                print("   %2d  %s%-5s %s" % (i + 1, w["expect"], ("/" + "".join(w["accept"])) if w.get("accept") else "",
                                            w["note"]))
        return 0

    if a.cmd == "gen":
        sl = [float(v) for v in a.slopes.split(",")]
        og = [float(v) for v in a.origins.split(",")]
        sl = sl * 10 if len(sl) == 1 else sl
        og = og * 10 if len(og) == 1 else og
        n, problems = screen(a.loops, a.interval)
        index = write_outputs(a.loops, a.interval, sl, og, a.out)
        print("wrote %d scenarios to %s (%d rounds x %d s); pre-screen %d wells, %d problems"
              % (len(index), a.out, a.loops, a.interval, n, len(problems)))
        for p in problems:
            print("  PROBLEM %s slot %d: %s" % p)
        return 1 if problems else 0

    if a.cmd == "screen":
        n, problems = screen(a.loops, a.interval, a.only, a.verbose, a.variant)
        print("\n%d wells screened, %d off their intended branch" % (n, len(problems)))
        for p in problems:
            print("  %s slot %d: %s" % p)
        return 1 if problems else 0

    if a.cmd == "replay":
        P = dict(PARAMS, timePerLoop=a.interval * 1000, amplification_time=a.loops)
        rows = load_cal_file(a.path)
        for i, row in enumerate(rows):
            m = analyse_slot(row[:a.loops], P, CONSTS)
            print("slot %2d  %-16s ct %6.2f  inc %7.1f  sharp %6.2f  climbs %d flag %d  brk %d rise %d" % (
                i + 1, m["outcome"], m["ct"], m["increase"], m["peak_y"], m["climbs"], m["flag"],
                m["break_i"], m["rising_i"]))
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
