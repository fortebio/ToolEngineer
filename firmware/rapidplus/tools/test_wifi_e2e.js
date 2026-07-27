/**
 * End-to-end test of every WiFi flow on the dashboard, driven through a REAL browser
 * (headless Edge over CDP) against the mock server. Catches the browser-side bugs a
 * curl/unit test can't: stale-cache after a remove, focus jump on network pick, the saved
 * list actually re-rendering, Connect/Forget wiring.
 *
 * The store ALGORITHM (ordering, dedup, cap) is covered separately and on-host by
 * tools/test_wifi_store.cpp; this file covers the UI + endpoint contract end to end.
 *
 * Spawns its OWN mock on a dedicated port so every run starts from a known state (two
 * saved networks) - otherwise a previous run's remove/connect would leave the shared
 * mock mutated and the assertions would drift.
 *
 * Usage:  node tools/test_wifi_e2e.js
 */
const { spawn } = require("child_process");
const http = require("http");
const path = require("path");
const os = require("os");

const PORT = 9231; // CDP port
const MOCK_PORT = 8091; // dedicated mock, spawned below
const BASE = `http://localhost:${MOCK_PORT}/`;
const EDGE =
  process.env.EDGE_PATH ||
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe";
const PY =
  process.env.PYTHON ||
  path.join(
    process.env.USERPROFILE || os.homedir(),
    ".platformio/penv/Scripts/python.exe",
  );

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
const check = (cond, msg, extra) => {
  console.log(`  ${cond ? "PASS" : "FAIL"}  ${msg}${extra ? "  (" + extra + ")" : ""}`);
  if (!cond) failures++;
};

