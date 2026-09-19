"""jtaglog.py — đọc log boot qua cổng USB-Serial/JTAG (VID 303A PID 1001) của ESP32-S3/P4.

    python scripts/jtaglog.py COM20 40 boot.log            # reset bằng esptool rồi đọc 40 s
    python scripts/jtaglog.py COM20 60 boot.log --no-reset # chỉ đọc (máy đang chạy)

Vì sao không dùng readlog.py: cổng USB-JTAG nằm TRONG chip nên khi chip reset thì cổng COM biến
mất rồi hiện lại (Windows mất ~1 s để enumerate) — xung RTS của readlog.py qua cổng này KHÔNG reset
được chip (đo 2026-09-19: uptime vẫn tăng), còn reset bằng esptool thì mất ~1 s log đầu (bootloader
+ vài dòng app_init). Script này: gọi `esptool --after hard_reset read_mac` rồi mở lại cổng liên tục
(20 ms) tới khi có, đọc N giây ra file (byte thô, giữ mã màu). Với bo S3 2.8" ES3N28P cắm cổng USB
native: bắt được từ `octal_psram` trở đi (đủ mọi dòng r4p.*). Muốn đủ cả bootloader → cắm cổng UART0
(43/44) và dùng readlog.py.
"""
import subprocess
import sys
import time

import serial


def open_port(port):
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = False
    s.rts = False
    s.open()
    return s


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        sys.exit(__doc__)
    port = args[0]
    secs = float(args[1])
    out = args[2] if len(args) > 2 else None
    if "--no-reset" not in sys.argv:
        # esptool tự reset chip khi xong (hard_reset qua USB-JTAG hoạt động, RTS thô thì không).
        subprocess.run([sys.executable, "-m", "esptool", "--port", port, "--after", "hard_reset",
                        "read_mac"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    data = bytearray()
    s = None
    reopen = 0
    while time.time() - t0 < secs:
        if s is None:
            try:
                s = open_port(port)
                reopen += 1
            except Exception:
                time.sleep(0.02)
                continue
        try:
            chunk = s.read(4096)
            if chunk:
                data += chunk
        except Exception:
            try:
                s.close()
            except Exception:
                pass
            s = None
            time.sleep(0.02)
    if out:
        open(out, "wb").write(bytes(data))
        print("%s: %d byte, mo cong %d lan" % (out, len(data), reopen))
    else:
        sys.stdout.buffer.write(bytes(data))


if __name__ == "__main__":
    main()
