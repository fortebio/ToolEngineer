#!/usr/bin/env python3
import argparse
import os
import signal
import sys
import time

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required; run after sourcing ESP-IDF export.sh") from exc


stop = False


def _handle_signal(_signum, _frame):
    global stop
    stop = True


def parse_duration(value: str) -> float:
    raw = value.strip().lower()
    if not raw:
        return 0
    units = {"s": 1, "m": 60, "h": 3600}
    if raw[-1] in units:
        return float(raw[:-1]) * units[raw[-1]]
    return float(raw)


def main() -> int:
    parser = argparse.ArgumentParser(description="Headless serial logger for soak testing")
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--duration", default="12h")
    parser.add_argument("--log", required=True)
    parser.add_argument("--quiet", action="store_true", help="Do not mirror serial bytes to stdout")
    args = parser.parse_args()

    duration = parse_duration(args.duration)
    deadline = time.monotonic() + duration if duration > 0 else None

    os.makedirs(os.path.dirname(args.log), exist_ok=True)
    signal.signal(signal.SIGINT, _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    print(f"Raw serial log: {args.log}", flush=True)
    print(f"Port: {args.port} baud={args.baud} duration={args.duration}", flush=True)
    with open(args.log, "ab", buffering=0) as out:
        while not stop:
            if deadline is not None and time.monotonic() >= deadline:
                break
            try:
                with serial.Serial(args.port, args.baud, timeout=1, rtscts=False, dsrdtr=False) as ser:
                    ser.dtr = False
                    ser.rts = False
                    while not stop:
                        if deadline is not None and time.monotonic() >= deadline:
                            break
                        try:
                            data = ser.read(4096)
                        except serial.SerialException as exc:
                            print(f"\nserial read failed; retrying: {exc}", flush=True)
                            break
                        if not data:
                            continue
                        out.write(data)
                        if not args.quiet:
                            sys.stdout.buffer.write(data)
                            sys.stdout.buffer.flush()
            except serial.SerialException as exc:
                print(f"serial open failed; retrying: {exc}", flush=True)
            if not stop and (deadline is None or time.monotonic() < deadline):
                time.sleep(2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
