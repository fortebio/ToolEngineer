#!/usr/bin/env python3
"""build_face_gifs.py — dựng bộ GIF "mặt robot" cho partition emo_spiffs của board P4.

Nguồn: docs/01_gif/*.gif (32 clip 480×320, nền đen, mắt cyan; docs/02_png/ là khung
PNG gốc, chỉ để xem). Đích: spiffs_face_image/<key>.gif — đúng tên file nguồn, đây là
key mà firmware (main/ui/face.c) tra.

Vì sao phải chuyển: bộ gốc 11 MB (128 khung PNG/clip, GIF 16–64 khung, delay 30–1000
ms), partition emo_spiffs của P4 là 0x560000 (5,375 MB, SPIFFS dùng được ~93 %). Trên
máy GIF được nhân đôi pixel (400×240 → 800×480 = đúng màn logical), nên:
  - canvas 400×240, nội dung co LANCZOS về 360×240 (giữ đúng tỉ lệ 3:2, không cắt —
    bbox nội dung gốc chạm y=26..308/320) rồi đặt giữa, hai lề 20 px đen (LZW nén ≈ 0).
  - một bảng màu chung cho cả clip (Pillow mới không ghi LCT từng khung → nén delta
    được), 16 màu: mắt cyan + glow trên nền đen, đã soi crop phóng 2× không thấy vân.
  - disposal=1 + optimize → Pillow chỉ ghi hộp khác biệt so với khung trước; player
    trên máy (gifdec.c) tô đè lên canvas nên đúng.
  - giữ nhịp từng khung; khung nguồn ngắn hơn --min-delay (40 ms = máy decode kịp,
    = BOARD_FACE_GIF_MIN_FRAME_MS) được GỘP vào khung kế (tổng thời lượng không đổi):
    1766 → 1561 khung, 4,36 → 3,9 MB. Sàn 50 ms = 2,9 MB, 60 ms = 2,5 MB nếu cần.

Chạy (Pillow ≥ 9):
  python tools/build_face_gifs.py                    # dựng + bảng size + kiểm ngân sách
  python tools/build_face_gifs.py --colors 32        # nét hơn, ~3,8 MB — KHÔNG vừa 0x3E0000
  python tools/build_face_gifs.py --preview x.png    # contact sheet khung giữa mỗi clip
"""
import argparse
import io
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_SRC = os.path.join(ROOT, "docs", "01_gif")
DEFAULT_OUT = os.path.join(ROOT, "spiffs_face_image")

# Partition emo_spiffs P4 = 0x560000 (partitions.p4-43lcd.csv); SPIFFS 512K từng cho
# 474641/524288 (90,5 %), lấy 90 % làm trần an toàn.
PARTITION_BYTES = 0x560000
BUDGET_BYTES = int(PARTITION_BYTES * 0.90)
MAX_FILE_BYTES = 460 * 1024        # display.c từ chối file > 460 KB

EXPECTED_KEYS = [
    "boot_up", "connect_fail", "connect_success",
    "emo_angry", "emo_confused", "emo_cry", "emo_happy", "emo_normal", "emo_sad",
    "emo_scared", "emo_shy", "emo_surprised", "emo_wink",
    "idle_bored", "idle_look_around", "idle_normal", "idle_sleepy",
    "listening", "speaking", "thinking",
    "sym_alarm", "sym_celebrate", "sym_countdown", "sym_event", "sym_music",
    "sym_reminder", "sym_timer", "sym_timer_digits",
    "sym_weather_cloud", "sym_weather_rain", "sym_weather_snow", "sym_weather_sun",
]


def load_frames(path, fit, canvas, min_delay):
    """Trả (frames RGB đã đặt lên canvas đen, delays ms).

    min_delay: sàn thời gian một khung trên máy. Khung nguồn ngắn hơn sàn được GỘP:
    bỏ khung, cộng delay của nó vào khung giữ kế tiếp → tổng thời lượng clip giữ
    nguyên, chỉ bớt khung (bớt decode + bớt byte). Máy decode ~12–24 ms/khung 800×480
    nên khung 30 ms không kịp; giữ nguyên thì lvgl_gif kẹp lên sàn và clip chạy chậm.
    """
    im = Image.open(path)
    frames, delays = [], []
    ox = (canvas[0] - fit[0]) // 2
    oy = (canvas[1] - fit[1]) // 2
    n = getattr(im, "n_frames", 1)
    acc = 0
    for i in range(n):
        im.seek(i)
        d = int(im.info.get("duration", 100))
        acc += d
        last = (i == n - 1)
        if acc < min_delay and not last:
            continue                     # gộp vào khung kế
        fr = im.convert("RGB")
        if fr.size != fit:
            fr = fr.resize(fit, Image.LANCZOS)
        c = Image.new("RGB", canvas, (0, 0, 0))
        c.paste(fr, (ox, oy))
        frames.append(c)
        delays.append(max(min_delay, acc))
        acc = 0
    return frames, delays


