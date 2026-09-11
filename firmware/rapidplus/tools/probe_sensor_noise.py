#!/usr/bin/env python3
"""Measure the optical read quality of a real run, from an uploaded payload.

    python tools/probe_sensor_noise.py sheet/test.json
    python tools/probe_sensor_noise.py tools/slots.txt --calibrated

Every number quoted in docs/plan/2026-09-10-auto-gain-auto-origin.md comes out of
this script. Nothing here is copied from the firmware: the point is to describe the
DATA the firmware produced, so a claim about noise can be re-checked on any run
instead of being argued.

The one thing it does mirror is the scoring pipeline (baseline -> Savitzky-Golay
-> derivative -> peak), because `sharpness` is the quantity min_sharpness is
compared against and a noise figure in raw counts says nothing on its own. That
mirror is marked MIRROR and is the only place drift here can hurt.

Stdlib only, on purpose - this has to run on whatever machine holds the logs.
"""
import argparse
import json
import math
import random
import statistics as st
import sys

# ---------------------------------------------------------------- SG (MIRROR)
# MIRROR of src/Alg/Algo.cpp: smooth() -> sg_smooth(window=sg_window, deg=sg_order)
# then differentiate() (forward / centred / backward). Defaults from define.h.
SG_WINDOW = 4      # half width; real window = 2*w+1 = 9
SG_ORDER = 2
DETECTION_MARGIN_MIN = 4.0
BASELINE_START_MIN = 2
BASELINE_RANGE_MIN = 2
MIN_SHARPNESS = 8.0
SUM_CEILING = 65535
REPEATS = 8


def _solve(a, b):
    """Gaussian elimination with partial pivoting; a is n x n, b is length n."""
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


def sg_coeffs(width, deg):
    """Row p = weights that evaluate the least-squares degree-`deg` fit of the
    window at window position p. Same construction as sgsmooth.cpp's sg_coeff,
    including the asymmetric border rows."""
    win = 2 * width + 1
    xs = [i - width for i in range(win)]
    ata = [[sum(x ** (i + j) for x in xs) for j in range(deg + 1)]
           for i in range(deg + 1)]
    rows = []
    for p in range(win):
        w = [0.0] * win
        for k in range(win):
            rhs = [xs[k] ** i for i in range(deg + 1)]
            coef = _solve(ata, rhs)
            w[k] = sum(coef[i] * (xs[p] ** i) for i in range(deg + 1))
        rows.append(w)
    return rows


_SG = sg_coeffs(SG_WINDOW, SG_ORDER)


def sg_smooth(v):
    n = len(v)
    win = 2 * SG_WINDOW + 1
    if n < win + 1:
        return list(v)
    r = [0.0] * n
    for i in range(SG_WINDOW):
        r[i] = sum(_SG[i][j] * v[j] for j in range(win))
        r[n - 1 - i] = sum(_SG[win - 1 - i][j] * v[n - win + j] for j in range(win))
    for i in range(n - win + 1):
        r[i + SG_WINDOW] = sum(_SG[SG_WINDOW][j] * v[i + j] for j in range(win))
    return r


def differentiate(y, h):
    n = len(y)
    if n < 2:
        return [0.0] * n
    o = [0.0] * n
    o[0] = (y[1] - y[0]) / h
    o[-1] = (y[-1] - y[-2]) / h
    for i in range(1, n - 1):
        o[i] = (y[i + 1] - y[i - 1]) / (2 * h)
    return o


def sharpness(y, h, start_min=DETECTION_MARGIN_MIN):
    """Peak of the derivative after detection_margin_time - the quantity
    min_sharpness is compared against (Algo.cpp predict_outcome, test 3)."""
    d = differentiate(sg_smooth(y), h)
    i0 = 0
    while i0 < len(y) and i0 * h < start_min:
        i0 += 1
    seg = d[i0 + 1:] or d[-1:]
    return max(seg)


# ---------------------------------------------------------------- helpers
def moving_average(v, k):
    n = len(v)
    half = k // 2
    out = []
    for i in range(n):
        lo, hi = max(0, i - half), min(n, i + half + 1)
        out.append(sum(v[lo:hi]) / (hi - lo))
    return out


def linreg(xs, ys):
    n = len(xs)
    mx, my = st.mean(xs), st.mean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    sxy = sum((xs[i] - mx) * (ys[i] - my) for i in range(n))
    syy = sum((y - my) ** 2 for y in ys)
    slope = sxy / sxx if sxx else 0.0
    r = sxy / math.sqrt(sxx * syy) if sxx and syy else 0.0
    return slope, my - slope * mx, r


