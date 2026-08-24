/**
 * Guard: the Profile card edits in MINUTES while the device keeps storing seconds and rounds.
 *
 * Why this exists. parastructure is 400 B against a 402 B ceiling (CLAUDE.md Setting #5), so the
 * device cannot carry a second copy of any duration in a friendlier unit, and the firmware's range
 * checks (handleConfigPost) are written against the stored units - seconds for lysis and opto
 * preheat, ROUNDS for amplification. The whole minutes/seconds conversion therefore lives in two
 * one-line hooks at the edge of the form (renderFields + collectFields, via f.toUi / f.toDev).
 *
 * That makes it exactly the kind of code that breaks quietly: a factor of 60 in the wrong
 * direction still produces a plausible-looking number, and the failure only shows up as a run that
 * lasts 40 seconds or 40 hours. So the round-trips are pinned here.
 *
 * The clamp is the part that must never regress: COUNTER indexes sensor67Value[10][130]: a larger
 * round count overflows it MID-RUN. The firmware rejects >130 as well, so this is the second of
 * two gates, not the only one - but a form that posts a value it knows will be refused is a form
 * that tells the operator "saved" and changes nothing.
 *
 * Pure math, no browser and no mock: it lifts the real field table out of data/script.js.
 *
 * Run:  node tools/test_profile_minutes.js
 */
const fs = require("fs");
const path = require("path");

const SRC = path.join(__dirname, "..", "data", "script.js");
const src = fs.readFileSync(SRC, "utf8");
const defineH = fs.readFileSync(path.join(__dirname, "..", "src", "define.h"), "utf8");

let failures = 0;
const check = (ok, msg, extra) => {
  console.log(`  ${ok ? "ok  " : "FAIL"}  ${msg}${extra ? "  -> " + extra : ""}`);
  if (!ok) failures++;
};

/* Lift `fields: [ ... ]` out of the profile card by bracket matching. A regex cannot do this:
   the array holds nested objects, arrow-free function bodies and their own brackets. */
function liftFields(cardId) {
  const at = src.indexOf(`id: "${cardId}"`);
  if (at < 0) throw new Error(`card "${cardId}" not found - has CARDS been restructured?`);
  const start = src.indexOf("fields: [", at);
  if (start < 0) throw new Error(`card "${cardId}" has no fields[]`);
  let i = src.indexOf("[", start);
  let depth = 0;
  for (let j = i; j < src.length; j++) {
    const c = src[j];
    if (c === "[") depth++;
    else if (c === "]") {
      depth--;
      if (depth === 0) return src.slice(i, j + 1);
    }
  }
  throw new Error("unbalanced fields[]");
}

// perLoopMs() normally reads the device config; here it is injected so the test controls it.
// TX() is the i18n lookup added in 2026-08. The labels it returns are not what this
// guard measures - it checks units and clamps - so it is stubbed to hand back the key.
const build = (perLoop) =>
  new Function("perLoopMs", "TX", "return " + liftFields("profile"))(
    () => perLoop,
    (k) => k
  );

const fields = build(20000);
const by = (p) => fields.find((f) => f.p === p);

// ---- 1. Shape: the two-fields-into-one merge actually happened ---------------------------
check(!by("time per loop"), '"time per loop" is no longer its own field (merged into minutes)');
check(fields.length === 5, "profile has 5 fields", String(fields.length));

for (const p of ["lysis duration", "amplification time", "opto preheat time"]) {
  const f = by(p);
  check(!!f, `field "${p}" is present`);
  if (!f) continue;
  check(f.u === "min", `"${p}" is labelled in minutes`, f.u);
  // A field with only one half of the pair reads correctly and saves garbage, or the reverse.
  check(
    typeof f.toUi === "function" && typeof f.toDev === "function",
    `"${p}" converts in BOTH directions`,
    `toUi=${typeof f.toUi} toDev=${typeof f.toDev}`,
  );
}

// ---- 2. Round-trips at the shipped defaults ----------------------------------------------
const rt = (p, stored, minutes) => {
  const f = by(p);
  if (!f) return;
  check(f.toUi(stored) === minutes, `${p}: ${stored} stored reads as ${minutes} min`, String(f.toUi(stored)));
  check(f.toDev(minutes) === stored, `${p}: ${minutes} min saves back as ${stored}`, String(f.toDev(minutes)));
};
rt("lysis duration", 600, 10); // default 600 s
rt("opto preheat time", 900, 15); // default since 2026-08-05: 15 min, matching the hotlid hold
rt("opto preheat time", 300, 5); // the previous default - still a conversion that must hold
rt("amplification time", 120, 40); // default 120 rounds x 20 s = the 40 minute run

