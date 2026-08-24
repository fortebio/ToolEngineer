/* ============================================================
 * RAPID live dashboard - Home / Result / Setting, fed by SSE.
 *   event "home"         -> home state (temps, status, notify, buttons, phase)
 *   event "new_readings" -> live chart point, scalar per channel {"#1":num,...}
 *
 * Home has three modes driven by status.phase:
 *   default  -> full temperature cards
 *   waitamp  -> slot-naming card; Start (red) locked until names confirmed
 *   running  -> after confirm (or once amplifying): live chart + compact temps
 * Result tab is a read-back of the stored run (table + /curve chart).
 * ============================================================ */

/* Inline SVG icons (no emoji/font dependency, encoding-safe) */
var SVG_OPEN =
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">';
var ICON_CLOCK =
    SVG_OPEN +
    '<circle cx="12" cy="12" r="9"/><polyline points="12 7 12 12 15.5 14"/></svg>';
var ICON_THERMO =
    SVG_OPEN +
    '<path d="M14 14.76V4.5a2.5 2.5 0 0 0-5 0v10.26a4.5 4.5 0 1 0 5 0z"/></svg>';

var SLOTS = 10;
var minPerRound = 1; // minutes per acquisition round (from /curve)

/* ---------- Bottom-nav screen switching ---------- */
var navItems = document.querySelectorAll(".nav-item");
navItems.forEach(function(btn) {
    btn.addEventListener("click", function() {
        cancelBackToHome(); // user chose a tab -> drop any pending auto-return-to-Home
        var name = btn.getAttribute("data-screen");
        document.querySelectorAll(".screen").forEach(function(s) {
            s.classList.toggle("active", s.id === "screen-" + name);
        });
        navItems.forEach(function(b) {
            b.classList.toggle("active", b === btn);
        });
        if (name === "result") {
            loadResultSlots();
            if (resultShown) loadCurve(resultView); // refresh stored curve on re-entry
            if (resultView.chart) resultView.chart.reflow();
        }
        if (name === "home" && homeChartOn && homeView.chart)
            homeView.chart.reflow();
        if (name === "setting") {
            showMenu();
            loadConfig().then(renderSetMenu); // forms are filled from the device's own config
        }
    });
});

document.getElementById("reloadBtn").addEventListener("click", function() {
    location.reload();
});

/* ---------- Button controls: click sends a command to the device ----------
 * SSE is one-way (server->client), so a press is a separate POST. A chip marked
 * ".locked" (Start before names are confirmed) swallows the click - web-only
 * gate; the physical button on the device still works. */
document.querySelectorAll(".btn-chip").forEach(function(chip) {
    chip.addEventListener("click", function() {
        if (chip.classList.contains("locked")) return;
        var btn = chip.getAttribute("data-btn");
        // Web "Amplification" (red chip at the idle start screen) opens the naming gate
        // (name before heating) instead of pressing RED - which, like the physical button,
        // would heat immediately. Every other state sends the real button.
        if (btn === "red" && curPhase === "idle") btn = "ampname";
        fetch("/control?btn=" + encodeURIComponent(btn), { method: "POST" })
            .then(function(r) {
                if (!r.ok) console.error("control failed:", r.status);
            })
            .catch(function(err) {
                console.error("control error:", err);
            });
    });
});

/* ---------- Connection status badge ---------- */
function setStatus(connected) {
    document.getElementById("status").className =
        "status " + (connected ? "on" : "off");
    document.getElementById("statusText").textContent = connected ?
        "Online" :
        "Offline";
}

/* ---------- Home screen rendering ---------- */
function txt(id, v) {
    var el = document.getElementById(id);
    if (el) el.textContent = v;
}

var lastPhase = ""; // previous status.phase, to detect run start
var curPhase = ""; // latest status.phase, so the Confirm handler knows the context
var confirmed = false; // user confirmed slot names -> Start unlocked, chart shown
var namingBuilt = false; // naming table populated for the current waitamp session
var homeChartOn = false; // home is currently in chart (running) mode
var homeCurveOn = false; // ...and that chart may be filled from /curve (run has data)
var prevBusy = false; // last SSE status.busy, to detect the busy->idle edge (Result re-arm)
/* The device ID as the MACHINE reports it (SSE home.device = the global id_device). The ID is
 * stored TWICE on the device - id_device (EEPROM 170) and parameter.device_id (EEPROM 512, the
 * "device ID" key in /config) - with different defaults ("RAPIDPlus" vs "RPL"), so on a unit
 * that never had this card saved they hold DIFFERENT strings. id_device is the operational one:
 * it is what the header shows, what the Google Sheet / ERP upload sends, and what /deviceid
 * writes. Keep it here so the Setting card can DISPLAY the same store it WRITES. */
var deviceIdNow = "";

function renderHome(d) {
    if (d.device) {
        deviceIdNow = d.device;
        txt("deviceName", d.device);
        txt("setDevice", d.device);
    }
    if (d.company) {
        txt("companyName", d.company);
        txt("setCompany", d.company);
    }
    // Live network state, so switching WiFi (or falling to SoftAP) is reflected on the web
    // within a second - the machine and the dashboard stay in sync. Kept in a global so the
    // WiFi panel can mark the active saved network too.
    if (d.net) {
        curNet = d.net;
        txt(
            "setNet",
            d.net.ap ?
            "SoftAP (" + (d.net.ssid || "hotspot") + ")" :
            d.net.ssid || "(not connected)",
        );
        txt("setIp", d.net.ip || "0.0.0.0");
    }

    var t = d.temps || {};
    // full cards
    txt("tLysis", fmtTemp(t.lysis));
    txt("tAmpLeft", fmtTemp(t.ampLeft));
    txt("tAmpRight", fmtTemp(t.ampRight));
    txt("tTopLeft", fmtTemp(t.topLeft));
    txt("tTopRight", fmtTemp(t.topRight));
    // compact strip (mirror)
    txt("cLysis", fmtTemp(t.lysis));
    txt("cAmpLeft", fmtTemp(t.ampLeft));
    txt("cAmpRight", fmtTemp(t.ampRight));
    txt("cTopLeft", fmtTemp(t.topLeft));
    txt("cTopRight", fmtTemp(t.topRight));

    var s = d.status || {};
    var phase = s.phase || "";
    txt("stateTitle", s.title || "Idle");
    txt("stateSub", s.subtitle || "");
    // heater phase -> thermometer icon + amber accent, otherwise clock
    var isHeat = phase === "heater";
    document.getElementById("stateIcon").innerHTML = isHeat ?
        ICON_THERMO :
        ICON_CLOCK;
    var stateBanner = document.querySelector(".banner.state");
    if (stateBanner) stateBanner.classList.toggle("heat", isHeat);

    // ---- naming / chart gate state machine ----
    // Two naming points:
    //   "waitname" (amp flow) = name the slots BEFORE heating; Confirm starts the preheat.
    //   "waitamp"  (lysis flow) = name after heating; Confirm unlocks Start.
    // `confirmed` must survive the heating in between, so it is reset only on a fresh
    // cycle (back to idle), not on every non-naming phase.
    curPhase = phase;
    // reviewStoredRun() is one-shot on Result-tab entry and gives up (409) if the device was
    // busy (heating/preheat/calib/OTA - these report phase "heater" etc., NOT "amplification",
    // so the entry guard does not stop the doomed attempt). Re-arm on the busy->idle EDGE while
    // the Result tab is open, so the stored run loads itself (table + chart) without needing the
    // physical WHITE key. Edge-triggered, not level: no per-second /slots spam, and no loop on an
    // empty-EEPROM machine that never goes ready. Never resurrects a new run: during amplification
    // s.busy is true (blocks this), and loadResultSlots only reviews when /slots ready===false.
    if (
        prevBusy &&
        !s.busy &&
        document.getElementById("screen-result").classList.contains("active")
    )
        loadResultSlots();
    prevBusy = !!s.busy;
    if (phase === "amplification") confirmed = true;
    else if (phase === "idle") confirmed = false; // new cycle -> require naming again
    var naming = (phase === "waitname" || phase === "waitamp") && !confirmed;
    if (naming) {
        var lbl = document.getElementById("confirmNamesLabel");
        if (lbl)
            lbl.textContent =
            phase === "waitname" ? "Confirm & start heating" : "Confirm & start";
    }
    // Chart stays up through "finished" so the completed curve is still on screen
    // after the run ends. It only goes away on the WHITE button (device or web
    // "Return"), which leaves escreenFinished -> escreenRestart -> phase "idle".
    // The machine's own error table (RED on the finished screen, pressed here or on the device).
    // It is a PANE of the run view, not a mode of its own: the compact temperature strip and the
    // slot table stay exactly where they were, and only the chart is swapped out. Following the
    // device's state rather than offering a web-only toggle means whoever is at the machine and
    // whoever is holding the phone see the same thing.
    var errorTable = phase === "errortable";
    var chartMode =
        (phase === "waitamp" && confirmed) ||
        phase === "amplification" ||
        phase === "finished" ||
        errorTable;

    // populate the slot table once per session (avoid clobbering an open select).
    // Built for BOTH stages: naming before the run, and as the run's legend while it plots -
    // the table is what tells the operator which coloured curve is which sample.
    if ((naming || chartMode) && !namingBuilt) {
        namingBuilt = true;
        loadNamingSlots();
    }
    if (!naming && !chartMode) namingBuilt = false;

    // Does the device hold THIS run's curve yet? At "waitamp" the run has not started,
    // and the device buffer still holds the PREVIOUS run (it is overwritten round by
    // round once amplification runs, and /curve falls back to the last run's length
    // while COUNTER is 0). So a not-yet-started run must show an EMPTY chart.
    var curveReady = phase === "amplification" || phase === "finished";

    // Entering the naming stage or a new run -> clear the live chart, so a previous
    // run left on screen (or in the device buffer) never shows under a new one.
    if (
        ["waitname", "waitamp", "amplification"].indexOf(phase) >= 0 &&
        lastPhase !== phase
    )
        resetView(homeView);
    lastPhase = phase;

    setHomeMode(naming, chartMode, curveReady, errorTable);

    var n = d.notify;
    var nBox = document.getElementById("notify");
    if (n && n.show) {
        txt("notifyTitle", n.title || "");
        txt("notifySub", n.subtitle || "");
        nBox.classList.remove("hidden");
    } else {
        nBox.classList.add("hidden");
    }

    var b = d.buttons || {};
    dot("bGreen", b.green);
    dot("bRed", b.red);
    dot("bWhite", b.white);

    // Chips show what each button DOES in the current state (device-driven)
    var act = d.actions || {};
    chipLabel("bGreen", act.green);
    chipLabel("bRed", act.red);
    chipLabel("bWhite", act.white);
    // Start (red) stays locked until names are confirmed
    document.getElementById("bRed").classList.toggle("locked", naming);

    // Setting tab: lock every card while the device is busy, and follow the calibration
    // wizard's step. `busy` is device-sent - `phase` alone is not enough (calib/OTA/tube
    // waits all report "idle"). The server rejects a stale POST with 409 regardless.
    applySettingLock(!!s.busy, s.calib || "");

    // Outcome of our last queued settings write (see settleSave/awaitCfg).
    if (d.cfg) lastCfg = d.cfg;

    // Hide the bottom nav for the duration of a run (lysis / amplification) so the
    // operator stays on Home. NOT while calibrating: that wizard lives in the Setting
    // tab, so hiding the nav would strand the user inside it.
    applyRunNav(!!s.busy && !s.calib);
}

var navHidden = false;

function applyRunNav(hide) {
    if (hide === navHidden) return;
    navHidden = hide;
    document.body.classList.toggle("nonav", hide);
    // A run can start while the user is on Result/Setting. Taking the nav away there
    // would leave them on a tab they cannot leave, so send them Home first.
    if (
        hide &&
        !document.getElementById("screen-home").classList.contains("active")
    ) {
        var home = document.querySelector('.nav-item[data-screen="home"]');
        if (home) home.click();
    }
}

// Toggle the Home layout between full-temps, naming, and running/chart modes.
// curveReady = the device holds this run's curve (see renderHome); only then do we
// pull /curve, otherwise a fresh run would inherit the previous run's points.
var homeErrOn = false;

function setHomeMode(naming, chartMode, curveReady, errorTable) {
    /* The slot table serves two stages. Before the run it is the naming form (hint + Confirm
       button). While the chart is up it stays as the run's legend - same coloured dots as the
       curves, so the operator can read which sample is which and toggle series - but the
       naming chrome goes away, since the run has already started. */
    show("namingCard", naming || chartMode);
    var title = document.getElementById("namingTitle");
    if (title) title.textContent = naming ? "Name the samples" : "Samples";
    var hint = document.querySelector("#namingCard .naming-hint");
    if (hint) hint.classList.toggle("hide", !naming);
    show("confirmNamesBtn", naming);
    // One slot, two panes: the chart, or the device's error table.
    show("homeChartCard", chartMode && !errorTable);
    show("homeErrorCard", errorTable);
    show("tempFullLysis", !chartMode);
    show("tempFullAmp", !chartMode);
    show("tempCompact", chartMode);
    // Fetch on the EDGE, not every frame: renderHome runs once a second and this table only
    // changes when a run ends.
    if (errorTable && !homeErrOn) loadErrors("homeErrorBody");
    homeErrOn = errorTable;
    var wantCurve = chartMode && !errorTable && curveReady;
    if (wantCurve && !homeCurveOn) loadCurve(homeView); // just became loadable
    homeCurveOn = wantCurve;
    homeChartOn = chartMode && !errorTable;
    if (homeChartOn && homeView.chart) homeView.chart.reflow();
}

function show(id, on) {
    var e = document.getElementById(id);
    if (e) e.classList.toggle("hide", !on);
}

// Label the chip with its action; dim it when the button does nothing here.
function chipLabel(id, label) {
    var el = document.getElementById(id);
    if (!el) return;
    var has = !!(label && label.length);
    /* No action in this state -> "-". Which physical button it is stays readable from the
       chip's own colour: .noact dims with grayscale(.35), light enough that green/red/white
       are still told apart, so spelling the name out would only add noise. */
    el.textContent = has ? label : "-";
    el.classList.toggle("noact", !has);
}

var DEG = String.fromCharCode(176); // degree sign built at runtime -> source stays ASCII
function fmtTemp(v) {
    return v === undefined || v === null ? "-" : v.toFixed(1) + DEG + "C";
}

