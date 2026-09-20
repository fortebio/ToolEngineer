"""uishot.py — gõ lệnh dev console qua USB-JTAG rồi chụp LCD bằng webcam (OpenCV): một vòng "đổi màn → chụp".

    python scripts/uishot.py COM20 2 out_dir "ui start" "btn white hold" "btn red"
        → mỗi lệnh: gửi, chờ 1,2 s, chụp 1 khung 1080p từ camera index 2 (DSHOW), xoay 180°, tự cắt vùng LCD
          (mask màu xanh navy/teal của theme), phóng ~960 px ngang → out_dir/NN_<lệnh>.jpg (+ _full.jpg)
    Tuỳ chọn: --rot 0|180 (mặc định 180: UGREEN đặt ngược), --res WxH (mặc định 1920x1080; 4K chậm hơn),
              --wait s (mặc định 1.2), --no-cmd (chỉ chụp một ảnh tên shot.jpg)

Camera: `python scripts/webcam_shot.py --list` để biết index (box ADM 2026-09-21: DSHOW 2 = UGREEN FineCam 4K CM973,
DSHOW 1 = webcam laptop). Cổng: mở KHÔNG DTR/RTS (như keytest.py) — kéo DTR/RTS làm chip USB-JTAG câm.
Lệnh `btn …` đánh thức màn (dev_console gọi display_note_user_activity) — màn ngủ sau CONFIG_RAPID4P_SCREEN_SLEEP_SEC.
"""
import os
import re
import sys
import time

import cv2
import numpy as np
import serial

ANSI = re.compile(rb"\x1b\[[0-9;]*m")


def open_port(port):
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.05
    s.dtr = False
    s.rts = False
    s.open()
    return s


def drain(s, seconds):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        buf += s.read(4096)
    txt = ANSI.sub(b"", buf).decode("utf-8", "replace")
    for line in txt.splitlines():
        if "r4p." in line or line.startswith("btn:") or line.startswith("ui:"):
            print("   |", line.strip())


def open_cam(idx, w, h):
    cap = cv2.VideoCapture(idx, cv2.CAP_DSHOW)
    if not cap.isOpened():
        sys.exit("khong mo duoc camera %d" % idx)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, w)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, h)
    cap.set(cv2.CAP_PROP_AUTOFOCUS, 1)
    t0 = time.time()
    while time.time() - t0 < 3.0:       # warm-up exposure/focus một lần cho cả phiên
        cap.read()
    return cap


def grab(cap):
    frame = None
    for _ in range(6):                  # bỏ khung đệm cũ, lấy khung mới nhất
        ok, f = cap.read()
        if ok:
            frame = f
    return frame


def crop_lcd(frame):
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    mask = cv2.inRange(hsv, (80, 40, 40), (130, 255, 255))
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((25, 25), np.uint8))
    cnts, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not cnts:
        return frame
    x, y, w, h = cv2.boundingRect(max(cnts, key=cv2.contourArea))
    if w < frame.shape[1] // 8:         # quá nhỏ = không phải LCD (màn tắt?)
        return frame
    pad = 12
    x0, y0 = max(0, x - pad), max(0, y - pad)
    x1, y1 = min(frame.shape[1], x + w + pad), min(frame.shape[0], y + h + pad)
    crop = frame[y0:y1, x0:x1]
    scale = max(1.0, 960.0 / crop.shape[1])
    return cv2.resize(crop, None, fx=scale, fy=scale, interpolation=cv2.INTER_CUBIC)


def main():
    args = sys.argv[1:]
    rot, res, wait, no_cmd = 180, (1920, 1080), 1.2, False
    while args and args[0].startswith("--"):
        k = args.pop(0)
        if k == "--rot":
            rot = int(args.pop(0))
        elif k == "--res":
            w, h = args.pop(0).lower().split("x")
            res = (int(w), int(h))
        elif k == "--wait":
            wait = float(args.pop(0))
        elif k == "--no-cmd":
            no_cmd = True
    if len(args) < 3:
        print(__doc__)
        return 2
    port, cam_idx, out_dir = args[0], int(args[1]), args[2]
    cmds = args[3:]
    os.makedirs(out_dir, exist_ok=True)
    cap = open_cam(cam_idx, *res)
    ser = None if no_cmd else open_port(port)
    if ser:
        drain(ser, 0.3)
    steps = [None] if no_cmd else cmds
    for i, cmd in enumerate(steps):
        if cmd is not None:
            print(">>>", cmd)
            ser.write((cmd + "\r\n").encode())
            ser.flush()
            drain(ser, wait)
        frame = grab(cap)
        if frame is None:
            print("   khong co khung")
            continue
        if rot == 180:
            frame = cv2.rotate(frame, cv2.ROTATE_180)
        name = "shot" if cmd is None else "%02d_%s" % (i, re.sub(r"[^a-z0-9]+", "_", cmd.lower()).strip("_"))
        cv2.imwrite(os.path.join(out_dir, name + "_full.jpg"), frame)
        crop = crop_lcd(frame)
        p = os.path.join(out_dir, name + ".jpg")
        cv2.imwrite(p, crop)
        print("   ->", p, "%dx%d" % (crop.shape[1], crop.shape[0]))
    cap.release()
    if ser:
        ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
