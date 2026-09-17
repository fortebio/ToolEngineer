#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-${ESPPORT:-}}"
DURATION="${2:-${SOAK_DURATION:-12h}}"
BAUD="${ESPBAUD:-921600}"
LOG_DIR="$ROOT/logs/soak"
PYTHON_BIN="${PYTHON:-python3}"

HEADLESS="${SOAK_HEADLESS:-0}"
if [[ ! -t 0 ]]; then
  HEADLESS=1
fi

if [[ -z "$PORT" ]]; then
  PORT="$(ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/ttyUSB* 2>/dev/null | head -n1 || true)"
fi

if [[ -z "$PORT" ]]; then
  echo "No serial port found. Pass one explicitly, e.g. scripts/soak_monitor.sh /dev/cu.usbmodemXXXX" >&2
  exit 1
fi

mkdir -p "$LOG_DIR"
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="$LOG_DIR/vimate-soak-$STAMP.log"

cd "$ROOT"
echo "Port: $PORT"
echo "Duration: $DURATION"
echo "Log: $LOG_FILE"
echo "Press Ctrl+] to exit monitor manually."

if [[ "$HEADLESS" == "1" ]]; then
  echo "stdin is not a TTY; using headless raw serial logger."
  "$PYTHON_BIN" "$ROOT/scripts/serial_soak_log.py" \
    --port "$PORT" \
    --baud "$BAUD" \
    --duration "$DURATION" \
    --log "$LOG_FILE" \
    --quiet
  exit $?
fi

if [[ -z "${IDF_PATH:-}" ]]; then
  echo "IDF_PATH is not set. Run: . /path/to/esp-idf/export.sh" >&2
  exit 1
fi

if command -v timeout >/dev/null 2>&1; then
  if timeout --help 2>/dev/null | grep -q -- '--foreground'; then
    timeout --foreground "$DURATION" idf.py -B build-soak -p "$PORT" -b "$BAUD" monitor 2>&1 | tee "$LOG_FILE"
  else
    timeout "$DURATION" idf.py -B build-soak -p "$PORT" -b "$BAUD" monitor 2>&1 | tee "$LOG_FILE"
  fi
else
  echo "timeout command not found; monitor will run until Ctrl+] or Ctrl+C."
  idf.py -B build-soak -p "$PORT" -b "$BAUD" monitor 2>&1 | tee "$LOG_FILE"
fi