function dot(id, on) {
    var el = document.getElementById(id);
    el.classList.toggle("on", !!on);
}

/* ---------- Charts (two views, same shape) ----------
 * homeView   -> live chart on Home, fed by new_readings, backfilled from /curve.
 * resultView -> stored-run chart on Result, drawn from /curve on demand.
 * Guarded: if Highcharts failed to load (offline with no bundle), chart is null
 * and everything else - SSE, Home, tables - still works. */
/* Series colours, hoisted out of buildSeries() so the slot tables can paint each row's
   toggle in the SAME colour as its line. Before this the table never told you which
   colour a slot was on the chart - you had to match by reading the legend. */
var SERIES_COLORS = [
    "#00BFFF",
    "#FF0000",
    "#FFD400",
    "#32CD32",
    "#D2691E",
    "#00CED1",
    "#9400D3",
    "#9ACD32",
    "#0000FF",
    "#FF69B4",
];

function buildSeries() {
    var colors = SERIES_COLORS;
    return colors.map(function(c, i) {
        return {
            name: "#" + (i + 1),
            type: "line",
            color: c,
            // ponytail: line only. hover marker stays - it is what anchors the tooltip.
            marker: { enabled: false },
        };
    });
}

function makeChart(divId) {
    return window.Highcharts ?
        new Highcharts.Chart({
            chart: { renderTo: divId },
            title: { text: undefined },
            credits: { enabled: false },
            xAxis: { title: { text: "Time (min)" }, labels: { enabled: true } },
            yAxis: {
                title: { text: null },
                min: 0,
                startOnTick: true,
                endOnTick: true,
                /* The scale starts at 0..200 and GROWS with the data - never clips, but never
                   collapses onto a tiny run either, so runs stay comparable at a glance.
                   Always exactly ten steps: the top is rounded up to a round number so every
                   gridline lands on a clean value instead of 57.3 / 68.76 / ...
                   (A plain tickInterval can't do this: it is a fixed step, so the number of
                   lines would change with the data.) */
                tickPositioner: function() {
                    var top = Math.max(this.dataMax || 0, 200); // floor of 200, then follow the data
                    var mag = Math.pow(10, Math.floor(Math.log(top) / Math.LN10) - 1);
                    top = Math.ceil(top / (mag * 5)) * (mag * 5); // round up to a tidy /10 value
                    var step = top / 10;
                    var out = [];
                    for (var i = 0; i <= 10; i++)
                        out.push(Math.round(i * step * 1e6) / 1e6);
                    return out;
                },
                labels: {
                    enabled: true,
                    /* Ten lines, but a label on every SECOND one: four labelled majors between the
                       ends, the rest read as minor gridlines. Labelling all ten crowds the axis,
                       especially on the 210px landscape chart. */
                    formatter: function() {
                        var p = this.axis.tickPositions;
                        var step = p && p.length > 1 ? p[1] - p[0] : 1;
                        return Math.round(this.value / step) % 2 === 0 ? this.value : "";
                    },
                },
            },
            series: buildSeries(),
        }) :
        null;
}

// A chart plus its per-run baseline state. The chart plots value - baseline, so every curve
// starts at zero and only real amplification lifts it off.
function makeView(divId, lastUpdateId) {
    return {
        chart: makeChart(divId),
        luId: lastUpdateId,
        baseSum: new Array(10).fill(0),
        baseCount: new Array(10).fill(0),
        baseline: new Array(10).fill(0),
        // RAW calibrated readings per channel, indexed by device round. The baseline is
        // subtracted at DRAW time, not stored subtracted: it is only final once its window has
        // passed, and pre-subtracting froze every early point against a baseline still moving.
        rawY: [
            [],
            [],
            [],
            [],
            [],
            [],
            [],
            [],
            [],
            []
        ],
        nextIdx: 0,
    };
}
var homeView = makeView("homeChart", "lastUpdateHome");
var resultView = makeView("resultChart", null);

/* ---------- Baseline window - THE TUNING KNOBS ----------
 * The optics and the solution take a couple of minutes to settle: the first readings climb
 * steeply from a cold start (measured on a real run: 311 -> 427 within five rounds) and only
 * then sit flat. Averaging from round 0 pulls the baseline BELOW that flat level, so the
 * settling ramp itself renders as a rising curve - every channel of that run showed a bump in
 * the first two minutes (up to 21.7 counts) and two channels that never amplify "lifted off"
 * at 1.3 min, which reads as a positive on a negative control.
 *
 * So the window starts AFTER the settle. Both values are in MINUTES and are meant to be
 * tuned: how long the optics take to settle is a property of the instrument, not of the code.
 * Measured on that run with 2 / 4 - bump in the first two minutes = 0 on all channels, the
 * flat channels never lift off, and the two real amplifications still lift at ~7 min. */
var BASELINE_START_MIN = 2; // skip this many minutes from the start of the run
var BASELINE_RANGE_MIN = 2; // then average this many minutes to get the baseline

// The window in ROUNDS. minPerRound comes from /curve, so this follows a re-configured
// "time per loop" without anyone having to convert the two numbers above.
function baselineWindow(total) {
    var per = minPerRound > 0 ? minPerRound : 1;
    var start = Math.round(BASELINE_START_MIN / per);
    var len = Math.max(1, Math.round(BASELINE_RANGE_MIN / per));
    // A run too short to reach the window still gets a baseline instead of a flat-zero chart.
    if (total !== undefined && start >= total) start = 0;
    return { start: start, len: len };
}

/* ---------- Savitzky-Golay smoothing (quadratic, order 2) ----------
 * SG fits a low-degree polynomial to a sliding window and takes the fitted centre,
 * so it smooths noise while preserving the sigmoid's shape and peak height far better
 * than a moving average. Order 2 matches the device's own sg_order.
 *
 * Symmetric quadratic weights for half-window m have the closed form
 *   w_j = 3(3m^2 + 3m - 1 - 5j^2) / ((2m+3)(2m+1)(2m-1)),  |j| <= m
 * They sum to 1, and collapse to identity at m = 1. Near the ends (and at the live
 * edge, where there are no future points yet) we shrink m to the largest symmetric
 * window that fits, so smoothing fades out gracefully instead of inventing values or
 * lagging. Recomputed on every new point, so earlier points refine as neighbours land. */
var SG_HALF = 3; // half-window -> 2*3+1 = 7 points (~140 s at 20 s/round)

function sgWeights(m) {
    var denom = (2 * m + 3) * (2 * m + 1) * (2 * m - 1);
    var w = [];
    for (var j = -m; j <= m; j++)
        w.push((3 * (3 * m * m + 3 * m - 1 - 5 * j * j)) / denom);
    return w;
}
// Precompute weights for every window we can actually use (m = 1..SG_HALF).
var SG_W = (function() {
    var t = [null]; // index by m; m = 0 is handled as identity
    for (var m = 1; m <= SG_HALF; m++) t[m] = sgWeights(m);
    return t;
})();

function sgSmooth(arr) {
    var n = arr.length,
        out = new Array(n);
    for (var i = 0; i < n; i++) {
        var m = Math.min(SG_HALF, i, n - 1 - i); // largest symmetric window that fits here
        if (m < 1) {
            out[i] = arr[i]; // endpoints: nothing to average against -> raw value
            continue;
        }
        var w = SG_W[m],
            acc = 0;
        for (var j = -m; j <= m; j++) acc += w[j + m] * arr[i + j];
        out[i] = acc;
    }
    return out;
}

// Redraw one channel: subtract the baseline, apply the noise floor, SG-smooth, plot.
// Floor BEFORE smoothing: sub-2 samples become real 0 input to the SG window, so the
// baseline sits flat at 0 instead of wobbling. Cost: the clamped zeros are averaged into the
// first rising points, so the curve leaves 0 slightly later, and the smoothed output is no
// longer clamped (yAxis min 0 hides any small negative dip).
// Until the baseline window has produced a sample there is nothing to subtract, so the
// channel draws flat at 0 - which is also what those settling rounds floor to afterwards.
function drawSmoothed(v, ch) {
    if (!v.chart || !v.chart.series[ch]) return;
    var raw = v.rawY[ch],
        base = v.baseline[ch],
        have = v.baseCount[ch] > 0,
        floored = new Array(raw.length);
    for (var i = 0; i < raw.length; i++) {
        // A gap in the run leaves a hole in the array; undefined - base is NaN, which Highcharts
        // would render as a break in the line.
        var y = have && raw[i] !== undefined ? raw[i] - base : 0;
        floored[i] = y < 0 ? 0 : y; // noise floor: values below 0 -> 0
    }
    var sm = sgSmooth(floored),
        pts = [];
    for (var j = 0; j < sm.length; j++) pts.push([j * minPerRound, sm[j]]);
    v.chart.series[ch].setData(pts, false);
}

function resetView(v) {
    for (var i = 0; i < 10; i++) {
        v.baseSum[i] = 0;
        v.baseCount[i] = 0;
        v.baseline[i] = 0;
        v.rawY[i] = [];
    }
    v.nextIdx = 0;
    if (v.chart) {
        v.chart.series.forEach(function(s) {
            s.setData([], false);
        });
        v.chart.redraw();
    }
}

// Redraw the WHOLE run from the device so a late/reconnected client isn't missing
// the stretch it wasn't listening for. Device is the source of truth for the curve.
function loadCurve(v) {
    if (!v.chart) return;
    fetch("/curve")
        .then(function(r) {
            return r.json();
        })
        .then(function(d) {
            if (!v.chart || !d || !d.series) return;
            minPerRound = (d.intervalMs || 60000) / 60000;
            for (var ch = 0; ch < 10 && ch < d.series.length; ch++) {
                var vals = d.series[ch] || [];
                // Window in rounds, clipped to what the run actually contains. Seeded into
                // baseSum/baseCount so live points arriving after the backfill keep accumulating
                // into the same window instead of restarting it.
                var w = baselineWindow(vals.length),
                    bsum = 0,
                    bn = 0;
                for (var k = w.start; k < Math.min(w.start + w.len, vals.length); k++) {
                    bsum += vals[k];
                    bn++;
                }
                v.baseSum[ch] = bsum;
                v.baseCount[ch] = bn;
                v.baseline[ch] = bn ? bsum / bn : 0;
                v.rawY[ch] = vals.slice(); // raw; drawSmoothed subtracts the baseline
                drawSmoothed(v, ch); // SG-smooth the whole channel, then setData
            }
            v.nextIdx = d.count || 0;
            applyNamesTo(v);
            applyVisTo(v);
            v.chart.redraw();
            // A review takes ~8 s on the device, and until it lands /curve returns 0 points. Show
            // Highcharts' loading label instead of a blank chart so the user waits for the redraw
            // (reviewStoredRun's poll calls loadCurve again when ready) instead of assuming it is
            // broken and reloading the page.
            if (!d.count && reviewing) v.chart.showLoading("Loading stored run…");
            else v.chart.hideLoading();
        })
        .catch(function() {});
}

function plotPoint(v, jsonValue) {
    if (!v.chart) return;
    var idx = jsonValue.i === undefined ? v.nextIdx : Number(jsonValue.i);
    Object.keys(jsonValue).forEach(function(k) {
        if (k.charAt(0) !== "#") return; // skip "i" / any meta key
        var ch = parseInt(k.slice(1), 10) - 1;
        if (!(ch >= 0 && ch < v.chart.series.length)) return;
        var y = Number(jsonValue[k]);
        if (isNaN(y)) return; // guard: SSE must send scalars, not arrays
        // Only rounds inside the window feed the baseline. Before it opens the channel has no
        // baseline at all and drawSmoothed holds it at 0 - which is what those settling rounds
        // floor to once the baseline does arrive, so nothing jumps when it does.
        var w = baselineWindow();
        if (idx >= w.start && idx < w.start + w.len) {
            v.baseSum[ch] += y;
            v.baseCount[ch]++;
            v.baseline[ch] = v.baseSum[ch] / v.baseCount[ch];
        }
        // Store the RAW reading; idx is the device round, so index by it (a re-sent round
        // overwrites, never appends a duplicate). Then re-smooth the whole channel: the SG window
        // over the newest points tightens as more arrive, and every earlier point is re-drawn
        // against the current baseline - so the window filling up corrects the run behind it
        // instead of leaving the first rounds subtracted by a half-formed average.
        v.rawY[ch][idx] = y;
        drawSmoothed(v, ch);
    });
    v.nextIdx = idx + 1;
    v.chart.redraw();
    if (v.luId) {
        var lu = document.getElementById(v.luId);
        if (lu) lu.textContent = new Date().toLocaleTimeString();
    }
}

/* ---------- Slots: naming (Home) + results (Result) ----------
 * Names persist on the device (POST /rename -> /slotnames.json). Visibility is a
 * per-browser view preference (localStorage), shared by both charts. */
var slotNames = new Array(10).fill(""); // disease per slot (fixed list)
var slotSamples = new Array(10).fill(""); // free-text sample label per slot

function slotVis() {
    try {
        return JSON.parse(localStorage.getItem("slotVis")) || [];
    } catch (e) {
        return [];
    }
}

function saveSlotVis(v) {
    try {
        localStorage.setItem("slotVis", JSON.stringify(v));
    } catch (e) {}
}

function isVisible(i) {
    return slotVis()[i] !== false; // default shown
}

// Cache names from a /slots response into slotNames; return the raw slot list.
/* Which build the device is running: "negative" = a shape-flagged Positive was already turned
 * Negative on the device; "flag" = the flag is advisory and the call stands. Read from /slots
 * rather than assumed, because the two builds are otherwise indistinguishable from the browser -
 * in the "negative" build a flagged well arrives as a plain "N" with nothing in that letter to
 * say why. Defaults to "flag" so an older device, which sends no shapeMode at all, is described
 * as annotating rather than overturning - the claim that is true of it. */
var shapeMode = "flag";

function ingestSlots(data) {
    var slots = (data && data.slots) || [];
    if (data && data.shapeMode) shapeMode = data.shapeMode;
    for (var i = 0; i < SLOTS; i++) {
        if (slots[i] && slots[i].name !== undefined)
            slotNames[i] = slots[i].name || "";
        if (slots[i] && slots[i].sample !== undefined)
            slotSamples[i] = slots[i].sample || "";
    }
    return slots;
}

