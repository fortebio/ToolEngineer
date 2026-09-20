"""webcam_shot.py — chụp màn LCD của máy bằng webcam (OpenCV, không cần ffmpeg).

    python scripts/webcam_shot.py --list                  # thử index 0..5 (DirectShow), in độ phân giải
    python scripts/webcam_shot.py 1 shot.jpg              # chụp 1 khung từ camera index 1 (đã warm-up)
    python scripts/webcam_shot.py 1 shot.jpg --crop x,y,w,h --scale 2   # cắt vùng màn rồi phóng to

Vì sao: box ADM không có ffmpeg; trình duyệt tích hợp của Claude chặn camera (NotAllowedError).
OpenCV cài trong venv IDF (`C:/Espressif/python_env/idf5.5_py3.11_env`, `pip install opencv-python`,
2026-09-20). Camera USB (UGREEN) thường KHÔNG hiện tên riêng trong PnP — dùng --list rồi xem ảnh để
biết index nào là camera nào. Warm-up ~1,2 s để auto-exposure ổn định (khung đầu tối/nhoè).
"""
import sys
import time

import cv2


def open_cam(idx, w=1280, h=720):
    cap = cv2.VideoCapture(idx, cv2.CAP_DSHOW)
    if not cap.isOpened():
        return None
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, w)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, h)
    return cap


def grab(cap, warm_s=1.2):
    t0 = time.time()
    frame = None
    while time.time() - t0 < warm_s:
        ok, f = cap.read()
        if ok:
            frame = f
    for _ in range(3):          # vài khung nữa sau warm-up, lấy khung cuối
        ok, f = cap.read()
        if ok:
            frame = f
    return frame


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = sys.argv[1:]
    if "--list" in opts:
        for i in range(6):
            cap = open_cam(i)
            if cap is None:
                print("%d: (khong mo duoc)" % i)
                continue
            f = grab(cap, 0.6)
            cap.release()
            if f is None:
                print("%d: mo duoc nhung khong co khung" % i)
            else:
                out = "cam%d.jpg" % i
                cv2.imwrite(out, f)
                print("%d: %dx%d -> %s" % (i, f.shape[1], f.shape[0], out))
        return
    if len(args) < 2:
        sys.exit(__doc__)
    idx, out = int(args[0]), args[1]
    cap = open_cam(idx)
    if cap is None:
        sys.exit("khong mo duoc camera %d" % idx)
    f = grab(cap)
    cap.release()
    if f is None:
        sys.exit("camera %d khong tra khung" % idx)
    if "--crop" in opts:
        x, y, w, h = [int(v) for v in opts[opts.index("--crop") + 1].split(",")]
        f = f[y:y + h, x:x + w]
    if "--scale" in opts:
        s = float(opts[opts.index("--scale") + 1])
        f = cv2.resize(f, None, fx=s, fy=s, interpolation=cv2.INTER_CUBIC)
    cv2.imwrite(out, f)
    print("%s %dx%d" % (out, f.shape[1], f.shape[0]))


if __name__ == "__main__":
    main()
