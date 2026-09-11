/**
 * Guard: the curve's SHAPE must not depend on the screen it is read on.
 *
 * The operator reads the shape (real sigmoid vs optical drift vs staircase) to decide what a
 * channel did. Before 2026-09-10 both axes stretched to fit the viewport, so one and the same
 * stored run drew its lift-off at 38.1 deg in landscape and 83.6 deg in portrait, and one Y
 * gridline step was worth anything from 0.64 to 7.28 minutes - an 11.4x spread on the same
 * phone, just held differently. The fix pins the scale (one Y step == CHART_MIN_PER_STEP
 * minutes of X) and pans a WINDOW of the run with a slider when it no longer fits.
 *
 * What is locked here:
 *   1. min/step is the SAME on every screen size          <- the invariant itself
 *   2. the pan slider appears exactly when the window is shorter than the run
 *   3. dragging the slider really moves the visible window
 *   4. there is NO escape hatch back to the stretch view, and the height stays script-owned
 *   5. the live tail-follow re-arms when the slider is returned to the end
 *   6. a LIVE run is drawn at that same scale from its first rounds - the scale comes from
 *      the run's PLANNED length, not from how many rounds have arrived. Sections 1-5 read a
 *      stored run, and that is exactly where the 2026-09-11 bug hid: on Home during a run the
 *      plot was 3472px tall at round 3, 4142px at round 6, and shrank every 20 s until round
 *      ~79 (measured, 390x844) - min/step read 2.5 the whole time, so 1-5 were green.
 *
 * Measured against the REAL chart (plotWidth / plotHeight / axis extremes read back off the
 * live Highcharts instance), never a copy of the geometry: a copy would agree with itself
 * while the shipped chart is distorted, which is the exact bug this guards.
 *
 * Usage:  python tools/sse_test_server.py --slots tools/slots.txt --reboot   (other shell -
 *              this test does NOT start it; without the mock you get a timeout that reads
 *              like a UI regression)
 *         node tools/test_chart_scale.js
 *
 * Section 6 starts a real run on the mock through /control. The mock is put back to idle
 * with its POST /__reset test hook first and last, so the guard can be re-run against the
 * same mock and does not leave it busy for whatever runs next.
 */
const { spawn } = require("child_process");
const http = require("http");
const path = require("path");
const os = require("os");

const PORT = 9237;
// UNIQUE per run. With a fixed --user-data-dir, launching while a browser from an earlier run
// is still alive does not start a new one - it attaches to the old one, and its leftover tab
// (still zoomed, still at the previous size) answers every query. That looked like sixteen product
// failures and was one stale window.
const PROFILE = path.join(os.tmpdir(), "edge-chart-scale-" + process.pid);
const URL = process.argv[2] || "http://localhost:8000/";
const EDGE =
  process.env.EDGE_PATH ||
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe";

// Portrait phones (the run cannot fit -> slider), a landscape phone (the case that used to
// squash the plot to 93px), a tablet and a desktop.
const SIZES = [
  { n: "portrait  360x740", w: 360, h: 740, mobile: true },
  { n: "portrait  390x844", w: 390, h: 844, mobile: true },
  { n: "portrait  412x915", w: 412, h: 915, mobile: true },
  { n: "landscape 844x390", w: 844, h: 390, mobile: true },
  { n: "tablet    768x1024", w: 768, h: 1024, mobile: true },
  { n: "desktop  1400x900", w: 1400, h: 900, mobile: false },
];

// Rounding: plot sizes are integers, so min/step lands a hair off the nominal value. 2% is
// far below the 11.4x this guard exists to catch and far above pixel rounding.
const TOL = 0.02;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// The mock (not the browser): drive the run the way the web chips do.
const MOCK = new (require("url").URL)(URL);
const mock = (method, p) =>
  new Promise((res, rej) => {
    const q = http.request(
      { host: MOCK.hostname, port: MOCK.port || 80, path: p, method },
      (r) => {
        let b = "";
        r.on("data", (c) => (b += c));
        r.on("end", () => {
          try {
            res(JSON.parse(b));
          } catch (e) {
            res(null);
          }
        });
      },
    );
    q.on("error", rej);
    q.end();
  });