/* The shape rule, described for a human. Arm A is a small step riding on a slow ramp; arm B is a
 * rise that never makes a step. The text names the arm AND quotes the two numbers behind it, so
 * an operator disputing a result can see what the machine measured rather than just that it
 * disapproved. Returns null when the well was not flagged. */
/* The note behind an F. The flag now means one thing - the well amplified, but too weakly or too
 * gradually to stand as a detection - so the text says that and quotes the numbers rather than
 * naming an arm of a rule that no longer exists.
 *
 * In the v2.4.3a build the same well arrives as "N" with the flag still set; no note is shown
 * there, because it would invite the operator to question a Negative that is already settled. */
function shapeNote(s) {
  if (!s || !s.shape || shapeMode === "negative") return null;
  var bits = [];
  if (s.rise !== null && s.rise !== undefined)
    bits.push("the rise took " + s.rise + " min");
  if (s.rate !== null && s.rate !== undefined)
    bits.push("fastest sustained climb " + s.rate + " RFU/min");
  return (
    "Flagged: this well did rise, but not steeply or strongly enough to report as a detection" +
    (bits.length ? " (" + bits.join("; ") + ")" : "") +
    ". Repeat this sample."
  );
}

/* Build the table ONCE, after /slots resolves - no empty pre-build. The old two-phase
 * "empty now, fill later" flashed a table you could interact with before the fetch
 * landed, and the fill then REBUILT it, wiping a selection made in that window (harmless
 * for a text field mid-typing, but a disease <select> is picked in one instant). */
function loadResultSlots() {
    fetch("/slots")
        .then(function(r) {
            return r.json();
        })
        .then(function(d) {
            buildTable("slotBody", ingestSlots(d), true);
            // ready=false means no run is cached in RAM (e.g. after a reboot), but the last
            // run's raw record still lives in EEPROM. Ask the device to reload+recompute it,
            // then refresh once it lands. Not while a new run is amplifying (that hides the
            // stale cache on purpose - do not resurrect the previous run over it).
            if (d && d.ready === false && curPhase !== "amplification")
                reviewStoredRun();
        })
        .catch(function() {
            buildTable("slotBody", [], true); // offline: still show the 10 empty rows
        });
}

/* After a reboot the RAM result cache is empty (/slots ready=false), but the last run's
 * raw 10x130 record persists in EEPROM. POST /reviewlast -> the device reloads and
 * recomputes it on SettingTask, then /slots ready flips true and /curve serves the
 * stored curve. Poll for it to land, then redraw. One attempt at a time. */
var reviewing = false;

function reviewStoredRun() {
    if (reviewing) return;
    reviewing = true;
    // ?go=1 is required by the device: the route is POST-registered but AsyncWebServer's
    // bitwise method match lets a bare GET (a link prefetch, a scanner) land in the same
    // handler, and an ~8 s EEPROM reload is not something a stray GET should trigger.
    fetch("/reviewlast?go=1", { method: "POST" })
        .then(function(r) {
            if (!r.ok) throw 0; // busy, or nothing to review -> stop
            var tries = 0;
            (function poll() {
                fetch("/slots")
                    .then(function(r) {
                        return r.json();
                    })
                    .then(function(d) {
                        if (d && d.ready) {
                            buildTable("slotBody", ingestSlots(d), true);
                            if (resultShown) loadCurve(resultView); // stored curve now available
                            reviewing = false;
                        } else if (++tries < 60) {
                            // MEASURED on the device: a review takes ~8.1 s (EEPROM record read +
                            // bResultGet re-running the detection over all 10 slots), NOT the "~10 ms"
                            // the old comment claimed. The old 15x200ms = 3 s budget expired long before
                            // the data landed, so the client gave up and the table/chart stayed empty -
                            // that is the "View Chart shows nothing until I reload the page" bug. 60x250ms
                            // = 15 s leaves headroom over the measured 8 s on a slower unit.
                            setTimeout(poll, 250);
                        } else {
                            reviewing = false; // gave up: EEPROM had no plausible stored run
                        }
                    })
                    .catch(function() {
                        reviewing = false;
                    });
            })();
        })
        .catch(function() {
            reviewing = false;
        });
}

function loadNamingSlots() {
    fetch("/slots")
        .then(function(r) {
            return r.json();
        })
        .then(function(d) {
            buildTable("namingBody", ingestSlots(d), false);
        })
        .catch(function() {
            buildTable("namingBody", [], false);
        });
}

// Build a slot table. withResults=true adds CT + result columns (Result tab);
// false is the name-only table on Home.
/* Slot name = one of a fixed set of shrimp diseases, chosen from a dropdown (tap the
 * field -> the disease options drop out). Empty = the slot keeps its #N default.
 * A <select> keeps every existing hook working (.slot-name value, onRename, the
 * cross-table sync in onRename, applyNamesTo) with no plumbing changes. */
var DISEASES = [
    "PCV",
    "PCM",
    "EHP",
    "EMS",
    "WSSV",
    "TPD",
    "PCT",
    "ISKNV",
    "PCS",
    "ASF p72",
    "ASF I177L",
    "ASF MGF",
];
var DASH = String.fromCharCode(8212); // em dash, built at runtime -> source stays ASCII

function makeDiseaseSelect(i, value) {
    var sel = document.createElement("select");
    sel.className = "slot-name disease-sel";
    sel.setAttribute("data-slot", i);
    [""].concat(DISEASES).forEach(function(d) {
        var o = document.createElement("option");
        o.value = d;
        o.textContent = d || DASH; // em dash (built at runtime) = "no disease -> #N default"
        if (d === value) o.selected = true;
        sel.appendChild(o);
    });
    // an unknown legacy free-text name (from before the fixed list) -> show it as an
    // extra option so it is not silently dropped, but it stays selectable.
    if (value && DISEASES.indexOf(value) < 0) {
        var o2 = document.createElement("option");
        o2.value = value;
        o2.textContent = value;
        o2.selected = true;
        sel.appendChild(o2);
    }
    sel.classList.toggle("assigned", !!value);
    sel.addEventListener("change", onRename);
    return sel;
}

/* Free-text sample label, sits beside the disease picker. Its own class (NOT .slot-name):
   the two fields flex against each other inside the cell, and this one wraps below on a
   narrow row instead of crushing the disease picker. */
function makeSampleInput(i, value) {
    var inp = document.createElement("input");
    inp.type = "text";
    inp.className = "sample-name";
    inp.setAttribute("data-slot", i);
    inp.maxLength = 32;
    inp.placeholder = "Sample";
    inp.setAttribute("aria-label", "Sample name for #" + (i + 1));
    inp.value = value || "";
    inp.addEventListener("change", onRenameSample);
    return inp;
}

function buildTable(tbodyId, slots, withResults) {
    var body = document.getElementById(tbodyId);
    if (!body) return;
    body.innerHTML = "";
    for (var i = 0; i < SLOTS; i++) {
        var s = slots[i] || {};
        var tr = document.createElement("tr");

        /* One "sample" cell instead of three columns (Show | Slot | Disease). The old layout
           put a row's checkbox and its own result badge nearly a screen apart on desktop, so
           reading one row meant crossing the whole table. Everything identifying the sample
           now sits together, and CT / Result stay as their own columns. */
        var sampleTd = document.createElement("td");
        sampleTd.className = "sample";
        // Flex lives on an inner div, NOT on the <td>: display:flex on a table cell drops its
        // table-cell role, and the row borders stop lining up across the columns.
        var sampleBox = document.createElement("div");
        sampleBox.className = "sample-cell";

        // Still a real <input type=checkbox>: keyboard, screen readers and the existing
        // onToggle/sync logic all keep working. Only its appearance changes - into the dot
        // carrying this slot's chart colour, which doubles as the show/hide control.
        var cb = document.createElement("input");
        cb.type = "checkbox";
        cb.className = "vis-dot";
        cb.checked = isVisible(i);
        cb.setAttribute("data-slot", i);
        cb.setAttribute("aria-label", "Show #" + (i + 1) + " on the chart");
        cb.style.setProperty("--series", SERIES_COLORS[i]);
        cb.addEventListener("change", onToggle);
        // Result reads colour | result | CT | sample, so the dot is its own leading column there.
        // The naming table has no verdict columns to order against, so it keeps the dot inline
        // with the #N it labels.
        if (!withResults) sampleBox.appendChild(cb);

        // #N belongs to the NAMING table only. There you are matching physical tubes to names and
        // the number IS the task; on Result it repeats what the row's fixed position already says,
        // and the 27px it took were the difference between a one-line row and a two-line one on a
        // 360px phone (measured 81px -> 43px per row).
        // Identity is not lost on Result: the table is always all ten rows in order, the coloured
        // dot carries the same series colour the chart legend labels #1..#10, and that dot's
        // aria-label still reads "Show #N on the chart" for a screen reader.
        if (!withResults) {
            var no = document.createElement("span");
            no.className = "slot-no";
            no.textContent = "#" + (i + 1);
            sampleBox.appendChild(no);
        }

        var cur = s.name !== undefined ? s.name || "" : slotNames[i] || "";
        sampleBox.appendChild(makeDiseaseSelect(i, cur));
        var curSample =
            s.sample !== undefined ? s.sample || "" : slotSamples[i] || "";
        sampleBox.appendChild(makeSampleInput(i, curSample));
        sampleTd.appendChild(sampleBox);

        if (withResults) {
            var visTd = document.createElement("td");
            visTd.className = "vis";
            visTd.appendChild(cb);

            var ctTd = document.createElement("td");
            ctTd.className = "ct";
            if (s.ct === null || s.ct === undefined)
                ctTd.innerHTML = '<span class="res-empty">-</span>'; // recede: most rows have no CT
            else ctTd.textContent = Number(s.ct).toFixed(1);

            var resTd = document.createElement("td");
            resTd.className = "res";
            var r = s.result || "";
            // F = Flagged (v2.4.3AT): the well amplified but its shape does not match a real
            // reaction. It is its own letter from the firmware, not a P wearing a decoration,
            // so it belongs in this list rather than in a marker beside the badge.
            if (r && "PNSEBF".indexOf(r) >= 0) {
                var badge = document.createElement("span");
                badge.className = "res-badge res-" + r;
                badge.textContent = r;
                // A letter alone says "F", not what F means. The explanation names the arm that
                // fired and quotes the two numbers behind it, so an operator disputing the call
                // can see what was measured rather than only that the machine disapproved.
                var note = shapeNote(s);
                if (note) {
                    badge.title = note;
                    badge.setAttribute("aria-label", note);
                    badge.setAttribute("role", "img");
                }
                resTd.appendChild(badge);
                // The rows that carry a detection are the ones the operator is looking for;
                // give them a quiet tint so they read first. F belongs here too - it is a well
                // that rose, and the whole point of the state is that it wants a human to look.
                if (r === "P" || r === "S" || r === "F") tr.classList.add("hit");
                if (r === "F") tr.classList.add("shaped");
            } else {
                resTd.innerHTML = '<span class="res-empty">-</span>';
            }

            // colour | result | CT | sample. The verdict leads because it is what the run is read
            // for; the sample identity trails because it is the thing you already know. Cell ORDER
            // is the DOM order here - a table cannot be reordered in CSS without breaking the
            // reading order for a screen reader, which would announce a verdict before saying which
            // sample it belongs to.
            tr.appendChild(visTd);
            tr.appendChild(resTd);
            tr.appendChild(ctTd);
            tr.appendChild(sampleTd);
        } else {
            tr.appendChild(sampleTd);
        }

        body.appendChild(tr);
    }
}

function cell(content) {
    var td = document.createElement("td");
    if (typeof content === "string") td.textContent = content;
    else td.appendChild(content);
    return td;
}

// The Name column is NOT sized in JS. table-layout:fixed plus the rem-sized value columns
// already hand it the exact remainder, at every width and root font size - that IS the
// clamp, for free. The old fitNameColumn() measured the widest disease string and pinned
// an inline px width with a hard 210px floor; 210 + 2 value columns overflowed the card on
// a 320px phone (312 in 254) and on the 820px desktop breakpoint (344 in 212), which is
// what made the Result table spill and the page scroll sideways. Do not reintroduce it.

function onToggle(e) {
    var i = Number(e.target.getAttribute("data-slot"));
    var v = slotVis();
    v[i] = e.target.checked;
    saveSlotVis(v);
    // keep the sibling table's checkbox in sync
    document
        .querySelectorAll('input[type="checkbox"][data-slot="' + i + '"]')
        .forEach(function(cb) {
            if (cb !== e.target) cb.checked = e.target.checked;
        });
    if (homeView.chart && homeView.chart.series[i])
        homeView.chart.series[i].setVisible(e.target.checked, true);
    if (resultView.chart && resultView.chart.series[i])
        resultView.chart.series[i].setVisible(e.target.checked, true);
    syncVisAll();
}

/* ---------- Show / hide EVERY slot ----------
 * Ten dots is a lot of clicking to isolate one curve or bring them all back. The header dot
 * drives all of them at once and reports the mix through the checkbox's own `indeterminate`
 * state, so "some hidden" needs no extra widget or wording. */
function setAllVis(on) {
    var v = slotVis();
    for (var i = 0; i < SLOTS; i++) v[i] = on;
    saveSlotVis(v);
    document.querySelectorAll("input.vis-dot[data-slot]").forEach(function(cb) {
        cb.checked = on;
    });
    // redraw ONCE per chart, not once per series: setVisible(.., true) on ten series is ten
    // full Highcharts redraws, and on the 120-point curve that is a visible stutter.
    [homeView, resultView].forEach(function(view) {
        if (!view.chart) return;
        for (var i = 0; i < SLOTS && i < view.chart.series.length; i++)
            view.chart.series[i].setVisible(on, false);
        view.chart.redraw();
    });
    syncVisAll();
}

// Reflect the per-slot state back onto the header dot. Called after any change and after a
// table render, so the header is never stale against the rows below it.
function syncVisAll() {
    var shown = 0;
    for (var i = 0; i < SLOTS; i++)
        if (isVisible(i)) shown++;
    document.querySelectorAll("input.vis-all").forEach(function(cb) {
        cb.checked = shown > 0;
        cb.indeterminate = shown > 0 && shown < SLOTS;
    });
}

