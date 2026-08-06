/**
 * Guard: the Result tab's "Error table" shows the machine's own per-slot sensor errors, and it
 * REPLACES the chart rather than stacking under it.
 *
 * The table exists on the device (RED on the finished/review screens -> screen_errorResult) but
 * had no web equivalent at all, so anyone reading the run from a phone could not see which
 * channels failed - on the one screen whose whole job is "what happened in each slot".
 *
 * What this pins, and why each line is here:
 *   - the swap is a SWAP. Both cards occupy one slot in the layout; showing one must hide the
 *     other, in both directions. Two full-height cards stacked is the failure this replaced.
 *   - all ten slots are listed, always. A table that only lists failures cannot be told apart
 *     from a table that failed to load.
 *   - the code shown is the DEVICE'S 4-digit encoding, so an operator can read one screen
 *     against the other. If the web ever invents its own numbering the two stop agreeing.
 *   - no horizontal overflow at 320px/131% font - the narrowest phone still in use, where a
 *     third column of free text is exactly the kind of thing that pushes the page sideways
 *     (see test_no_hscroll.js for why that symptom reads as "the UI is shifted right").
 *
 * Runs against the REAL page through the mock, so it catches a regression from any source.
 *
 * Usage:  python tools/sse_test_server.py --reboot     (another shell - this test does NOT start
 *           it; --reboot so the Result tab has a stored run to report on)
 *         node tools/test_error_table.js
 */
const { spawn } = require("child_process");
const http = require("http");
const path = require("path");
const P = 9351, PROF = path.join(require("os").tmpdir(), "edge-errtbl");
const EDGE = process.env.EDGE_PATH || "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
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
  await send("Page.enable"); await send("Runtime.enable"); await send("Network.enable");
  await send("Network.setCacheDisabled", { cacheDisabled: true });

  for (const c of [{ w: 390, f: 16 }, { w: 320, f: 21 }, { w: 1280, f: 16 }]) {
    console.log(`--- ${c.w}px, font ${c.f}px ---`);
    await send("Emulation.setDeviceMetricsOverride", { width: c.w, height: 860, deviceScaleFactor: 2, mobile: c.w < 800 });
    await send("Page.setFontSizes", { fontSizes: { standard: c.f } });
    await send("Page.navigate", { url: "http://localhost:8000/" });
    await sleep(2300);
    await ev(`document.querySelector('.nav-item[data-screen="result"]').click()`);
    await sleep(2600); // reviewlast lands

    // 1. show the chart first, so the swap is a real swap
    await ev(`document.getElementById('viewChartBtn').click()`);
    await sleep(1200);
    let r = await ev(`(function(){return {chart:!document.getElementById('resultChartCard').classList.contains('hide'),
      errs:!document.getElementById('resultErrorCard').classList.contains('hide')};})()`);
    ok("View chart shows the chart only", r.chart && !r.errs, JSON.stringify(r));

    // 1b. the button must be REACHABLE, not merely present. Everything below drives it with
    // getElementById(...).click(), which happily fires on an element that is display:none, zero
    // sized or buried under another one - so on its own it proves the handler works and nothing
    // about whether a person could ever run it.
    // Scroll to it FIRST. elementFromPoint takes VIEWPORT coordinates and returns null for a
    // point outside the window, so hit-testing a button that sits below the fold reports "something
    // is covering it" when nothing is - which is what this check did on its first run, with the
    // chart card pushing the button off screen. Scrolling is also what a person does.
    const btn = await ev(`(function(){
      var b = document.getElementById('viewErrorsBtn');
      if (!b) return { missing: true };
      b.scrollIntoView({ block: 'center' });
      var r = b.getBoundingClientRect(), cs = getComputedStyle(b);
      var hit = document.elementFromPoint(r.left + r.width/2, r.top + r.height/2);
      return { w: Math.round(r.width), h: Math.round(r.height),
               shown: cs.display !== 'none' && cs.visibility !== 'hidden' && Number(cs.opacity) > 0,
               inViewport: r.top >= 0 && r.bottom <= window.innerHeight,
               topmost: !!hit && (hit === b || b.contains(hit)) };})()`);
    ok("the Error table button is visible", !btn.missing && btn.shown && btn.w > 0 && btn.h > 0,
       JSON.stringify(btn));
    // 24x24 is the WCAG 2.2 target-size minimum; this is a device operated with gloves on.
    ok("its tap target is big enough", btn.w >= 24 && btn.h >= 24, `${btn.w}x${btn.h}`);
    ok("nothing is covering it", btn.topmost === true);

    // 2. now the error table must TAKE ITS PLACE
    await ev(`document.getElementById('viewErrorsBtn').click()`);
    await sleep(1200);
    r = await ev(`(function(){
      var ec=document.getElementById('resultErrorCard'), cc=document.getElementById('resultChartCard');
      var rows=ec.querySelectorAll('.err-table tbody tr');
      var bad=ec.querySelectorAll('tr.err-row');
      var de=document.documentElement;
      return { chart: !cc.classList.contains('hide'), errs: !ec.classList.contains('hide'),
               rows: rows.length, bad: bad.length,
               summary: (ec.querySelector('.err-summary')||{}).textContent||'',
               firstCode: (rows[2]&&rows[2].querySelector('.err-code')||{}).textContent||'',
               ovf: de.scrollWidth - de.clientWidth,
               cardTop: Math.round(ec.getBoundingClientRect().top) };})()`);
    ok("Error table replaces the chart", r.errs && !r.chart, JSON.stringify({ chart: r.chart, errs: r.errs }));
    ok("all 10 slots listed", r.rows === 10, String(r.rows));
    ok("the two seeded errors are flagged", r.bad === 2, `${r.bad} rows`);
    ok("slot #3 shows the device's own code", r.firstCode === "102", r.firstCode);
    ok("summary counts them", /2 of 10/.test(r.summary), r.summary);
    ok("no horizontal overflow", r.ovf <= 0, String(r.ovf));

    // 3. and back
    await ev(`document.getElementById('viewChartBtn').click()`);
    await sleep(900);
    r = await ev(`(function(){return {chart:!document.getElementById('resultChartCard').classList.contains('hide'),
      errs:!document.getElementById('resultErrorCard').classList.contains('hide')};})()`);
    ok("switching back hides the error table", r.chart && !r.errs, JSON.stringify(r));

  }
  ws.close(); e.kill();
  console.log(fails ? `\n${fails} FAILED` : "\nall checks passed");
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