const mockPhase = async () => {
  const h = await mock("GET", "/home");
  return h && h.status ? h.status.phase : "";
};
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

let failures = 0;
const check = (ok, msg, extra) => {
  console.log(`  ${ok ? "PASS" : "FAIL"}  ${msg}${extra ? "  (" + extra + ")" : ""}`);
  if (!ok) failures++;
};

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
  const size = (s) =>
    send("Emulation.setDeviceMetricsOverride", {
      width: s.w,
      height: s.h,
      deviceScaleFactor: 1,
      mobile: s.mobile,
    });

  await send("Page.enable");
  await send("Runtime.enable");
  await send("Network.enable");
  // Without this the browser re-serves an older style.css and the guard measures a build
  // that is not the one on disk.
  await send("Network.setCacheDisabled", { cacheDisabled: true });
  // Idle first: a mock left mid-run hides the stored run (/slots ready=false while
  // amplifying) and section 1 would report "no curve data" for an environment reason.
  const reset = await mock("POST", "/__reset").catch(() => null);
  if (!reset || !reset.ok) {
    console.error("mock has no POST /__reset - update tools/sse_test_server.py");
    process.exit(2);
  }
  await size(SIZES[1]);
  await send("Page.navigate", { url: URL });

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

  // Match the chart by renderTo.id: homeChart also carries ten series and is built FIRST, so
  // a bare "ten series" search locks onto it and reports 0 points for ever.
  const CHART = `Highcharts.charts.filter(Boolean).find(x => x.renderTo.id === 'resultChart')`;
  let got = 0;
  for (let i = 0; i < 80; i++) {
    got = await ev(
      `(()=>{const c=window.Highcharts&&${CHART};return c&&c.series[0]?c.series[0].points.length:0;})()`,
    );
    if (got > 10) break;
    await sleep(300);
  }
  if (!got) {
    console.error("no curve data - start the mock with --slots tools/slots.txt --reboot");
    process.exit(2);
  }

  // One read of everything the invariant is stated in terms of.
  const READ = `(() => {
    const c = ${CHART};
    const xa = c.xAxis[0], ya = c.yAxis[0];
    const pan = document.getElementById('resultChartPan');
    const nav = document.getElementById('resultChartNav');
    const pts = c.series[0].points;
    const runLen = pts.length ? pts[pts.length - 1].x : 0;
    const pxMin = c.plotWidth / (xa.max - xa.min);
    return {
      vw: innerWidth,
      runLen: +runLen.toFixed(2),
      win: +(xa.max - xa.min).toFixed(2),
      lo: +xa.min.toFixed(2), hi: +xa.max.toFixed(2),
      steps: ya.tickPositions.length - 1,
      minStep: +((c.plotHeight / 10) / pxMin).toFixed(3),
      plotW: Math.round(c.plotWidth), plotH: Math.round(c.plotHeight),
      navHidden: !nav || nav.classList.contains('hide'),
      panDisabled: !pan || pan.disabled,
      panMax: pan ? +Number(pan.max).toFixed(2) : null,
      panValue: pan ? +Number(pan.value).toFixed(2) : null,
      // Any control that could hand the axes back to the viewport reopens the very spread
      // this file exists to close, so their ABSENCE is part of the invariant.
      scaleCtrls: document.querySelectorAll('.chart-fit, [id$=ChartFit]').length,
      panLabel: pan ? pan.getAttribute('aria-label') : null,
      inlineH: document.getElementById('resultChart').style.height || '',
    };
  })()`;

  /* Resize and WAIT FOR IT TO LAND. A fixed sleep is not enough: the page debounces resize
   * before rescaling, and a desktop -> portrait switch (which also flips CDP's `mobile` flag)
   * took longer than 850 ms, so the next read returned the OLD geometry - the whole test then
   * measured a chart that was still desktop-shaped and five checks failed for one reason.
   * Poll until two consecutive reads agree instead; a page that genuinely stopped responding
   * to resize still fails here, it just fails by timing out rather than by reading stale
   * numbers. */
  const resize = async (s) => {
    await size(s);
    let prev = null;
    for (let i = 0; i < 40; i++) {
      await sleep(150);
      const r = await ev(READ);
      // innerWidth first: "two reads agree" alone cannot tell SETTLED apart from NEVER MOVED,
      // and a resize that silently did not apply then returns stale numbers that read as a
      // product bug. Check the viewport really is the one that was asked for.
      if (
        r.vw === s.w &&
        prev &&
        prev.vw === s.w &&
        r.plotW === prev.plotW &&
        r.plotH === prev.plotH &&
        r.panMax === prev.panMax
      )
        return r;
      prev = r;
    }
    throw new Error(
      `chart never settled at ${s.n} (last innerWidth ${prev && prev.vw}, wanted ${s.w})`,
    );
  };

  // ---- 1 + 2: the invariant, and when the slider is offered ------------------------------
  console.log("1. one Y gridline step is worth the same minutes on every screen\n");
  const seen = [];
  for (const s of SIZES) {
    const r = await resize(s);
    seen.push({ s, r });
    check(
      r.steps === 10,
      `${s.n}  Y axis still 10 steps`,
      `got ${r.steps}`,
    );
    console.log(
      `        ${String(r.plotW).padStart(4)}x${String(r.plotH).padStart(4)}px` +
        `   min/step ${String(r.minStep).padStart(6)}` +
        `   window ${String(r.win).padStart(5)}/${r.runLen}` +
        `   slider ${r.panDisabled ? "off" : "ON "}`,
    );
    // The slider is exactly the mechanism for "the run does not fit", so its state must
    // follow that and nothing else - a slider that is on when everything is visible is a
    // control that does nothing, and one that is off when it is needed strands the operator.
    const needed = r.win < r.runLen - 1e-6;
    check(
      r.panDisabled === !needed,
      `${s.n}  slider ${needed ? "enabled" : "disabled"} to match the window`,
      `win ${r.win} of ${r.runLen}`,
    );
    check(!r.navHidden, `${s.n}  control row is visible with a run loaded`);
  }
  const steps = seen.map((x) => x.r.minStep);
  const spread = Math.max(...steps) / Math.min(...steps);
  check(
    spread <= 1 + TOL,
    `min/step identical across all ${SIZES.length} sizes`,
    `spread ${spread.toFixed(3)}x over ${Math.min(...steps)}..${Math.max(...steps)}`,
  );

  // ---- 3: the slider actually pans -------------------------------------------------------
  console.log("\n2. the slider moves the window (portrait 390x844)");
  let before = await resize(SIZES[1]);
  check(!before.panDisabled, "slider is live on a portrait phone", `max ${before.panMax}`);
  check(
    Math.abs(before.panValue - before.panMax) < 0.05,
    "starts at the END of the run (live tail-follow)",
    `value ${before.panValue} of ${before.panMax}`,
  );
  // Drive it the way a user does: set the value and dispatch the real input event, so the
  // page's own listener runs rather than the test poking internals.
  await ev(`(() => {
    const p = document.getElementById('resultChartPan');
    p.value = 0;
    p.dispatchEvent(new Event('input', { bubbles: true }));
  })()`);
  await sleep(400);
  let after = await ev(READ);
  check(after.lo < before.lo - 1, "dragging to 0 shows the START of the run", `lo ${before.lo} -> ${after.lo}`);
  check(
    Math.abs(after.win - before.win) < 0.2,
    "the window KEEPS ITS WIDTH while panning",
    `${before.win} -> ${after.win} min`,
  );
  check(
    Math.abs(after.minStep - before.minStep) < 0.05,
    "and the reading scale does not change with it",
    `min/step ${before.minStep} -> ${after.minStep}`,
  );

  // ---- 5: tail-follow re-arms ------------------------------------------------------------
  console.log("\n3. sliding back to the end re-arms the live tail-follow");
  await ev(`(() => {
    const p = document.getElementById('resultChartPan');
    p.value = p.max;
    p.dispatchEvent(new Event('input', { bubbles: true }));
  })()`);
  await sleep(400);
  const back = await ev(READ);
  check(
    Math.abs(back.hi - back.runLen) < 0.2,
    "window is back on the newest data",
    `hi ${back.hi} of ${back.runLen}`,
  );
  // Without this a live run would freeze on whatever stretch the user last looked at.
  const follows = await ev(`(() => {
    // resultView is module-scope in script.js; reach it the way the page does.
    return typeof resultView !== 'undefined' ? !!resultView.follow : null;
  })()`);
  check(follows !== false, "tail-follow flag re-armed", `follow=${follows}`);

  // ---- 4: the scale is the ONLY scale -----------------------------------------------------
  console.log("\n4. there is no way back to the stretch-to-the-box view");
  const pinned = await ev(READ);
  // A "Fit run" toggle was built on 2026-09-10 and removed the same day: two reading scales
  // means the slope of a curve only means something once you have checked which mode is on,
  // which is the property this whole feature exists to remove. If a control like it comes
  // back, every measurement above becomes conditional again.
  check(
    pinned.scaleCtrls === 0,
    "no control offers a second reading scale",
    `found ${pinned.scaleCtrls}`,
  );
  // The height is derived from the scale and written inline. An empty inline height means
  // CSS (--chart-h, i.e. the viewport) owns it again - the old behaviour by another route.
  check(
    /^\d+px$/.test(pinned.inlineH),
    "chart height stays derived from the scale, not from the viewport",
    `inline height "${pinned.inlineH}"`,
  );

  // ---- accessibility: the slider must be operable without sight or a mouse ---------------
  console.log("\n5. the controls carry names (keyboard and screen reader)");
  check(!!pinned.panLabel, "pan slider has an accessible name", pinned.panLabel || "none");

  // ---- 6: a LIVE run keeps that scale from its first rounds -------------------------------
  console.log("\n6. a live run is drawn at the stored run's scale from its first rounds");
  // The Home chart, read the same way as the Result one above.
  const HOME = `Highcharts.charts.filter(Boolean).find(x => x.renderTo.id === 'homeChart')`;
  const READ_HOME = `(() => {
    const c = ${HOME};
    if (!c) return null;
    const xa = c.xAxis[0], ya = c.yAxis[0];
    const pan = document.getElementById('homeChartPan');
    const el = document.getElementById('homeChart');
    return {
      vw: innerWidth,
      n: c.series[0].points.length,
      lo: +(+xa.min).toFixed(2), hi: +(+xa.max).toFixed(2),
      steps: ya.tickPositions.length - 1,
      minStep: +((c.plotHeight / 10) / (c.plotWidth / (xa.max - xa.min))).toFixed(3),
      plotW: Math.round(c.plotWidth), plotH: Math.round(c.plotHeight),
      chartH: el.clientHeight,
      panDisabled: !pan || pan.disabled,
    };
  })()`;
  // Poll the Home chart until `cond` holds, then hand back the read.
  const homeUntil = async (cond, what, ms) => {
    const t0 = Date.now();
    let r = null;
    while (Date.now() - t0 < ms) {
      r = await ev(READ_HOME);
      if (r && cond(r)) return r;
      await sleep(200);
    }
    throw new Error(`timed out waiting for ${what} (last: ${JSON.stringify(r)})`);
  };
  const untilPhase = async (want, ms) => {
    const t0 = Date.now();
    let ph = "";
    while (Date.now() - t0 < ms) {
      ph = await mockPhase();
      if (ph === want) return;
      await sleep(250);
    }
    throw new Error(`mock never reached phase "${want}" (last "${ph}")`);
  };
  const portrait = seen[1].r, desktop = seen[SIZES.length - 1].r; // stored-run references

  await resize(SIZES[1]);
  await clickWhenReady('.nav-item[data-screen="home"]');
  // idle --ampname--> waitname --red(confirm)--> heater --(timed)--> waitamp --red--> run
  await mock("POST", "/control?btn=ampname");
  await untilPhase("waitname", 5000);
  await mock("POST", "/control?btn=red");
  await untilPhase("waitamp", 30000);
  await mock("POST", "/control?btn=red");
  await untilPhase("amplification", 5000);

  const early = await homeUntil((r) => r.n >= 3 && r.chartH > 0, "3 live rounds on Home", 30000);
  console.log(
    `        round ${early.n}: ${early.plotW}x${early.plotH}px   min/step ${early.minStep}` +
      `   window ${early.lo}..${early.hi}   slider ${early.panDisabled ? "off" : "ON "}`,
  );
  check(early.steps === 10, "portrait: Y axis still 10 steps during the run", `got ${early.steps}`);
  check(
    Math.abs(early.minStep - portrait.minStep) <= portrait.minStep * TOL,
    "portrait: live min/step equals the stored run's",
    `${early.minStep} vs ${portrait.minStep}`,
  );
  // The derived height is the number the bug lived in; px/min alone was masked by the
  // 10.4 px/min floor on a phone.
  check(
    Math.abs(early.plotH - portrait.plotH) <= 2,
    "portrait: live plot is the stored run's height at round " + early.n,
    `${early.plotH}px vs ${portrait.plotH}px`,
  );
  check(early.lo === 0, "portrait: window starts at minute 0 of the run", `lo ${early.lo}`);
  check(early.panDisabled, "portrait: slider parked while the data fits the window");

  const later = await homeUntil((r) => r.n >= early.n + 3, "3 more rounds", 30000);
  check(
    Math.abs(later.plotH - early.plotH) <= 1,
    `portrait: plot height did not move between round ${early.n} and ${later.n}`,
    `${early.plotH}px -> ${later.plotH}px`,
  );
  check(
    Math.abs(later.hi - early.hi) < 0.05,
    "portrait: the window did not stretch to the new rounds",
    `hi ${early.hi} -> ${later.hi}`,
  );

  // Desktop is the case the floor cannot mask: px/min there is plotWidth / run length, so
  // a scale taken from the rounds so far would be wrong by the whole ratio, not clamped.
  // Same settle rule as resize(): the viewport must really be the desktop one and two
  // consecutive reads must agree, else this reads the phone geometry and fails for nothing.
  const DESK = SIZES[SIZES.length - 1];
  await size(DESK);
  let prevD = null, live = null, settled = false;
  for (let i = 0; i < 80 && !settled; i++) {
    await sleep(150);
    live = await ev(READ_HOME);
    settled = !!(live && live.vw === DESK.w && prevD && prevD.vw === DESK.w &&
      prevD.plotW === live.plotW && prevD.plotH === live.plotH);
    prevD = live;
  }
  if (!settled)
    throw new Error(`Home chart never settled at ${DESK.n} (last: ${JSON.stringify(live)})`);
  console.log(
    `        desktop round ${live.n}: ${live.plotW}x${live.plotH}px   min/step ${live.minStep}` +
      `   window ${live.lo}..${live.hi}`,
  );
  // Height RELATIVE to width, not absolute: on a wide screen the Home card sits in a
  // two-column grid and is ~10px narrower than the Result card, so the derived heights
  // legitimately differ by a few px. What must agree is height per unit width - that is
  // the scale, and it was off by 10x+ before the fix.
  const liveAspect = live.plotH / live.plotW, storedAspect = desktop.plotH / desktop.plotW;
  check(
    Math.abs(liveAspect - storedAspect) <= storedAspect * TOL,
    "desktop: plot height derives from the planned run (same height:width as stored)",
    `${liveAspect.toFixed(4)} vs ${storedAspect.toFixed(4)} (${live.plotW}x${live.plotH} vs ${desktop.plotW}x${desktop.plotH})`,
  );
  check(
    Math.abs(live.hi - desktop.runLen) < 0.05,
    "desktop: axis spans the PLANNED run, not the rounds received",
    `hi ${live.hi} vs run ${desktop.runLen}`,
  );
  check(
    Math.abs(live.minStep - desktop.minStep) <= desktop.minStep * TOL,
    "desktop: live min/step equals the stored run's",
    `${live.minStep} vs ${desktop.minStep}`,
  );

  await mock("POST", "/__reset"); // leave the mock idle for whatever runs next

  console.log(`\n${failures ? failures + " FAILED" : "all checks passed"}`);
  ws.close();
  edge.kill();
  process.exit(failures ? 1 : 0);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