document.querySelectorAll("input.vis-all").forEach(function(cb) {
    cb.addEventListener("change", function() {
        setAllVis(cb.checked);
    });
});
// At load, not only from applyVisTo(): that runs when a chart is drawn, and the stored state
// has to show on the header the moment the page opens - the markup ships `checked`, so a
// reload with slots hidden would otherwise claim everything is plotted. syncVisAll reads
// localStorage, not the DOM, so it is correct before a single row exists.
syncVisAll();

function onRename(e) {
    var i = Number(e.target.getAttribute("data-slot"));
    slotNames[i] = e.target.value;
    fetch("/rename?slot=" + i + "&name=" + encodeURIComponent(e.target.value), {
        method: "POST",
    }).catch(function(err) {
        console.error("rename failed", err);
    });
    // keep the sibling table's control in sync (value + assigned-chip styling)
    document
        .querySelectorAll('.slot-name[data-slot="' + i + '"]')
        .forEach(function(inp) {
            if (inp !== e.target) inp.value = e.target.value;
            inp.classList.toggle("assigned", !!e.target.value);
        });
    applyNamesTo(homeView);
    applyNamesTo(resultView);
}

// Sample label is web-only, persisted next to the disease (POST /rename?...&sample=).
// Chart series names stay tied to the disease (applyNamesTo) - the sample label is a
// per-slot identifier, not a legend entry.
function onRenameSample(e) {
    var i = Number(e.target.getAttribute("data-slot"));
    slotSamples[i] = e.target.value;
    fetch("/rename?slot=" + i + "&sample=" + encodeURIComponent(e.target.value), {
        method: "POST",
    }).catch(function(err) {
        console.error("sample rename failed", err);
    });
    // keep the sibling table's field (naming <-> result) in sync
    document
        .querySelectorAll('.sample-name[data-slot="' + i + '"]')
        .forEach(function(inp) {
            if (inp !== e.target) inp.value = e.target.value;
        });
}

function applyNamesTo(v) {
    if (!v.chart) return;
    for (var i = 0; i < 10; i++) {
        if (v.chart.series[i])
            v.chart.series[i].update({ name: (slotNames[i] || "").trim() || "#" + (i + 1) },
                false,
            );
    }
    v.chart.redraw();
}

function applyVisTo(v) {
    if (!v.chart) return;
    for (var i = 0; i < 10; i++) {
        if (v.chart.series[i]) v.chart.series[i].setVisible(isVisible(i), false);
    }
    syncVisAll(); // the header dot follows the same stored state the series just did
}

/* Confirm names. Two contexts:
 *   waitname (amp flow) -> names are set, so tell the device to START HEATING now
 *     (RED). Heating runs next; the naming card goes away.
 *   waitamp (lysis flow) -> heating already done; just unlock Start + reveal the chart.
 * Blank slots fall back to #1-#10 inside applyNamesTo. */
var confirmBtn = document.getElementById("confirmNamesBtn");
if (confirmBtn)
    confirmBtn.addEventListener("click", function() {
        confirmed = true;
        applyNamesTo(homeView);
        if (curPhase === "waitname") {
            // advance the device: RED here = "Confirm & heat" (starts the 67C preheat).
            // renderHome will show the heating view next tick (confirmed, not naming/chart).
            fetch("/control?btn=red", { method: "POST" }).catch(function() {});
        } else {
            // waitamp: naming off, chart on but NOT loadable (run has not started, so the
            // chart opens empty instead of inheriting the previous run in the device buffer).
            setHomeMode(false, true, false); // renderHome reaffirms next tick
            document.getElementById("bRed").classList.remove("locked");
        }
    });

/* Result tab: the stored-run chart and the sensor-error table share one slot in the layout.
 * They answer the same question about the same run from two sides, so showing one hides the
 * other rather than stacking a second full-height card under it. */
var resultShown = false;

function showResultPane(which) {
    var chart = document.getElementById("resultChartCard");
    var errs = document.getElementById("resultErrorCard");
    chart.classList.toggle("hide", which !== "chart");
    errs.classList.toggle("hide", which !== "errors");
    if (which === "chart") {
        resultShown = true;
        loadCurve(resultView); // draw the whole stored run
        // Highcharts sizes to a container that was display:none until a moment ago.
        if (resultView.chart) resultView.chart.reflow();
    } else {
        loadErrors();
    }
}

/* GET /errors -> the same per-slot table the machine draws (RED on the finished screen).
 * `ready` false means there is no run to report on, which is NOT the same as "a run with no
 * errors" - saying "no errors" for an empty device would be a clean bill of health nobody
 * earned. */
function loadErrors(boxId) {
    // Two callers, one renderer: the Result tab's "Error table" button and Home, which mirrors
    // the device whenever it is showing this table itself.
    var box = document.getElementById(boxId || "resultErrorBody");
    if (!box) return;
    box.textContent = "Loading...";
    fetch("/errors", { cache: "no-store" })
        .then(function(r) {
            return r.json();
        })
        .then(function(d) {
            box.innerHTML = "";
            if (!d || !d.ready) {
                box.appendChild(el("p", "f-hint", "No stored run to report on yet."));
                return;
            }
            var rows = d.slots || [];
            var bad = rows.filter(function(s) {
                return s && s.code !== null && s.code !== undefined;
            }).length;

            var head = el(
                "p",
                "err-summary" + (bad ? " err-summary-bad" : ""),
                bad ?
                bad + " of " + rows.length + " channels reported a sensor error" :
                "No sensor errors in the last run",
            );
            box.appendChild(head);

            var t = el("table", "err-table");
            var thead = el("thead");
            var htr = el("tr");
            ["Slot", "Code", "Detail"].forEach(function(h) {
                htr.appendChild(el("th", null, h));
            });
            thead.appendChild(htr);
            t.appendChild(thead);
            var tb = el("tbody");
            for (var i = 0; i < rows.length; i++) {
                var s = rows[i] || {};
                var tr = el("tr");
                if (s.code !== null && s.code !== undefined) tr.className = "err-row";
                tr.appendChild(el("td", "err-slot", "#" + (i + 1)));
                // The device prints this same 4-digit code, so an operator can read one screen
                // against the other. DASH (em dash) for "nothing wrong here", matching the
                // machine's own "----".
                // PAD TO 4: the TFT prints it with sprintf("%04d") (errorCheck.cpp), so a code whose
                // module digit is 0 shows as "0102" there and showed as "102" here - the same number
                // read as two different codes, which is exactly what cross-reading is supposed to
                // prevent.
                tr.appendChild(
                    el(
                        "td",
                        "err-code",
                        s.code === null || s.code === undefined ?
                        DASH :
                        String(s.code).padStart(4, "0"),
                    ),
                );
                tr.appendChild(el("td", "err-text", s.text || ""));
                tb.appendChild(tr);
            }
            t.appendChild(tb);
            box.appendChild(t);
        })
        .catch(function() {
            box.innerHTML = "";
            box.appendChild(el("p", "f-hint", "Could not read the error table."));
        });
}

var viewBtn = document.getElementById("viewChartBtn");
if (viewBtn)
    viewBtn.addEventListener("click", function() {
        showResultPane("chart");
    });
var errBtn = document.getElementById("viewErrorsBtn");
if (errBtn)
    errBtn.addEventListener("click", function() {
        showResultPane("errors");
    });

/* ============================================================
 * SETTING tab: 7 cards over ONE /config endpoint.
 *
 * The device applies only the JSON keys present, so each card just POSTs the subset it
 * owns. Forms are DECLARED below and rendered by one generic renderer - hand-writing
 * seven forms (and seven JSON bodies) is how the shapes drift apart. The exact key
 * strings and nesting are the firmware's contract (ForteSetting::JsonDataConfig).
 *
 * Client-side limits mirror the server's, but the SERVER is the real guard: it enforces
 * them again and rejects a busy device with 409. These heaters are physical.
 * ============================================================ */
var SVG_CHEV = SVG_OPEN + '<polyline points="9 18 15 12 9 6"/></svg>';

function ico(d) {
    return SVG_OPEN + d + "</svg>";
}

var CARDS = [{
        id: "wifi",
        title: "WiFi",
        desc: "Scan and join a network",
        custom: "wifi",
        icon: ico(
            '<path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><line x1="12" y1="20" x2="12.01" y2="20"/>',
        ),
    },
    // {
    //   id: "id",
    //   title: "Device ID",
    //   desc: "Change the machine id",
    //   custom: "id",
    //   icon: ico(
    //     '<rect x="3" y="4" width="18" height="16" rx="2"/><circle cx="9" cy="10" r="2"/><path d="M15 8h3M15 12h3M5 17c1-2 5-2 6 0"/>',
    //   ),
    // },
    {
        id: "profile",
        title: "Profile Configuration",
        desc: "Lysis / amplification temperature and time",
        icon: ico(
            '<path d="M14 14.76V4.5a2.5 2.5 0 0 0-5 0v10.26a4.5 4.5 0 1 0 5 0z"/>',
        ),
        // Every duration here is entered in MINUTES. The device stores seconds (lysis, opto
        // preheat) and rounds (amplification) and keeps doing so - see the toUi/toDev note in
        // renderFields for why the conversion cannot move onto the device.
        fields: [{
                p: "lysis temperature",
                l: "Lysis temperature",
                u: "C",
                min: 20,
                max: 110,
                step: 0.1,
            },
            {
                p: "lysis duration", // the device key stays "lysis duration"; only the label moved
                l: "Lysis time",
                u: "min",
                min: 0,
                max: 1092, // = 65535 s, the uint16 the device stores this in
                step: 0.1, // 6 s of resolution: the stored unit is seconds, do not round it away
                toUi: function(s) {
                    return Math.round(s / 6) / 10;
                },
                toDev: function(m) {
                    return Math.round(m * 60);
                },
            },
            {
                p: "amplification temperature",
                l: "Amplification temperature",
                u: "C",
                min: 20,
                max: 110,
                step: 0.1,
            },
            // ONE field where there were two. The device counts ROUNDS and asks "time per loop"
            // how long a round is; the operator thinks in minutes and the round is fixed at 20 s.
            // "time per loop" is deliberately no longer in this list, so collectFields never sends
            // it and the device keeps its own value - which is also the value converted against
            // here, so a machine set to a different round length still reads back the truth
            // instead of a number computed from an assumption.
            {
                p: "amplification time",
                l: "Amplification time",
                u: "min",
                min: 1,
                max: 43, // 130 rounds x 20 s = 43.3 min. toDev clamps for real - see below
                step: 0.5,
                toUi: function(r) {
                    return Math.round((r * perLoopMs()) / 6000) / 10;
                },
                toDev: function(m) {
                    // 130 is not a preference: COUNTER indexes sensor67Value[10][130] and anything
                    // larger overflows it mid-run. The firmware rejects >130 too (handleConfigPost);
                    // this clamp just stops the form ever posting a value it knows will be refused.
                    var r = Math.round((m * 60000) / perLoopMs());
                    return Math.min(130, Math.max(1, r));
                },
            },
            {
                p: "opto preheat time",
                l: "Opto preheat",
                u: "min",
                min: 0,
                max: 60, // = 3600 s, the firmware's own cap
                step: 0.5,
                toUi: function(s) {
                    return Math.round(s / 6) / 10;
                },
                toDev: function(m) {
                    return Math.round(m * 60);
                },
            },
        ],
        hint: "All times are in minutes. A round is 20 s on the device, so amplification tops out at 43 min (130 rounds).",
    },
    // ---- HIDDEN ON PURPOSE, to be switched back on later (decided 2026-08-02) -------------
    // LED, Calibration, PID/heater and Other parameters are commented out, not deleted. Their
    // renderers and routes are all still live, so re-enabling a card is just uncommenting its
    // entry - do NOT "clean up" renderCalib() or the /calib firmware routes as dead code.
    //
    // Known gap while Calibration is hidden: BLUE long-press on the machine still starts the
    // wizard, and /calib?action=cancel is the only clean way out (WHITE calls ESP.restart(),
    // and the flags stay latched, which wedges the NEXT calibration). With no card there is no
    // web escape - a calibration begun at the machine has to be finished at the machine.
    // {
    //   id: "led",
    //   title: "LED",
    //   desc: "Per-channel brightness",
    //   icon: ico(
    //     '<path d="M9 18h6M10 22h4"/><path d="M15.09 14c.18-.98.65-1.74 1.41-2.5A4.65 4.65 0 0 0 18 8 6 6 0 0 0 6 8c0 1 .23 2.23 1.5 3.5A4.61 4.61 0 0 1 8.91 14"/>',
    //   ),
    //   fields: [
    //     {
    //       p: "LED power",
    //       l: "LED power per slot",
    //       arr: 10,
    //       min: 0,
    //       max: 255,
    //       step: 1,
    //       cols: 5,
    //     },
    //     {
    //       p: "LED Duration",
    //       l: "LED on time before reading",
    //       u: "ms",
    //       min: 0,
    //       max: 10000,
    //       step: 10,
    //     },
    //   ],
    // },
    // {
    //   id: "calib",
    //   title: "Calibration",
    //   desc: "Run the on-device calibration",
    //   custom: "calib",
    //   icon: ico('<path d="M3 12h4l3 8 4-16 3 8h4"/>'),
    // },
    // {
    //   id: "pid",
    //   title: "PID / heater",
    //   desc: "Gains, overheat limits, hotlid PWM",
    //   icon: ico(
    //     '<circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M19.1 4.9L17 7M7 17l-2.1 2.1"/>',
    //   ),
    //   fields: [
    //     {
    //       p: "PID parameter",
    //       l: "PID - lysis heater",
    //       arr: 3,
    //       names: ["Kp", "Ki", "Kd"],
    //       min: 0,
    //       max: 1000,
    //       step: 0.01,
    //       cols: 3,
    //     },
    //     {
    //       p: "PID2 parameter",
    //       l: "PID - amplification heaters",
    //       arr: 3,
    //       names: ["Kp", "Ki", "Kd"],
    //       min: 0,
    //       max: 1000,
    //       step: 0.01,
    //       cols: 3,
    //     },
    //     {
    //       p: "PID3 parameter",
    //       l: "PID - hotlid",
    //       arr: 3,
    //       names: ["Kp", "Ki", "Kd"],
    //       min: 0,
    //       max: 1000,
    //       step: 0.01,
    //       cols: 3,
    //     },
    //     {
    //       p: "Bottom overheat value",
    //       l: "Bottom overheat",
    //       u: "C",
    //       arr: 3,
    //       min: 0,
    //       max: 50,
    //       step: 0.1,
    //       cols: 3,
    //     },
    //     {
    //       p: "Top overheat value",
    //       l: "Top overheat",
    //       u: "C",
    //       arr: 2,
    //       min: 0,
    //       max: 50,
    //       step: 0.1,
    //     },
    //     {
    //       p: "top heater PWM",
    //       l: "Hotlid PWM [low, high]",
    //       mat: [2, 2],
    //       min: 0,
    //       max: 255,
    //       step: 1,
    //     },
    //   ],
    //   hint: "Wrong gains or setpoints drive the heaters directly. Change with care.",
    // },
    // {
    //   id: "other",
    //   title: "Other parameters",
    //   desc: "Algorithm, sensors, buzzer",
    //   icon: ico(
    //     '<line x1="4" y1="21" x2="4" y2="14"/><line x1="4" y1="10" x2="4" y2="3"/><line x1="12" y1="21" x2="12" y2="12"/><line x1="12" y1="8" x2="12" y2="3"/><line x1="20" y1="21" x2="20" y2="16"/><line x1="20" y1="12" x2="20" y2="3"/><line x1="1" y1="14" x2="7" y2="14"/><line x1="9" y1="8" x2="15" y2="8"/><line x1="17" y1="16" x2="23" y2="16"/>',
    //   ),
    //   fields: [
    //     { p: "parameters.min increase", l: "Min increase", step: 0.1 },
    //     { p: "parameters.min sharpness", l: "Min sharpness", step: 0.1 },
    //     {
    //       p: "parameters.min slight positive time",
    //       l: "Min slight positive time",
    //       step: 0.1,
    //     },
    //     {
    //       p: "parameters.detect shape",
    //       l: "Detect shape (lag phase)",
    //       bool: true,
    //     },
    //     {
    //       p: "parameters.detection margin time",
    //       l: "Detection margin time",
    //       step: 0.1,
    //     },
    //     { p: "parameters.arm percentile", l: "Arm percentile", step: 0.01 },
    //     {
    //       p: "parameters.transition percentile",
    //       l: "Transition percentile",
    //       step: 0.01,
    //     },
    //     { p: "parameters.sg order", l: "SG order", min: 0, max: 255, step: 1 },
    //     { p: "parameters.sg window", l: "SG window", min: 0, max: 255, step: 1 },
    //     {
    //       p: "parameters.baseline start",
    //       l: "Baseline start",
    //       u: "min",
    //       min: 0,
    //       max: 255,
    //       step: 1,
    //     },
    //     {
    //       p: "parameters.baseline range",
    //       l: "Baseline range",
    //       u: "min",
    //       min: 0,
    //       max: 255,
    //       step: 1,
    //     },
    //     {
    //       p: "temperature value calibration",
    //       l: "Temperature offset (bottom 1-3, top 1-2, ambient)",
    //       u: "C",
    //       arr: 6,
    //       min: -20,
    //       max: 20,
    //       step: 0.1,
    //       cols: 3,
    //     },
    //     {
    //       p: "bottom temperature sensor seq",
    //       l: "Bottom sensor order",
    //       arr: 3,
    //       min: 0,
    //       max: 2,
    //       step: 1,
    //       cols: 3,
    //     },
    //     {
    //       p: "top temperature sensor seq",
    //       l: "Top sensor order",
    //       arr: 3,
    //       min: 0,
    //       max: 2,
    //       step: 1,
    //       cols: 3,
    //     },
    //     { p: "units", l: "Units", text: true, maxlen: 9 },
    //     { p: "buzzer", l: "Buzzer", sel: ["On", "Off"] },
    //     { p: "kitId", l: "Kit id", min: 0, step: 1 },
    //   ],
    // },
    {
        id: "ota",
        title: "Firmware",
        desc: "Check for and install updates",
        custom: "ota",
        icon: ico(
            '<path d="M12 3v12"/><path d="M8 11l4 4 4-4"/><path d="M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"/>',
        ),
    },
];

