/* ============================================================
 * RAPID live dashboard - Home / Process / Setting, fed by SSE.
 *   event "home"         -> home screen state (temps, status, notify, buttons)
 *   event "new_readings" -> process chart, scalar per channel {"#1":num,...}
 * ============================================================ */

/* Inline SVG icons (no emoji/font dependency, encoding-safe) */
var SVG_OPEN = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">';
var ICON_CLOCK = SVG_OPEN + '<circle cx="12" cy="12" r="9"/><polyline points="12 7 12 12 15.5 14"/></svg>';
var ICON_THERMO = SVG_OPEN + '<path d="M14 14.76V4.5a2.5 2.5 0 0 0-5 0v10.26a4.5 4.5 0 1 0 5 0z"/></svg>';

/* ---------- Bottom-nav screen switching ---------- */
var navItems = document.querySelectorAll(".nav-item");
navItems.forEach(function (btn) {
  btn.addEventListener("click", function () {
    var name = btn.getAttribute("data-screen");
    document.querySelectorAll(".screen").forEach(function (s) {
      s.classList.toggle("active", s.id === "screen-" + name);
    });
    navItems.forEach(function (b) { b.classList.toggle("active", b === btn); });
    if (name === "process" && chartT) chartT.reflow(); // chart was hidden -> fix sizing
  });
});

document.getElementById("reloadBtn").addEventListener("click", function () {
  location.reload();
});

/* ---------- Button controls: click sends a command to the device ----------
 * SSE is one-way (server->client), so a press is a separate POST. The device
 * acts on it and the resulting LED state comes back via the "home" event. */
document.querySelectorAll(".btn-chip").forEach(function (chip) {
  chip.addEventListener("click", function () {
    var btn = chip.getAttribute("data-btn");
    fetch("/control?btn=" + encodeURIComponent(btn), { method: "POST" })
      .then(function (r) { if (!r.ok) console.error("control failed:", r.status); })
      .catch(function (err) { console.error("control error:", err); });
  });
});

/* ---------- Connection status badge ---------- */
function setStatus(connected) {
  document.getElementById("status").className = "status " + (connected ? "on" : "off");
  document.getElementById("statusText").textContent = connected ? "Online" : "Offline";
}

/* ---------- Home screen rendering ---------- */
function txt(id, v) { document.getElementById(id).textContent = v; }

function renderHome(d) {
  if (d.device)  { txt("deviceName", d.device); txt("setDevice", d.device); }
  if (d.company) { txt("companyName", d.company); txt("setCompany", d.company); }

  var t = d.temps || {};
  txt("tLysis",    fmtTemp(t.lysis));
  txt("tAmpLeft",  fmtTemp(t.ampLeft));
  txt("tAmpRight", fmtTemp(t.ampRight));
  txt("tTopLeft",  fmtTemp(t.topLeft));
  txt("tTopRight", fmtTemp(t.topRight));

  var s = d.status || {};
  txt("stateTitle", s.title || "Idle");
  txt("stateSub", s.subtitle || "");
  // heater phase -> thermometer icon + amber accent, running process -> clock
  var isHeat = s.phase === "heater";
  document.getElementById("stateIcon").innerHTML = isHeat ? ICON_THERMO : ICON_CLOCK;
  var stateBanner = document.querySelector(".banner.state");
  if (stateBanner) stateBanner.classList.toggle("heat", isHeat);

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
  dot("bGreen", b.green); dot("bRed", b.red); dot("bWhite", b.white);
}

var DEG = String.fromCharCode(176); // degree sign built at runtime -> source stays ASCII
function fmtTemp(v) { return (v === undefined || v === null) ? "-" : v.toFixed(1) + DEG + "C"; }
function dot(id, on) {
  var el = document.getElementById(id);
  el.classList.toggle("on", !!on);
}

/* ---------- Process chart (Highcharts) ----------
 * Guarded: if the Highcharts CDN fails (e.g. phone on the device SoftAP with no
 * internet), keep chartT null so the rest of the script - SSE wiring, the whole
 * Home screen - still runs instead of throwing at top level. */
var chartT = window.Highcharts
  ? new Highcharts.Chart({
      chart: { renderTo: "chart" },
      title: { text: undefined },
      credits: { enabled: false },
      xAxis: { title: { text: "Time" }, labels: { enabled: false } },
      yAxis: { title: { text: "Fluorescent (nm FAM)" } },
      series: buildSeries(),
    })
  : null;

function buildSeries() {
  var colors = ["#00BFFF","#FF0000","#FFD400","#32CD32","#D2691E",
                "#00CED1","#9400D3","#9ACD32","#0000FF","#FF69B4"];
  return colors.map(function (c, i) {
    return { name: "#" + (i + 1), type: "line", color: c,
             marker: { symbol: "circle", radius: 2, fillColor: c } };
  });
}

function plotResult(jsonValue) {
  if (!chartT) return; // Highcharts unavailable -> skip chart, Home still works
  var keys = Object.keys(jsonValue);
  for (var i = 0; i < keys.length && i < chartT.series.length; i++) {
    var y = Number(jsonValue[keys[i]]);
    if (isNaN(y)) continue;               // guard: SSE must send scalars, not arrays
    var x = new Date().getTime();
    var shift = chartT.series[i].data.length > 40;
    chartT.series[i].addPoint([x, y], true, shift, true);
  }
  var lu = document.getElementById("lastUpdate");
  if (lu) lu.textContent = new Date().toLocaleTimeString();
}

/* ---------- SSE wiring ---------- */
if (!!window.EventSource) {
  var source = new EventSource("/events");

  source.addEventListener("open", function () { setStatus(true); }, false);

  source.addEventListener("error", function (e) {
    if (e.target.readyState != EventSource.OPEN) setStatus(false);
  }, false);

  source.addEventListener("home", function (e) {
    try { renderHome(JSON.parse(e.data)); } catch (err) { console.error(err); }
  }, false);

  source.addEventListener("new_readings", function (e) {
    try { plotResult(JSON.parse(e.data)); } catch (err) { console.error(err); }
  }, false);
}
