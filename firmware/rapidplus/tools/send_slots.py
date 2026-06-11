#!/usr/bin/env python3
"""
Gui du lieu raw cua tat ca slot vao thiet bi FBT-DxD qua UART chi voi 1 lan chay.

Firmware (start_amplification_simulation trong ForteSetting.cpp) nhan tung slot
mot, dinh dang:  {"Slot":[v0,v1,...]}<slot_index>#
Script nay tu dong gui lan luot tung dong trong file du lieu.

Dinh dang file du lieu (mac dinh slots.txt):
    - Moi dong la 1 slot, cac gia tri cach nhau bang dau phay.
    - Dong 1 -> slot 0, dong 2 -> slot 1, ... toi da 10 dong (slot 0..9).
    - Dong trong / bat dau bang '#' bi bo qua (de ghi chu).

Vi du chay (dong cua so Serial Monitor khac truoc khi chay):
    python tools/send_slots.py COM5 tools/slots_scaled.txt
    python tools/send_slots.py COM5 tools/slots_scaled.txt --gap 2.0

LUU Y: --gap PHAI > 1.0 giay. Firmware doc Serial bang readBytes timeout 1s, neu
gap nho hon thi cac message bi noi lien, dau '#' khong duoc phat hien -> thiet bi
bao 'Long json data continue receiving' roi 'cmd is too long'.

Yeu cau: pip install pyserial
"""
import argparse
import os
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("Thieu pyserial. Cai bang:  pip install pyserial")

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def parse_args():
    p = argparse.ArgumentParser(description="Gui du lieu raw nhieu slot vao FBT-DxD qua UART")
    p.add_argument("port", help="Cong COM cua thiet bi, vd COM5")
    p.add_argument("datafile", nargs="?", default=None,
                   help="File du lieu, moi dong = 1 slot (mac dinh: slots.txt nam canh script)")
    p.add_argument("--baud", type=int, default=115200, help="Baud rate (mac dinh 115200)")
    p.add_argument("--loops", type=int, default=120,
                   help="So gia tri can cho moi slot = amplification_time tren thiet bi (mac dinh 120)")
    p.add_argument("--gap", type=float, default=2.0,
                   help="Thoi gian cho giua 2 slot, giay. PHAI > 1.0 vi firmware doc Serial bang "
                        "readBytes timeout 1s; gap nho hon se lam cac message bi noi lien -> loi "
                        "'cmd is too long'. Mac dinh 2.0.")
    return p.parse_args()


def load_slots(path):
    slots = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip().rstrip(",")
            if not line or line.startswith("#"):
                continue
            vals = [v.strip() for v in line.split(",") if v.strip() != ""]
            slots.append(vals)
    return slots


def open_serial(port, baud):
    # Tat DTR/RTS de han che ESP32 tu reset khi mo cong
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    time.sleep(0.3)
    ser.reset_input_buffer()
    return ser


def resolve_datafile(datafile):
    """Tim file du lieu: mac dinh la slots.txt canh script;
    neu duong dan tuong doi khong co o thu muc hien tai thi thu canh script."""
    if datafile is None:
        return os.path.join(SCRIPT_DIR, "slots.txt")
    if os.path.isfile(datafile):
        return datafile
    alt = os.path.join(SCRIPT_DIR, os.path.basename(datafile))
    if os.path.isfile(alt):
        return alt
    sys.exit(f"Khong tim thay file du lieu: '{datafile}'\n"
             f"Da thu them: '{alt}'")


def main():
    a = parse_args()
    datafile = resolve_datafile(a.datafile)
    slots = load_slots(datafile)
    if not slots:
        sys.exit(f"File '{datafile}' khong co du lieu hop le.")
    print(f"File du lieu: {datafile}")
    if len(slots) > 10:
        sys.exit(f"File co {len(slots)} dong nhung toi da 10 slot.")

    ser = open_serial(a.port, a.baud)
    print(f"Da mo {a.port} @ {a.baud}. Gui {len(slots)} slot...\n")

    for idx, vals in enumerate(slots):
        if len(vals) < a.loops:
            print(f"[!] Slot {idx}: chi co {len(vals)} gia tri (< loops={a.loops}); "
                  f"cac vi tri thieu se duoc thiet bi doc thanh 0.")
        payload = '{"Slot":[' + ",".join(vals) + ']}' + str(idx) + '#'
        if len(payload) > 2000:
            print(f"[!] Slot {idx}: payload {len(payload)} bytes, gan/qua buffer 2KB cua firmware. "
                  f"Giam so gia tri moi dong neu thiet bi bao loi 'cmd is too long'.")
        ser.write(payload.encode("ascii"))
        ser.flush()
        print(f"[>] Slot {idx}: gui {len(vals)} gia tri ({len(payload)} bytes)")

        # Doc phan hoi cua thiet bi trong khoang gap
        deadline = time.time() + a.gap
        chunks = []
        while time.time() < deadline:
            data = ser.read(ser.in_waiting or 1)
            if data:
                chunks.append(data)
        resp = b"".join(chunks).decode("utf-8", "replace").strip()
        if resp:
            print("    " + resp.replace("\n", "\n    "))
        print()

    ser.close()
    print("Xong. Du lieu da nam trong EEPROM cua thiet bi.")
    print("Gui tiep lenh 'getResult' (qua Serial Monitor) de tinh & xem ket qua 10 slot.")


if __name__ == "__main__":
    main()
