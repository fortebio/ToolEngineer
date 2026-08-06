/**
 * Guard: after a run, RED swaps the Home chart for the machine's own error table, and WHITE
 * takes it away again.
 *
 * The web FOLLOWS the device here - it does not offer a view of its own. RED at escreenFinished
 * moves the machine to escreenErrorResult (button.cpp), and Home reacts to that phase. So
 * whoever is standing at the machine and whoever is holding the phone are looking at the same
 * thing, whichever of them pressed the button.
 *
 * This drives a WHOLE run through the UI - name, confirm, start, wait it out - because the
 * interesting state only exists at the end of one, and every shortcut to it would be testing a
 * fixture instead of the flow.
 *
 * What it pins, and why:
 *   - the swap is a swap, both directions. Two full-height cards stacked is the failure it
 *     replaced.
 *   - the run LAYOUT survives: compact temperature strip stays, the full cards stay away. The
 *     error table is a pane of the run view, not a mode of its own.
 *   - the phase is `errortable`, never `finished`. Writing this test found the mock reporting
 *     "finished" while its own state machine had already moved on, which is the same mistake
 *     the firmware would make if escreenErrorResult ever folded back into another case - and it
 *     is invisible from the outside, because everything still looks plausible.
 *   - the red chip is LABELLED. escreenErrorResult had no fillActions case, so all three chips
 *     came up blank on the one screen that prints "Press white key to test next".
 *
 * Usage:  python tools/sse_test_server.py --full     (another shell - this test does NOT start
 *           it; --full compresses the clock so a whole run takes ~10 s)
 *         node tools/test_home_error_table.js
 */
const { spawn } = require("child_process");
const http = require("http");
const path = require("path");
const P = 9371, PROF = path.join(require("os").tmpdir(), "edge-homeerr");
const EDGE = process.env.EDGE_PATH || "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
const sleep = (m) => new Promise((r) => setTimeout(r, m));
const j = (p) => new Promise((res, rej) => http.get({ host: "127.0.0.1", port: P, path: p }, (r) => {
  let b = ""; r.on("data", (c) => (b += c)); r.on("end", () => res(JSON.parse(b))); }).on("error", rej));
let fails = 0;
const ok = (n, c, d) => { console.log(`${c ? "  ok  " : "FAIL  "}${n}${d ? "  -> " + d : ""}`); if (!c) fails++; };

(async () => {
  const e = spawn(EDGE, ["--headless=new", "--disable-gpu", `--remote-debugging-port=${P}`, `--user-data-dir=${PROF}`, "about:blank"]);
  let t = []; for (let i = 0; i < 40; i++) { try { t = await j("/json"); if (t.some((x) => x.type === "page")) break; } catch (_) {} await sleep(300); }
  const ws = new WebSocket(t.find((x) => x.type === "page").webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r)); let id = 0; const pend = new Map();
  ws.onmessage = (m) => { const o = JSON.parse(m.data); if (o.id && pend.has(o.id)) { pend.get(o.id)(o); pend.delete(o.id); } };
  const send = (me, pa = {}) => new Promise((r) => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method: me, params: pa })); });
  const ev = async (x) => { const r = await send("Runtime.evaluate", { expression: x, returnByValue: true, awaitPromise: true });
    if (r.result && r.result.exceptionDetails) throw new Error(JSON.stringify(r.result.exceptionDetails));
    return r.result && r.result.result ? r.result.result.value : undefined; };
  const state = () => ev(`(function(){
    var q=function(i){return document.getElementById(i);};
    var vis=function(i){var e=q(i);return e && !e.classList.contains('hide');};
    return { phase: (window.curPhase||''), chart: vis('homeChartCard'), errs: vis('homeErrorCard'),
             compact: vis('tempCompact'), fullTemp: vis('tempFullLysis'),
             rows: q('homeErrorBody') ? q('homeErrorBody').querySelectorAll('.err-table tbody tr').length : 0,
             bad: q('homeErrorBody') ? q('homeErrorBody').querySelectorAll('tr.err-row').length : 0,
             redLabel: (q('bRed')||{}).textContent||'' };})()`);
  const waitPhase = (p, ms) => ev(`new Promise(function(res){var n=0;var iv=setInterval(function(){
      if(window.curPhase===${JSON.stringify(p)}){clearInterval(iv);res(1);}else if(++n>${Math.round(ms / 200)}){clearInterval(iv);res(0);}},200);})`);

  await send("Page.enable"); await send("Runtime.enable"); await send("Network.enable");
  await send("Network.setCacheDisabled", { cacheDisabled: true });
  await send("Emulation.setDeviceMetricsOverride", { width: 390, height: 900, deviceScaleFactor: 2, mobile: true });
  await send("Page.navigate", { url: "http://localhost:8000/" });
  await sleep(2400);

  // drive a whole run: name -> confirm -> start -> wait it out
  await ev(`document.getElementById('bRed').click()`);            // idle -> waitname
  ok("naming gate opened", await waitPhase("waitname", 4000) === 1);
  await sleep(600);
  await ev(`document.getElementById('confirmNamesBtn').click()`); // -> heater
  ok("heating started", await waitPhase("heater", 6000) === 1);
  ok("reached waitamp", await waitPhase("waitamp", 20000) === 1);
  await ev(`document.getElementById('bRed').click()`);            // -> amplification
  ok("amplification started", await waitPhase("amplification", 8000) === 1);
  ok("run finished", await waitPhase("finished", 120000) === 1);
  await sleep(900);

  let s = await state();
  ok("finished: chart on Home, no error table", s.chart && !s.errs, JSON.stringify({ chart: s.chart, errs: s.errs }));
  ok("red chip offers the error table", /error/i.test(s.redLabel), s.redLabel);

  // THE ASK: red -> table takes the chart's place
  await ev(`document.getElementById('bRed').click()`);
  ok("machine moved to the error-table screen", await waitPhase("errortable", 6000) === 1);
  await sleep(1200);
  s = await state();
  ok("Home swapped chart -> error table", s.errs && !s.chart, JSON.stringify({ chart: s.chart, errs: s.errs }));
  ok("all 10 slots listed", s.rows === 10, String(s.rows));
  ok("the seeded failures are flagged", s.bad === 2, `${s.bad} rows`);
  ok("still the run layout (compact temps, no full cards)", s.compact && !s.fullTemp,
     JSON.stringify({ compact: s.compact, full: s.fullTemp }));

  // and white takes it away again
  await ev(`document.getElementById('bWhite').click()`);
  await sleep(1800);
  s = await state();
  ok("white leaves the table AND the chart behind", !s.errs && !s.chart, JSON.stringify({ chart: s.chart, errs: s.errs, phase: s.phase }));
  ok("full temperature cards are back", s.fullTemp && !s.compact, JSON.stringify({ compact: s.compact, full: s.fullTemp }));

  ws.close(); e.kill();
  console.log(fails ? `\n${fails} FAILED` : "\nall checks passed");
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
