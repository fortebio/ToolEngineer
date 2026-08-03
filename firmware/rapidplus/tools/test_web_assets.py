#!/usr/bin/env python3
"""
Guard the UI-in-firmware invariant (Pha 1 of docs/plan/2026-07-28-ota-fleet-upgrade-243.md).

serveStatic is GONE. Nothing serves data/ any more, so there is no longer a safety net that
quietly picks up a newly added file: an asset that is referenced by the UI but missing from
the embed list AND the route table is a hard 404 on a device in the field - and the symptom
(a page that half-loads) shows up only after flashing.

Four checks, host-side, no hardware:

1. Every local asset referenced by data/index.html (href=/src=) is embedded by
   tools/pio_gzip_data.py AND registered in kWebAssets[] in src/webDashboard.cpp.
2. src/webDashboard.cpp no longer calls serveStatic (old filesystem content must stay
   unreachable; that is the whole point of embedding).
3. beginResponse() for an asset is never given a template processor - the template filler
   scans the body for '%' and would corrupt gzip bytes in place.
4. If src/webAssets.h has been generated, its arrays match data/ byte for byte and its
   ETag is the hash the generator would produce. (Skipped when the header is absent - it is
   generated at build time and gitignored.)

    python tools/test_web_assets.py     # exit 0 = pass
"""
import gzip
import hashlib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "data"
SRC = ROOT / "src"
fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)


