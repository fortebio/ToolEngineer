#!/usr/bin/env python3
"""uicmd.py — gửi lệnh tới dev console (core/dev_console.c) của Rapid4P qua UART.

    python scripts\\uicmd.py COM7 "ui settings"          # chuyển màn
    python scripts\\uicmd.py COM7 "btn do" "ui" --wait 2  # nhiều lệnh, chờ 2 s đọc phản hồi
    python scripts\\uicmd.py auto "help"                  # auto = tìm CH343

Mở cổng với DTR/RTS THẢ (như readlog.py) để KHÔNG reset/giữ chip. In mọi byte nhận được
trong --wait giây (mặc định 1.0) — gồm cả log ESP_LOG chen vào.
Cần pyserial: python -m pip install --user pyserial
"""
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__file__))
from readlog import find_ch343, open_port  # noqa: E402


def main() -> int:
    argv = sys.argv[1:]
    wait = 1.0
    if "--wait" in argv:
        i = argv.index("--wait")
        wait = float(argv[i + 1])
        del argv[i:i + 2]
    args = [a for a in argv if not a.startswith("--")]
    if len(args) < 2:
        print(__doc__)
        return 2
    port = find_ch343() if args[0] == "auto" else args[0]
    if not port:
        print("khong tim thay CH343; chi ro COMxx")
        return 1
    s = open_port(port)
    try:
        s.reset_input_buffer()
        for cmd in args[1:]:
            s.write((cmd + "\r\n").encode())
            s.flush()
            t_end = time.time() + wait
            buf = b""
            while time.time() < t_end:
                chunk = s.read(4096)
                if chunk:
                    buf += chunk
            sys.stdout.write(buf.decode("utf-8", "replace"))
            sys.stdout.flush()
    finally:
        s.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
