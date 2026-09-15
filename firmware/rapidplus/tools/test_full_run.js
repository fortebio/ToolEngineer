#!/usr/bin/env node
/**
 * End-to-end test of the FULL amplification procedure against the web dashboard.
 *
 * Walks a whole run the way a user does and asserts the web at every stage:
 *   heater -> waitamp (name slots, Start locked) -> Confirm -> amplification
 *   (all 120 rounds) -> finished (chart stays) -> Result read-back
 *
 * Runs the client at REAL scale: `sse_test_server.py --full` reports
 * amplification_time = 120 rounds x timePerLoop = 20 s (= a 40 minute run,
 * src/define.h) with a compressed wall clock, so chart point counts, /curve
 * payload sizes and x-axis values are exactly what the device produces - in ~20 s.
 *
 * Drives headless Edge over the DevTools Protocol (node stdlib only - no
 * playwright/puppeteer). It polls the DOM, so it never races the SSE stream.
 *
 * Usage:
 *   python tools/sse_test_server.py --full     # terminal 1
 *   node tools/test_full_run.js                # terminal 2
 *
 * Exits non-zero on the first failed assertion.
 */
const fs = require("fs");
const http = require("http");
const { spawn } = require("child_process");

const URL = process.env.RAPID_URL || "http://localhost:8000";
const CDP_PORT = 9222;
const EDGE =
  process.env.EDGE_PATH ||
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe";
const PROFILE = require("os").tmpdir() + "/rapid-e2e-profile";