var cfgCache = null; // last GET /config
var deviceBusy = false;
var calibStep = "";
var openCard = null;

/* dotted path get/set - "parameters.sg order" is nested, "LED power" is not */
function getPath(o, p) {
    var parts = p.split(".");
    for (var i = 0; i < parts.length; i++) {
        if (o === undefined || o === null) return undefined;
        o = o[parts[i]];
    }
    return o;
}

function setPath(o, p, v) {
    var parts = p.split(".");
    for (var i = 0; i < parts.length - 1; i++) {
        if (!o[parts[i]]) o[parts[i]] = {};
        o = o[parts[i]];
    }
    o[parts[parts.length - 1]] = v;
}

/* How long one amplification round lasts, in ms. Read from the device rather than hardcoded:
   it is a stored parameter ("time per loop", 20 s by default), and the Profile card converts
   rounds <-> minutes against it. A machine set to something else must still show the truth. */
function perLoopMs() {
    var v = Number(getPath(cfgCache, "time per loop"));
    return v > 0 ? v : 20000;
}

function el(tag, cls, txt) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (txt !== undefined) e.textContent = txt;
    return e;
}

/* ---------- menu ---------- */
function renderSetMenu() {
    var m = document.getElementById("setMenu");
    if (!m) return;
    m.innerHTML = "";
    CARDS.forEach(function(c) {
        var b = el("button", "set-card");
        b.type = "button";
        b.setAttribute("data-card", c.id);
        b.disabled = deviceBusy; // server enforces this too (409)
        var i = el("span", "set-ico");
        i.innerHTML = c.icon;
        var body = el("div", "set-card-body");
        body.appendChild(el("div", "set-card-title", c.title));
        body.appendChild(el("div", "set-card-desc", c.desc));
        var ch = el("span", "set-chev");
        ch.innerHTML = SVG_CHEV;
        b.appendChild(i);
        b.appendChild(body);
        b.appendChild(ch);
        b.addEventListener("click", function() {
            openPanel(c.id);
        });
        m.appendChild(b);
    });
}

function setMsg(text, ok) {
    var m = document.getElementById("setMsg");
    if (!m) return;
    m.textContent = text || "";
    m.className = "set-msg" + (text ? (ok ? " ok" : " err") : "");
}

function showMenu() {
    openCard = null;
    document.getElementById("setDetail").classList.add("hide");
    document.getElementById("setMenu").classList.remove("hide");
    document.getElementById("setAbout").classList.remove("hide");
}

/* A POST only tells us the device QUEUED the write - it cannot wait for the apply
 * (blocking the device's network task is what trips its watchdog). SettingTask applies
 * it up to ~10ms later and DROPS it if a run started in between. So never say "Saved"
 * off the 200: take the seq the POST returned and wait for the device to report that
 * seq's real outcome on the 1s home event. */
var lastCfg = { seq: 0, state: "none" };

function awaitCfg(seq, cb) {
    var t0 = Date.now();
    (function poll() {
        // >= : a later request may have bumped the seq past ours already
        if (lastCfg.seq >= seq && lastCfg.state !== "pending")
            return cb(lastCfg.state);
        if (Date.now() - t0 > 6000) return cb("timeout");
        setTimeout(poll, 200);
    })();
}

// Report a queued save honestly: "Saved" only once the device says it wrote it.
function settleSave(res, btn, onSaved) {
    if (!(res.j && res.j.ok)) {
        btn.disabled = false;
        setMsg((res.j && res.j.error) || "Save failed (" + res.s + ")", false);
        return;
    }
    setMsg("Saving...", true);
    awaitCfg(res.j.seq || 0, function(state) {
        btn.disabled = false;
        if (state === "applied") {
            loadConfig(); // safe to re-read now: the device has actually written it
            onSaved();
        } else if (state === "busy") {
            setMsg(
                "Not saved - the device started a run. Try again when it is idle.",
                false,
            );
        } else {
            setMsg("Could not confirm the save - check the device.", false);
        }
    });
}

// After a save lands, the operator's next move is watching the machine - so drop them
// back on Home. Delay first: navigating instantly swallows the "Saved" confirmation and
// leaves them unsure whether it took. Only ever called on a CONFIRMED save.
// The timer is cancelled if the user navigates away first (see cancelBackToHome), so a
// pending "go Home" can't yank them off a tab they chose in the meantime.
var backHomeTimer = null;

function cancelBackToHome() {
    if (backHomeTimer) {
        clearTimeout(backHomeTimer);
        backHomeTimer = null;
    }
}

function backToHomeAfterSave(delay) {
    cancelBackToHome();
    backHomeTimer = setTimeout(function() {
        backHomeTimer = null;
        showMenu(); // reset the panel, so re-entering Setting starts at the card menu
        var home = document.querySelector('.nav-item[data-screen="home"]');
        if (home) home.click();
    }, delay || 900);
}

function openPanel(id) {
    var c = CARDS.find(function(x) {
        return x.id === id;
    });
    if (!c || deviceBusy) return;
    openCard = id;
    document.getElementById("setPanelTitle").textContent = c.title;
    document.getElementById("setMenu").classList.add("hide");
    document.getElementById("setAbout").classList.add("hide");
    document.getElementById("setDetail").classList.remove("hide");
    setMsg("");
    // Before the per-panel renderers, which return early: the menu holding the focused card
    // is display:none by now, so focus has to land somewhere inside the panel or it is lost.
    document.getElementById("setBack").focus();
    var form = document.getElementById("setForm");
    form.innerHTML = "";
    if (c.custom === "wifi") return renderWifi(form);
    if (c.custom === "id") return renderDeviceId(form);
    if (c.custom === "calib") return renderCalib(form);
    if (c.custom === "ota") return renderOta(form);
    renderFields(form, c);
}

// Hand focus back to the card that opened the panel - a keyboard user is otherwise dropped
// on <body> and has to tab from the top of the page to get back where they were standing.
// Wired to THIS listener, not to showMenu(): showMenu also runs from the bottom nav on
// re-entry (script.js:46) and from backToHomeAfterSave(), and openCard survives leaving the
// tab with a panel open. Doing it inside showMenu() stole focus off the nav button the user
// had just pressed, and loadConfig().then(renderSetMenu) then emptied #setMenu and destroyed
// the very node now holding it - focus landed on <body>, the exact failure this prevents.
// Back is the only path where the thing being hidden IS the thing holding focus.
document.getElementById("setBack").addEventListener("click", function() {
    var was = openCard;
    showMenu();
    var card =
        was && document.querySelector('.set-card[data-card="' + was + '"]');
    if (card) card.focus();
});

/* ---------- generic value form ---------- */
function numInput(f, val, ph) {
    var i = el("input", "f-in");
    i.type = "number";
    if (f.min !== undefined) i.min = f.min;
    if (f.max !== undefined) i.max = f.max;
    i.step = f.step || "any";
    if (ph !== undefined) i.placeholder = ph;
    i.value = val === undefined || val === null ? "" : val;
    return i;
}

function renderFields(form, c) {
    if (!cfgCache) {
        form.appendChild(el("p", "f-hint", "Loading..."));
        return;
    }
    if (c.hint) form.appendChild(el("p", "f-hint", c.hint));

    c.fields.forEach(function(f) {
        var row = el("div", "f-row");
        var lbl = el("label", "f-lbl");
        lbl.textContent = f.l;
        if (f.u) {
            var u = el("span", "f-unit", "  (" + f.u + ")");
            lbl.appendChild(u);
        }
        row.appendChild(lbl);

        var cur = getPath(cfgCache, f.p);

        if (f.mat) {
            // 2x2 hotlid PWM: rows of [low, high]
            var wrap = el("div", "f-arr");
            for (var r = 0; r < f.mat[0]; r++)
                for (var k = 0; k < f.mat[1]; k++) {
                    var cell = el("div", "f-cell");
                    var idx = el(
                        "span",
                        "f-idx",
                        "H" + (r + 1) + " " + (k === 0 ? "low" : "high"),
                    );
                    var inp = numInput(f, cur && cur[r] ? cur[r][k] : "");
                    inp.setAttribute("data-mat", r + "," + k);
                    // One label cannot name four boxes: each cell carries its own name instead.
                    inp.setAttribute("aria-label", f.l + " " + idx.textContent);
                    cell.appendChild(idx);
                    cell.appendChild(inp);
                    wrap.appendChild(cell);
                }
            wrap.setAttribute("data-path", f.p);
            row.appendChild(wrap);
        } else if (f.arr) {
            var w = el("div", "f-arr" + (f.cols ? " n" + f.cols : ""));
            for (var j = 0; j < f.arr; j++) {
                var ce = el("div", "f-cell");
                var ix = el("span", "f-idx", f.names ? f.names[j] : "#" + (j + 1));
                var ip = numInput(f, cur ? cur[j] : "");
                ip.setAttribute("data-i", j);
                ip.setAttribute("aria-label", f.l + " " + ix.textContent);
                ce.appendChild(ix);
                ce.appendChild(ip);
                w.appendChild(ce);
            }
            w.setAttribute("data-path", f.p);
            row.appendChild(w);
        } else if (f.bool) {
            var sb = el("select", "f-sel");
            ["true", "false"].forEach(function(v) {
                var o = el("option", null, v);
                o.value = v;
                if (String(!!cur) === v) o.selected = true;
                sb.appendChild(o);
            });
            sb.setAttribute("data-path", f.p);
            sb.setAttribute("data-kind", "bool");
            row.appendChild(sb);
        } else if (f.sel) {
            var s = el("select", "f-sel");
            f.sel.forEach(function(v) {
                var o = el("option", null, v);
                o.value = v;
                if (cur === v) o.selected = true;
                s.appendChild(o);
            });
            s.setAttribute("data-path", f.p);
            s.setAttribute("data-kind", "str");
            row.appendChild(s);
        } else if (f.text) {
            var t = el("input", "f-in");
            t.type = "text";
            t.maxLength = f.maxlen || 9;
            t.value = cur === undefined ? "" : cur;
            t.setAttribute("data-path", f.p);
            t.setAttribute("data-kind", "str");
            row.appendChild(t);
        } else {
            // f.toUi/f.toDev let a field be EDITED in one unit and STORED in another (minutes in
            // the form, seconds or rounds on the device). The conversion lives here and in
            // collectFields, nowhere else: parastructure is 400 B against a 402 B ceiling so the
            // device cannot carry a second copy in friendlier units, and the firmware's range
            // checks are written against the stored units.
            var n = numInput(f, f.toUi && cur !== undefined ? f.toUi(cur) : cur);
            n.setAttribute("data-path", f.p);
            n.setAttribute("data-kind", "num");
            n.setAttribute("data-min", f.min === undefined ? "" : f.min);
            n.setAttribute("data-max", f.max === undefined ? "" : f.max);
            row.appendChild(n);
        }
        // A <label> that is neither for= nor an ancestor names NOTHING: the reader announces
        // "edit, blank" and clicking the text does not focus the box. Wired here, once, so
        // every branch above inherits it; the array/matrix rows hold several controls and
        // carry their own aria-label instead, so they fall out of the count on purpose.
        var ctrls = row.querySelectorAll(".f-in, .f-sel");
        if (ctrls.length === 1) {
            ctrls[0].id = "f-" + f.p.replace(/[^a-z0-9]+/gi, "-");
            lbl.htmlFor = ctrls[0].id;
        }
        form.appendChild(row);
    });

    var save = el("button", "save-btn", "Save");
    save.type = "button";
    save.addEventListener("click", function() {
        saveFields(c, save);
    });
    form.appendChild(save);
}

