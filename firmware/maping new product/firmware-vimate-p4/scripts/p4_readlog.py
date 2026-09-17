#!/usr/bin/env python3
"""Đọc log UART của board P4 qua cầu CH343 (115200 8N1) mà không cần `idf.py monitor`.

    python scripts/p4_readlog.py --list                   # liệt kê cổng, đánh dấu cổng CH343 của board
    python scripts/p4_readlog.py auto 60                  # tự tìm CH343, reset board rồi đọc 60 s ra stdout
    python scripts/p4_readlog.py COM48 90 boot.log        # cổng cụ thể, ghi file (bytes thô, giữ mã màu)
    python scripts/p4_readlog.py auto 120 live.log --no-reset   # không reset, bắt tiếp phiên đang chạy

Mặc định reset board bằng xung RTS (EN) y như esptool. `--no-reset` dùng khi cần xem
trạng thái hiện tại (ví dụ sau khi nói "Hi Lily") — cổng được mở với DTR/RTS thả nên
chip chạy tiếp; mở cổng bằng pyserial mặc định (DTR/RTS kéo) là chip ĐỨNG IM, màn
đen (xem open_port).

Cổng COM đổi theo lần cắm / cổng USB (COM47 ↔ COM48 trong hai ngày 12–13/09/2026):
`auto` tìm thiết bị "USB-Enhanced-SERIAL CH343" (VID 1A86, PID 55D3). Các COM3/4/5/10/
11/22/23 trên máy này là Bluetooth/SOL ảo, không phải board.

Sau khi đọc xong, kiểm nhanh (skill vimate-p4-verify):
    grep -a -c "task_wdt\\|Guru Meditation\\|Ringbuffer FEED full" boot.log   # phải = 0
    grep -a "diag heap" boot.log | tail -3
"""
import sys
import time

try:
    import serial  # pyserial (có sẵn trong Python của ESP-IDF Tools)
except ImportError:
    sys.exit("thieu pyserial: pip install pyserial")


CH343_VID_PID = (0x1A86, 0x55D3)


def find_ch343():
    """Cổng đầu tiên là cầu CH343 của board, hoặc None."""
    from serial.tools import list_ports
    for p in list_ports.comports():
        if (p.vid, p.pid) == CH343_VID_PID:
            return p.device
    return None


def list_ports_pretty():
    from serial.tools import list_ports
    for p in sorted(list_ports.comports(), key=lambda x: x.device):
        tag = "  <- board (CH343)" if (p.vid, p.pid) == CH343_VID_PID else ""
        print("%-6s %s%s" % (p.device, p.description, tag))


def open_port(port):
    """Mở cổng với DTR/RTS ĐỀU THẢ (False) TRƯỚC khi open.

    pyserial mặc định kéo cả DTR lẫn RTS lên khi open; qua mạch auto-reset của CH343
    trên board này thì trạng thái đó GIỮ CHIP IM (màn đen, UART câm, không boot) cho
    tới khi thả lại — đã đo 13/09/2026: open mặc định → 0 byte; open dtr=rts=False →
    chip chạy tiếp (không reset) hoặc boot lại nếu đang bị giữ. Đây là lý do mọi bản
    `--no-reset` trước đó bắt được 0 byte và người dùng thấy "màn hình đen"."""
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
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    if "--list" in flags:
        list_ports_pretty()
        return
    if not args:
        sys.exit(__doc__)
    port = args[0]
    if port.lower() == "auto":
        port = find_ch343()
        if not port:
            sys.exit("khong thay cong CH343 (VID 1A86 PID 55D3) - board chua cam? xem --list")
        print("cong CH343: %s" % port, file=sys.stderr)
    secs = float(args[1]) if len(args) > 1 else 20.0
    out = args[2] if len(args) > 2 else None

    s = open_port(port)
    if "--no-reset" not in flags:
        # Reset như esptool hard-reset: DTR thả, RTS kéo (EN xuống) 150 ms rồi thả.
        s.setRTS(True)
        time.sleep(0.15)
        s.setRTS(False)

    # Ghi file NGAY khi có dữ liệu (không gom tới cuối): cáp USB lỏng / cổng đổi số
    # giữa chừng làm read() ném "GetOverlappedResult failed (Access is denied)" — đã
    # mất trọn 4 phút log một lần (13/09). Gặp lỗi cổng thì thử mở lại tối đa 20 s.
    t0 = time.time()
    buf = bytearray()
    f = open(out, "wb") if out else None
    def sink(chunk):
        buf.extend(chunk)
        if f:
            f.write(chunk); f.flush()
    try:
        while time.time() - t0 < secs:
            try:
                chunk = s.read(4096)
            except serial.SerialException as e:
                print("cong %s loi: %s - thu mo lai" % (port, e), file=sys.stderr)
                try: s.close()
                except Exception: pass
                s = None
                for _ in range(40):
                    time.sleep(0.5)
                    try:
                        if args[0].lower() == "auto":
                            port = find_ch343() or port
                        s = open_port(port)
                        print("da mo lai %s" % port, file=sys.stderr)
                        break
                    except serial.SerialException:
                        s = None
                if not s:
                    print("khong mo lai duoc cong - dung", file=sys.stderr)
                    break
                continue
            if chunk:
                sink(chunk)
    except KeyboardInterrupt:
        pass
    finally:
        if s:
            s.close()
        if f:
            f.close()

    if out:
        print("%s: %d byte, %d dong" % (out, len(buf), buf.count(b"\n")))
    else:
        sys.stdout.buffer.write(buf)


if __name__ == "__main__":
    main()