def white_sd(v):
    """Drift-free noise: sd of successive differences / sqrt(2). A slow ramp
    cancels out of a difference, so this measures the per-round term only."""
    d = [v[i + 1] - v[i] for i in range(len(v) - 1)]
    return st.pstdev(d) / math.sqrt(2) if len(d) > 1 else 0.0


# ---------------------------------------------------------------- loading
def load(path, interval_s):
    """Returns (rows, slopes, label). slopes is None when already calibrated."""
    if path.endswith(".json"):
        d = json.load(open(path, encoding="utf-8"))
        rows = [[float(x) for x in s.split(",") if x.strip()]
                for s in d["amplification"]]
        label = "%s %s" % (d.get("id_device", "?"), d.get("time", ""))
        return rows, d.get("slopes"), label, d
    rows = [[float(x) for x in l.split(",") if x.strip()]
            for l in open(path, encoding="utf-8") if l.strip()]
    return rows, None, path, {}


# ---------------------------------------------------------------- sections
def section_levels(rows, slopes, out):
    print("")
    print("== 1. LEVELS ==  the stored value is the SUM of 8 reads, in a uint16_t")
    print("   slot     min     max    mean   headroom   per-read")
    worst = 0.0
    for i, a in enumerate(rows):
        mx = max(a)
        worst = max(worst, mx)
        print("   %4d %7.0f %7.0f %7.1f %8.0fx %10.1f"
              % (i, min(a), mx, st.mean(a), SUM_CEILING / mx, mx / REPEATS))
    print("   worst stored value %.0f -> %.0fx below the %d ceiling;"
          " per-read ceiling is %d counts"
          % (worst, SUM_CEILING / worst, SUM_CEILING, SUM_CEILING // REPEATS))
    out["max_stored"] = worst
    if slopes:
        lv = [st.mean(a) for a in rows]
        print("   slopes  %.3f .. %.3f  (spread %.2fx)"
              % (min(slopes), max(slopes), max(slopes) / min(slopes)))
        print("   levels  %.0f .. %.0f  (spread %.2fx)"
              % (min(lv), max(lv), max(lv) / min(lv)))
        print("   -> a level spread far wider than the slope spread means the")
        print("      channel-to-channel difference is an OFFSET (stray light), not")
        print("      sensitivity. origins[] is where that belongs, and it is all zeros.")


def section_noise_law(rows, skip, out):
    print("")
    print("== 2. NOISE LAW ==  does sigma scale with the signal?")
    print("   slot     mean   sigma(drift-free)     rel%   sigma/sqrt(mean)")
    means, sds = [], []
    for i, a in enumerate(rows):
        w = a[skip:]
        m, s = st.mean(w), white_sd(w)
        means.append(m)
        sds.append(s)
        print("   %4d %8.1f %18.3f %8.2f%% %17.3f"
              % (i, m, s, 100 * s / m, s / math.sqrt(m)))
    slope, icept, r = linreg(means, sds)
    print("")
    print("   fit: sigma = %.5f*mean + %.3f   (r = %+.3f)" % (slope, icept, r))
    print("   additive       predicts slope 0")
    print("   multiplicative predicts slope %.5f, intercept 0"
          % (st.mean(sds) / st.mean(means)))
    print("   shot noise     predicts sigma/sqrt(mean) constant")
    if abs(r) < 0.4:
        print("   -> ADDITIVE (quantisation / readout limited).")
        print("      Optical gain k then multiplies the signal and leaves sigma where")
        print("      it is: SNR improves by k, NOT by sqrt(k).")
    else:
        print("   -> level-dependent; re-read the columns above before assuming gain helps.")
    out["noise_sigma_raw"] = st.mean(sds)
    out["noise_law_r"] = r


def section_sync_steps(rows, h, out):
    print("")
    print("== 3. SYNCHRONOUS STEPS ==  every channel moving together in one round")
    n = min(len(a) for a in rows)
    thr = 3 * st.mean([white_sd(a) for a in rows])
    found = 0
    for k in range(n - 1):
        step = [a[k + 1] - a[k] for a in rows]
        if not (all(s > thr for s in step) or all(s < -thr for s in step)):
            continue
        found += 1
        old = [a[k] for a in rows]
        new = [a[k + 1] for a in rows]
        add = st.mean([new[i] - old[i] for i in range(len(old))])
        mul = st.mean([new[i] / old[i] for i in range(len(old))])
        res_add = st.pstdev([new[i] - old[i] - add for i in range(len(old))])
        res_mul = st.pstdev([new[i] - mul * old[i] for i in range(len(old))])
        print("   round %d->%d (t=%.1f min): mean step %+.0f (%+.0f%%)"
              % (k, k + 1, k * h, add, 100 * (mul - 1)))
        print("     additive model  residual sd %6.1f  (offset %+.1f)" % (res_add, add))
        print("     multiplicative  residual sd %6.1f  (factor %.3f)" % (res_mul, mul))
        if res_add <= res_mul:
            print("     -> ADDITIVE: a dark reading (LED off) would measure this.")
        else:
            print("     -> MULTIPLICATIVE: needs a lit reference; a dark one misses it.")
    if not found:
        print("   none (threshold |step| > %.1f on all %d channels)" % (thr, len(rows)))
    out["sync_steps"] = found


def section_common_mode(rows, slopes, skip, out):
    print("")
    print("== 4. COMMON MODE ==  how much of the per-round wobble is shared")
    cal = [[v / (slopes[i] if slopes else 1.0) for v in a[skip:]]
           for i, a in enumerate(rows)]
    n = min(len(a) for a in cal)
    cal = [a[:n] for a in cal]
    tr = [moving_average(a, 21) for a in cal]
    lo, hi = 10, n - 10
    fast = [[cal[i][j] - tr[i][j] for j in range(lo, hi)] for i in range(len(cal))]
    m = hi - lo
    cm = [st.mean([f[j] for f in fast]) for j in range(m)]
    var_slot = st.mean([st.pvariance(f) for f in fast])
    frac = st.pvariance(cm) / var_slot if var_slot else 0.0
    print("   per-slot fast sd   %.3f" % math.sqrt(var_slot))
    print("   common-mode sd     %.3f" % st.pstdev(cm))
    print("   -> the common mode is %.0f%% of the per-round variance." % (100 * frac))
    print("      That share is a machine-level disturbance - not photon noise and not")
    print("      biology. It is exactly the part a per-round reference could remove.")
    out["common_mode_frac"] = frac
    return cal, cm, lo, hi


def section_sharpness(cal, cm, lo, hi, h, results, out):
    print("")
    print("== 5. SHARPNESS ==  the statistic min_sharpness (%.1f) is compared against"
          % MIN_SHARPNESS)
    b0 = int(BASELINE_START_MIN / h)
    b1 = int((BASELINE_START_MIN + BASELINE_RANGE_MIN) / h) + 1
    print("   slot   as measured   minus per-round common mode   verdict")
    before, after = [], []
    for i, a in enumerate(cal):
        base = st.mean(a[b0:b1]) if b1 > b0 else 0.0
        s0 = sharpness([v - base for v in a], h)
        corr = list(a)
        for j in range(lo, hi):
            corr[j] -= cm[j - lo]
        cb = st.mean(corr[b0:b1]) if b1 > b0 else 0.0
        s1 = sharpness([v - cb for v in corr], h)
        before.append(s0)
        after.append(s1)
        tag = results[i] if results and i < len(results) else ""
        print("   %4d %13.2f %29.2f   %s" % (i, s0, s1, tag))
    print("")
    print("   range %.2f..%.2f  ->  %.2f..%.2f   (median %.2f -> %.2f)"
          % (min(before), max(before), min(after), max(after),
             st.median(before), st.median(after)))
    out["sharp_before"] = st.median(before)
    out["sharp_after"] = st.median(after)


def section_noise_reference(sigma_cal, h, rounds, channels, out, trials=4000):
    print("")
    print("== 6. NOISE-ONLY REFERENCE ==  sharpness of a FLAT curve plus gaussian noise")
    print("   Nothing is amplifying here. A threshold is only worth what it costs in")
    print("   false calls, so the last two columns are the ones that decide whether a")
    print("   candidate min_sharpness can be lowered. 'per run' assumes %d independent"
          % channels)
    print("   channels, so P_run = 1 - (1 - P_ch)^%d." % channels)
    rng = random.Random(20260910)
    print("   sigma   mean    p99   p99.9    max    P(>8.0)/ch   P(>4.0)/ch   P(>4.0)/run")
    for k in (1.0, 0.71, 0.5, 0.35, 0.25):
        s = sigma_cal * k
        vals = sorted(sharpness([rng.gauss(0, s) for _ in range(rounds)], h)
                      for _ in range(trials))
        p8 = sum(1 for x in vals if x > MIN_SHARPNESS) / trials
        p4 = sum(1 for x in vals if x > 4.0) / trials
        prun = 1.0 - (1.0 - p4) ** channels
        note = "  <- today" if k == 1.0 else ""
        print("   %5.2f  %5.2f %6.2f %6.2f %6.2f %11.3f%% %12.3f%% %12.2f%%%s"
              % (s, st.mean(vals), vals[int(0.99 * trials)], vals[int(0.999 * trials)],
                 vals[-1], 100 * p8, 100 * p4, 100 * prun, note))
    print("")
    print("   min_sharpness is %.1f today. 4.0 is the bottom of the qPCR-confirmed ASF"
          % MIN_SHARPNESS)
    print("   range (4.0-8.4, docs/history/2026-08-21) - the reason anyone wants it")
    print("   lowered. Read P(>4.0)/run as the price of doing that at each noise level.")
    out["noise_ref_sigma"] = sigma_cal


def section_budget():
    print("")
    print("== 7. INTEGRATION BUDGET ==  same total integration, split differently")
    print("   The stored value is N reads summed, each read proportional to ALS_IT.")
    print("   Hold N*IT constant and THE NUMBER IS UNCHANGED - slopes, origins,")
    print("   min_increase and min_sharpness all keep their meaning. Only the noise")
    print("   moves, because the additive term of section 2 is per CONVERSION:")
    print("       noise of a sum of N reads = sqrt(N) * sigma0")
    print("")
    print("   Dwell floor is (N+1)*IT: the first FULLY lit conversion lands at 2*IT")
    print("   (see the settle rule below) and each further read is one IT later.")
    print("   TICK adds the 20 ms SensorTask quantisation per read plus ~100 ms of")
    print("   mux/LED switching - an estimate, to be replaced by measurement #4.")
    print("")
    print("    N x IT      scale   noise    vs today   dwell   +tick   x10 ch")
    for n, it in ((8, 100), (4, 200), (2, 400), (1, 800)):
        noise = math.sqrt(n)
        dwell = (n + 1) * it
        real = dwell + n * 20 + 100
        fits = "ok" if real * 10 <= 20000 else "DOES NOT FIT a 20 s round"
        print("    %d x %3d ms  %5.0fx   %5.2f s0   %6.2fx  %6d  %6d  %5.1f s  %s"
              % (n, it, n * it / 100.0, noise, math.sqrt(REPEATS) / noise,
                 dwell, real, real * 10 / 1000.0, fits))
    print("")
    print("   The settle rule is 2*IT: when the LED turns on, the conversion already")
    print("   in flight is contaminated, so the first FULLY lit conversion completes")
    print("   at 2*IT. LED_DELAY_TIME is 200 ms and ALS_IT is 100 ms today - exactly")
    print("   on that boundary, which is the only reason the first read is clean.")
    print("   Raising IT without raising the settle biases every first read low,")
    print("   silently. calib_sensor() does not wait at all - see the plan, C10.")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", help="uploaded payload .json, or a .txt of calibrated rows")
    ap.add_argument("--interval", type=float, default=20.0,
                    help="seconds per round (default 20)")
    ap.add_argument("--skip", type=int, default=6,
                    help="rounds to drop from the head, optical settling (default 6)")
    ap.add_argument("--calibrated", action="store_true",
                    help="input is already calibrated; do not divide by slopes")
    ap.add_argument("--json", action="store_true", help="also emit the summary as JSON")
    a = ap.parse_args()

    rows, slopes, label, doc = load(a.path, a.interval)
    if a.calibrated:
        slopes = None
    h = a.interval / 60.0
    results = doc.get("result")
    rounds = min(len(r) for r in rows)

    print("=== optical read quality: %s ===" % label)
    print("    %d channels x %d rounds @ %.0f s/round  (%.1f min)"
          % (len(rows), rounds, a.interval, rounds * a.interval / 60.0))

    out = {}
    section_levels(rows, slopes, out)
    section_noise_law(rows, a.skip, out)
    section_sync_steps(rows, h, out)
    cal, cm, lo, hi = section_common_mode(rows, slopes, a.skip, out)
    section_sharpness(cal, cm, lo, hi, h, results, out)
    sig = st.mean([white_sd(c) for c in cal])
    section_noise_reference(sig, h, rounds - a.skip, len(rows), out)
    section_budget()

    print("")
    print("== SUMMARY ==")
    print("   per-round noise (calibrated)   %.2f" % sig)
    print("   common mode share              %.0f%%" % (100 * out.get("common_mode_frac", 0)))
    print("   sharpness on these curves      %.2f -> %.2f without the common mode"
          % (out.get("sharp_before", 0), out.get("sharp_after", 0)))
    print("   min_sharpness threshold        %.2f" % MIN_SHARPNESS)
    if a.json:
        print("")
        print(json.dumps(out, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