// Collect ONLY this card's keys into the body: the device merges them onto the rest.
function collectFields(c, form) {
    var body = {};
    var bad = null;
    c.fields.forEach(function(f) {
        if (f.mat) {
            var w = form.querySelector('[data-path="' + f.p + '"]');
            var rows = [];
            for (var r = 0; r < f.mat[0]; r++) rows.push([]);
            w.querySelectorAll("input").forEach(function(i) {
                var rc = i.getAttribute("data-mat").split(",");
                rows[+rc[0]][+rc[1]] = Number(i.value);
                if (i.value === "" || isNaN(Number(i.value))) bad = bad || f.l;
            });
            setPath(body, f.p, rows);
        } else if (f.arr) {
            var wa = form.querySelector('[data-path="' + f.p + '"]');
            var a = [];
            wa.querySelectorAll("input").forEach(function(i) {
                a[+i.getAttribute("data-i")] = Number(i.value);
                if (i.value === "" || isNaN(Number(i.value))) bad = bad || f.l;
                i.classList.toggle("bad", i.value === "");
            });
            setPath(body, f.p, a);
        } else {
            var e2 = form.querySelector('[data-path="' + f.p + '"]');
            var kind = e2.getAttribute("data-kind");
            if (kind === "bool") setPath(body, f.p, e2.value === "true");
            else if (kind === "str") setPath(body, f.p, e2.value);
            else {
                if (e2.value === "" || isNaN(Number(e2.value))) {
                    bad = bad || f.l;
                    e2.classList.add("bad");
                } else e2.classList.remove("bad");
                var num = Number(e2.value);
                setPath(body, f.p, f.toDev ? f.toDev(num) : num);
            }
        }
    });
    return { body: body, bad: bad };
}

function saveFields(c, btn) {
    var form = document.getElementById("setForm");
    var r = collectFields(c, form);
    if (r.bad) {
        setMsg("Check the value for: " + r.bad, false);
        return;
    }
    btn.disabled = true;
    setMsg("Saving...", true);
    fetch("/config", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(r.body),
        })
        .then(function(res) {
            return res.json().then(function(j) {
                return { s: res.status, j: j };
            });
        })
        .then(function(o) {
            settleSave(o, btn, function() {
                setMsg("Saved to the device.", true);
                backToHomeAfterSave();
            });
        })
        .catch(function() {
            btn.disabled = false;
            setMsg("Save failed - no connection", false);
        });
}

/* ---------- Device ID (two stores: global id_device + parameter.device_id) ---------- */
/* ---------- Firmware / OTA panel ----------
 * GET /ota reports what is installed vs what GitHub offers; POST /ota?action=check
 * queues the (blocking) HTTPS check onto SettingTask, action=update hands the download
 * to NetworkTask. The device reboots into the new build on success, so this panel never
 * gets a "done" reply - the page simply loses the connection and reconnects.
 */
/* ---------- MD5 ----------
 * Here because ESP32's Update class verifies MD5 and nothing else (Updater.cpp), while the
 * browser's crypto.subtle offers SHA-1/SHA-256 and NOT MD5 - there is nowhere to borrow it
 * from. ~45 lines is the price of never asking a person to transcribe a 32-character digest,
 * which is the same as the price of the check actually being used.
 *
 * K is a LITERAL TABLE, not Math.floor(abs(sin(i+1)) * 2**32). The derivation is textbook,
 * but it leans on the last bits of a libm sine agreeing across every browser and phone that
 * reaches this dashboard. One wrong constant would reject every upload on some devices and
 * none on others - the worst kind of bug to be handed. A table cannot drift.
 *
 * Verified against the 7 RFC 1321 vectors, against node crypto for every length 0..200 (the
 * block and padding boundaries), and against md5sum of the real 2.4 MB firmware.bin. */
var MD5_K = [
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
];
var MD5_S = [
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
];

function md5Hex(bytes) {
    var n = bytes.length;
    // message + 0x80 + zeros + 8-byte little-endian bit count, rounded up to a 64-byte multiple
    var padded = new Uint8Array((((n + 8) >> 6) + 1) << 6);
    padded.set(bytes);
    padded[n] = 0x80;
    var w = new DataView(padded.buffer);
    // Bit count via a float divide, NOT shifts: a 2.4 MB image is ~19 million bits, well past
    // what a 32-bit shift can express, and `bits >>> 32` is a no-op in JS rather than 0.
    var bits = n * 8;
    w.setUint32(padded.length - 8, bits >>> 0, true);
    w.setUint32(padded.length - 4, Math.floor(bits / 4294967296) >>> 0, true);

    var a0 = 0x67452301,
        b0 = 0xefcdab89,
        c0 = 0x98badcfe,
        d0 = 0x10325476;
    var m = new Int32Array(16);
    for (var off = 0; off < padded.length; off += 64) {
        for (var j = 0; j < 16; j++) m[j] = w.getInt32(off + j * 4, true);
        var A = a0,
            B = b0,
            C = c0,
            D = d0;
        for (var i = 0; i < 64; i++) {
            var f, g;
            if (i < 16) {
                f = (B & C) | (~B & D);
                g = i;
            } else if (i < 32) {
                f = (D & B) | (~D & C);
                g = (5 * i + 1) & 15;
            } else if (i < 48) {
                f = B ^ C ^ D;
                g = (3 * i + 5) & 15;
            } else {
                f = C ^ (B | ~D);
                g = (7 * i) & 15;
            }
            var tmp = D;
            D = C;
            C = B;
            var x = (A + f + MD5_K[i] + m[g]) | 0;
            B = (B + ((x << MD5_S[i]) | (x >>> (32 - MD5_S[i])))) | 0;
            A = tmp;
        }
        a0 = (a0 + A) | 0;
        b0 = (b0 + B) | 0;
        c0 = (c0 + C) | 0;
        d0 = (d0 + D) | 0;
    }
    // Little-endian per word, and LOWERCASE: Update.end() compares case-sensitively against
    // MD5Builder's always-lowercase digest.
    var out = "";
    var words = [a0, b0, c0, d0];
    for (var k = 0; k < 4; k++)
        for (var b = 0; b < 4; b++)
            out += (((words[k] >>> (b * 8)) & 255) + 256).toString(16).slice(1);
    return out;
}

function renderOta(form) {
    var info = el("div", "ota-info");
    form.appendChild(info);
    var actions = el("div", "wz-actions");
    form.appendChild(actions);

    var checkBtn = el("button", "save-btn", "Check for updates");
    checkBtn.type = "button";
    var updBtn = el("button", "save-btn", "Install update");
    updBtn.type = "button";
    updBtn.classList.add("hide");
    actions.appendChild(checkBtn);
    actions.appendChild(updBtn);

    function paint(o) {
        info.innerHTML = "";
        // No build number since v2.4.4: the server offers a FILE NAME, not a version code.
        var rows = [
            ["Installed", o.version]
        ];
        if (o.newVersion && o.hasUpdate) rows.push(["Available", o.newVersion]);
        rows.push([
            "Status", !o.online ?
            "No internet - cannot check" :
            o.state === "updating" ?
            "Installing..." :
            o.hasUpdate ?
            "Update available" :
            o.checkFailed ?
            "Check failed - try again" :
            o.checked ?
            "Up to date" :
            "Not checked yet",
        ]);
        rows.forEach(function(r) {
            var p = el("p", "setting-row", r[0] + ": ");
            p.appendChild(el("span", "", r[1]));
            info.appendChild(p);
        });
        updBtn.classList.toggle("hide", !o.hasUpdate);
        checkBtn.disabled = !o.online || o.state === "updating";
        updBtn.disabled = o.state === "updating";
    }

    function load() {
        return fetch("/ota")
            .then(function(r) {
                return r.json();
            })
            .then(paint)
            .catch(function() {
                info.textContent = "Could not read firmware status.";
            });
    }

    checkBtn.addEventListener("click", function() {
        checkBtn.disabled = true;
        setMsg("Checking...", true);
        fetch("/ota?action=check", { method: "POST" })
            .then(function(r) {
                return r.json().then(function(j) {
                    return { s: r.status, j: j };
                });
            })
            .then(function(o) {
                if (o.s !== 200) {
                    setMsg(o.j.error || "Check failed", false);
                    checkBtn.disabled = false;
                    return;
                }
                // SettingTask does the HTTPS GET; poll until the reported state settles.
                var tries = 0;
                (function poll() {
                    load().then(function() {
                        if (++tries < 20) setTimeout(poll, 500);
                        else setMsg("", true);
                    });
                })();
                setMsg("Checking with the update server...", true);
            })
            .catch(function() {
                setMsg("Check failed", false);
                checkBtn.disabled = false;
            });
    });

    updBtn.addEventListener("click", function() {
        if (!confirm(
                "Install the new firmware? The machine downloads it and RESTARTS. " +
                "Do not power it off during the update.",
            ))
            return;
        updBtn.disabled = true;
        setMsg("Downloading... the machine will restart by itself.", true);
        fetch("/ota?action=update", { method: "POST" })
            .then(function(r) {
                return r.json().then(function(j) {
                    return { s: r.status, j: j };
                });
            })
            .then(function(o) {
                if (o.s !== 200) {
                    setMsg(o.j.error || "Could not start the update", false);
                    updBtn.disabled = false;
                }
                // On success there is nothing more to report: the device reboots mid-flight.
            })
            .catch(function() {
                setMsg("Could not start the update", false);
                updBtn.disabled = false;
            });
    });

    /* ---- Second way in: install a .bin from this computer ----
       Needed when the machine has no internet at all (its own SoftAP), or for a build that
       is not published to the update server yet. */
    form.appendChild(el("hr", "ota-sep"));
    form.appendChild(
        el(
            "p",
            "f-hint",
            "Or install a firmware file from this computer - works with no internet, " +
            "e.g. while connected to the machine's own WiFi.",
        ),
    );
    var fileRow = el("div", "f-row");
    var fileLbl = el("label", "f-lbl", "Firmware file (.bin)");
    var fin = el("input", "f-in");
    fin.type = "file";
    fin.accept = ".bin";
    fin.id = "otaFile";
    fileLbl.htmlFor = fin.id;
    fileRow.appendChild(fileLbl);
    fileRow.appendChild(fin);
    form.appendChild(fileRow);

    var upBtn = el("button", "save-btn", "Upload & install");
    upBtn.type = "button";
    form.appendChild(upBtn);
    var prog = el("p", "f-hint ota-prog", "");
    form.appendChild(prog);

    upBtn.addEventListener("click", function() {
        var f = fin.files && fin.files[0];
        if (!f) {
            setMsg("Choose a .bin file first", false);
            return;
        }
        if (!confirm(
                "Install " +
                f.name +
                "? The machine RESTARTS when it finishes. Do not power it off during the upload.",
            ))
            return;

        // Hash the file HERE, then hand the digest to the machine as ?md5=. That is what turns
        // a dropped WiFi connection from a brick into a rejected upload: without it
        // Update.end(true) sets _size = progress() - "however many bytes arrived IS the whole
        // image" - so a transfer cut at 80% installs and the unit never boots again.
        //
        // Computed rather than typed, on purpose. An earlier version asked for the digest in a
        // text box; a customer at a remote site cannot reasonably be asked to transcribe 32 hex
        // characters, and a check people skip is not a check. The browser already has the bytes.
        //
        // FileReader, not Blob.arrayBuffer(): same result, but supported on the older phones
        // that reach this dashboard by scanning the QR code.
        upBtn.disabled = true;
        setMsg("Checking file...", true);
        var fr = new FileReader();
        fr.onerror = function() {
            upBtn.disabled = false;
            setMsg("Could not read that file", false);
        };
        fr.onload = function() {
            // ~40 ms for a 2.4 MB image, so no progress indicator earns its place here.
            sendFirmware(f, md5Hex(new Uint8Array(fr.result)));
        };
        fr.readAsArrayBuffer(f);
    });

    function sendFirmware(f, md5) {
        var fd = new FormData();
        fd.append("firmware", f);
        // XMLHttpRequest, not fetch: only XHR reports UPLOAD progress, and a ~2.3 MB image
        // over WiFi takes long enough that a silent screen looks like a hang.
        var xhr = new XMLHttpRequest();
        setMsg("Uploading...", true);
        xhr.upload.onprogress = function(e) {
            if (e.lengthComputable)
                prog.textContent =
                "Uploading " +
                Math.round((e.loaded / e.total) * 100) +
                "% (" +
                Math.round(e.loaded / 1024) +
                " / " +
                Math.round(e.total / 1024) +
                " KB)";
        };
        xhr.onload = function() {
            prog.textContent = "";
            upBtn.disabled = false;
            if (xhr.status === 200)
                setMsg(
                    "Installed. The machine is restarting - reload in a moment.",
                    true,
                );
            else {
                var msg = "Upload failed";
                try {
                    msg = JSON.parse(xhr.responseText).error || msg;
                } catch (e) {}
                setMsg(msg, false);
            }
        };
        xhr.onerror = function() {
            prog.textContent = "";
            upBtn.disabled = false;
            setMsg("Upload failed", false);
        };
        // Firmware validates + lowercases it too (webDashboard.cpp:1343-1350), then
        // Update.setMD5() makes Update.end() REFUSE a short or altered image.
        xhr.open("POST", "/otaupload?md5=" + md5);
        xhr.send(fd);
    }

    load();
}