async function main() {
  // Fresh mock, isolated on its own port.
  const mock = spawn(PY, [
    path.join(__dirname, "sse_test_server.py"),
    String(MOCK_PORT),
  ]);
  mock.on("error", (e) => {
    console.error("cannot launch mock:", e.message, "- set PYTHON");
    process.exit(2);
  });
  // wait for the mock to answer
  for (let i = 0; i < 30; i++) {
    const up = await new Promise((res) =>
      http
        .get(`http://localhost:${MOCK_PORT}/home`, (r) => {
          r.resume();
          res(true);
        })
        .on("error", () => res(false)),
    );
    if (up) break;
    await sleep(300);
  }
  const cleanup = () => {
    try {
      mock.kill();
    } catch (e) {}
  };
  process.on("exit", cleanup);

  const edge = spawn(EDGE, [
    "--headless=new",
    "--disable-gpu",
    `--remote-debugging-port=${PORT}`,
    `--user-data-dir=${path.join(os.tmpdir(), "edge-wifi-e2e")}`,
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
  const ev = async (expr) => {
    const r = await send("Runtime.evaluate", {
      expression: expr,
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
  await send("Network.setCacheDisabled", { cacheDisabled: false }); // DON'T disable cache: we WANT to prove no-store handles it
  await send("Emulation.setDeviceMetricsOverride", {
    width: 1280,
    height: 900,
    deviceScaleFactor: 1,
    mobile: false,
  });

  // Fresh mock state: two saved networks (accept-confirm auto-yes for the test).
  await send("Page.navigate", { url: BASE });
  await sleep(2000);
  await ev(`window.confirm = () => true;`); // auto-accept the reboot/switch prompts

  // (Re-)open the WiFi panel from scratch so its saved list reflects current server
  // state. Needed after Connect leaves the UI in a "rebooting" limbo that never re-fetches.
  const openWifiPanel = async () => {
    await ev(`document.querySelector('.nav-item[data-screen="setting"]').click()`);
    await sleep(200);
    await ev(`(() => { const b = document.getElementById('setBack'); if (b && !b.closest('.hide')) b.click(); })()`);
    await sleep(200);
    // Empty the old list FIRST, so the wait below means "the fresh render landed" and not
    // "the previous render is still on screen" - otherwise assertions read stale rows.
    await ev(`(() => { const b = document.getElementById('wifiSaved'); if (b) b.innerHTML = ''; })()`);
    await ev(
      `[...document.querySelectorAll('.set-card')].find(x => x.textContent.indexOf('WiFi') >= 0).click()`,
    );
    for (let i = 0; i < 20; i++) {
      if (await ev(`document.querySelectorAll('#wifiSaved .saved-row').length > 0`)) break;
      await sleep(500);
    }
  };
  await openWifiPanel();

  console.log("WiFi end-to-end (real browser + mock)\n");

  // 1. Saved list renders both networks and marks ONLY the connected one. There is no
  //    "preferred" badge any more: it was derived from list position, not from the EEPROM
  //    pair, so after a boot fallback it labelled a row the machine was not on.
  const saved0 = await ev(`(() => {
    const rows = [...document.querySelectorAll('#wifiSaved .saved-row')];
    return { count: rows.length,
             names: rows.map(r => r.querySelector('.wifi-name').textContent.replace('connected','').trim()),
             text: rows.map(r => r.textContent).join(' '),
             badges: rows.map(r => !!r.querySelector('.wifi-badge-on')) };
  })()`);
  check(saved0.count === 2, `saved list shows 2 networks`, `got ${saved0.count}`);
  check(!/preferred/i.test(saved0.text), `no "preferred" badge (list order says it)`);
  check(
    saved0.badges.filter(Boolean).length === 1,
    `exactly one row is badged "connected"`,
    `badges: ${saved0.badges.join(",")}`,
  );

  // 1b. Connect must be offered on every row EXCEPT the connected one. Force the state
  //     that used to break it: the machine joined a saved net that is NOT the front of the
  //     list (boot fallback leaves EEPROM/order untouched), so row 0 is preferred-but-not-
  //     connected. The old UI gated Connect on `i !== 0` and left that row with no way back.
  await post2(`current=${encodeURIComponent((await j2()).nets[1].ssid)}`);
  await openWifiPanel();
  const gate = await ev(`(() => {
    const rows = [...document.querySelectorAll('#wifiSaved .saved-row')];
    return { onNow: rows.map(r => !!r.querySelector('.wifi-badge-on')),
             hasConnect: rows.map(r => !!r.querySelector('.wifi-use')) };
  })()`);
  check(
    gate.onNow[1] === true && gate.hasConnect[0] === true,
    `Connect stays reachable on row 0 when the machine is on another saved net`,
    `connected: ${gate.onNow.join(",")} | connect btn: ${gate.hasConnect.join(",")}`,
  );
  check(
    gate.hasConnect.every((h, k) => h === !gate.onNow[k]),
    `Connect is hidden on exactly the connected row, nowhere else`,
  );
  await post2("current="); // back to the normal "connected == front" state
  await openWifiPanel();

  // 2. Picking a nearby network fills the SSID box and focuses the password field.
  // The scan is async (mock: 202 "scanning" then 200 with the list ~1.5 s later), so wait
  // for the nearby list to populate before clicking.
  for (let i = 0; i < 20; i++) {
    if (await ev(`document.querySelectorAll('#wifiList .wifi-item').length > 0`)) break;
    await sleep(500);
  }
  const pick = await ev(`(() => {
    const n = document.querySelector('#wifiList .wifi-item');
    if (!n) return { ok: false };
    n.click();
    const s = document.getElementById('wifiSsid'), p = document.getElementById('wifiPass');
    return { ok: true, ssid: s.value, focused: document.activeElement === p,
             firstNet: document.querySelector('#wifiList .wifi-name').textContent };
  })()`);
  check(pick.ok && pick.ssid === pick.firstNet, `picking a network fills the SSID box`, pick.ssid);
  check(pick.focused, `focus jumps to the password field after picking`);

  // 3. Connect on a network the machine is NOT on -> promotes it to preferred (mock
  //    reboots). Pick by "has a Connect button", not by index: the button is gated on
  //    connectedness now, so an index could land on the row that has none.
  const beforeConnect = (await j2()).nets.map((x) => x.ssid);
  const picked = await ev(`(() => {
    const row = [...document.querySelectorAll('#wifiSaved .saved-row')]
      .find(r => r.querySelector('.wifi-use'));
    if (!row) return '';
    const s = row.querySelector('.wifi-name').textContent.replace('connected','').trim();
    row.querySelector('.wifi-use').click();
    return s;
  })()`);
  await sleep(600);
  const afterConnect = (await j2()).nets.map((x) => x.ssid);
  check(
    // beforeConnect[0] !== picked keeps this honest: clicking the net that is ALREADY at
    // the front would "pass" without proving anything moved.
    !!picked && beforeConnect[0] !== picked && afterConnect[0] === picked,
    `Connect promotes the chosen network to the front`,
    `${beforeConnect.join(",")} -> ${afterConnect.join(",")} (picked ${picked || "none"})`,
  );

  // 4. Forget removes a network AND the list re-renders without it. This is the reported
  //    bug: loadSavedWifi's re-fetch uses cache:"no-store", so the removed network does
  //    not reappear from a cached GET. Cache is left ENABLED for this run to exercise it.
  //    Re-open the panel first so the UI matches server state after the Connect above.
  await openWifiPanel();
  const toForget = (await j2()).nets[0].ssid;
  await ev(`(() => {
    const rows = [...document.querySelectorAll('#wifiSaved .saved-row')];
    rows[0].querySelector('.wifi-forget').click();
  })()`);
  await sleep(800); // POST + re-fetch + re-render
  const uiAfterForget = await ev(`
    [...document.querySelectorAll('#wifiSaved .saved-row .wifi-name')]
      .map(x => x.textContent.replace('connected','').trim())`);
  const serverAfterForget = (await j2()).nets.map((x) => x.ssid);
  check(
    !serverAfterForget.includes(toForget),
    `Forget removes the network on the device`,
    `server: ${serverAfterForget.join(",") || "(empty)"}`,
  );
  check(
    !uiAfterForget.includes(toForget),
    `the forgotten network disappears from the UI (no stale cache)`,
    `ui: ${uiAfterForget.join(",") || "(empty)"}`,
  );

  // 5. Wrong password: save does NOT commit; device reboots, TESTS, fails, reverts, and
  //    the panel shows "could not join - re-enter". The list must be UNCHANGED.
  await openWifiPanel();
  const beforeWrong = (await j2()).nets.map((x) => x.ssid);
  await ev(`(() => {
    document.getElementById('wifiSsid').value = 'NewRouter';       // not in the scan
    document.getElementById('wifiPass').value = 'wrongpass';       // mock: fails the test
    // Save & reboot button (confirm() is auto-yes'd at the top of the test).
    [...document.querySelectorAll('#setForm button')]
      .find(b => /Save/.test(b.textContent)).click();
  })()`);
  await sleep(600);
  const afterWrong = (await j2()).nets.map((x) => x.ssid);
  check(
    afterWrong.join(",") === beforeWrong.join(","),
    `wrong password does NOT change the saved list (no commit)`,
    `${beforeWrong.join(",")} -> ${afterWrong.join(",")}`,
  );
  // The save flow polls the device until its queued change lands, so ITS message ("rebooting
  // to test ...") arrives late. Let it land first: re-opening the panel before that would let
  // it overwrite the trial notice we are about to assert on, which is a race, not a bug.
  for (let i = 0; i < 20; i++) {
    if (await ev(`/rebooting to test/i.test((document.getElementById('setMsg')||{}).textContent||'')`))
      break;
    await sleep(300);
  }
  // Re-open the panel: loadSavedWifi reads the trial:failed result and shows the notice.
  await openWifiPanel();
  const msg = await ev(`(document.getElementById('setMsg') || {}).textContent || ''`);
  check(
    /could not join/i.test(msg) && /NewRouter/.test(msg),
    `panel warns "could not join <ssid> - re-enter" after a failed trial`,
    msg.slice(0, 60),
  );

  console.log(`\n${failures ? failures + " FAILED" : "all WiFi e2e checks passed"}`);
  ws.close();
  edge.kill();
  process.exit(failures ? 1 : 0);

  // helper: drive the mock's /wifilist directly (form-encoded, like the panel does)
  async function post2(body) {
    return new Promise((res, rej) => {
      const u = new URL("/wifilist", BASE);
      const rq = http.request(
        {
          host: u.hostname,
          port: u.port,
          path: u.pathname,
          method: "POST",
          headers: {
            "Content-Type": "application/x-www-form-urlencoded",
            "Content-Length": Buffer.byteLength(body),
          },
        },
        (r) => {
          let b = "";
          r.on("data", (c) => (b += c));
          r.on("end", () => res(b));
        },
      );
      rq.on("error", rej);
      rq.end(body);
    });
  }

  // helper: read the mock's /wifilist directly (server truth, bypassing the browser)
  async function j2() {
    return new Promise((res, rej) => {
      const u = new URL("/wifilist", BASE);
      http
        .get({ host: u.hostname, port: u.port, path: u.pathname }, (r) => {
          let b = "";
          r.on("data", (c) => (b += c));
          r.on("end", () => res(JSON.parse(b)));
        })
        .on("error", rej);
    });
  }
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
