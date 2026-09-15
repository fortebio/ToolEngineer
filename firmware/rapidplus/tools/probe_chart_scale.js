/**
 * Probe: does the SHAPE of the curve survive a change of screen / orientation?
 *
 * The operator reads the SHAPE of the amplification curve (real sigmoid vs drift vs
 * staircase). Today both axes stretch to fit whatever box the viewport gives, so the
 * rendered slope of one and the same run is a property of the phone's orientation, not of
 * the sample. This probe turns that into numbers instead of an argument.
 *
 * It drives the REAL page and reads back the REAL Highcharts instance (plotWidth /
 * plotHeight / axis extremes) - a copy of the geometry maths in the probe would happily
 * agree with itself while the shipped chart is distorted.
 *
 * Modes:
 *   (default)  measure the chart AS SHIPPED across 7 screen sizes
 *   --after    apply the proposed rule live, then measure the same way
 *
 * The invariant the proposal introduces is the "min/step" column: one Y gridline step is
 * always worth the same number of X minutes, so a given lift-off draws at the same angle
 * everywhere. Watch that column, not the pixel columns.
 *
 * Usage:  python tools/sse_test_server.py --slots tools/slots.txt --reboot   (other shell)
 *         node tools/probe_chart_scale.js [--after] [url]
 *
 * Numbers behind docs/plan/2026-09-10-chart-scale-bat-bien-va-thanh-truot.md
 */
const { spawn } = require("child_process");
const http = require("http");
const path = require("path");
const os = require("os");

const AFTER = process.argv.includes("--after");
const URL = process.argv.find((a) => a.startsWith("http")) || "http://localhost:8000/";
const PORT = 9236;
const PROFILE = path.join(os.tmpdir(), "edge-chart-scale-probe");
const EDGE =
  process.env.EDGE_PATH ||
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe";

// The proposed constants. CHART_MIN_PER_STEP is THE SHAPE: one Y gridline step is worth
// this many minutes of X. 2.5 is what desktop already renders today, so this standardises
// on the scale engineers are used to reading rather than inventing a new one.
const CHART_MIN_PER_STEP = Number(process.env.MIN_PER_STEP || 2.5);
const CHART_PX_PER_MIN_MIN = Number(process.env.PX_PER_MIN_MIN || 10.4);

const SIZES = [
  { n: "portrait  360x740", w: 360, h: 740 },
  { n: "portrait  390x844", w: 390, h: 844 },
  { n: "portrait  412x915", w: 412, h: 915 },
  { n: "landscape 844x390", w: 844, h: 390 },
  { n: "landscape 915x412", w: 915, h: 412 },
  { n: "tablet    768x1024", w: 768, h: 1024 },
  { n: "desktop  1400x900", w: 1400, h: 900 },
];

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const j = (p) =>
  new Promise((res, rej) =>
    http
      .get({ host: "127.0.0.1", port: PORT, path: p }, (r) => {
        let b = "";
        r.on("data", (c) => (b += c));
        r.on("end", () => res(JSON.parse(b)));
      })
      .on("error", rej),
  );

