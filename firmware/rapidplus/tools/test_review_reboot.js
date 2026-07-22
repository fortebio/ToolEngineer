#!/usr/bin/env node
/**
 * End-to-end test of the Result-tab "review a stored run after a reboot".
 *
 * Simulates a power-cycle where the RAM result cache is empty (/slots ready=false)
 * but the last run still lives in EEPROM. Opening the Result tab must POST /reviewlast,
 * the device reloads+recomputes the stored run, and the table + chart fill in - exactly
 * what the user asked for: "Result -> view the run already measured in the device, even
 * after power off".
 *
 * Boots its own mock in --reboot mode and drives headless Edge over the DevTools
 * Protocol (node stdlib only). Exits non-zero on the first failed assertion.
 *
 * Usage:  node tools/test_review_reboot.js
 */
const http = require("http");
const { spawn } = require("child_process");

const URL = "http://localhost:8000";
const CDP_PORT = 9223;
const EDGE =
  process.env.EDGE_PATH ||
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe";
const PROFILE = require("os").tmpdir() + "/rapid-review-profile";

let failures = 0;
const ok = (cond, msg, extra) => {
  console.log(`  ${cond ? "PASS" : "FAIL"}  ${msg}${extra ? "  (" + extra + ")" : ""}`);
  if (!cond) failures++;
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const httpJson = (path) =>
  new Promise((res, rej) =>
    http
      .get({ host: "127.0.0.1", port: CDP_PORT, path }, (r) => {
        let b = "";
        r.on("data", (c) => (b += c));
        r.on("end", () => res(JSON.parse(b)));
      })
      .on("error", rej),
  );

async function main() {
  // Any leftover mock still bound to 8000 would answer with its OWN (possibly already
  // reviewed) state and make this test lie - kill it first so the run is hermetic.
  try {
    require("child_process").execSync(
      "powershell -Command \"Get-CimInstance Win32_Process -Filter \\\"name='python.exe'\\\" | Where-Object { $_.CommandLine -like '*sse_test_server*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }\"",
      { stdio: "ignore" },
    );
  } catch (e) {}

  // 1. boot the mock as if the device just rebooted with a run still in EEPROM
  const mock = spawn("python", ["tools/sse_test_server.py", "--reboot"], {
    stdio: "ignore",
  });
  await sleep(1200);

  const edge = spawn(EDGE, [
    "--headless=new",
    "--disable-gpu",
    `--remote-debugging-port=${CDP_PORT}`,
    `--user-data-dir=${PROFILE}`,
    "--window-size=900,1500",
    "about:blank",
  ]);
  edge.on("error", (e) => {
    console.error("cannot launch Edge:", e.message, "\nSet EDGE_PATH.");
    mock.kill();
    process.exit(2);
  });

  let targets = [];
  for (let i = 0; i < 30; i++) {
    try {
      targets = await httpJson("/json");
      if (targets.some((t) => t.type === "page")) break;
    } catch (e) {}
    await sleep(300);
  }
  const page = targets.find((t) => t.type === "page");
  if (!page) throw new Error("no Edge page target (is Edge installed?)");

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

  await send("Page.enable");
  await send("Runtime.enable");
  await send("Page.navigate", { url: URL });
  await sleep(1500);

  const ev = async (expr) => {
    const r = await send("Runtime.evaluate", {
      expression: expr,
      returnByValue: true,
      awaitPromise: true,
    });
    if (r.result && r.result.exceptionDetails)
      throw new Error("JS error: " + JSON.stringify(r.result.exceptionDetails));
    return r.result && r.result.result ? r.result.result.value : undefined;
  };
  const poll = async (expr, label, ms) => {
    const t0 = Date.now();
    while (Date.now() - t0 < ms) {
      if (await ev(expr)) return true;
      await sleep(150);
    }
    console.log(`  FAIL  timeout waiting for ${label}`);
    failures++;
    return false;
  };
  const pts = (view) =>
    ev(`(window.${view}&&${view}.chart)?${view}.chart.series[0].data.length:-1`);

  // ---------------------------------------------------------- 1. rebooted state
  console.log("\n1) REBOOTED - device is idle, results are NOT cached in RAM");
  await poll("lastPhase==='idle'", "idle start screen", 20000);
  const readyBefore = await ev(
    "fetch('/slots').then(r=>r.json()).then(d=>d.ready)",
  );
  ok(readyBefore === false, "/slots reports ready=false before review", `ready=${readyBefore}`);
  const curveBefore = await ev(
    "fetch('/curve').then(r=>r.json()).then(d=>d.count)",
  );
  ok(curveBefore === 0, "/curve is empty before review", `count=${curveBefore}`);

  // ------------------------------------------------------------ 2. open Result
  console.log("\n2) OPEN RESULT - client auto-reloads the stored run from EEPROM");
  await ev(
    "document.querySelector('.nav-item[data-screen=\"result\"]').click()",
  );
  // reviewStoredRun POSTs /reviewlast, then polls /slots until ready -> table rebuilds
  await poll(
    "document.querySelectorAll('#slotBody .res-badge').length>0",
    "result badges appear (stored run reloaded)",
    8000,
  );
  const badges = await ev(
    "document.querySelectorAll('#slotBody .res-badge').length",
  );
  ok(badges > 0, "Result table shows the stored run's P/N/S results", `${badges} badges`);
  const readyAfter = await ev(
    "fetch('/slots').then(r=>r.json()).then(d=>d.ready)",
  );
  ok(readyAfter === true, "/slots now reports ready=true (review applied)");

  // -------------------------------------------------------------- 3. the chart
  console.log("\n3) VIEW CHART - the stored curve draws");
  await ev("document.getElementById('viewChartBtn').click()");
  await poll("resultView.chart&&resultView.chart.series[0].data.length>0", "stored curve drawn", 8000);
  const p = await pts("resultView");
  ok(p > 0, "stored curve rendered on the Result chart", `${p} points`);

  ws.close();
  edge.kill();
  mock.kill();
  console.log(`\n${failures ? "FAILED " + failures : "ALL PASSED"}`);
  process.exit(failures ? 1 : 0);
}

main().catch((e) => {
  console.error(e);
  process.exit(2);
});
