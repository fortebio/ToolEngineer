/**
 * Which layout does each real device get?
 *
 * The dashboard switches to the two-column layout on `min-width: 820px AND min-height:
 * 600px`, so the split is by WINDOW SIZE, not by device class - an iPad mini and a
 * half-screen laptop window land on the phone layout. This prints the answer per viewport
 * so the user manual can describe the two layouts without guessing.
 *
 * Usage: node tools/probe_layouts.js      (needs the mock: python tools/sse_test_server.py)
 */
const { spawn } = require("child_process");
const http = require("http");
const path = require("path");
const os = require("os");
const fs = require("fs");

const CDP = 9229;
const URL = process.argv[2] || "http://localhost:8000/";
const PROFILE = path.join(os.tmpdir(), "edge-probe-layouts");
const EDGE =
  process.env.EDGE_PATH ||
  "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";

const VIEWPORTS = [
  ["iPhone SE            ", 375, 667],
  ["iPhone 14            ", 390, 844],
  ["iPhone 14 Pro Max    ", 430, 932],
  ["iPhone 14 landscape  ", 844, 390],
  ["iPad mini portrait   ", 744, 1133],
  ["iPad 10.9 portrait   ", 820, 1180],
  ["iPad Pro 11 portrait ", 834, 1194],
  ["iPad 10.9 landscape  ", 1180, 820],
  ["iPad Pro 12.9 land.  ", 1366, 1024],
  ["Laptop 1280x800      ", 1280, 800],
  ["Laptop half-window   ", 640, 800],
  ["Desktop 1920x1080    ", 1920, 1080],
];

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const get = (port, p) =>
  new Promise((res, rej) =>
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

const PROBE = `(function () {
  var home = document.getElementById('screen-home');
  var cs = getComputedStyle(home);
  var root = getComputedStyle(document.documentElement);
  var nav = document.querySelector('.bottom-nav');
  var navBox = nav.getBoundingClientRect();
  var cols = cs.display === 'grid' ? cs.gridTemplateColumns.split(' ').length : 1;
  var naming = document.getElementById('namingCard');
  return JSON.stringify({
    cols: cols,
    display: cs.display,
    maxw: root.getPropertyValue('--maxw').trim(),
    headerH: root.getPropertyValue('--header-h').trim(),
    navBottomGap: Math.round(window.innerHeight - navBox.bottom),
    navWidth: Math.round(navBox.width),
    hscroll: document.documentElement.scrollWidth - window.innerWidth,
    namingHidden: naming ? getComputedStyle(naming).display === 'none' : null
  });
})()`;

async function main() {
  fs.rmSync(PROFILE, { recursive: true, force: true });
  const edge = spawn(EDGE, [
    "--headless=new",
    "--disable-gpu",
    "--hide-scrollbars",
    "--disable-extensions",
    "--no-first-run",
    `--remote-debugging-port=${CDP}`,
    `--user-data-dir=${PROFILE}`,
    "about:blank",
  ]);
  edge.on("error", (e) => {
    console.error("cannot launch Edge:", e.message);
    process.exit(2);
  });

  let targets = [];
  for (let i = 0; i < 40; i++) {
    try {
      targets = await get(CDP, "/json");
      if (targets.some((t) => t.type === "page")) break;
    } catch (e) {}
    await sleep(300);
  }
  const ws = new WebSocket(targets.find((t) => t.type === "page").webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r));
  let id = 0;
  const pend = new Map();
  ws.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.id && pend.has(m.id)) {
      pend.get(m.id)(m);
      pend.delete(m.id);
    }
  };
  const send = (method, params = {}) =>
    new Promise((res) => {
      const mid = ++id;
      pend.set(mid, res);
      ws.send(JSON.stringify({ id: mid, method, params }));
    });

  await send("Page.enable");
  await send("Runtime.enable");
  await send("Network.enable");
  await send("Network.setCacheDisabled", { cacheDisabled: true });

  console.log(
    "device                  size       layout    cols  --maxw  hdr   nav(w/gap)  hscroll",
  );
  console.log("-".repeat(92));
  for (const [name, w, h] of VIEWPORTS) {
    await send("Emulation.setDeviceMetricsOverride", {
      width: w,
      height: h,
      deviceScaleFactor: 1,
      mobile: w < 820,
    });
    await send("Page.navigate", { url: URL });
    await sleep(1800);
    const r = await send("Runtime.evaluate", { expression: PROBE, returnByValue: true });
    const d = JSON.parse(r.result.result.value);
    const layout = d.cols >= 2 ? "2-COLUMN" : "1-column";
    console.log(
      `${name} ${String(w).padStart(4)}x${String(h).padEnd(5)} ${layout}  ${String(d.cols).padStart(3)}   ` +
        `${d.maxw.padEnd(6)} ${d.headerH.padEnd(5)} ${String(d.navWidth).padStart(4)}/${String(d.navBottomGap).padStart(2)}      ` +
        `${d.hscroll > 0 ? "OVERFLOW +" + d.hscroll : "ok"}`,
    );
  }

  ws.close();
  edge.kill();
  process.exit(0);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
