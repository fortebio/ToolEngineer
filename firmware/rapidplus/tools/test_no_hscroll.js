/**
 * Guard: no screen may scroll SIDEWAYS on a phone.
 *
 * Reported from the field on an OPPO Reno15 F: text overflowing the Result table and the
 * Setting screen "shifted to the right". Both are the same defect - something is wider than
 * the viewport, so the page pans. The shift is the giveaway: while the document overflows,
 * Chrome on mobile widens the LAYOUT viewport to the content, and `.bottom-nav` (position
 * fixed, width:100%) stretches with it while the header stays at the body width. Measured at
 * 320 px: innerWidth 367 vs clientWidth 320, nav 367 wide under a 320 wide header.
 *
 * It was never seen in testing because every check ran at 390 px with a default font. The
 * matrix below is the point of this guard:
 *   - 320 px: the narrowest phone still in use, and the width the old layout broke at
 *   - 360 px: the most common Android CSS width, incl. the reported device class
 *   - 130 %  font: OPPO/Samsung "display size" scaling. rem-sized columns grow, px ones do
 *            not, and the overflow roughly triples
 *
 * Runs against the REAL page: it drives the tabs and reads scrollWidth, so it catches a
 * regression from ANY source (a new column, a longer string, a nowrap, a fixed px width),
 * not just the two that caused this report.
 *
 * Usage:  python tools/sse_test_server.py --slots tools/slots.txt --reboot   (another shell -
 *           this test does NOT start it; --reboot so the Result table has rows to size)
 *         node tools/test_no_hscroll.js
 */
const { spawn } = require("child_process");
const http = require("http");
const path = require("path");
const os = require("os");

const PORT = 9261;
const PROFILE = path.join(os.tmpdir(), "edge-no-hscroll");
const URL = process.argv[2] || "http://localhost:8000/";
const EDGE =
  process.env.EDGE_PATH ||
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe";

