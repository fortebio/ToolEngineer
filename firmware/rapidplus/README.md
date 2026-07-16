# FBT x Dxd Project

ESP32 (PlatformIO / Arduino) firmware for the FBT RAPID LAMP-PCR device.

## Web dashboard (SSE)

A browser dashboard served from the device over Server-Sent Events (SSE).

- Client files: `data/` (`index.html`, `script.js`, `style.css`) — served from
  LittleFS, no build step. Screens: **Home** (temps, status, notifications,
  button states), **Process** (realtime amplification chart), **Setting**.
- Layout spec: `GUI_SSE/GUI.md`.
- SSE contract: two events on `/events` — `home` (home-screen state) and
  `new_readings` (scalar per channel for the chart). See the latest entry in
  `history/` for the exact JSON shapes.

### Test the dashboard without hardware

```bash
python tools/sse_test_server.py        # then open http://localhost:8000
python tools/sse_test_server.py selftest
```

A Python stdlib mock ESP32 that serves the real `data/` files and streams fake
run data (Lysis heating → Amplification → finished), so the UI can be validated
in a browser before flashing.

## Conventions

- **File content is English**; Vietnamese is only for discussion, to avoid
  font/encoding issues on the device display and in editors.
- Every substantive change is logged as a dated markdown file in `history/`.

## Build / flash (PlatformIO)

```bash
pio run -e esp32dev              # build firmware
pio run -e esp32dev -t upload    # flash firmware
pio run -e esp32dev -t uploadfs  # upload data/ to LittleFS
pio test -e esp32dev_test -v     # on-device unit tests
```
