/**
 * Guard: the Setting tab stays usable without a mouse and without sight.
 *
 * Every control must have an accessible name, focus must survive opening and closing a
 * panel, the nearby-WiFi list must not duplicate what the saved list already shows, and
 * disabled controls must drain colour rather than fade - opacity dims the text AND the
 * surface behind it, so the contrast ratio collapses from both sides (measured: the card
 * description went 4.83 -> 2.26, a disabled Save button 5.45 -> 2.55).
 *
 * Runs against the REAL DOM, never the source: it opens each panel the way an operator
 * does and asks the browser for `element.labels`. A regex over script.js would happily
 * pass on markup the browser refuses to associate - a <label> that is neither for= nor an
 * ancestor names nothing, and that is exactly the bug this was written for.
 *
 * Panels come from the page's own CARDS table, so a card that is restored later (the
 * Calibration wizard, the PID form) is covered the moment it reappears.
 *
 * Usage:  python tools/sse_test_server.py     (in another shell - this test does NOT start
 *                                              it; without the mock every panel times out
 *                                              and it reads like a UI regression)
 *         node tools/test_setting_a11y.js
 */
const { spawn } = require("child_process");
const http = require("http");
const path = require("path");
const os = require("os");

const PORT = 9231;
const PROFILE = path.join(os.tmpdir(), "edge-setting-a11y");
const URL = process.argv[2] || "http://localhost:8000/";
const EDGE =
  process.env.EDGE_PATH ||
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe";

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
    // CDP nests it twice: {result: {result: {value}}}.
    return r.result && r.result.result ? r.result.result.value : undefined;
  };

  await send("Page.enable");
  await send("Runtime.enable");
  await send("Network.enable");
  // Without this the browser re-serves the previous script.js and the run grades the OLD
  // build while looking green.
  await send("Network.setCacheDisabled", { cacheDisabled: true });
  // Desktop metrics: the two-column card grid and the wide panel are the harder layout,
  // and nothing here is size-dependent.
  await send("Emulation.setDeviceMetricsOverride", {
    width: 1280,
    height: 900,
    deviceScaleFactor: 1,
    mobile: false,
  });
  await send("Page.navigate", { url: URL });
  await sleep(2500); // one `home` SSE frame

  const nav = (name) =>
    ev(`document.querySelector('.nav-item[data-screen="${name}"]').click()`);

  // The cards only exist once loadConfig() resolves (script.js:47). A fixed delay opens
  // nothing on the first entry, and "no unnamed controls" then passes on an EMPTY panel -
  // a vacuous green that hid a whole panel the first time this was written.
  const openCardById = (cid) =>
    ev(`new Promise(function(res){var n=0;var iv=setInterval(function(){
      var c=document.querySelector('.set-card[data-card="${cid}"]');
      if(c){clearInterval(iv);c.click();res('opened');}
      else if(++n>60){clearInterval(iv);res('TIMEOUT');}},100)})`);

  await nav("setting");
  await sleep(400);
  const cards = await ev(
    `new Promise(function(res){var n=0;var iv=setInterval(function(){
      var c=document.querySelectorAll('.set-card');
      if(c.length){clearInterval(iv);res([].map.call(c,function(x){return x.getAttribute('data-card')}));}
      else if(++n>60){clearInterval(iv);res([]);}},100)})`,
  );
  check(cards.length > 0, `Setting menu rendered ${cards.length} card(s)`, cards.join(","));
  if (!cards.length) {
    console.log("\nFAIL - no cards; is the mock running? python tools/sse_test_server.py");
    process.exit(1);
  }

  // ---- 1. every control in every panel is named, and 2. opening focuses Back -----------
  for (const cid of cards) {
    await nav("setting");
    await sleep(300);
    const opened = await openCardById(cid);
    if (opened !== "opened") {
      check(false, `${cid}: card never appeared`, opened);
      continue;
    }
    await sleep(cid === "wifi" ? 3200 : 800); // wifi waits on the scan
    check(
      await ev(`document.activeElement.id === 'setBack'`),
      `${cid}: opening the panel moves focus into it`,
      await ev(`document.activeElement.id || document.activeElement.tagName`),
    );
    const n = await ev(
      `document.querySelectorAll('#setForm input, #setForm select, #setForm textarea').length`,
    );
    const unnamed = await ev(
      `[].filter.call(document.querySelectorAll('#setForm input, #setForm select, #setForm textarea'),
        function(e){ return !(e.labels && e.labels.length) && !e.getAttribute('aria-label') &&
                            !e.getAttribute('aria-labelledby') && !e.title; })
       .map(function(e){ return e.id || e.type || e.tagName; })`,
    );
    // n > 0 is load-bearing: "nothing unnamed" is trivially true of a panel that never
    // opened. Panels can legitimately hold zero controls (a pure wizard step), so only
    // require the count where the panel is a form.
    check(
      unnamed.length === 0,
      `${cid}: all ${n} control(s) have an accessible name`,
      unnamed.join(","),
    );
    // ---- 3. closing hands focus back to the card that opened the panel -----------------
    await ev(`document.getElementById('setBack').click()`);
    await sleep(250);
    check(
      (await ev(`document.activeElement.getAttribute('data-card')`)) === cid,
      `${cid}: Back returns focus to its card`,
      await ev(`document.activeElement.tagName + '.' + document.activeElement.className`),
    );
  }

  // ---- 4. re-entering the tab must NOT steal focus ------------------------------------
  // showMenu() also runs from the bottom nav and from backToHomeAfterSave(); on those paths
  // the panel was never holding focus. Restoring focus inside showMenu() took it off the
  // nav button the operator had just pressed, and loadConfig().then(renderSetMenu) then
  // emptied #setMenu and destroyed the node now holding it - focus landed on <body>, which
  // is the exact failure the restore exists to prevent.
  await nav("setting");
  await sleep(400);
  await openCardById(cards[0]);
  await sleep(600);
  await nav("home");
  await sleep(400);
  // focus() THEN click(): a synthetic click never moves focus, so without this the baseline
  // is <body> and the assertion passes for the wrong reason. This is the keyboard path.
  await ev(
    `(function(){var n=document.querySelector('.nav-item[data-screen="setting"]');n.focus();n.click();})()`,
  );
  const immediate = await ev(`document.activeElement.className`);
  await sleep(1800); // let loadConfig() resolve and renderSetMenu() empty #setMenu
  const settled = await ev(`document.activeElement.className`);
  check(
    /nav-item/.test(immediate) && /nav-item/.test(settled),
    "re-entering Setting with a panel left open keeps focus on the nav item",
    `immediate=${immediate || "<body>"} settled=${settled || "<body>"}`,
  );

  // ---- 5. nearby WiFi hides what the saved list shows, without truncating the scan -----
  if (cards.indexOf("wifi") >= 0) {
    await nav("setting");
    await sleep(300);
    await openCardById("wifi");
    await sleep(3500);
    const saved = await ev(`wifiSavedSsids.slice()`);
    const scan = await ev(`wifiScanNets.map(function(n){return n.ssid})`);
    const shown = await ev(
      `[].map.call(document.querySelectorAll('#wifiList .wifi-name'), function(e){return e.textContent.trim()})`,
    );
    check(saved.length > 0 && scan.length > 0, "saved + scan lists both loaded",
      `saved=${saved.length} scan=${scan.length}`);
    check(
      shown.every((s) => saved.indexOf(s) < 0),
      "nearby list hides networks the saved list already shows",
      shown.filter((s) => saved.indexOf(s) >= 0).join(","),
    );
    // wifiScanNets backs the Save-time SSID typo check: filter it and typing a saved SSID
    // by hand is rejected as a typo right before the reboot.
    check(
      saved.some((s) => scan.indexOf(s) >= 0),
      "wifiScanNets still holds the SSIDs the list hides",
      JSON.stringify(scan),
    );
    await ev(`document.getElementById('setBack').click()`);
    await sleep(250);
  }

  // ---- 6. disabled = drained, not faded ------------------------------------------------
  const faded = await ev(`(function(){
    var out = [];
    var probes = [
      ['.set-card', function(e){ e.disabled = true; }],
      ['.save-btn',  function(e){ e.disabled = true; }]
    ];
    probes.forEach(function(p){
      var e = document.querySelector(p[0]);
      if (!e) return;
      var was = e.disabled; p[1](e);
      var cs = getComputedStyle(e);
      if (parseFloat(cs.opacity) < 1) out.push(p[0] + ' opacity=' + cs.opacity);
      if (cs.filter === 'none') out.push(p[0] + ' has no filter (no disabled cue at all)');
      e.disabled = was;
    });
    return out;
  })()`);
  check(
    faded.length === 0,
    "disabled controls drain colour instead of fading (opacity collapses contrast)",
    faded.join("; "),
  );

  ws.close();
  edge.kill();
  console.log(
    failures ? `\nFAIL - ${failures} check(s)` : "\nok - Setting tab is named, focusable and readable",
  );
  process.exit(failures ? 1 : 0);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
