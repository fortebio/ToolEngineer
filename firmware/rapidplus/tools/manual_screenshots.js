/**
 * Screenshots for the END-USER MANUAL: walk the mock device through a COMPLETE run and
 * capture the web UI at every step the operator actually sees.
 *
 * ui_screenshot.js is stateless (design review of fixed screens). This one drives the real
 * run state machine - idle -> name -> heat -> insert tube -> amplify -> finished -> result -
 * so the pictures in the manual are the screens in the order the operator meets them.
 *
 * Usage:  node tools/manual_screenshots.js [outDir]
 * Starts and stops its own mock; --slots replays a REAL captured run so the chart in the
 * manual shows real curves, not a synthetic sigmoid.
 */
const { spawn } = require("child_process");
const http = require("http");
const fs = require("fs");
const path = require("path");
const os = require("os");

const ROOT = path.resolve(__dirname, "..");
const OUT = path.resolve(process.argv[2] || path.join(ROOT, "docs", "manual", "img"));
const PORT = 8000;
const CDP_PORT = 9224;
const PROFILE = path.join(os.tmpdir(), "edge-manual-shots");
const EDGE =
  process.env.EDGE_PATH ||
  "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";

const MOBILE = { w: 390, h: 844 };
const DESKTOP = { w: 1280, h: 860 };
// iPad 10.9 portrait: 820x1180 is the NARROWEST viewport that still gets the two-column
// layout (the media query is min-width 820 AND min-height 600). Worth its own figures -
// an iPad user comparing against a 1280px laptop shot cannot tell whether the tighter
// columns on their screen are the same layout or a broken one.
const IPAD = { w: 820, h: 1180 };

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const log = (...a) => console.log(...a);

function get(port, p) {
  return new Promise((res, rej) =>
    http
      .get({ host: "127.0.0.1", port, path: p }, (r) => {
        let b = "";
        r.on("data", (c) => (b += c));
        r.on("end", () => {
          try {
            res(JSON.parse(b));
          } catch (e) {
            rej(e);
          }
        });
      })
      .on("error", rej),
  );
}

/* Some screens cannot be identified by `phase`: eheatLysis reports phase "heater", the same
   value the preheat before it reports (fillStatus does this deliberately - only the title
   differs). Wait on the title for those. */
async function waitTitle(want, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let last = "?";
  while (Date.now() < deadline) {
    try {
      const h = await get(PORT, "/home");
      last = h.status.title;
      if (last.indexOf(want) >= 0) return h;
    } catch (e) {}
    await sleep(300);
  }
  throw new Error(`timeout waiting for title "${want}" (device shows "${last}")`);
}

async function waitPhase(want, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let last = "?";
  while (Date.now() < deadline) {
    try {
      const h = await get(PORT, "/home");
      last = h.status.phase;
      if (last === want) return h;
    } catch (e) {}
    await sleep(400);
  }
  throw new Error(`timeout waiting for phase ${want} (device is in "${last}")`);
}

// Slot layout used across every picture in the manual, so the same ten samples follow the
// reader from the naming table to the chart legend to the result table.
const DISEASES = ["EHP", "WSSV", "TPD", "EMS", "PCV", "PCM", "PCT", "ISKNV", "PCS", "EHP"];
const SAMPLES = ["NC", "A01", "A02", "A03", "B01", "B02", "B03", "C01", "C02", "PC"];

const FILL_NAMES = `(function(){
  var d=${JSON.stringify(DISEASES)}, s=${JSON.stringify(SAMPLES)};
  document.querySelectorAll('#namingBody .slot-name').forEach(function(el,i){
    el.value=d[i]||''; el.dispatchEvent(new Event('change',{bubbles:true}));
  });
  document.querySelectorAll('#namingBody .sample-name').forEach(function(el,i){
    el.value=s[i]||''; el.dispatchEvent(new Event('change',{bubbles:true}));
  });
  return document.querySelectorAll('#namingBody .slot-name').length;
})()`;