def uncommented(text, strip_strings=False):
    """Drop // and /* */ so a route or a call named in prose cannot satisfy a check -
    and so the comments that explain what serveStatic used to do don't trip check 2.
    strip_strings also blanks "..." literals, so a log message that merely NAMES an API is
    not mistaken for a call to it."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = "\n".join(re.sub(r"//.*", "", ln) for ln in text.splitlines())
    if strip_strings:
        text = re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', text)
    return text


gen = (ROOT / "tools" / "pio_gzip_data.py").read_text(encoding="utf-8", errors="replace")
dash = uncommented((SRC / "webDashboard.cpp").read_text(encoding="utf-8", errors="replace"))

# The generator's EMBED table: [(filename, C identifier)]
embed = re.findall(r'\("([^"]+)",\s*"(WEB_ASSET_\w+)"\)', gen)
check(len(embed) >= 5, "pio_gzip_data.py: EMBED table not found (or shrank unexpectedly)")

# The firmware's route table: {"/path", IDENT, sizeof(IDENT), "mime", gz}
routes = re.findall(r'\{"(/[^"]*)",\s*(WEB_ASSET_\w+),\s*sizeof\((WEB_ASSET_\w+)\)', dash)
check(bool(routes), "webDashboard.cpp: kWebAssets[] table not found")
for path, ident, sz in routes:
    check(ident == sz, f"webDashboard.cpp: route {path} sends {ident} but sizes it with sizeof({sz})")
route_paths = {p for p, _, _ in routes}
check("/" in route_paths, "webDashboard.cpp: no route for '/' - serveStatic's setDefaultFile is gone")

# 1. every asset the page asks for is embedded and routed
html = (DATA / "index.html").read_text(encoding="utf-8", errors="replace")
refs = set(re.findall(r'(?:href|src)="([^"]+)"', html))
for ref in sorted(refs):
    if ref.startswith(("http://", "https://", "//", "data:", "#")):
        continue
    name = ref.lstrip("./")
    embedded = any(f == name or f == name + ".gz" for f, _ in embed)
    check(embedded, f"data/index.html references {ref!r}, which pio_gzip_data.py does not embed")
    check("/" + name in route_paths, f"data/index.html references {ref!r}, which has no route in kWebAssets[]")

# 2/3. the filesystem path is really gone, and no template processor near the assets
check("serveStatic" not in dash, "webDashboard.cpp: serveStatic is back - old LittleFS content is reachable again")
m = re.search(r"beginResponse\(200, a\.mime, a\.data, a\.len([^)]*)\)", dash)
check(m is not None, "webDashboard.cpp: the embedded-asset beginResponse() call was not found")
if m:
    check(m.group(1).strip() == "",
          "webDashboard.cpp: beginResponse() for an asset got a 4th argument - a template "
          "processor would rewrite '%' inside the gzip bytes")

# 4. generated header still matches data/ (only if it has been generated)
header = SRC / "webAssets.h"
if header.is_file():
    text = header.read_text(encoding="utf-8", errors="replace")
    h = hashlib.sha1()
    for fname, ident in embed:
        blob = (DATA / fname).read_bytes()
        h.update(fname.encode())
        h.update(blob)
        am = re.search(r"static const uint8_t %s\[(\d+)\] = \{(.*?)\};" % ident, text, re.S)
        if not am:
            fails.append(f"src/webAssets.h: array {ident} missing (stale header? re-run the build)")
            continue
        got = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", am.group(2)))
        check(int(am.group(1)) == len(blob) and got == blob,
              f"src/webAssets.h: {ident} does not match data/{fname} ({len(got)} vs {len(blob)} bytes)")
    etag = re.search(r'#define WEB_ASSETS_ETAG "(\w+)"', text)
    check(etag is not None, "src/webAssets.h: WEB_ASSETS_ETAG missing")
    if etag:
        check(etag.group(1) == h.hexdigest()[:16],
              "src/webAssets.h: ETag does not match the current data/ contents - browsers would "
              "keep serving a cached old UI after an update")
else:
    print("note: src/webAssets.h not generated yet (build once) - check 4 skipped")

# 6. the firmware mounts no filesystem at all. The UI is in flash, the slot labels are in
# NVS: with both moved, the state of the spiffs partition on a unit in the field - empty,
# never formatted, or holding a 2020 demo image - stops mattering. A LittleFS.begin() creeping
# back would quietly make it matter again, and LittleFS.begin(true) would format 1.5 MB with
# core-0 WDT disabled while ControlTask drives the heater.
for f in sorted(SRC.rglob("*.cpp")) + sorted(SRC.rglob("*.h")):
    if f.name == "webAssets.h":
        continue  # generated, 786 KB of hex
    code = uncommented(f.read_text(encoding="utf-8", errors="replace"), strip_strings=True)
    for i, line in enumerate(code.splitlines(), 1):
        if re.search(r"\bLittleFS\s*\.", line) or re.search(r'#\s*include\s*[<"]LittleFS\.h', line):
            fails.append(f"{f.relative_to(ROOT)}:{i}: LittleFS is back - the firmware must not "
                         f"depend on the spiffs partition (labels live in NVS, the UI in flash)")

# 5. the embedded .gz really is the current source. The generator only re-zips when the
# .gz mtime is older than the source, and mtimes are not content: a checkout, a copy or a
# touch can leave a stale .gz looking fresh. That used to mean "device serves the old UI";
# now it means "the old UI is baked into firmware.bin".
# Only the files the generator actually derives (GZIP_ME). highcharts.js.gz ships pre-made
# and is the MINIFIED build, while data/highcharts.js is the pretty-printed copy of the same
# v11.4.8 - they are not meant to match, and serveStatic served the .gz too, so nothing about
# what the browser receives changed there.
gzip_me = re.search(r"GZIP_ME = \(([^)]*)\)", gen)
check(gzip_me is not None, "pio_gzip_data.py: GZIP_ME tuple not found")
for name in re.findall(r'"([^"]+)"', gzip_me.group(1) if gzip_me else ""):
    gz, src = DATA / (name + ".gz"), DATA / name
    if not (gz.is_file() and src.is_file()):
        continue
    try:
        same = gzip.decompress(gz.read_bytes()) == src.read_bytes()
    except OSError:
        same = False
    check(same, f"data/{name}.gz is stale: it does not decompress to data/{name} "
                f"(delete the .gz and rebuild)")

for f in fails:
    print("FAIL " + f)
print(("FAILED (%d)" % len(fails)) if fails else "ok - UI assets embedded, routed and in sync")
sys.exit(1 if fails else 0)