def encode(frames, delays, colors):
    canvas = frames[0].size
    # Bảng màu chung: quantize một dải dọc ghép mọi khung.
    strip = Image.new("RGB", (canvas[0], canvas[1] * len(frames)))
    for i, fr in enumerate(frames):
        strip.paste(fr, (0, i * canvas[1]))
    pal = strip.quantize(colors=colors, method=Image.Quantize.MEDIANCUT,
                         dither=Image.Dither.NONE)
    q = [fr.quantize(colors=colors, palette=pal, dither=Image.Dither.NONE)
         for fr in frames]
    buf = io.BytesIO()
    q[0].save(buf, format="GIF", save_all=True, append_images=q[1:],
              duration=delays, loop=0, disposal=1, optimize=True)
    return buf.getvalue()


def verify(data, canvas, delays):
    """Mở lại file: đúng cỡ, tổng thời lượng bằng nguồn (Pillow gộp khung trùng
    hoàn toàn và cộng delay — số khung có thể ít hơn, thời lượng thì không đổi)."""
    im = Image.open(io.BytesIO(data))
    assert im.size == canvas, f"size {im.size} != {canvas}"
    n = getattr(im, "n_frames", 1)
    total = 0
    for i in range(n):
        im.seek(i)
        total += int(im.info.get("duration", 0))
    assert total == sum(delays), f"tong delay {total} != {sum(delays)}"
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--canvas", default="400x240", help="kích thước GIF (px)")
    ap.add_argument("--fit", default="360x240", help="nội dung co về (px), đặt giữa canvas")
    ap.add_argument("--colors", type=int, default=16)
    ap.add_argument("--min-delay", type=int, default=40,
                    help="sàn ms/khung; khung nguồn ngắn hơn được gộp (giữ tổng thời lượng)")
    ap.add_argument("--budget-kb", type=int, default=BUDGET_BYTES // 1024)
    ap.add_argument("--preview", default=None, help="ghi contact sheet PNG")
    args = ap.parse_args()

    canvas = tuple(int(v) for v in args.canvas.lower().split("x"))
    fit = tuple(int(v) for v in args.fit.lower().split("x"))
    os.makedirs(args.out, exist_ok=True)

    keys = sorted(f[:-4] for f in os.listdir(args.src) if f.lower().endswith(".gif"))
    missing = [k for k in EXPECTED_KEYS if k not in keys]
    extra = [k for k in keys if k not in EXPECTED_KEYS]
    if missing:
        print(f"THIEU clip nguon: {missing}", file=sys.stderr)
    if extra:
        print(f"clip ngoai bang face.c (van chuyen, firmware khong dung): {extra}")

    total = 0
    rows = []
    previews = []
    for key in keys:
        src = os.path.join(args.src, key + ".gif")
        frames, delays = load_frames(src, fit, canvas, args.min_delay)
        data = encode(frames, delays, args.colors)
        n = verify(data, canvas, delays)
        with open(os.path.join(args.out, key + ".gif"), "wb") as f:
            f.write(data)
        total += len(data)
        rows.append((key, len(data), n, min(delays), max(delays), sum(delays)))
        previews.append((key, frames[len(frames) // 2]))
        if len(data) > MAX_FILE_BYTES:
            print(f"QUA CO: {key} {len(data)} B > {MAX_FILE_BYTES}", file=sys.stderr)

    print(f"{'key':22s} {'KB':>7s} {'fr':>3s} {'delay ms':>11s} {'tong ms':>8s}")
    for key, sz, n, dmin, dmax, dsum in rows:
        print(f"{key:22s} {sz/1024:7.1f} {n:3d} {dmin:5d}-{dmax:<5d} {dsum:8d}")
    nfr = sum(r[2] for r in rows)
    print(f"TONG {total/1024:.0f} KB / ngan sach {args.budget_kb} KB "
          f"(partition {PARTITION_BYTES/1024:.0f} KB) — {len(rows)} file, {nfr} khung, "
          f"{args.colors} mau, canvas {canvas[0]}x{canvas[1]}, san {args.min_delay} ms")

    if args.preview:
        cols = 6
        w, h = canvas[0] // 2, canvas[1] // 2
        rowsn = (len(previews) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * w, rowsn * (h + 16)), "white")
        d = ImageDraw.Draw(sheet)
        for i, (key, fr) in enumerate(previews):
            x, y = (i % cols) * w, (i // cols) * (h + 16)
            sheet.paste(fr.resize((w, h)), (x, y + 16))
            d.text((x + 3, y + 2), key, fill="black")
        sheet.save(args.preview)
        print(f"preview: {args.preview}")

    if total > args.budget_kb * 1024:
        print("FAIL: vuot ngan sach partition", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
