/**
 * Screenshot the dashboard in its real states, for design review.
 *
 * Edge's plain --screenshot flag CANNOT be used here: the page holds an SSE connection
 * open forever, so --virtual-time-budget never elapses and the browser hangs. Same reason
 * the E2E test drives CDP - we connect, wait for the UI to actually paint from live data,
 * then capture.
 *
 * Usage:  node tools/ui_screenshot.js [outDir] [baseUrl]
 * Needs the mock running:  python tools/sse_test_server.py
 */
const { spawn } = require("child_process");
const http = require("http");
const fs = require("fs");
const path = require("path");

const OUT = process.argv[2] || ".";
const URL = process.argv[3] || "http://localhost:8000/";
const CDP_PORT = 9223;
const PROFILE = path.join(require("os").tmpdir(), "edge-ui-shots");
const EDGE =
  process.env.EDGE_PATH ||
  "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const httpJson = (p) =>
  new Promise((res, rej) =>
    http
      .get({ host: "127.0.0.1", port: CDP_PORT, path: p }, (r) => {
        let b = "";
        r.on("data", (c) => (b += c));
        r.on("end", () => res(JSON.parse(b)));
      })
      .on("error", rej),
  );

// Each shot: viewport + a script that puts the UI into the state we want to look at.
// Drive the real nav buttons rather than internal functions - show() is module-scoped,
// and clicking is also what the operator actually does.
const tab = (n) => `document.querySelector('.nav-item[data-screen="${n}"]').click()`;
const SHOTS = [
  { name: "mobile-home", w: 390, h: 900, setup: "" },
  { name: "mobile-result", w: 390, h: 1000, setup: tab("result") },
  { name: "mobile-setting", w: 390, h: 1200, setup: tab("setting") },
  {
    name: "mobile-setting-panel",
    w: 390,
    h: 1100,
    setup: tab("setting") + "; document.querySelector('.set-card').click()",
  },
  // Phone held sideways: WIDE but SHORT. Clears the 820px desktop breakpoint while having
  // less vertical room than a portrait phone - the case a width-only media query gets wrong.
  { name: "landscape-home", w: 844, h: 390, setup: "" },
  { name: "landscape-result", w: 844, h: 390, setup: tab("result") },
  { name: "desktop-home", w: 1280, h: 860, setup: "" },
  { name: "desktop-result", w: 1280, h: 860, setup: tab("result") },
  { name: "desktop-setting", w: 1280, h: 860, setup: tab("setting") },
  // Firmware/OTA panel: open Setting, then click the card whose title is "Firmware".
  { name: "setting-ota", w: 1280, h: 860,
    setup: tab("setting") + "; setTimeout(() => { const c = [...document.querySelectorAll('.set-card')].find(x => x.textContent.indexOf('Firmware') >= 0); if (c) c.click(); }, 250)" },
  // WiFi panel: open Setting -> WiFi, wait for the scan, then click the first network
  // to check it fills the SSID box and jumps to the password field.
  { name: "setting-wifi", w: 1280, h: 900,
    setup: tab("setting") + "; setTimeout(() => { const c = [...document.querySelectorAll('.set-card')].find(x => x.textContent.indexOf('WiFi') >= 0); if (c) c.click(); setTimeout(() => { const n = document.querySelector('.wifi-item'); if (n) n.click(); }, 2200); }, 250)" },
  // Wide screen with the stored-run chart open: checks the table stays a readable width
  // while the chart spans everything.
  { name: "wide-result-chart", w: 1600, h: 1000,
    setup: tab("result") + "; setTimeout(()=>document.getElementById('viewChartBtn').click(),300)" },
];

async function main() {
  const edge = spawn(EDGE, [
    "--headless=new",
    "--disable-gpu",
    "--hide-scrollbars",
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
      targets = await httpJson("/json");
      if (targets.some((t) => t.type === "page")) break;
    } catch (e) {}
    await sleep(300);
  }
  const page = targets.find((t) => t.type === "page");
  if (!page) throw new Error("no Edge page target");

  const ws = new WebSocket(page.webSocketDebuggerUrl); // global since Node 22, as in test_full_run.js
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

  await send("Page.enable");
  await send("Runtime.enable");
  // Without this the browser happily re-serves the previous style.css and you review the
  // OLD design while believing you are looking at your edits. Cost me one wrong reading.
  await send("Network.enable");
  await send("Network.setCacheDisabled", { cacheDisabled: true });

  for (const s of SHOTS) {
    await send("Emulation.setDeviceMetricsOverride", {
      width: s.w,
      height: s.h,
      deviceScaleFactor: 2,
      mobile: s.w < 820,
    });
    await send("Page.navigate", { url: URL });
    await sleep(2200); // let SSE deliver at least one `home` frame
    if (s.setup)
      await send("Runtime.evaluate", { expression: s.setup, awaitPromise: true });
    await sleep(900);
    const r = await send("Page.captureScreenshot", {
      format: "png",
      captureBeyondViewport: true,
    });
    const file = path.join(OUT, `ui-${s.name}.png`);
    fs.writeFileSync(file, Buffer.from(r.result.data, "base64"));
    console.log(`${file}  ${Math.round(r.result.data.length * 0.75 / 1024)}KB`);
  }

  ws.close();
  edge.kill();
  process.exit(0);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