function renderDeviceId(form) {
    form.appendChild(
        el(
            "p",
            "f-hint",
            "Max 9 characters. Used on the dashboard and in the Google Sheet upload.",
        ),
    );
    var row = el("div", "f-row");
    var lbl = el("label", "f-lbl", "Device ID");
    var i = el("input", "f-in");
    i.type = "text";
    i.maxLength = 9;
    i.id = "idInput";
    lbl.htmlFor = i.id;
    // deviceIdNow = SSE home.device, i.e. the ID the machine itself reports. Never cfgCache:
    // the card must display the same value POST /deviceid writes, or the operator "corrects" an
    // ID that was never the one in use.
    i.value = deviceIdNow;
    row.appendChild(lbl);
    row.appendChild(i);
    form.appendChild(row);
    // Below the input, not above it: test_device_id.py slices this function to a fixed length and
    // needs deviceIdNow inside the slice, and a warning reads better right above the button it
    // applies to. Deliberately does not name the hotspot prefix - the device builds that
    // (dashboardApName) and a hardcoded copy here would go stale, which is the bug this warns about.
    form.appendChild(
        el(
            "p",
            "f-hint",
            "Saving reboots the device so it can re-announce the new name: the hotspot SSID, the .local address and the QR code all change. If you are connected to the machine's own hotspot you will have to re-join it under the new name.",
        ),
    );
    var b = el("button", "save-btn", "Save & reboot");
    b.type = "button";
    b.addEventListener("click", function() {
        var v = i.value.trim();
        if (!v || v.length > 9) {
            setMsg("ID must be 1..9 characters", false);
            return;
        }
        b.disabled = true;
        setMsg("Saving...", true);
        fetch("/deviceid", {
                method: "POST",
                headers: { "Content-Type": "application/x-www-form-urlencoded" },
                body: "id=" + encodeURIComponent(v),
            })
            .then(function(r) {
                return r.json().then(function(j) {
                    return { s: r.status, j: j };
                });
            })
            .then(function(o) {
                settleSave(o, b, function() {
                    setMsg("Saved - restarting to apply the new name.", true);
                    backToHomeAfterSave();
                });
            })
            .catch(function() {
                b.disabled = false;
                setMsg("Failed - no connection", false);
            });
    });
    form.appendChild(b);
}

/* ---------- WiFi: async scan, then save + reboot ---------- */
var curNet = null; // latest {ssid, ip, ap} from the home SSE event (current connection)
var wifiScanNets = []; // last scan result, so Save can warn on an SSID typo before rebooting
var wifiSavedSsids = []; // what the Saved list above is already showing (Nearby hides these)
// Was this SSID seen in the last scan? (Unknown scan -> treat as "yes" so we never block.)
function wifiScanHas(ssid) {
    if (!wifiScanNets.length) return true; // no scan data yet -> don't second-guess the user
    return wifiScanNets.some(function(n) {
        return n.ssid === ssid;
    });
}

function renderWifi(form) {
    /* Two jobs, two views. Joining a network (scan -> pick -> password -> reboot) and managing
     * the ones already stored are separate tasks, and stacking them made a panel long enough
     * that the Save button sat below the fold on a phone.
     *
     * Radios, not a hand-rolled tab strip: a radiogroup gives arrow-key navigation, ONE tab
     * stop and the checked state to a screen reader for free. Each input is wrapped by its own
     * <label>, so the visible word IS the accessible name. */
    var views = [
        { id: "connect", label: "Connect" },
        { id: "saved", label: "Saved" },
    ];
    var seg = el("div", "wifi-seg");
    seg.setAttribute("role", "radiogroup");
    seg.setAttribute("aria-label", "WiFi view");
    var panes = {};
    views.forEach(function(v, i) {
        var lab = el("label", "wifi-seg-btn");
        var r = el("input");
        r.type = "radio";
        r.name = "wifiView";
        r.value = v.id;
        r.checked = i === 0; // Connect first: it is the reason the panel gets opened
        r.addEventListener("change", function() {
            views.forEach(function(o) {
                panes[o.id].classList.toggle("hide", o.id !== v.id);
            });
        });
        lab.appendChild(r);
        lab.appendChild(el("span", null, v.label));
        seg.appendChild(lab);
    });
    form.appendChild(seg);
    views.forEach(function(v, i) {
        panes[v.id] = el("div", "wifi-pane" + (i === 0 ? "" : " hide"));
        form.appendChild(panes[v.id]);
    });
    var connect = panes.connect;
    var savedPane = panes.saved;

    connect.appendChild(
        el(
            "p",
            "f-hint",
            // Don't name the hotspot prefix here: it is built by dashboardApName() on the device
            // (currently "FBT-<id>") and a hardcoded copy went stale the moment that changed.
            // net.ssid on the home event carries the live name when we are actually on the AP.
            "The device reboots to join the network. If you are connected to its own hotspot, this page will disconnect.",
        ),
    );
    // Saved networks (stored in NVS): the machine tries these in order if the preferred
    // one is out of range, so a lab can move it between rooms without reconfiguring.
    // Rendered even while its view is hidden - loadSavedWifi() also feeds wifiSavedSsids, which
    // the Connect view's nearby list filters against.
    var saved = el("div", "wifi-list");
    saved.id = "wifiSaved";
    saved.appendChild(el("p", "f-hint", "Loading..."));
    savedPane.appendChild(saved);
    loadSavedWifi();

    // <details>, not a hand-rolled toggle: the browser supplies the click target, the keyboard
    // handling and the expanded/collapsed state to assistive tech for free. Open while the
    // operator is still choosing; picking a network collapses it (see renderWifiList) so the
    // SSID and password boxes come up to meet the thumb instead of sitting under 8 scan rows.
    var scan = el("details", "wifi-scan");
    scan.id = "wifiScan";
    scan.open = true;
    scan.appendChild(el("summary", "f-lbl", "Nearby networks"));
    // Rescan lives INSIDE the <details>, not in the <summary>: a button nested in a summary is
    // nested interactive content, and its click also toggles the disclosure unless every event
    // is stopped by hand. Here it is a plain sibling of the list it refreshes.
    var again = el("button", "wifi-rescan");
    again.type = "button";
    // Icon only. The label moves to aria-label/title: with the button parked in the corner of
    // the disclosure row there is no room for text, and "Scan again" was the accessible NAME -
    // dropping the span without this would leave a nameless button for a screen reader.
    again.setAttribute("aria-label", "Scan again");
    again.title = "Scan again";
    again.innerHTML =
        SVG_OPEN +
        '<path d="M21 12a9 9 0 1 1-2.64-6.36"/><polyline points="21 3 21 9 15 9"/>' +
        "</svg>";
    again.addEventListener("click", function() {
        if (again.disabled) return;
        again.disabled = true;
        var list = document.getElementById("wifiList");
        if (list) {
            list.innerHTML = "";
            list.appendChild(el("p", "f-hint", "Scanning..."));
        }
        // The device restarts its scan on the next GET when it is not already running, so this is
        // just the same poll from tries=0. Re-enabled by renderWifiList, which every exit path of
        // the poll reaches - success, give-up and error alike - so the button can never stay dead.
        pollWifiScan(0);
    });
    scan.appendChild(again);
    var list = el("div", "wifi-list");
    list.id = "wifiList";
    list.appendChild(el("p", "f-hint", "Scanning..."));
    scan.appendChild(list);
    connect.appendChild(scan);

    /* A real text box, not just the scan list: picking from the list fills it in, but a
       HIDDEN network never appears in a scan and could not be joined at all before. */
    var srow = el("div", "f-row");
    var slbl = el("label", "f-lbl", "Network name (SSID)");
    srow.appendChild(slbl);
    var ss = el("input", "f-in");
    ss.type = "text";
    ss.id = "wifiSsid";
    slbl.htmlFor = ss.id;
    ss.maxLength = 32; // firmware rejects 0 or >32 (handleWifi)
    ss.placeholder = "pick one above, or type a hidden network";
    srow.appendChild(ss);
    connect.appendChild(srow);

    var row = el("div", "f-row");
    var plbl = el("label", "f-lbl", "Password");
    row.appendChild(plbl);
    var pw = el("input", "f-in");
    pw.type = "password";
    pw.id = "wifiPass";
    plbl.htmlFor = pw.id;
    pw.maxLength = 54; // EEPROM slot is 54 chars, not WPA2's 63
    pw.placeholder = "leave empty for an open network";
    row.appendChild(pw);
    connect.appendChild(row);

    var b = el("button", "save-btn", "Save & reboot");
    b.type = "button";
    b.addEventListener("click", function() {
        // Read the BOX, not the list selection: it is the one thing on screen the operator
        // can see and correct, and it is also how a hidden network gets in. Keep it in a LOCAL:
        // the device polls before it answers, so the callback below runs long after the panel
        // may have re-rendered - a module-scope copy read "null" by then.
        var wifiPick = ss.value.trim();
        if (!wifiPick) {
            setMsg("Enter or pick a network name", false);
            ss.focus();
            return;
        }
        if (wifiPick.length > 32) {
            setMsg("Network name must be 1..32 characters", false);
            ss.focus();
            return;
        }
        // Catch an SSID typo BEFORE the reboot: if the name isn't in the scan and looks like a
        // mistake, ask. A hidden network legitimately won't appear, so this is a confirm, not a
        // hard block. (The password still can't be checked here - only a boot-test can.)
        if (!wifiScanHas(wifiPick)) {
            if (!confirm(
                    '"' +
                    wifiPick +
                    '" was not seen in the scan. Save anyway? (OK for a hidden network.)',
                ))
                return;
        }
        b.disabled = true;
        setMsg("Testing " + wifiPick + "...", true);
        fetch("/wifi", {
                method: "POST",
                headers: { "Content-Type": "application/x-www-form-urlencoded" },
                body: "ssid=" +
                    encodeURIComponent(wifiPick) +
                    "&pass=" +
                    encodeURIComponent(pw.value),
            })
            .then(function(r) {
                return r.json().then(function(j) {
                    return { s: r.status, j: j };
                });
            })
            .then(function(o) {
                settleSave(o, b, function() {
                    // The device does NOT commit yet - it reboots and TESTS the password. If it is
                    // wrong, it comes back on the OLD network and the WiFi panel will show a "could
                    // not join" notice (from /wifilist trial result). So send them to Home to watch
                    // it reconnect, not a false "Saved".
                    setMsg(
                        "Rebooting to test " +
                        wifiPick +
                        ". If the password is wrong it stays on the current network.",
                        true,
                    );
                    backToHomeAfterSave(2600);
                });
            })
            .catch(function() {
                b.disabled = false;
                setMsg("Failed - no connection", false);
            });
    });
    connect.appendChild(b);
    pollWifiScan(0);
}

// The device scans asynchronously (a blocking scan would freeze every SSE client), so
// GET /wifiscan answers 202 while it runs and 200 with the list when it is done.
function pollWifiScan(tries) {
    if (openCard !== "wifi") return;
    fetch("/wifiscan")
        .then(function(r) {
            return r.json();
        })
        .then(function(d) {
            if (openCard !== "wifi") return;
            if (d.scanning) {
                if (tries < 20)
                    setTimeout(function() {
                        pollWifiScan(tries + 1);
                    }, 700);
                else renderWifiList([]);
                return;
            }
            renderWifiList(d.networks || []);
        })
        .catch(function() {
            if (openCard === "wifi") renderWifiList(null);
        });
}

/* Saved-network list: GET /wifilist -> {max, nets:[{ssid}]}. Each row has a Forget
   button (POST /wifilist?remove=ssid). SSIDs only - passwords never leave the device. */
function loadSavedWifi() {
    // no-store: after a remove/connect POST, a cached GET would redraw the OLD list and the
    // network would look like it was never forgotten.
    fetch("/wifilist", { cache: "no-store" })
        .then(function(r) {
            return r.json();
        })
        .then(function(d) {
            var box = document.getElementById("wifiSaved");
            if (!box) return;
            box.innerHTML = "";
            // The device rebooted to TEST new credentials and did not connect: it reverted to
            // the old network. Message depends on WHY it failed, so a moved (out-of-range)
            // network isn't blamed on a wrong password.
            if (d && d.trial && d.trial.result === "failed") {
                var why =
                    d.trial.reason === "range" ?
                    "it was not found (out of range?)" :
                    "wrong password?";
                setMsg(
                    'Could not join "' +
                    d.trial.ssid +
                    '" - ' +
                    why +
                    " It stayed on the current network. Re-enter below.",
                    false,
                );
            }
            var nets = (d && d.nets) || [];
            // Nearby hides whatever is saved, so it has to be re-rendered from here: the two
            // lists are separate fetches in either order, and a Forget must put the network
            // back into Nearby without waiting for the next scan.
            wifiSavedSsids = nets.map(function(n) {
                return n.ssid;
            });
            if (wifiScanNets.length) renderWifiList(wifiScanNets);
            if (!nets.length) {
                box.appendChild(el("p", "f-hint", "None saved yet."));
                return;
            }
            // ONE source of truth for this render: /wifilist "current" is what the device reports
            // as it answers. curNet (SSE, up to 1 s old) is only the fallback. Deciding per row -
            // "active OR curNet matches" - lets a stale SSE badge a SECOND row "connected" and,
            // worse, hide that row's Connect button.
            var live = (d && d.current) || (curNet && !curNet.ap ? curNet.ssid : "");
            nets.forEach(function(n) {
                // The ONLY badge. The old "preferred" one was list position, not the EEPROM pair, so
                // after a boot fallback it sat on a row the machine was not on and just read as
                // "wrong network". List order already says which one is tried first.
                var onNow = !!live && n.ssid === live;
                var row = el("div", "wifi-item saved-row");
                var name = el("span", "wifi-name", n.ssid);
                if (onNow)
                    name.appendChild(el("span", "wifi-badge wifi-badge-on", "connected"));
                row.appendChild(name);

                var acts = el("div", "saved-acts");
                // Connect: switch the machine to this saved network. It already has the password
                // (never sent to the browser), so we only send the SSID; the device promotes it to
                // preferred and reboots to join it (a runtime switch would deadlock - see firmware).
                // Offered on every row EXCEPT the connected one. Gating on index instead hid it from
                // row 0 - exactly the row you need when boot fell back to a different saved network.
                if (!onNow) {
                    var use = el("button", "wifi-use", "Connect");
                    use.type = "button";
                    use.addEventListener("click", function() {
                        if (!confirm(
                                "Switch to " + n.ssid + "? The machine reboots to join it.",
                            ))
                            return;
                        use.disabled = true;
                        setMsg("Switching to " + n.ssid + "...", true);
                        fetch("/wifilist", {
                                method: "POST",
                                headers: { "Content-Type": "application/x-www-form-urlencoded" },
                                body: "connect=" + encodeURIComponent(n.ssid),
                            })
                            .then(function(r) {
                                return r.json().then(function(j) {
                                    return { s: r.status, j: j };
                                });
                            })
                            .then(function(o) {
                                if (o.s === 200)
                                    setMsg("Rebooting to join " + n.ssid + ".", true);
                                else {
                                    setMsg(o.j.error || "Could not switch", false);
                                    use.disabled = false;
                                }
                            })
                            .catch(function() {
                                setMsg("Could not switch", false);
                                use.disabled = false;
                            });
                    });
                    acts.appendChild(use);
                }

                var del = el("button", "wifi-forget", "Forget");
                del.type = "button";
                del.addEventListener("click", function() {
                    del.disabled = true;
                    fetch("/wifilist", {
                            method: "POST",
                            headers: { "Content-Type": "application/x-www-form-urlencoded" },
                            body: "remove=" + encodeURIComponent(n.ssid),
                        })
                        .then(function() {
                            loadSavedWifi();
                        })
                        .catch(function() {
                            del.disabled = false;
                        });
                });
                acts.appendChild(del);
                row.appendChild(acts);
                box.appendChild(row);
            });
        })
        .catch(function() {
            var box = document.getElementById("wifiSaved");
            if (box)
                box.innerHTML = "<p class='f-hint'>Could not load saved networks.</p>";
        });
}

