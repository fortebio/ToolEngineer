/**
 * Guard: the amplification chart's Y axis must ALWAYS show exactly 10 steps (11 gridlines),
 * whatever the data reaches, with a floor of 200 that grows - never clips - with the curve.
 *
 * Runs against the REAL chart (not a copy of the maths): it drives the page, pushes data
 * into the live Highcharts instance and reads back `yAxis[0].tickPositions`. A duplicated
 * implementation in the test would happily pass while the shipped one is broken.
 *
 * Usage:  python tools/sse_test_server.py     (in another shell - this test does NOT start
 *                                              it; without the mock you get a timeout that
 *                                              reads like a UI regression)
 *         node tools/test_chart_ticks.js
 */
const { spawn } = require("child_process");
const http = require("http");
const path = require("path");
const os = require("os");

const PORT = 9225;
const PROFILE = path.join(os.tmpdir(), "edge-chart-ticks");
const URL = process.argv[2] || "http://localhost:8000/";
const EDGE =
  process.env.EDGE_PATH ||
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe";

const STEPS = 10; // the invariant
const FLOOR = 200; // axis never tops out below this (raised from 50 on 2026-07-29:
// calibrated VEML readings sit in the hundreds, and a 50-high axis turned baseline noise
// into what looked like signal)

// dataMax -> expected axis top. Covers: empty, tiny, below/at/just over the floor, and
// several magnitudes above, because the rounding switches decade there.
const CASES = [
  { max: 0, top: 200 },
  { max: 0.4, top: 200 },
  { max: 12, top: 200 },
  { max: 49.9, top: 200 },
  { max: 50, top: 200 },
  { max: 57.3, top: 200 },
  { max: 88, top: 200 },
  { max: 120, top: 200 },
  { max: 199.9, top: 200 },
  { max: 200, top: 200 },
  { max: 213, top: 250 },
  { max: 340, top: 350 },
  { max: 580, top: 600 },
  { max: 1234, top: 1500 },
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
    if (r.result && r.result.exceptionDetails)
      throw new Error(JSON.stringify(r.result.exceptionDetails));
    // CDP nests it twice: {result: {result: {value}}}. Reading r.result.value silently
    // yields undefined, which looks exactly like "the page is broken".
    return r.result && r.result.result ? r.result.result.value : undefined;
  };

  await send("Page.enable");
  await send("Runtime.enable");
  await send("Network.enable");
  await send("Network.setCacheDisabled", { cacheDisabled: true });
  await send("Emulation.setDeviceMetricsOverride", {
    width: 1400,
    height: 900,
    deviceScaleFactor: 1,
    mobile: false,
  });
  await send("Page.navigate", { url: URL });

  // Poll for the element instead of sleeping a fixed 2.2 s: the mock serves the
  // uncompressed 634 KB highcharts.js, so a slower machine is still parsing when a fixed
  // wait expires and querySelector returns null - "Cannot read properties of null" that
  // looks like a UI regression but is just the clock.
  const clickWhenReady = async (selector) => {
    for (let i = 0; i < 60; i++) {
      if (await ev(`!!document.querySelector(${JSON.stringify(selector)})`)) {
        await ev(`document.querySelector(${JSON.stringify(selector)}).click()`);
        return;
      }
      await sleep(250);
    }
    throw new Error(`timed out waiting for ${selector} - is the mock server running?`);
  };

  // Open Result and reveal the stored-run chart.
  await clickWhenReady('.nav-item[data-screen="result"]');
  await sleep(500);
  await clickWhenReady("#viewChartBtn");
  await sleep(1500);

  const ready = await ev(
    `!!(window.Highcharts && Highcharts.charts.filter(Boolean).length)`,
  );
  if (!ready) {
    console.error("Highcharts not present - is the mock server running?");
    process.exit(2);
  }

  console.log(`Y axis invariant: exactly ${STEPS} steps, floor ${FLOOR}\n`);
  for (const c of CASES) {
    // Push a flat-then-peak series into every slot so dataMax is exactly c.max.
    const res = await ev(`(() => {
      const ch = Highcharts.charts.filter(Boolean).find(x => x.series.length >= 10);
      ch.series.forEach(s => s.setData([[0, 0], [1, ${c.max}]], false));
      ch.redraw();
      const ax = ch.yAxis[0];
      const p = ax.tickPositions.slice();
      return { n: p.length, first: p[0], last: p[p.length - 1],
               even: p.every((v, i, a) => i < 2 || Math.abs((v - a[i-1]) - (a[1] - a[0])) < 1e-6) };
    })()`);

    check(
      res.n === STEPS + 1,
      `dataMax ${String(c.max).padEnd(6)} -> ${res.n - 1} steps`,
      `expected ${STEPS}`,
    );
    check(res.last === c.top, `                 top ${res.last}`, `expected ${c.top}`);
    check(res.first === 0, `                 starts at ${res.first}`);
    check(res.even, `                 steps evenly spaced`);
  }

  console.log(`\n${failures ? failures + " FAILED" : "all checks passed"}`);
  ws.close();
  edge.kill();
  process.exit(failures ? 1 : 0);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
