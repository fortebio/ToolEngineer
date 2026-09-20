"""keytest.py — gõ lệnh dev console qua cổng USB-Serial/JTAG (S3 2.8": console + log cùng cổng) và
in log trả về. Dùng để chạy kịch bản nút cơ mà không cần nối nút (btn green|red|white [hold|rep]).

    python scripts/keytest.py COM20 "btn green" "btn red" "btn white hold"      # mỗi lệnh chờ 0,8 s
    python scripts/keytest.py COM20 --wait 3 "btn red"                          # chờ 3 s sau mỗi lệnh
    python scripts/keytest.py COM20 --reset                                      # reset (esptool) rồi đọc 20 s

Chỉ dùng được khi board build với CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y (sdkconfig.defaults.s3_28lcd);
P4 (console UART0) dùng scripts/uicmd.py qua cổng UART0. Log in ra đã bỏ mã màu ANSI.
"""
import re
import subprocess
import sys
import time

import serial

ANSI = re.compile(rb"\x1b\[[0-9;]*m")


def open_port(port, retries=200):
    for _ in range(retries):
        try:
            s = serial.Serial()
            s.port = port
            s.baudrate = 115200
            s.timeout = 0.05
            # PHẢI tắt DTR/RTS TRƯỚC khi mở: pyserial mặc định kéo cả hai → mạch auto-reset của
            # USB-JTAG hiểu là "vào download mode" → console im bặt (gặp 2026-09-20). Như jtaglog.py.
            s.dtr = False
            s.rts = False
            s.open()
            return s
        except serial.SerialException:
            time.sleep(0.05)
    raise SystemExit("khong mo duoc " + port)


def drain(s, seconds):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        chunk = s.read(4096)
        if chunk:
            buf += chunk
    txt = ANSI.sub(b"", buf).decode("utf-8", "replace")
    for line in txt.splitlines():
        line = line.strip("\r")
        if line.strip():
            print("   |", line)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    port = sys.argv[1]
    args = sys.argv[2:]
    wait = 0.8
    if args and args[0] == "--reset":
        subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
                        "--before", "default_reset", "--after", "hard_reset", "read_mac"],
                       check=False, capture_output=True)
        s = open_port(port)
        drain(s, float(args[1]) if len(args) > 1 else 20)
        return 0
    if args and args[0] == "--wait":
        wait = float(args[1])
        args = args[2:]
    s = open_port(port)
    drain(s, 0.3)
    for cmd in args:
        print(">>>", cmd)
        s.write((cmd + "\r\n").encode())
        s.flush()
        drain(s, wait)
    return 0


if __name__ == "__main__":
    sys.exit(main())