// The shipped default is what an operator actually sees the first time they open this card, so
// pin the form's minute view of it against the firmware rather than against a number typed here.
// Anchor on the DECLARATION, not on the identifier. A plain indexOf("optopreheatduration") finds
// the first mention, which is inside the comment block above the field, and the regex then picked
// a number out of the prose - it read 45 (a round count) as 45 seconds and reported the default as
// 0.75 min. Third time this session a check has been fooled by a comment; anchor on syntax.
const decl = /uint16_t\s+optopreheatduration\s*=\s*([^;]+);/.exec(defineH);
check(!!decl, "found the optopreheatduration declaration in src/define.h");
const defExpr = decl ? decl[1] : "";
const mul = /^\s*(\d+)\s*\*\s*(\d+)\s*$/.exec(defExpr);
const defSec = mul ? Number(mul[1]) * Number(mul[2]) : Number(defExpr);
check(
  by("opto preheat time").toUi(defSec) === defSec / 60,
  `the firmware default (${defSec} s) reads as ${defSec / 60} min in the form`,
  String(by("opto preheat time").toUi(defSec)),
);
check(defSec <= 3600, "the firmware default is inside handleConfigPost's 0..3600 s range", String(defSec));

// ---- 3. The clamp: sensor67Value[10][130] ------------------------------------------------
const amp = by("amplification time");
check(amp.toDev(60) === 130, "60 min clamps to 130 rounds, not 180 (buffer overflow)", String(amp.toDev(60)));
check(amp.toDev(43.3) === 130, "43.3 min lands exactly on the 130-round cap", String(amp.toDev(43.3)));
check(amp.toDev(0) === 1, "0 min floors at 1 round, never 0", String(amp.toDev(0)));
check(amp.max <= 43.4, "the input's own max hint stays inside the cap", String(amp.max));

// ---- 4. The round length is the DEVICE's, not a constant ---------------------------------
// A machine whose "time per loop" is not 20 s must still read back the truth. Hardcoding 20000
// would make this pair agree with each other and disagree with the machine.
const at10s = build(10000).find((f) => f.p === "amplification time");
check(at10s.toUi(120) === 20, "at a 10 s round, 120 rounds reads as 20 min", String(at10s.toUi(120)));
check(at10s.toDev(20) === 120, "...and 20 min saves back as 120 rounds", String(at10s.toDev(20)));
check(at10s.toDev(60) === 130, "the 130-round clamp holds at any round length", String(at10s.toDev(60)));

// ---- 5. Stored ranges still fit the firmware's own limits ---------------------------------
// handleConfigPost rejects lysis >65535 s and opto preheat >3600 s. The form's minute maxima
// must not be able to produce more than that, or Save fails with a 400 the operator cannot read.
check(by("lysis duration").toDev(by("lysis duration").max) <= 65535, "lysis max stays <= 65535 s",
  String(by("lysis duration").toDev(by("lysis duration").max)));
check(by("opto preheat time").toDev(by("opto preheat time").max) <= 3600, "preheat max stays <= 3600 s",
  String(by("opto preheat time").toDev(by("opto preheat time").max)));

// ---- 6. perLoopMs() asks the DEVICE, and this is a separate assertion on purpose ----------
// Section 4 INJECTS perLoopMs, so it proves the fields call it instead of baking 20 s into their
// own arithmetic - but it is blind to what perLoopMs itself does. Replacing its body with a bare
// `return 20000` left every check above green, which is how this one came to exist.
// Run the REAL function, do not pattern-match it. A first attempt asserted that the body
// contained a getPath(cfgCache, "time per loop") call, and a seeded `return 20000;` sailed
// straight past it: the read was still in the source, just no longer connected to the result.
// Calling it cannot be fooled that way. getPath is stubbed because its dotted-path handling is
// not what is under test here, and none of these keys are dotted.
// Was pinned to a CRLF terminator and swallowed the whole file once script.js
// changed to LF, turning this guard into a syntax error nobody saw.
const BRACE = String.fromCharCode(10) + "}";
const plmAt = src.indexOf("function perLoopMs(");
const plm = plmAt < 0 ? "" : src.slice(plmAt, src.indexOf(BRACE, plmAt));
const realPerLoop = (cfg) =>
  new Function("cfgCache", "getPath", plm + "\n}\nreturn perLoopMs();")(cfg, (o, p) => o && o[p]);

check(realPerLoop({ "time per loop": 10000 }) === 10000, "perLoopMs() returns the DEVICE's round length",
  String(realPerLoop({ "time per loop": 10000 })));
check(realPerLoop({}) === 20000, "perLoopMs() falls back to 20 s when the key is missing",
  String(realPerLoop({})));
check(realPerLoop({ "time per loop": 0 }) === 20000, "a zero round length cannot divide by zero downstream",
  String(realPerLoop({ "time per loop": 0 })));

console.log(
  failures ? `\nFAIL - ${failures} check(s)` : "\nok - profile edits in minutes, stores seconds/rounds, clamp holds",
);
process.exit(failures ? 1 : 0);
