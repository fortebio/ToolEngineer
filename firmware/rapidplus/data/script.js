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
navItems.forEach(function (btn) {
  btn.addEventListener("click", function () {
    cancelBackToHome(); // user chose a tab -> drop any pending auto-return-to-Home
    var name = btn.getAttribute("data-screen");
    document.querySelectorAll(".screen").forEach(function (s) {
      s.classList.toggle("active", s.id === "screen-" + name);
    });
    navItems.forEach(function (b) {
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

document.getElementById("reloadBtn").addEventListener("click", function () {
  location.reload();
});

/* ---------- Button controls: click sends a command to the device ----------
 * SSE is one-way (server->client), so a press is a separate POST. A chip marked
 * ".locked" (Start before names are confirmed) swallows the click - web-only
 * gate; the physical button on the device still works. */
document.querySelectorAll(".btn-chip").forEach(function (chip) {
  chip.addEventListener("click", function () {
    if (chip.classList.contains("locked")) return;
    var btn = chip.getAttribute("data-btn");
    // Web "Amplification" (red chip at the idle start screen) opens the naming gate
    // (name before heating) instead of pressing RED - which, like the physical button,
    // would heat immediately. Every other state sends the real button.
    if (btn === "red" && curPhase === "idle") btn = "ampname";
    fetch("/control?btn=" + encodeURIComponent(btn), { method: "POST" })
      .then(function (r) {
        if (!r.ok) console.error("control failed:", r.status);
      })
      .catch(function (err) {
        console.error("control error:", err);
      });
  });
});

/* ---------- Connection status badge ---------- */
function setStatus(connected) {
  document.getElementById("status").className =
    "status " + (connected ? "on" : "off");
  document.getElementById("statusText").textContent = connected
    ? "Online"
    : "Offline";
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

function renderHome(d) {
  if (d.device) {
    txt("deviceName", d.device);
    txt("setDevice", d.device);
  }
  if (d.company) {
    txt("companyName", d.company);
    txt("setCompany", d.company);
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
  document.getElementById("stateIcon").innerHTML = isHeat
    ? ICON_THERMO
    : ICON_CLOCK;
  var stateBanner = document.querySelector(".banner.state");
  if (stateBanner) stateBanner.classList.toggle("heat", isHeat);

  // ---- naming / chart gate state machine ----
  // Two naming points:
  //   "waitname" (amp flow) = name the slots BEFORE heating; Confirm starts the preheat.
  //   "waitamp"  (lysis flow) = name after heating; Confirm unlocks Start.
  // `confirmed` must survive the heating in between, so it is reset only on a fresh
  // cycle (back to idle), not on every non-naming phase.
  curPhase = phase;
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
  var chartMode =
    (phase === "waitamp" && confirmed) ||
    phase === "amplification" ||
    phase === "finished";

  // populate the naming table once per waitamp session (avoid clobbering typing)
  if (naming && !namingBuilt) {
    namingBuilt = true;
    loadNamingSlots();
  }
  if (!naming) namingBuilt = false;

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

  setHomeMode(naming, chartMode, curveReady);

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
function setHomeMode(naming, chartMode, curveReady) {
  show("namingCard", naming);
  show("homeChartCard", chartMode);
  show("tempFullLysis", !chartMode);
  show("tempFullAmp", !chartMode);
  show("tempCompact", chartMode);
  var wantCurve = chartMode && curveReady;
  if (wantCurve && !homeCurveOn) loadCurve(homeView); // just became loadable
  homeCurveOn = wantCurve;
  homeChartOn = chartMode;
  if (chartMode && homeView.chart) homeView.chart.reflow();
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
function buildSeries() {
  var colors = [
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
  return colors.map(function (c, i) {
    return {
      name: "#" + (i + 1),
      type: "line",
      color: c,
      marker: { symbol: "circle", radius: 2, fillColor: c },
    };
  });
}

function makeChart(divId) {
  return window.Highcharts
    ? new Highcharts.Chart({
        chart: { renderTo: divId },
        title: { text: undefined },
        credits: { enabled: false },
        xAxis: { title: { text: "Time (min)" }, labels: { enabled: true } },
        yAxis: {
          title: { text: null },
          labels: { enabled: true },
          tickInterval: 5,
          min: 0,
        }, // ticks every 5, from 0
        series: buildSeries(),
      })
    : null;
}

// A chart plus its per-run baseline state (baseline = mean of first BASELINE_N
// points; the chart plots value - baseline so each curve starts near 0).
function makeView(divId, lastUpdateId) {
  return {
    chart: makeChart(divId),
    luId: lastUpdateId,
    baseSum: new Array(10).fill(0),
    baseCount: new Array(10).fill(0),
    baseline: new Array(10).fill(0),
    // baseline-subtracted values per channel, kept so SG can re-smooth the whole run
    // (the chart series holds only the smoothed output).
    rawY: [[], [], [], [], [], [], [], [], [], []],
    nextIdx: 0,
  };
}
var homeView = makeView("homeChart", "lastUpdateHome");
var resultView = makeView("resultChart", null);
var BASELINE_N = 5;

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
var SG_W = (function () {
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

// Redraw one channel from its retained raw (baseline-subtracted) values, SG-smoothed.
// Noise floor is applied AFTER smoothing so it doesn't fabricate flat runs beforehand.
function drawSmoothed(v, ch) {
  if (!v.chart || !v.chart.series[ch]) return;
  var sm = sgSmooth(v.rawY[ch]),
    pts = [];
  for (var j = 0; j < sm.length; j++) {
    var val = sm[j];
    if (val < 5) val = 0; // noise floor: values below 5 -> 0
    pts.push([j * minPerRound, val]);
  }
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
    v.chart.series.forEach(function (s) {
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
    .then(function (r) {
      return r.json();
    })
    .then(function (d) {
      if (!v.chart || !d || !d.series) return;
      minPerRound = (d.intervalMs || 60000) / 60000;
      for (var ch = 0; ch < 10 && ch < d.series.length; ch++) {
        var vals = d.series[ch] || [];
        var bn = Math.min(BASELINE_N, vals.length),
          bsum = 0;
        for (var k = 0; k < bn; k++) bsum += vals[k];
        v.baseSum[ch] = bsum;
        v.baseCount[ch] = bn;
        v.baseline[ch] = bn ? bsum / bn : 0;
        v.rawY[ch] = [];
        for (var j = 0; j < vals.length; j++)
          v.rawY[ch].push(vals[j] - v.baseline[ch]);
        drawSmoothed(v, ch); // SG-smooth the whole channel, then setData
      }
      v.nextIdx = d.count || 0;
      applyNamesTo(v);
      applyVisTo(v);
      v.chart.redraw();
    })
    .catch(function () {});
}

function plotPoint(v, jsonValue) {
  if (!v.chart) return;
  var idx = jsonValue.i === undefined ? v.nextIdx : Number(jsonValue.i);
  Object.keys(jsonValue).forEach(function (k) {
    if (k.charAt(0) !== "#") return; // skip "i" / any meta key
    var ch = parseInt(k.slice(1), 10) - 1;
    if (!(ch >= 0 && ch < v.chart.series.length)) return;
    var y = Number(jsonValue[k]);
    if (isNaN(y)) return; // guard: SSE must send scalars, not arrays
    if (v.baseCount[ch] < BASELINE_N) {
      v.baseSum[ch] += y;
      v.baseCount[ch]++;
      v.baseline[ch] = v.baseSum[ch] / v.baseCount[ch];
    }
    // Retain the raw baseline-subtracted value; idx is the device round, so index by it
    // (a re-sent round overwrites, never appends a duplicate). Then re-smooth the whole
    // channel: the SG window over the newest points tightens as more of them arrive.
    v.rawY[ch][idx] = y - v.baseline[ch];
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
var slotNames = new Array(10).fill("");

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
function ingestSlots(data) {
  var slots = (data && data.slots) || [];
  for (var i = 0; i < SLOTS; i++) {
    if (slots[i] && slots[i].name !== undefined)
      slotNames[i] = slots[i].name || "";
  }
  return slots;
}

/* Build the table ONCE, after /slots resolves - no empty pre-build. The old two-phase
 * "empty now, fill later" flashed a table you could interact with before the fetch
 * landed, and the fill then REBUILT it, wiping a selection made in that window (harmless
 * for a text field mid-typing, but a disease <select> is picked in one instant). */
function loadResultSlots() {
  fetch("/slots")
    .then(function (r) {
      return r.json();
    })
    .then(function (d) {
      buildTable("slotBody", ingestSlots(d), true);
      // ready=false means no run is cached in RAM (e.g. after a reboot), but the last
      // run's raw record still lives in EEPROM. Ask the device to reload+recompute it,
      // then refresh once it lands. Not while a new run is amplifying (that hides the
      // stale cache on purpose - do not resurrect the previous run over it).
      if (d && d.ready === false && curPhase !== "amplification")
        reviewStoredRun();
    })
    .catch(function () {
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
  fetch("/reviewlast", { method: "POST" })
    .then(function (r) {
      if (!r.ok) throw 0; // busy, or nothing to review -> stop
      var tries = 0;
      (function poll() {
        fetch("/slots")
          .then(function (r) {
            return r.json();
          })
          .then(function (d) {
            if (d && d.ready) {
              buildTable("slotBody", ingestSlots(d), true);
              if (resultShown) loadCurve(resultView); // stored curve now available
              reviewing = false;
            } else if (++tries < 15) {
              setTimeout(poll, 200); // SettingTask drains in ~10ms; allow a slow flash read
            } else {
              reviewing = false; // gave up: EEPROM had no plausible stored run
            }
          })
          .catch(function () {
            reviewing = false;
          });
      })();
    })
    .catch(function () {
      reviewing = false;
    });
}

function loadNamingSlots() {
  fetch("/slots")
    .then(function (r) {
      return r.json();
    })
    .then(function (d) {
      buildTable("namingBody", ingestSlots(d), false);
    })
    .catch(function () {
      buildTable("namingBody", [], false);
    });
}

// Build a slot table. withResults=true adds CT + result columns (Result tab);
// false is the name-only table on Home.
/* Slot name = one of a fixed set of shrimp diseases, chosen from a dropdown (tap the
 * field -> the disease options drop out). Empty = the slot keeps its #N default.
 * A <select> keeps every existing hook working (.slot-name value, onRename, the
 * cross-table sync in onRename, applyNamesTo, fitNameColumn) with no plumbing changes. */
var DISEASES = ["PC", "EHP", "EMS", "WSSV", "TPD"];
var DASH = String.fromCharCode(8212); // em dash, built at runtime -> source stays ASCII

function makeDiseaseSelect(i, value) {
  var sel = document.createElement("select");
  sel.className = "slot-name disease-sel";
  sel.setAttribute("data-slot", i);
  [""].concat(DISEASES).forEach(function (d) {
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

function buildTable(tbodyId, slots, withResults) {
  var body = document.getElementById(tbodyId);
  if (!body) return;
  body.innerHTML = "";
  for (var i = 0; i < SLOTS; i++) {
    var s = slots[i] || {};
    var tr = document.createElement("tr");

    var cb = document.createElement("input");
    cb.type = "checkbox";
    cb.checked = isVisible(i);
    cb.setAttribute("data-slot", i);
    cb.addEventListener("change", onToggle);
    tr.appendChild(cell(cb));

    tr.appendChild(cell("#" + (i + 1)));

    var cur = s.name !== undefined ? s.name || "" : slotNames[i] || "";
    tr.appendChild(cell(makeDiseaseSelect(i, cur)));

    if (withResults) {
      tr.appendChild(
        cell(
          s.ct === null || s.ct === undefined ? "-" : Number(s.ct).toFixed(1),
        ),
      );
      var td = document.createElement("td");
      var r = s.result || "";
      if (r && "PNSEB".indexOf(r) >= 0) {
        var badge = document.createElement("span");
        badge.className = "res-badge res-" + r;
        badge.textContent = r;
        td.appendChild(badge);
      } else {
        td.innerHTML = '<span class="res-empty">-</span>';
      }
      tr.appendChild(td);
    }

    body.appendChild(tr);
  }
  fitNameColumn(tbodyId);
}

function cell(content) {
  var td = document.createElement("td");
  if (typeof content === "string") td.textContent = content;
  else td.appendChild(content);
  return td;
}

// Size the Name column to the widest name in THIS table; the other (value)
// columns share the rest evenly (table-layout: fixed).
function fitNameColumn(tbodyId) {
  var body = document.getElementById(tbodyId);
  if (!body) return;
  var table = body.parentNode; // <table>
  var inputs = table.querySelectorAll(".slot-name");
  var th = table.querySelector("thead th.col-name");
  if (!inputs.length || !th) return;
  var meas = document.getElementById("nameMeasure");
  if (!meas) {
    meas = document.createElement("span");
    meas.id = "nameMeasure";
    meas.style.cssText =
      "position:absolute;visibility:hidden;white-space:pre;left:-9999px;font-size:0.85rem";
    document.body.appendChild(meas);
  }
  var w = 0;
  inputs.forEach(function (inp) {
    meas.textContent = inp.value || inp.placeholder || "";
    if (meas.offsetWidth > w) w = meas.offsetWidth;
  });
  th.style.width = Math.min(Math.max(w + 34, 90), 460) + "px"; // + padding, clamped
}

function onToggle(e) {
  var i = Number(e.target.getAttribute("data-slot"));
  var v = slotVis();
  v[i] = e.target.checked;
  saveSlotVis(v);
  // keep the sibling table's checkbox in sync
  document
    .querySelectorAll('input[type="checkbox"][data-slot="' + i + '"]')
    .forEach(function (cb) {
      if (cb !== e.target) cb.checked = e.target.checked;
    });
  if (homeView.chart && homeView.chart.series[i])
    homeView.chart.series[i].setVisible(e.target.checked, true);
  if (resultView.chart && resultView.chart.series[i])
    resultView.chart.series[i].setVisible(e.target.checked, true);
}

function onRename(e) {
  var i = Number(e.target.getAttribute("data-slot"));
  slotNames[i] = e.target.value;
  fetch("/rename?slot=" + i + "&name=" + encodeURIComponent(e.target.value), {
    method: "POST",
  }).catch(function (err) {
    console.error("rename failed", err);
  });
  // keep the sibling table's control in sync (value + assigned-chip styling)
  document
    .querySelectorAll('.slot-name[data-slot="' + i + '"]')
    .forEach(function (inp) {
      if (inp !== e.target) inp.value = e.target.value;
      inp.classList.toggle("assigned", !!e.target.value);
    });
  applyNamesTo(homeView);
  applyNamesTo(resultView);
  fitNameColumn("namingBody");
  fitNameColumn("slotBody");
}

function applyNamesTo(v) {
  if (!v.chart) return;
  for (var i = 0; i < 10; i++) {
    if (v.chart.series[i])
      v.chart.series[i].update(
        { name: (slotNames[i] || "").trim() || "#" + (i + 1) },
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
}

/* Confirm names. Two contexts:
 *   waitname (amp flow) -> names are set, so tell the device to START HEATING now
 *     (RED). Heating runs next; the naming card goes away.
 *   waitamp (lysis flow) -> heating already done; just unlock Start + reveal the chart.
 * Blank slots fall back to #1-#10 inside applyNamesTo. */
var confirmBtn = document.getElementById("confirmNamesBtn");
if (confirmBtn)
  confirmBtn.addEventListener("click", function () {
    confirmed = true;
    applyNamesTo(homeView);
    if (curPhase === "waitname") {
      // advance the device: RED here = "Confirm & heat" (starts the 67C preheat).
      // renderHome will show the heating view next tick (confirmed, not naming/chart).
      fetch("/control?btn=red", { method: "POST" }).catch(function () {});
    } else {
      // waitamp: naming off, chart on but NOT loadable (run has not started, so the
      // chart opens empty instead of inheriting the previous run in the device buffer).
      setHomeMode(false, true, false); // renderHome reaffirms next tick
      document.getElementById("bRed").classList.remove("locked");
    }
  });

/* Result tab: reveal the stored-run chart on demand. */
var resultShown = false;
var viewBtn = document.getElementById("viewChartBtn");
if (viewBtn)
  viewBtn.addEventListener("click", function () {
    document.getElementById("resultChartCard").classList.remove("hide");
    resultShown = true;
    loadCurve(resultView); // draw the whole stored run
    if (resultView.chart) resultView.chart.reflow();
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

var CARDS = [
  {
    id: "wifi",
    title: "WiFi",
    desc: "Scan and join a network",
    custom: "wifi",
    icon: ico(
      '<path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><line x1="12" y1="20" x2="12.01" y2="20"/>',
    ),
  },
  {
    id: "id",
    title: "Device ID",
    desc: "Change the machine id",
    custom: "id",
    icon: ico(
      '<rect x="3" y="4" width="18" height="16" rx="2"/><circle cx="9" cy="10" r="2"/><path d="M15 8h3M15 12h3M5 17c1-2 5-2 6 0"/>',
    ),
  },
  {
    id: "profile",
    title: "Profile Configuration",
    desc: "Lysis / amplification temperature and time",
    icon: ico(
      '<path d="M14 14.76V4.5a2.5 2.5 0 0 0-5 0v10.26a4.5 4.5 0 1 0 5 0z"/>',
    ),
    fields: [
      {
        p: "lysis temperature",
        l: "Lysis temperature",
        u: "C",
        min: 20,
        max: 110,
        step: 0.1,
      },
      {
        p: "lysis duration",
        l: "Lysis duration",
        u: "s",
        min: 0,
        max: 65535,
        step: 1,
      },
      {
        p: "amplification temperature",
        l: "Amplification temperature",
        u: "C",
        min: 20,
        max: 110,
        step: 0.1,
      },
      {
        p: "amplification time",
        l: "Amplification rounds",
        u: "max 130",
        min: 1,
        max: 130,
        step: 1,
      },
      {
        p: "time per loop",
        l: "Time per round",
        u: "ms",
        min: 1000,
        max: 120000,
        step: 100,
      },
      {
        p: "opto preheat time",
        l: "Opto preheat",
        u: "s",
        min: 0,
        max: 3600,
        step: 1,
      },
    ],
    hint: "Rounds x time per round = run length. 120 x 20000 ms = a 40 minute run.",
  },
  {
    id: "led",
    title: "LED",
    desc: "Per-channel brightness",
    icon: ico(
      '<path d="M9 18h6M10 22h4"/><path d="M15.09 14c.18-.98.65-1.74 1.41-2.5A4.65 4.65 0 0 0 18 8 6 6 0 0 0 6 8c0 1 .23 2.23 1.5 3.5A4.61 4.61 0 0 1 8.91 14"/>',
    ),
    fields: [
      {
        p: "LED power",
        l: "LED power per slot",
        arr: 10,
        min: 0,
        max: 255,
        step: 1,
        cols: 5,
      },
      {
        p: "LED Duration",
        l: "LED on time before reading",
        u: "ms",
        min: 0,
        max: 10000,
        step: 10,
      },
    ],
  },
  {
    id: "calib",
    title: "Calibration",
    desc: "Run the on-device calibration",
    custom: "calib",
    icon: ico('<path d="M3 12h4l3 8 4-16 3 8h4"/>'),
  },
  {
    id: "pid",
    title: "PID / heater",
    desc: "Gains, overheat limits, hotlid PWM",
    icon: ico(
      '<circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M19.1 4.9L17 7M7 17l-2.1 2.1"/>',
    ),
    fields: [
      {
        p: "PID parameter",
        l: "PID - lysis heater",
        arr: 3,
        names: ["Kp", "Ki", "Kd"],
        min: 0,
        max: 1000,
        step: 0.01,
        cols: 3,
      },
      {
        p: "PID2 parameter",
        l: "PID - amplification heaters",
        arr: 3,
        names: ["Kp", "Ki", "Kd"],
        min: 0,
        max: 1000,
        step: 0.01,
        cols: 3,
      },
      {
        p: "PID3 parameter",
        l: "PID - hotlid",
        arr: 3,
        names: ["Kp", "Ki", "Kd"],
        min: 0,
        max: 1000,
        step: 0.01,
        cols: 3,
      },
      {
        p: "Bottom overheat value",
        l: "Bottom overheat",
        u: "C",
        arr: 3,
        min: 0,
        max: 50,
        step: 0.1,
        cols: 3,
      },
      {
        p: "Top overheat value",
        l: "Top overheat",
        u: "C",
        arr: 2,
        min: 0,
        max: 50,
        step: 0.1,
      },
      {
        p: "top heater PWM",
        l: "Hotlid PWM [low, high]",
        mat: [2, 2],
        min: 0,
        max: 255,
        step: 1,
      },
    ],
    hint: "Wrong gains or setpoints drive the heaters directly. Change with care.",
  },
  {
    id: "other",
    title: "Other parameters",
    desc: "Algorithm, sensors, buzzer",
    icon: ico(
      '<line x1="4" y1="21" x2="4" y2="14"/><line x1="4" y1="10" x2="4" y2="3"/><line x1="12" y1="21" x2="12" y2="12"/><line x1="12" y1="8" x2="12" y2="3"/><line x1="20" y1="21" x2="20" y2="16"/><line x1="20" y1="12" x2="20" y2="3"/><line x1="1" y1="14" x2="7" y2="14"/><line x1="9" y1="8" x2="15" y2="8"/><line x1="17" y1="16" x2="23" y2="16"/>',
    ),
    fields: [
      { p: "parameters.min increase", l: "Min increase", step: 0.1 },
      { p: "parameters.min sharpness", l: "Min sharpness", step: 0.1 },
      {
        p: "parameters.min slight positive time",
        l: "Min slight positive time",
        step: 0.1,
      },
      {
        p: "parameters.detect shape",
        l: "Detect shape (lag phase)",
        bool: true,
      },
      {
        p: "parameters.detection margin time",
        l: "Detection margin time",
        step: 0.1,
      },
      { p: "parameters.arm percentile", l: "Arm percentile", step: 0.01 },
      {
        p: "parameters.transition percentile",
        l: "Transition percentile",
        step: 0.01,
      },
      { p: "parameters.sg order", l: "SG order", min: 0, max: 255, step: 1 },
      { p: "parameters.sg window", l: "SG window", min: 0, max: 255, step: 1 },
      {
        p: "parameters.baseline start",
        l: "Baseline start",
        u: "min",
        min: 0,
        max: 255,
        step: 1,
      },
      {
        p: "parameters.baseline range",
        l: "Baseline range",
        u: "min",
        min: 0,
        max: 255,
        step: 1,
      },
      {
        p: "temperature value calibration",
        l: "Temperature offset (bottom 1-3, top 1-2, ambient)",
        u: "C",
        arr: 6,
        min: -20,
        max: 20,
        step: 0.1,
        cols: 3,
      },
      {
        p: "bottom temperature sensor seq",
        l: "Bottom sensor order",
        arr: 3,
        min: 0,
        max: 2,
        step: 1,
        cols: 3,
      },
      {
        p: "top temperature sensor seq",
        l: "Top sensor order",
        arr: 3,
        min: 0,
        max: 2,
        step: 1,
        cols: 3,
      },
      { p: "units", l: "Units", text: true, maxlen: 9 },
      { p: "buzzer", l: "Buzzer", sel: ["On", "Off"] },
      { p: "kitId", l: "Kit id", min: 0, step: 1 },
    ],
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
  CARDS.forEach(function (c) {
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
    b.addEventListener("click", function () {
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
  awaitCfg(res.j.seq || 0, function (state) {
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
  backHomeTimer = setTimeout(function () {
    backHomeTimer = null;
    showMenu(); // reset the panel, so re-entering Setting starts at the card menu
    var home = document.querySelector('.nav-item[data-screen="home"]');
    if (home) home.click();
  }, delay || 900);
}

function openPanel(id) {
  var c = CARDS.find(function (x) {
    return x.id === id;
  });
  if (!c || deviceBusy) return;
  openCard = id;
  document.getElementById("setPanelTitle").textContent = c.title;
  document.getElementById("setMenu").classList.add("hide");
  document.getElementById("setAbout").classList.add("hide");
  document.getElementById("setDetail").classList.remove("hide");
  setMsg("");
  var form = document.getElementById("setForm");
  form.innerHTML = "";
  if (c.custom === "wifi") return renderWifi(form);
  if (c.custom === "id") return renderDeviceId(form);
  if (c.custom === "calib") return renderCalib(form);
  renderFields(form, c);
}

document.getElementById("setBack").addEventListener("click", showMenu);

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

  c.fields.forEach(function (f) {
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
        ce.appendChild(ix);
        ce.appendChild(ip);
        w.appendChild(ce);
      }
      w.setAttribute("data-path", f.p);
      row.appendChild(w);
    } else if (f.bool) {
      var sb = el("select", "f-sel");
      ["true", "false"].forEach(function (v) {
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
      f.sel.forEach(function (v) {
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
      var n = numInput(f, cur);
      n.setAttribute("data-path", f.p);
      n.setAttribute("data-kind", "num");
      n.setAttribute("data-min", f.min === undefined ? "" : f.min);
      n.setAttribute("data-max", f.max === undefined ? "" : f.max);
      row.appendChild(n);
    }
    form.appendChild(row);
  });

  var save = el("button", "save-btn", "Save");
  save.type = "button";
  save.addEventListener("click", function () {
    saveFields(c, save);
  });
  form.appendChild(save);
}

// Collect ONLY this card's keys into the body: the device merges them onto the rest.
function collectFields(c, form) {
  var body = {};
  var bad = null;
  c.fields.forEach(function (f) {
    if (f.mat) {
      var w = form.querySelector('[data-path="' + f.p + '"]');
      var rows = [];
      for (var r = 0; r < f.mat[0]; r++) rows.push([]);
      w.querySelectorAll("input").forEach(function (i) {
        var rc = i.getAttribute("data-mat").split(",");
        rows[+rc[0]][+rc[1]] = Number(i.value);
        if (i.value === "" || isNaN(Number(i.value))) bad = bad || f.l;
      });
      setPath(body, f.p, rows);
    } else if (f.arr) {
      var wa = form.querySelector('[data-path="' + f.p + '"]');
      var a = [];
      wa.querySelectorAll("input").forEach(function (i) {
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
        setPath(body, f.p, Number(e2.value));
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
    .then(function (res) {
      return res.json().then(function (j) {
        return { s: res.status, j: j };
      });
    })
    .then(function (o) {
      settleSave(o, btn, function () {
        setMsg("Saved to the device.", true);
        backToHomeAfterSave();
      });
    })
    .catch(function () {
      btn.disabled = false;
      setMsg("Save failed - no connection", false);
    });
}

/* ---------- Device ID (two stores: global id_device + parameter.device_id) ---------- */
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
  i.value = (cfgCache && cfgCache["device ID"]) || "";
  row.appendChild(lbl);
  row.appendChild(i);
  form.appendChild(row);
  var b = el("button", "save-btn", "Save");
  b.type = "button";
  b.addEventListener("click", function () {
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
      .then(function (r) {
        return r.json().then(function (j) {
          return { s: r.status, j: j };
        });
      })
      .then(function (o) {
        settleSave(o, b, function () {
          setMsg("Saved.", true);
          backToHomeAfterSave();
        });
      })
      .catch(function () {
        b.disabled = false;
        setMsg("Failed - no connection", false);
      });
  });
  form.appendChild(b);
}

/* ---------- WiFi: async scan, then save + reboot ---------- */
var wifiPick = null;
function renderWifi(form) {
  wifiPick = null;
  form.appendChild(
    el(
      "p",
      "f-hint",
      "The device reboots to join the network. If you are connected to its RAPID-... hotspot, this page will disconnect.",
    ),
  );
  var list = el("div", "wifi-list");
  list.id = "wifiList";
  list.appendChild(el("p", "f-hint", "Scanning..."));
  form.appendChild(list);

  var row = el("div", "f-row");
  row.appendChild(el("label", "f-lbl", "Password"));
  var pw = el("input", "f-in");
  pw.type = "password";
  pw.id = "wifiPass";
  pw.maxLength = 54; // EEPROM slot is 54 chars, not WPA2's 63
  pw.placeholder = "leave empty for an open network";
  row.appendChild(pw);
  form.appendChild(row);

  var b = el("button", "save-btn", "Save & reboot");
  b.type = "button";
  b.addEventListener("click", function () {
    if (!wifiPick) {
      setMsg("Pick a network first", false);
      return;
    }
    b.disabled = true;
    setMsg("Saving...", true);
    fetch("/wifi", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body:
        "ssid=" +
        encodeURIComponent(wifiPick) +
        "&pass=" +
        encodeURIComponent(pw.value),
    })
      .then(function (r) {
        return r.json().then(function (j) {
          return { s: r.status, j: j };
        });
      })
      .then(function (o) {
        settleSave(o, b, function () {
          setMsg(
            "Saved. The device is rebooting to join " + wifiPick + ".",
            true,
          );
          // Longer than the other cards: this message is the one worth reading, and
          // the reboot drops the connection anyway - Home is where they watch it
          // come back (Offline -> Online).
          backToHomeAfterSave(2200);
        });
      })
      .catch(function () {
        b.disabled = false;
        setMsg("Failed - no connection", false);
      });
  });
  form.appendChild(b);
  pollWifiScan(0);
}

// The device scans asynchronously (a blocking scan would freeze every SSE client), so
// GET /wifiscan answers 202 while it runs and 200 with the list when it is done.
function pollWifiScan(tries) {
  if (openCard !== "wifi") return;
  fetch("/wifiscan")
    .then(function (r) {
      return r.json();
    })
    .then(function (d) {
      if (openCard !== "wifi") return;
      if (d.scanning) {
        if (tries < 20)
          setTimeout(function () {
            pollWifiScan(tries + 1);
          }, 700);
        else renderWifiList([]);
        return;
      }
      renderWifiList(d.networks || []);
    })
    .catch(function () {
      if (openCard === "wifi") renderWifiList(null);
    });
}

function renderWifiList(nets) {
  var list = document.getElementById("wifiList");
  if (!list) return;
  list.innerHTML = "";
  if (nets === null) {
    list.appendChild(el("p", "f-hint", "Scan failed."));
    return;
  }
  if (!nets.length) {
    list.appendChild(el("p", "f-hint", "No networks found."));
    return;
  }
  nets.sort(function (a, b) {
    return b.rssi - a.rssi;
  });
  nets.forEach(function (n) {
    var b = el("button", "wifi-item");
    b.type = "button";
    b.appendChild(el("span", "wifi-name", n.ssid || "(hidden)"));
    b.appendChild(
      el("span", "wifi-meta", (n.open ? "open  " : "lock  ") + n.rssi + " dBm"),
    );
    b.addEventListener("click", function () {
      wifiPick = n.ssid;
      list.querySelectorAll(".wifi-item").forEach(function (x) {
        x.classList.remove("sel");
      });
      b.classList.add("sel");
      setMsg("Selected " + n.ssid, true);
    });
    list.appendChild(b);
  });
}

/* ---------- Calib wizard: mirrors the device's own flow ----------
 * It is a human-in-the-loop procedure, not a routine we can just run: preheat to 55 C
 * (a 5 minute hold), pick a slot, then FOUR measurements, each needing the matching
 * tube physically placed in the slot. We only drive the device's buttons. */
var CALIB_TUBES = ["300", "200", "100", "0"];
var CALIB_STEPS = [
  {
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
    .then(function (r) {
      return r.json().then(function (j) {
        return { s: r.status, j: j };
      });
    })
    .then(function (o) {
      if (!(o.j && o.j.ok))
        setMsg((o.j && o.j.error) || "Failed (" + o.s + ")", false);
      else setMsg("", true);
    })
    .catch(function () {
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
  var idx = CALIB_STEPS.findIndex(function (s) {
    return s.k === cur;
  });

  CALIB_STEPS.forEach(function (s, i) {
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
    st.addEventListener("click", function () {
      calibPost("start");
    });
    act.appendChild(st);
  } else {
    if (cur === "slot") {
      var row = el("div", "f-row");
      row.appendChild(el("label", "f-lbl", "Slot"));
      var sel = el("select", "f-sel");
      for (var i = 0; i < 10; i++) {
        var o = el("option", null, "#" + (i + 1));
        o.value = i;
        sel.appendChild(o);
      }
      sel.addEventListener("change", function () {
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
    nx.addEventListener("click", function () {
      calibPost("next");
    });
    var me = el("button", "save-btn", "Measure (blue)");
    me.type = "button";
    me.addEventListener("click", function () {
      calibPost("measure");
    });
    var cn = el("button", "back-btn");
    cn.type = "button";
    cn.style.cssText =
      "width:auto;padding:0 0.9rem;height:40px;font-weight:700";
    cn.textContent = "Cancel";
    cn.addEventListener("click", function () {
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
    .then(function (r) {
      return r.json();
    })
    .then(function (d) {
      cfgCache = d;
    })
    .catch(function () {});
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
    function () {
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
    function (e) {
      if (e.target.readyState != EventSource.OPEN) setStatus(false);
    },
    false,
  );

  source.addEventListener(
    "home",
    function (e) {
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
    function (e) {
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
(function () {
  var t = document.querySelector(
    '.nav-item[data-screen="' + (location.hash || "").slice(1) + '"]',
  );
  if (t) t.click();
})();