const EXPECT_ROUNDS = 120; // amplification_time
const EXPECT_INTERVAL_MS = 20000; // timePerLoop -> 40 min run
const EXPECT_MIN_PER_ROUND = EXPECT_INTERVAL_MS / 60000;

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
  const shown = (id) =>
    `(function(){var e=document.getElementById('${id}');return !!e && !e.classList.contains('hide');})()`;
  const pts = (view) =>
    ev(`(window.${view}&&${view}.chart)?${view}.chart.series[0].data.length:-1`);

  // ---------------------------------------------------------------- 1. heater
  // The device boots idle (escreenStart) and waits - press GREEN (Lysis) to start,
  // the same as a user would.
  console.log("\n1) HEATER - press Lysis to start the run");
  await poll("lastPhase==='idle'", "idle start screen", 20000);
  await ev("document.getElementById('bGreen').click()");
  await poll("lastPhase==='heater'", "heater phase", 30000);
  ok(!(await ev(shown("homeChartCard"))), "chart hidden while heating");
  ok(!(await ev(shown("namingCard"))), "naming card hidden while heating");
  ok(await ev(shown("tempFullLysis")), "full temperature cards shown");

  // ------------------------------------------------------- 2. waitamp / naming
  console.log("\n2) WAITAMP - name slots, Start locked");
  await poll("lastPhase==='waitamp'", "waitamp phase", 30000);
  await sleep(400);
  ok(await ev(shown("namingCard")), "naming card shown");
  ok(
    await ev("document.getElementById('bRed').classList.contains('locked')"),
    "Start (red) is locked until names are confirmed",
  );
  ok((await ev("document.querySelectorAll('#namingBody tr').length")) === 10, "10 slot rows to name");
  ok((await pts("homeView")) === 0, "chart EMPTY for a run that has not started");

  // Name a slot; it must persist to the device and reach the Result table.
  // The disease field is a <select> over a FIXED list (PC/EHP/EMS/WSSV/TPD), not free text:
  // assigning a value that is not an option leaves the select on "" and the whole naming
  // chain then silently tests nothing. Use a real code.
  await ev(
    "(function(){var i=document.querySelectorAll('#namingBody .slot-name')[2];" +
      "i.value='EHP';i.dispatchEvent(new Event('change'));})()",
  );
  await sleep(400);
  const saved = await ev(
    "fetch('/slots').then(r=>r.json()).then(d=>d.slots[2].name)",
  );
  ok(saved === "EHP", "typed name persisted to the device", `/slots -> ${saved}`);

  // --------------------------------------------------------------- 3. confirm
  console.log("\n3) CONFIRM - unlock Start, reveal chart, collapse temps");
  await ev("document.getElementById('confirmNamesBtn').click()");
  await sleep(500);
  ok(!(await ev("document.getElementById('bRed').classList.contains('locked')")), "Start unlocked");
  ok(await ev(shown("homeChartCard")), "live chart revealed");
  ok(await ev(shown("tempCompact")), "temperatures collapsed to the compact strip");
  ok(!(await ev(shown("tempFullLysis"))), "full temperature cards hidden");
  ok((await pts("homeView")) === 0, "chart still EMPTY before the run starts");
  ok(
    (await ev("homeView.chart.series[2].name")) === "EHP",
    "confirmed name applied to the chart series",
  );

  // --------------------------------------------------- 4. amplification (full)
  // Press Start on the web - the run does not begin until the user does (the mock
  // holds at waitamp exactly like ewaitampTube does).
  console.log(`\n4) AMPLIFICATION - press Start, stream all ${EXPECT_ROUNDS} rounds`);
  await ev("document.getElementById('bRed').click()");
  await poll("lastPhase==='amplification'", "amplification phase (after Start press)", 30000);
  const early = await pts("homeView");
  ok(early < 20, "chart starts near zero points (new run, not the previous one)", `${early} pts`);

  await poll(`homeView.chart.series[0].data.length>=${EXPECT_ROUNDS}`, "all rounds streamed", 60000);
  const total = await pts("homeView");
  ok(total === EXPECT_ROUNDS, `chart holds exactly ${EXPECT_ROUNDS} points (no sliding window)`, `${total}`);

  const minPer = await ev("minPerRound");
  ok(
    Math.abs(minPer - EXPECT_MIN_PER_ROUND) < 1e-6,
    `x axis uses the device interval (${EXPECT_INTERVAL_MS} ms/round)`,
    `minPerRound=${minPer}`,
  );
  const lastX = await ev("homeView.chart.series[0].data[homeView.chart.series[0].data.length-1].x");
  const expectLastX = (EXPECT_ROUNDS - 1) * EXPECT_MIN_PER_ROUND;
  ok(
    Math.abs(lastX - expectLastX) < 1e-6,
    `last point sits at ${expectLastX.toFixed(2)} min (a ${((EXPECT_ROUNDS * EXPECT_INTERVAL_MS) / 60000).toFixed(0)}-minute run)`,
    `x=${lastX}`,
  );

  // /curve at full scale: this is the payload the device must serve at run end
  const curve = await ev(
    "fetch('/curve').then(r=>r.text()).then(t=>JSON.stringify({bytes:t.length,count:JSON.parse(t).count}))",
  );
  const c = JSON.parse(curve);
  ok(c.count === EXPECT_ROUNDS, "/curve serves the whole run", `count=${c.count}`);
  console.log(`  INFO  /curve payload at full scale = ${c.bytes} bytes`);

  // A backfill must reproduce the live stream exactly: same count, no gap, no dupe.
  // (This is what a reconnect does - /curve is the device's source of truth.)
  await ev("loadCurve(homeView)");
  await sleep(800);
  const afterBackfill = await pts("homeView");
  ok(
    afterBackfill === EXPECT_ROUNDS,
    "/curve backfill reproduces the live run exactly (no gap, no duplicate)",
    `${afterBackfill} pts`,
  );

  // ---------------------------------------------------------- 5. finished
  console.log("\n5) FINISHED - chart stays until the white button");
  await poll("lastPhase==='finished'", "finished phase", 40000);
  await sleep(600);
  ok(await ev(shown("homeChartCard")), "chart STAYS on Home after the run ends");
  ok(await ev(shown("tempCompact")), "compact temps stay after the run ends");
  const finPts = await pts("homeView");
  ok(finPts === EXPECT_ROUNDS, "finished chart still holds the whole run", `${finPts} pts`);

  // ------------------------------------------------------------- 6. Result tab
  // Read back the stored run BEFORE the white press restarts the machine.
  console.log("\n6) RESULT - read back the stored run");
  await ev("document.querySelector('.nav-item[data-screen=\"result\"]').click()");
  await sleep(700);
  await ev("document.getElementById('viewChartBtn').click()");
  await poll(`resultView.chart.series[0].data.length>=${EXPECT_ROUNDS}`, "stored curve", 15000);
  const rp = await pts("resultView");
  ok(rp === EXPECT_ROUNDS, "Result chart redraws the whole stored run", `${rp} pts`);
  // Poll: the Result table is (re)built only after /slots answers, so reading it the
  // instant the chart appears is a race, not a product bug.
  await poll(
    "document.querySelectorAll('#slotBody .slot-name')[2] && " +
      "document.querySelectorAll('#slotBody .slot-name')[2].value==='EHP'",
    "name reaches the Result table",
    5000,
  ).catch(function () {});
  const name = await ev("document.querySelectorAll('#slotBody .slot-name')[2].value");
  ok(name === "EHP", "Result table shows the name set on Home", `got ${JSON.stringify(name)}`);
  // The row is 3 cells now (Sample | CT | Result), not the old 5 (Show|Slot|Disease|CT|Result):
  // children[3] was undefined and threw a TypeError that read like a page crash.
  const ct = await ev("document.querySelectorAll('#slotBody tr')[0].children[1].textContent");
  ok(ct !== "-" && ct !== "", "Result table shows CT for the finished run", `CT=${ct}`);

  // ------------------------------------------------- 7. white clears the chart
  console.log("\n7) WHITE - only this clears the Home chart");
  await ev("document.querySelector('.nav-item[data-screen=\"home\"]').click()");
  await sleep(400);
  await ev("document.getElementById('bWhite').click()"); // real press -> device restarts
  await poll("lastPhase!=='finished'", "device left the finished screen", 10000);
  await sleep(400);
  ok(!(await ev(shown("homeChartCard"))), "white/Return clears the chart");
  ok(await ev(shown("tempFullLysis")), "white/Return restores the full temperature cards");

  console.log(
    `\n${failures ? "FAILED" : "ALL PASSED"} - ${failures} failure(s)\n`,
  );
  ws.close();
  edge.kill();
  process.exit(failures ? 1 : 0);
}

main().catch((e) => {
  console.error("\nERROR:", e.message);
  process.exit(2);
});