function renderWifiList(nets) {
    var list = document.getElementById("wifiList");
    if (!list) return;
    // Re-arm "Scan again". EVERY exit of pollWifiScan lands here - a list, giving up after 20
    // tries, and the fetch error path - so this is the one place that can do it without a second
    // timer to leak. Without it the button disables itself on the first press and stays dead for
    // the life of the panel, which is exactly the case an operator hits: the network they were
    // waiting for did not show up in the first scan.
    var again = document.querySelector(".wifi-rescan");
    if (again) again.disabled = false;
    list.innerHTML = "";
    if (nets === null) {
        wifiScanNets = [];
        list.appendChild(el("p", "f-hint", "Scan failed."));
        return;
    }
    wifiScanNets = nets; // remember for the Save-time SSID typo check
    if (!nets.length) {
        list.appendChild(el("p", "f-hint", "No networks found."));
        return;
    }
    // Drop what the Saved list directly above already shows. The connected network appeared
    // in both, and tapping the Nearby copy cleared the password box and asked the operator to
    // re-type a password that already works. Nothing is hidden - the row is one section up,
    // with a Connect button that uses the stored password.
    // DISPLAY only: wifiScanNets keeps the full scan, or typing a saved SSID by hand would
    // be rejected as a typo before the reboot.
    var shown = nets.filter(function(n) {
        return wifiSavedSsids.indexOf(n.ssid) < 0;
    });
    if (!shown.length) {
        list.appendChild(
            el("p", "f-hint", "Every network in range is already saved above."),
        );
        return;
    }
    shown.sort(function(a, b) {
        return b.rssi - a.rssi;
    });
    shown.forEach(renderScanRow);
}

/* Signal strength as the phone's own glyph: three arcs over a dot, lit from the inside out,
 * plus a padlock when the network needs a password. "lock  -73 dBm" asked the operator to
 * know that -73 is worse than -48; the arcs say it without being read.
 *
 * dBm is NOT thrown away - it stays in title/aria-label, so a screen reader and an engineer
 * chasing a weak link both still get the number. Strength is carried by HOW MANY arcs are
 * lit, never by colour alone. */
var RSSI_STEPS = [-60, -70, -80]; // >= -60 -> 3 arcs, >= -70 -> 2, >= -80 -> 1, else 0
function rssiBars(rssi) {
    var n = 0;
    for (var i = 0; i < RSSI_STEPS.length; i++)
        if (rssi >= RSSI_STEPS[i]) n++;
    return n; // 0..3
}
var RSSI_WORD = ["weak", "fair", "good", "excellent"];

function wifiMeta(n) {
    var bars = rssiBars(Number(n.rssi));
    var wrap = el("span", "wifi-meta");
    var sig = el("span", "wifi-sig sig-" + bars);
    // Arcs are drawn largest-first so the dimmed ones sit BEHIND; each is its own path so the
    // lit/dim split is a class on the parent, not a redraw.
    sig.innerHTML =
        SVG_OPEN +
        '<path class="a3" d="M2 8.5a16 16 0 0 1 20 0"/>' +
        '<path class="a2" d="M5 12a11 11 0 0 1 14 0"/>' +
        '<path class="a1" d="M8.5 15.5a6 6 0 0 1 7 0"/>' +
        '<circle class="a0" cx="12" cy="19" r="1.1" fill="currentColor" stroke="none"/>' +
        "</svg>";
    wrap.appendChild(sig);
    if (!n.open) {
        var lk = el("span", "wifi-lock");
        lk.innerHTML =
            SVG_OPEN +
            '<rect x="5" y="11" width="14" height="9" rx="2"/><path d="M8 11V7.5a4 4 0 0 1 8 0V11"/>' +
            "</svg>";
        wrap.appendChild(lk);
    }
    var text =
        "Signal " +
        RSSI_WORD[bars] +
        " (" +
        n.rssi +
        " dBm), " +
        (n.open ? "open network" : "password required");
    wrap.title = text;
    wrap.setAttribute("aria-label", text);
    return wrap;
}

function renderScanRow(n) {
    var list = document.getElementById("wifiList");
    if (list) {
        var b = el("button", "wifi-item");
        b.type = "button";
        b.appendChild(el("span", "wifi-name", n.ssid || "(hidden)"));
        b.appendChild(wifiMeta(n));
        b.addEventListener("click", function() {
            list.querySelectorAll(".wifi-item").forEach(function(x) {
                x.classList.remove("sel");
            });
            b.classList.add("sel");
            // The list has done its job - fold it away so the SSID and password boxes are the next
            // thing on screen. Not destroyed: the summary is still there to re-open and change.
            var scan = document.getElementById("wifiScan");
            if (scan) scan.open = false;
            // Fill the name box and drop straight into the password field: picking a network
            // is never the last step, so make the next one the obvious one.
            var box = document.getElementById("wifiSsid");
            if (box) box.value = n.ssid;
            var pass = document.getElementById("wifiPass");
            if (pass) {
                pass.value = "";
                // Open networks need no password - say so instead of waiting for input.
                pass.placeholder = n.open ?
                    "open network - leave empty" :
                    "enter the password";
                pass.scrollIntoView({ block: "center", behavior: "smooth" });
                pass.focus();
            }
            setMsg("Selected " + n.ssid, true);
        });
        list.appendChild(b);
    }
}

/* ---------- Calib wizard: mirrors the device's own flow ----------
 * UNREACHABLE RIGHT NOW and that is deliberate: the "calib" entry in CARDS is commented out
 * (see the note there). Everything below, the openPanel dispatch and the applySettingLock
 * step hook are kept working so the card can be switched back on in one line. Not dead code.
 * It is a human-in-the-loop procedure, not a routine we can just run: preheat to 55 C
 * (a 5 minute hold), pick a slot, then FOUR measurements, each needing the matching
 * tube physically placed in the slot. We only drive the device's buttons. */
var CALIB_TUBES = ["300", "200", "100", "0"];
var CALIB_STEPS = [{
        k: "preheatStart",
        t: "Start preheat",
        s: "Press Next to begin heating to 55 C.",
    },
    {
        k: "preheating",
        t: "Preheating to 55 C",
        s: "Both amplification heaters must reach 55 C, then hold 5 minutes. This takes a while.",
    },
    {
        k: "select",
        t: "Choose calibration",
        s: "Press Measure to enter calibration (not amplification).",
    },
    {
        k: "slot",
        t: "Pick the slot",
        s: "Choose which slot to calibrate, then press Measure.",
    },
    { k: "mode", t: "Confirm mode", s: "Press Measure to start." },
    {
        k: "measure",
        t: "Measure the 4 tubes",
        s: "Insert each tube, then press Measure - one press per tube.",
    },
    { k: "complete", t: "Result", s: "Check the slope, then Save." },
    {
        k: "save",
        t: "Save",
        s: "Press Measure to write the slope to the device.",
    },
];

function calibPost(action, extra) {
    return fetch("/calib?action=" + action + (extra || ""), { method: "POST" })
        .then(function(r) {
            return r.json().then(function(j) {
                return { s: r.status, j: j };
            });
        })
        .then(function(o) {
            if (!(o.j && o.j.ok))
                setMsg((o.j && o.j.error) || "Failed (" + o.s + ")", false);
            else setMsg("", true);
        })
        .catch(function() {
            setMsg("Failed - no connection", false);
        });
}

function renderCalib(form) {
    form.innerHTML = "";
    form.appendChild(
        el(
            "p",
            "f-hint",
            "Calibration runs on the device and needs you at the machine: it heats to 55 C (about 5 minutes), then you place 4 tubes (300, 200, 100, 0) in the slot, one measurement each. Only the slope is saved.",
        ),
    );

    var cur = calibStep;
    var idx = CALIB_STEPS.findIndex(function(s) {
        return s.k === cur;
    });

    CALIB_STEPS.forEach(function(s, i) {
        var d = el(
            "div",
            "wz-step" + (s.k === cur ? " on" : idx > i && idx >= 0 ? " done" : ""),
        );
        var n = el("span", "wz-num", String(i + 1));
        var b = el("div", "wz-body");
        b.appendChild(el("div", "wz-title", s.t));
        b.appendChild(el("div", "wz-sub", s.s));
        d.appendChild(n);
        d.appendChild(b);
        form.appendChild(d);
    });

    var act = el("div", "wz-actions");
    if (!cur) {
        var st = el("button", "save-btn", "Start calibration");
        st.type = "button";
        st.addEventListener("click", function() {
            calibPost("start");
        });
        act.appendChild(st);
    } else {
        if (cur === "slot") {
            var row = el("div", "f-row");
            var slotLbl = el("label", "f-lbl", "Slot");
            row.appendChild(slotLbl);
            var sel = el("select", "f-sel");
            sel.id = "calibSlot";
            slotLbl.htmlFor = sel.id;
            for (var i = 0; i < 10; i++) {
                var o = el("option", null, "#" + (i + 1));
                o.value = i;
                sel.appendChild(o);
            }
            sel.addEventListener("change", function() {
                calibPost("slot", "&n=" + sel.value);
            });
            row.appendChild(sel);
            form.appendChild(row);
        }
        if (cur === "measure") {
            form.appendChild(
                el(
                    "p",
                    "f-hint",
                    "Insert tube " +
                    CALIB_TUBES.join(" -> ") +
                    " in order, pressing Measure after each.",
                ),
            );
        }
        var nx = el("button", "save-btn", "Next (red)");
        nx.type = "button";
        nx.addEventListener("click", function() {
            calibPost("next");
        });
        var me = el("button", "save-btn", "Measure (blue)");
        me.type = "button";
        me.addEventListener("click", function() {
            calibPost("measure");
        });
        var cn = el("button", "back-btn");
        cn.type = "button";
        cn.style.cssText =
            "width:auto;padding:0 0.9rem;height:40px;font-weight:700";
        cn.textContent = "Cancel";
        cn.addEventListener("click", function() {
            // The device has no clean abort (white on the slot screen reboots it), so the
            // server unwinds the flags by hand. Without this the NEXT calibration is dead.
            calibPost("cancel");
        });
        act.appendChild(nx);
        act.appendChild(me);
        act.appendChild(cn);
    }
    form.appendChild(act);
}

/* ---------- config load + busy lock ---------- */
function loadConfig() {
    return fetch("/config")
        .then(function(r) {
            return r.json();
        })
        .then(function(d) {
            cfgCache = d;
        })
        .catch(function() {});
}

// Called from renderHome on every home event.
function applySettingLock(busy, cstep) {
    var changed = busy !== deviceBusy;
    deviceBusy = busy;
    var b = document.getElementById("setBusy");
    if (b) b.classList.toggle("hidden", !busy);
    if (changed) renderSetMenu(); // grey/ungrey the cards
    // Deliberately NOT closing an open panel when the device goes busy: closing it hides
    // the outcome of a save that was just rejected ("Not saved - the device started a
    // run"), which is exactly the message the user needs. Nothing is at risk by staying -
    // the card menu is greyed, and a Save from here gets a 409 from the server anyway.
    if (cstep !== calibStep) {
        calibStep = cstep;
        if (openCard === "calib") renderCalib(document.getElementById("setForm"));
    }
}

/* ---------- SSE wiring ---------- */
if (!!window.EventSource) {
    var source = new EventSource("/events");

    source.addEventListener(
        "open",
        function() {
            setStatus(true);
            // (re)connected: resync visible charts from the device so any stretch we
            // missed while disconnected is filled in, not lost. Only for a run that has
            // its own curve (homeCurveOn) - a pending run must stay empty.
            if (homeCurveOn) loadCurve(homeView);
            if (resultShown) loadCurve(resultView);
        },
        false,
    );

    source.addEventListener(
        "error",
        function(e) {
            if (e.target.readyState != EventSource.OPEN) setStatus(false);
        },
        false,
    );

    source.addEventListener(
        "home",
        function(e) {
            try {
                renderHome(JSON.parse(e.data));
            } catch (err) {
                console.error(err);
            }
        },
        false,
    );

    source.addEventListener(
        "new_readings",
        function(e) {
            try {
                plotPoint(homeView, JSON.parse(e.data)); // live chart lives on Home
            } catch (err) {
                console.error(err);
            }
        },
        false,
    );
}

/* Deep-link: open a tab from the URL hash, e.g. /#result */
(function() {
    var t = document.querySelector(
        '.nav-item[data-screen="' + (location.hash || "").slice(1) + '"]',
    );
    if (t) t.click();
})();