// width x root-font-% . Keep 390/100 in: it is the size everything was designed at, so a
// failure there means the fix for the narrow case regressed the common one.
const MATRIX = [
  { w: 320, f: 100 },
  { w: 320, f: 130 },
  { w: 360, f: 100 },
  { w: 360, f: 130 },
  { w: 390, f: 100 },
  { w: 412, f: 130 },
];
const SCREENS = ["home", "result", "setting"];

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
    return r.result && r.result.result ? r.result.result.value : undefined;
  };

  await send("Page.enable");
  await send("Runtime.enable");
  await send("Network.enable");
  await send("Network.setCacheDisabled", { cacheDisabled: true });

  for (const c of MATRIX) {
    // mobile: true is load-bearing - the layout-viewport widening that produces the visible
    // "shifted right" symptom only happens in mobile mode.
    await send("Emulation.setDeviceMetricsOverride", {
      width: c.w,
      height: 800,
      deviceScaleFactor: 3,
      mobile: true,
    });
    await send("Page.setFontSizes", {
      fontSizes: { standard: Math.round((16 * c.f) / 100) },
    });
    await send("Page.navigate", { url: URL });
    await sleep(2200);

    for (const sc of SCREENS) {
      await ev(`document.querySelector('.nav-item[data-screen="${sc}"]').click()`);
      await sleep(sc === "result" ? 2500 : 900);
      // Home's chart card carries .hide in every state this guard can reach from an idle
      // mock, so everything inside it measured 0 and a real overflow in its card head went
      // unseen (320px/130%: scrollWidth 347 vs 320, found by review, not by this file).
      // Reveal it in the same tick as the measurement - renderHome re-hides it on the next
      // SSE frame, so showing it a few hundred ms earlier measures the hidden card again.
      if (sc === "home")
        await ev(`document.getElementById('homeChartCard').classList.remove('hide')`);
      if (sc === "result") {
        // open the stored curve too: the chart is the widest thing on the page
        await ev(`(function(){var b=document.getElementById('viewChartBtn'); if(b) b.click();})()`);
        await sleep(1500);
      }
      if (sc === "setting") {
        // Open a PANEL, not just the card menu. The menu is four short rows and always fit;
        // the panels are where the wide content lives, and a real overflow in the WiFi one
        // (saved rows: SSID + badge + two buttons, 366px inside a 320px client at 130% font)
        // sat here unseen because this guard only ever looked at the menu.
        await ev(`new Promise(res=>{var n=0;var iv=setInterval(function(){
          var c=document.querySelector('.set-card[data-card="wifi"]');
          if(c){clearInterval(iv);c.click();res(1);}else if(++n>60){clearInterval(iv);res(0);}},100)})`);
        await sleep(3400); // the scan has to land or the list is a one-line "Scanning..."
      }
      const r = await ev(`(function(){
        var hc = document.getElementById('homeChartCard');
        if (hc) hc.classList.remove('hide'); // same tick as the measurement - see above
        var de = document.documentElement, cw = de.clientWidth, over = [];
        document.querySelectorAll('#screen-${sc} *, .header *, .bottom-nav').forEach(function (el) {
          var b = el.getBoundingClientRect();
          if (b.width > 0 && b.right > cw + 0.5)
            over.push((el.tagName.toLowerCase()) +
              (typeof el.className === 'string' && el.className ? '.' + el.className.trim().split(/\\s+/)[0] : '') +
              '@' + Math.round(b.right));
        });
        var seen = {}, uniq = [];
        over.forEach(function (s) { var k = s.split('@')[0]; if (!seen[k]) { seen[k] = 1; uniq.push(s); } });
        return { ovf: de.scrollWidth - cw, inner: window.innerWidth, cw: cw, who: uniq.slice(0, 4) };
      })()`);
      check(
        r.ovf <= 0,
        `${c.w}px @ ${c.f}% font - ${sc} does not scroll sideways`,
        r.ovf > 0 ? `+${r.ovf}px, innerWidth ${r.inner} vs ${r.cw}: ${r.who.join(" ")}` : "",
      );
      // The nav stretching past the viewport is the visible half of the bug; assert it apart
      // from scrollWidth so a future partial fix cannot leave the symptom behind.
      const nav = await ev(
        `Math.round(document.querySelector('.bottom-nav').getBoundingClientRect().width)`,
      );
      if (r.ovf <= 0)
        check(nav <= c.w + 0.5, `${c.w}px @ ${c.f}% - ${sc} bottom nav stays in the viewport`, `${nav}px`);
    }
  }

  // ---- Desktop, large font: the same track bug, but it hides as ASYMMETRY -------------
  // At 1280 the two-column Setting menu never overflows, so scrollWidth stays clean while a
  // bare `1fr 1fr` silently inflates track 1 to its widest card's min-content and leaves the
  // remainder to track 2. Measured before the fix at 150% root font: 496.031px / 387.922px -
  // a visibly lopsided menu with no horizontal scroll to give it away. Assert the tracks are
  // equal, which is what "1fr 1fr" was written to mean.
  await send("Emulation.setDeviceMetricsOverride", {
    width: 1280, height: 900, deviceScaleFactor: 1, mobile: false,
  });
  await send("Page.setFontSizes", { fontSizes: { standard: 24 } }); // 150%
  await send("Page.navigate", { url: URL });
  await sleep(2200);
  await ev(`document.querySelector('.nav-item[data-screen="setting"]').click()`);
  await sleep(1400);
  const grid = await ev(`(function(){
    var m = document.getElementById('setMenu'), de = document.documentElement;
    var cols = getComputedStyle(m).gridTemplateColumns.split(' ').map(parseFloat);
    return { cols: cols, spread: cols.length > 1 ? Math.abs(cols[0] - cols[1]) : 0,
             ovf: de.scrollWidth - de.clientWidth };
  })()`);
  check(grid.ovf <= 0, "1280px @ 150% font - setting does not scroll sideways", `+${grid.ovf}px`);
  check(
    grid.spread <= 1,
    "1280px @ 150% font - the two Setting columns are equal width",
    grid.cols.map((c) => Math.round(c) + "px").join(" vs "),
  );

  ws.close();
  edge.kill();
  console.log(
    failures
      ? `\nFAIL - ${failures} check(s)`
      : "\nok - no screen scrolls sideways at 320-412px @ 100-130% font, desktop tracks even at 150%",
  );
  process.exit(failures ? 1 : 0);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
