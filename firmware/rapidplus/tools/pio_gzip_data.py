"""Regenerate data/*.gz right before the LittleFS image is built.

AsyncWebServer's serveStatic prefers `<file>.gz` when it exists and serves it with
Content-Encoding: gzip, so shipping both means the browser only ever downloads the
small one (index.html 10K->2.5K, script.js 48K->14K, style.css 21K->6K). That matters
here: the ESP32's largest contiguous heap block is what limits how big a response
AsyncTCP can push, and it is measured in tens of KB.

Why this runs automatically instead of a manual `gzip` step: a hand-made .gz goes stale
the moment someone edits the source, and the device then silently serves the OLD UI
while the file on disk looks right. That is a miserable bug to chase. Generating on
every FS build makes staleness impossible.

Hooked from platformio.ini via tools/pio_upload_all.py.
"""
import gzip
import os

Import("env")  # noqa: F821 - injected by PlatformIO/SCons

# Text assets only. Do not touch highcharts.js.gz - it ships pre-made, and re-zipping
# a 634KB file on every build would just burn time.
GZIP_ME = ("index.html", "script.js", "style.css")


def gzip_data_dir(source, target, env):  # noqa: ARG001 - SCons action signature
    data_dir = env.subst("$PROJECT_DATA_DIR")
    if not os.path.isdir(data_dir):
        return
    for name in GZIP_ME:
        src = os.path.join(data_dir, name)
        if not os.path.isfile(src):
            continue
        dst = src + ".gz"
        # Skip if the .gz is already newer than its source.
        if os.path.isfile(dst) and os.path.getmtime(dst) >= os.path.getmtime(src):
            continue
        with open(src, "rb") as f:
            raw = f.read()
        # mtime=0 -> byte-identical output for identical input, so an unchanged file
        # does not churn the FS image.
        with gzip.GzipFile(dst, "wb", compresslevel=9, mtime=0) as f:
            f.write(raw)
        print("gzip: %s  %d -> %d bytes" % (name, len(raw), os.path.getsize(dst)))


# Runs before the littlefs image is assembled from data/.
env.AddPreAction("$BUILD_DIR/littlefs.bin", gzip_data_dir)  # noqa: F821