const tab = (n) => `document.querySelector('.nav-item[data-screen="${n}"]').click()`;
const openCard = (title) =>
  `(function(){var c=[].slice.call(document.querySelectorAll('.set-card')).find(function(x){return x.textContent.indexOf(${JSON.stringify(
    title,
  )})>=0}); if(c) c.click(); return !!c;})()`;

async function main() {
  fs.mkdirSync(OUT, { recursive: true });

  // ---- mock device -------------------------------------------------------
  const mock = spawn(
    "python",
    [path.join(ROOT, "tools", "sse_test_server.py"), "--slots", path.join(ROOT, "tools", "slots.txt")],
    { cwd: ROOT, stdio: ["ignore", "pipe", "pipe"] },
  );
  mock.stdout.on("data", () => {});
  mock.stderr.on("data", () => {});
  for (let i = 0; i < 40; i++) {
    try {
      await get(PORT, "/home");
      break;
    } catch (e) {
      await sleep(300);
    }
  }
  log("mock up");

  // ---- browser -----------------------------------------------------------
  // Fresh profile + no extensions: a reused profile picks up Edge's bundled extensions,
  // and one of them injected a "Customize modifications" popup and restyled the page text
  // INTO the screenshots. A contaminated picture in a printed manual is worse than no
  // picture, and it is invisible until someone looks closely at the wrong thing.
  fs.rmSync(PROFILE, { recursive: true, force: true });
  const edge = spawn(EDGE, [
    "--headless=new",
    "--disable-gpu",
    "--hide-scrollbars",
    "--disable-extensions",
    "--disable-component-extensions-with-background-pages",
    "--no-first-run",
    "--no-default-browser-check",
    "--disable-background-networking",
    `--remote-debugging-port=${CDP_PORT}`,
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
      targets = await get(CDP_PORT, "/json");
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
  /* Every CDP call is bounded. Without this a lost reply - a dead page target, a browser
     that went away - leaves the await pending FOREVER: the run hangs with no error, no exit
     and no further log line, and the only symptom is a log file that stopped growing. That
     is exactly how a capture silently died at image 11 and left a stale image set behind. */
  const send = (method, params = {}, timeoutMs = 120000) =>
    new Promise((res, rej) => {
      const mid = ++id;
      const timer = setTimeout(() => {
        pending.delete(mid);
        rej(new Error(`CDP timeout after ${timeoutMs}ms: ${method}`));
      }, timeoutMs);
      pending.set(mid, (m) => {
        clearTimeout(timer);
        res(m);
      });
      ws.send(JSON.stringify({ id: mid, method, params }));
    });
  ws.onclose = () => {
    for (const [mid, resolve] of pending) pending.delete(mid);
    console.error("CDP socket closed - the browser went away");
  };
  const js = (expr) =>
    send("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true });
  const jsVal = async (expr) => {
    const m = await js(expr);
    return m.result && m.result.result ? m.result.result.value : undefined;
  };

  /* Wait until the PAGE has seen the phase, not just the device.
     The red chip is state-dependent: at idle the client rewrites it into the naming gate
     (btn = "ampname"), anywhere else it sends a plain RED. Clicking before the first SSE
     frame lands therefore sends the wrong command and the machine heats immediately -
     which is exactly what the physical button does, so nothing errors, the run just
     silently skips the naming step. Gate every press on the client's own view of phase. */
  async function waitClientPhase(want, timeoutMs = 20000) {
    const deadline = Date.now() + timeoutMs;
    let last = "?";
    while (Date.now() < deadline) {
      last = await jsVal("typeof curPhase !== 'undefined' ? curPhase : ''");
      if (last === want) return;
      await sleep(300);
    }
    throw new Error(`timeout: browser still shows phase "${last}", wanted "${want}"`);
  }
  async function press(id, fromPhase) {
    await waitClientPhase(fromPhase);
    await js(`document.getElementById('${id}').click()`);
  }

  /* Union bounding box of one or more selectors, in page coordinates.
     A whole-phone capture is 780x1688 - beside a paragraph of text that is ~330px tall, two
     thirds of the figure is chrome and empty card. Clipping to the cards a step is actually
     about makes the picture short and wide, which both fills the column and prints legibly. */
  async function clipRect(selectors, pad = 14) {
    const raw = await jsVal(`(function () {
      var sel = ${JSON.stringify(selectors)};
      var boxes = [];
      sel.forEach(function (s) {
        var e = document.querySelector(s);
        if (e && e.getBoundingClientRect().height > 4) boxes.push(e.getBoundingClientRect());
      });
      if (!boxes.length) return "";
      var sx = window.scrollX, sy = window.scrollY, p = ${pad};
      var l = Math.min.apply(null, boxes.map(function (b) { return b.left; }));
      var t = Math.min.apply(null, boxes.map(function (b) { return b.top; }));
      var r = Math.max.apply(null, boxes.map(function (b) { return b.right; }));
      var bo = Math.max.apply(null, boxes.map(function (b) { return b.bottom; }));
      return JSON.stringify({
        x: Math.max(0, l + sx - p), y: Math.max(0, t + sy - p),
        width: (r - l) + p * 2, height: (bo - t) + p * 2
      });
    })()`);
    if (!raw) throw new Error(`clip: nothing matched ${selectors.join(", ")}`);
    return JSON.parse(raw);
  }

  await send("Page.enable");
  await send("Runtime.enable");
  await send("Network.enable");
  await send("Network.setCacheDisabled", { cacheDisabled: true });

  const setView = (v) =>
    send("Emulation.setDeviceMetricsOverride", {
      width: v.w,
      height: v.h,
      deviceScaleFactor: 2,
      mobile: v.w < 820,
    });

  let shotNo = 0;
  /* full=false captures ONLY the viewport - the phone screen as the operator sees it.
     Use it where the page is mostly an empty chart card (the chart opens as soon as names
     are confirmed, but has no data until Start), otherwise the figure is a band of content
     over a page of white. Everything else captures the whole scrollable page. */
  async function shot(name, view, opts = {}) {
    // Mobile figures default to the viewport. A full-page phone capture is 780x2900, which
    // has to be shrunk to ~2 in wide to fit a printed page - too small to read. The desktop
    // capture of the same step carries the detail; the phone shot shows the phone.
    const full = opts.full !== undefined ? opts.full : !(view && view.w < 820);
    const settle = opts.settle || 900;
    shotNo++;
    if (opts.clip) {
      if (view) { await setView(view); await sleep(500); }
      await sleep(settle);
      const rect = await clipRect(opts.clip);
      const c = await send("Page.captureScreenshot", {
        format: "png", captureBeyondViewport: true,
        clip: { x: rect.x, y: rect.y, width: rect.width, height: rect.height, scale: 2 },
      });
      const f = path.join(OUT, `${String(shotNo).padStart(2, "0")}-${name}.png`);
      fs.writeFileSync(f, Buffer.from(c.result.data, "base64"));
      log(`  ${path.basename(f)}  ${Math.round((c.result.data.length * 0.75) / 1024)}KB  [clip]`);
      return;
    }
    if (view) {
      await setView(view);
      await sleep(500);
    }
    await sleep(settle);
    /* The dashboard's bottom nav is position:fixed. captureBeyondViewport renders the WHOLE
       page but leaves the fixed bar anchored to the 860px viewport, so on any page taller
       than that it gets painted straight across the middle of the figure - it was sitting
       on top of slots #4 and #5 in the result and error tables. Let it flow to the real
       bottom of the document for the shot, then put it back. */
    if (full) {
      await js(`(function () {
        var n = document.querySelector('.bottom-nav');
        if (n) {
          n.dataset.capPos = n.style.position || '';
          n.style.position = 'static';
          // The bar is centred with left:50% AND transform:translateX(-50%). The left stops
          // applying once it is static, but the TRANSFORM does not - it kept shifting the bar
          // half its width off the left edge, which looked like a broken render. Clear the
          // transform (and pin the width) or the fix only trades one artefact for another.
          // (No backticks in this comment: it lives inside a template literal.)
          n.dataset.capW = n.style.width || '';
          n.dataset.capT = n.style.transform || '';
          n.style.width = '100%';
          n.style.maxWidth = 'none';
          n.style.transform = 'none';
          n.style.left = 'auto';
        }
        document.body.dataset.capPad = document.body.style.paddingBottom || '';
        document.body.style.paddingBottom = '0';
      })()`);
      await sleep(280);
    }
    const r = await send("Page.captureScreenshot", { format: "png", captureBeyondViewport: full });
    if (full) {
      await js(`(function () {
        var n = document.querySelector('.bottom-nav');
        if (n) {
          n.style.position = n.dataset.capPos || '';
          n.style.width = n.dataset.capW || '';
          n.style.transform = n.dataset.capT || '';
          n.style.maxWidth = '';
          n.style.left = '';
          delete n.dataset.capPos; delete n.dataset.capW; delete n.dataset.capT;
        }
        document.body.style.paddingBottom = document.body.dataset.capPad || '';
        delete document.body.dataset.capPad;
      })()`);
    }
    const file = path.join(OUT, `${String(shotNo).padStart(2, "0")}-${name}.png`);
    fs.writeFileSync(file, Buffer.from(r.result.data, "base64"));
    log(`  ${path.basename(file)}  ${Math.round((r.result.data.length * 0.75) / 1024)}KB`);
  }

  await setView(MOBILE);
  await send("Page.navigate", { url: `http://localhost:${PORT}/` });
  await sleep(2600); // let SSE deliver a home frame and the tables build

  // 1. Idle: the screen the operator meets after opening the dashboard -----
  log("phase idle");
  await shot("home-idle-mobile", MOBILE);
  await shot("home-idle-desktop", DESKTOP);
  await shot("home-idle-ipad", IPAD);
  await setView(MOBILE);
  await sleep(600);

  // 2. Press Amplification (red) -> naming gate ---------------------------
  log("press RED -> waitname");
  await press("bRed", "idle");
  await waitPhase("waitname", 10000);
  await sleep(1800);
  await shot("naming-empty-mobile", MOBILE);
  log("  filled rows:", await jsVal(FILL_NAMES));
  await sleep(1200);
  await shot("naming-filled-mobile", MOBILE, { clip: ["#namingCard"] });
  await shot("naming-filled-desktop", DESKTOP);
  await shot("naming-filled-ipad", IPAD);
  await setView(MOBILE);
  await sleep(600);

  // 3. Confirm -> preheat --------------------------------------------------
  log("Confirm -> heater");
  await press("confirmNamesBtn", "waitname");
  await waitPhase("heater", 10000);
  await sleep(2200);
  await shot("heating-mobile", MOBILE, { clip: ["#screen-home .banner.state", "#tempFullAmp"] });

  // 4. Wait for the "insert amplification tube" prompt ---------------------
  log("waiting for waitamp ...");
  await waitPhase("waitamp", 40000);
  await sleep(2000);
  await shot("waitamp-mobile", MOBILE, { clip: ["#screen-home .banner.state", "#buttonsCard"] });
  await shot("waitamp-desktop", DESKTOP);
  await setView(MOBILE);
  await sleep(600);

  // 5. Start the run -------------------------------------------------------
  log("press START -> amplification");
  await press("bRed", "waitamp");
  await waitPhase("amplification", 10000);
  await sleep(9000); // a few rounds in: chart open, curve still flat
  await shot("amp-early-mobile", MOBILE);
  log("  running ... (waiting for the curves to take off)");
  await sleep(62000);
  await shot("amp-live-mobile", MOBILE, { clip: ["#homeChartCard"] });
  await shot("amp-live-desktop", DESKTOP);
  await shot("amp-live-ipad", IPAD);
  await setView(MOBILE);
  await sleep(600);

  // 6. Run complete --------------------------------------------------------
  log("waiting for finished ...");
  await waitPhase("finished", 180000);
  await sleep(3000);
  await shot("finished-mobile", MOBILE, { clip: ["#notify", "#buttonsCard"] });
  await shot("finished-desktop", DESKTOP);

  // RED on the finished screen puts the DEVICE into its error-result screen, and the web
  // follows: Home swaps the chart for the same table. Capture it, then WHITE back so the
  // rest of the walk starts from a known state.
  await press("bRed", "finished");
  await waitPhase("errortable", 10000);
  await sleep(2600);
  await shot("home-errortable-mobile", MOBILE, { clip: ["#homeErrorCard"] });
  await press("bWhite", "errortable");
  await waitPhase("idle", 10000);
  await sleep(1200);
  await js(tab("result"));
  await sleep(2500);
  await setView(MOBILE);
  await sleep(800);

  // 7. Result tab ----------------------------------------------------------
  log("Result tab");
  await js(tab("result"));
  await sleep(3500);
  await shot("result-table-mobile", MOBILE);
  await js(`document.getElementById('viewChartBtn').click()`);
  await sleep(3500);
  await shot("result-chart-mobile", MOBILE);
  await shot("result-chart-desktop", DESKTOP);
  await shot("result-chart-ipad", IPAD);

  // Error table: same pane as the chart, not stacked under it. Two views of one run from
  // opposite sides, so the operator swaps between them rather than scrolling past one.
  await js(`document.getElementById('viewErrorsBtn').click()`);
  await sleep(2500);
  await shot("result-errors-desktop", DESKTOP);
  await shot("result-errors-mobile", MOBILE, { clip: ["#resultErrorCard"] });
  await setView(MOBILE);
  await sleep(800);

  // 8. Setting tab ---------------------------------------------------------
  log("Setting tab");
  await js(tab("setting"));
  await sleep(2000);
  await shot("setting-menu-mobile", MOBILE);
  await shot("setting-menu-desktop", DESKTOP);

  await setView(DESKTOP);
  await js(openCard("WiFi"));
  await sleep(3200); // the scan is async: 202 then the list
  await shot("setting-wifi-connect", DESKTOP);
  await js(
    `(function(){var r=document.querySelector('input[name="wifiView"][value="saved"]'); if(r){r.checked=true; r.dispatchEvent(new Event('change',{bubbles:true}));} return !!r;})()`,
  );
  await sleep(1500);
  await shot("setting-wifi-saved", DESKTOP);

  await js(`document.getElementById('setBack').click()`);
  await sleep(1200);
  await js(openCard("Profile Configuration"));
  await sleep(1800);
  await shot("setting-profile", DESKTOP);

  await js(`document.getElementById('setBack').click()`);
  await sleep(1200);
  await js(openCard("Firmware"));
  await sleep(1800);
  await shot("setting-firmware", DESKTOP);

  // 9. Lysis leg -----------------------------------------------------------
  // The other way into a run, and the one the amplification-only walk never reaches. It
  // stops TWICE for a person - drop the lysis tube in, then take the hot tube out - and
  // those two screens have no counterpart anywhere else in the procedure.
  log("Lysis flow");
  await setView(MOBILE);
  await js(tab("home"));
  await sleep(1600);
  // The error-table capture above already pressed WHITE, so the device is back at idle.
  await press("bGreen", "idle");            // GREEN = Lysis
  await waitTitle("Heating lysis block", 15000);
  await sleep(2200);
  await shot("lysis-heating-mobile", MOBILE, { clip: ["#screen-home .banner.state", "#tempFullAmp"] });

  await waitPhase("waitlysis", 40000);
  await sleep(1800);
  await shot("lysis-inserttube-mobile", MOBILE, { clip: ["#screen-home .banner.state", "#buttonsCard"] });

  await press("bRed", "waitlysis");         // RED = "Start lysis"
  await waitTitle("Lysis running", 15000);  // reports phase "heater", not its own phase
  await sleep(1500);
  await shot("lysis-running-mobile", MOBILE, { clip: ["#screen-home .banner.state", "#buttonsCard"] });

  await waitPhase("waitphase2", 40000);
  await sleep(1800);
  await shot("lysis-removetube-mobile", MOBILE, { clip: ["#screen-home .banner.state", "#buttonsCard"] });
  await shot("lysis-removetube-desktop", DESKTOP);

  log(`\ndone: ${shotNo} images in ${OUT}`);
  ws.close();
  edge.kill();
  mock.kill();
  process.exit(0);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