async function main() {
  const edge = spawn(EDGE, [
    "--headless=new",
    "--disable-gpu",
    `--remote-debugging-port=${PORT}`,
    `--user-data-dir=${PROFILE}`,
    "about:blank",
  ]);
  edge.on("error", (e) => {
    console.error("cannot launch Edge:", e.message, "- set EDGE_PATH");
    process.exit(2);
  });

  let targets = [];
  for (let i = 0; i < 40; i++) {
    try {
      targets = await j("/json");
      if (targets.some((t) => t.type === "page")) break;
    } catch (e) {}
    await sleep(300);
  }
  const page = targets.find((t) => t.type === "page");
  if (!page) throw new Error("no Edge page target");

  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r));
  let id = 0;
  const pending = new Map();
  ws.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) {
      pending.get(m.id)(m);
      pending.delete(m.id);
    }
  };
  const send = (method, params = {}) =>
    new Promise((res) => {
      const mid = ++id;
      pending.set(mid, res);
      ws.send(JSON.stringify({ id: mid, method, params }));
    });
  const ev = async (expression) => {
    const r = await send("Runtime.evaluate", {
      expression,
      returnByValue: true,
      awaitPromise: true,
    });
    // CDP nests the value twice; reading r.result.value silently yields undefined, which
    // looks exactly like a broken page.
    return r.result && r.result.result ? r.result.result.value : undefined;
  };

  await send("Page.enable");
  await send("Runtime.enable");
  await send("Network.enable");
  // Without this the browser re-serves an older style.css and we measure the wrong build.
  await send("Network.setCacheDisabled", { cacheDisabled: true });
  await send("Emulation.setDeviceMetricsOverride", {
    width: 390,
    height: 844,
    deviceScaleFactor: 1,
    mobile: true,
  });
  await send("Page.navigate", { url: URL });

  // Poll for the element rather than sleeping a fixed time: the mock serves the
  // uncompressed 634 KB highcharts.js and a slower machine is still parsing.
  const clickWhenReady = async (sel) => {
    for (let i = 0; i < 80; i++) {
      if (await ev(`!!document.querySelector(${JSON.stringify(sel)})`)) {
        await ev(`document.querySelector(${JSON.stringify(sel)}).click()`);
        return;
      }
      await sleep(250);
    }
    throw new Error(`timed out waiting for ${sel} - is the mock server running?`);
  };
  await clickWhenReady('.nav-item[data-screen="result"]');
  await sleep(600);
  await clickWhenReady("#viewChartBtn");
  // A review takes ~8 s on the device; the mock is quick but poll for real points anyway.
  // Match on renderTo.id: homeChart also carries ten series and is created FIRST, so a
  // bare `find(series.length >= 10)` locks onto it and reports 0 points for ever.
  let got = 0;
  for (let i = 0; i < 80; i++) {
    got = await ev(
      `(()=>{const c=window.Highcharts&&Highcharts.charts.filter(Boolean)` +
        `.find(x=>x.renderTo.id==='resultChart');return c&&c.series[0]?c.series[0].points.length:0;})()`,
    );
    if (got > 10) break;
    await sleep(300);
  }
  if (!got) {
    console.error("no curve data - is the mock running with --slots/--reboot?");
    process.exit(2);
  }

  console.log(
    AFTER
      ? `AFTER  - proposed rule: 1 Y-step = ${CHART_MIN_PER_STEP} min, floor ${CHART_PX_PER_MIN_MIN} px/min\n`
      : "BEFORE - chart as shipped\n",
  );
  console.log(
    "size                 plotW x plotH  px/min  px/step  min/step  window/run   slider  steepest  card/usable",
  );

  const rows = [];
  for (const s of SIZES) {
    await send("Emulation.setDeviceMetricsOverride", {
      width: s.w,
      height: s.h,
      deviceScaleFactor: 1,
      mobile: s.w < 1000,
    });
    await sleep(850);
    const r = await ev(`(() => {
      const c = Highcharts.charts.filter(Boolean)
        .find(x => x.series.length >= 10 && x.renderTo.id === 'resultChart');
      if (!c) return null;
      const cont = document.getElementById('resultChart');
      const pts = c.series[0].points;
      const runLen = pts.length ? pts[pts.length - 1].x : 40;
      ${
        AFTER
          ? `
      // ---- tier 1: reclaim the chrome (measured: 117 -> 34 px in landscape) ----
      c.xAxis[0].update({ title: { text: null } }, false);
      c.update({ legend: { enabled: false }, chart: { spacingBottom: 2, spacingTop: 2 } }, false);
      cont.style.height = 'auto'; c.reflow();
      const chromeW = c.chartWidth - c.plotWidth, chromeH = 34;
      const plotW0 = cont.getBoundingClientRect().width - chromeW;
      // ---- tier 2/3: scale = max(floor, fit-the-run); HEIGHT FOLLOWS THE SCALE ----
      const pxMin = Math.max(${CHART_PX_PER_MIN_MIN}, plotW0 / runLen);
      cont.style.height = (pxMin * ${CHART_MIN_PER_STEP} * 10 + chromeH) + 'px';
      c.reflow();
      const win = Math.min(c.plotWidth / pxMin, runLen);
      c.xAxis[0].setExtremes(0, win, true, false);
      `
          : `c.reflow();`
      }
      const xa = c.xAxis[0], ya = c.yAxis[0];
      const pxMinA = c.plotWidth / (xa.max - xa.min);
      const pxUnitA = c.plotHeight / (ya.max - ya.min);
      // Steepest rendered segment across all ten channels - the shape number.
      let best = 0, slot = 0;
      c.series.forEach((se, si) => {
        const p = se.points;
        for (let i = 1; i < p.length; i++) {
          const a = Math.atan2((p[i].y - p[i-1].y) * pxUnitA,
                               (p[i].x - p[i-1].x) * pxMinA) * 180 / Math.PI;
          if (a > best) { best = a; slot = si + 1; }
        }
      });
      const card = document.getElementById('resultChartCard');
      const hdr = document.querySelector('.header').getBoundingClientRect().height;
      const nav = parseFloat(getComputedStyle(document.body).getPropertyValue('--nav-h')) || 0;
      const visible = xa.max - xa.min;
      return {
        pw: Math.round(c.plotWidth), ph: Math.round(c.plotHeight),
        pxMin: +pxMinA.toFixed(2), stepPx: +(c.plotHeight / 10).toFixed(1),
        minStep: +((c.plotHeight / 10) / pxMinA).toFixed(2),
        win: +visible.toFixed(1), runLen: +runLen.toFixed(1),
        slider: visible < runLen - 0.05,
        slope: +best.toFixed(1), slot,
        card: Math.round(card.getBoundingClientRect().height),
        usable: Math.round(innerHeight - hdr - nav),
      };
    })()`);
    if (!r) {
      console.log(s.n.padEnd(20) + "(no chart)");
      continue;
    }
    rows.push(r);
    console.log(
      s.n.padEnd(20) +
        String(r.pw).padStart(6) +
        " x" +
        String(r.ph).padStart(5) +
        String(r.pxMin).padStart(8) +
        String(r.stepPx).padStart(9) +
        String(r.minStep).padStart(10) +
        (r.win + "/" + r.runLen).padStart(12) +
        (r.slider ? "YES" : "no").padStart(8) +
        (r.slope + "deg").padStart(10) +
        ("  " + r.card + "/" + r.usable).padStart(13),
    );
  }

  const slopes = rows.map((r) => r.slope);
  const steps = rows.map((r) => r.minStep);
  const spread = (a) => (Math.max(...a) / Math.min(...a)).toFixed(2);
  console.log(
    `\nslope   ${Math.min(...slopes)}deg .. ${Math.max(...slopes)}deg   (spread ${spread(slopes)}x)` +
      `\nmin/step ${Math.min(...steps)} .. ${Math.max(...steps)}       (spread ${spread(steps)}x)` +
      `\n\nA spread of 1.00x on min/step is the invariant: the same run reads the same way on` +
      `\nevery screen. Anything above ~1.05x means the shape still depends on the viewport.`,
  );

  ws.close();
  edge.kill();
  process.exit(0);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
