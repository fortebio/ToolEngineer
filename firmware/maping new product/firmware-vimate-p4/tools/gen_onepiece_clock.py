#!/usr/bin/env python3
"""Sinh nền đồng hồ phong cách One Piece - Luffy cho màn 480x320 (3.5" ngang).
KHUNG GIẤY DA ở GIỮA để trống → firmware vẽ giờ ĐỎ to + ngày live đè lên.
Bản gần đúng bằng Pillow (bãi biển + nón rơm + Jolly Roger + thuyền + biển hiệu)."""
import math
from PIL import Image, ImageDraw, ImageFont

S = 2
W, H = 480 * S, 320 * S
img = Image.new("RGB", (W, H))
d = ImageDraw.Draw(img)


def P(v):
    return int(round(v * S))


def font(sz, bold=True):
    paths = [
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf" if bold else "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    ]
    for p in paths:
        try:
            return ImageFont.truetype(p, sz)
        except Exception:
            pass
    return ImageFont.load_default()


def ctext(draw, cx, y, s, fnt, fill, anchor="mm"):
    draw.text((cx, y), s, font=fnt, fill=fill, anchor=anchor)


# ---- Nền: trời → biển → cát ----
for y in range(H):
    t = y / H
    if t < 0.58:  # trời
        k = t / 0.58
        c = (int(135 + 90 * k), int(206 + 35 * k), int(235 + 18 * k))
    elif t < 0.72:  # biển
        c = (52, 140, 200)
    else:  # cát
        k = (t - 0.72) / 0.28
        c = (int(244 - 10 * k), int(216 - 12 * k), int(160 - 18 * k))
    d.line([(0, y), (W, y)], fill=c)

# sóng nhẹ
for i in range(6):
    yy = P(196 + i * 3)
    d.line([(0, yy + (i % 2) * P(2)), (W, yy)], fill=(120, 180, 225), width=S)

# ---- Mặt trời cười (góc trên-trái) ----
sx, sy, sr = P(46), P(46), P(30)
for k in range(12):
    a = k * math.pi / 6
    d.line([(sx + sr * math.cos(a), sy + sr * math.sin(a)),
            (sx + (sr + P(12)) * math.cos(a), sy + (sr + P(12)) * math.sin(a))],
           fill=(255, 200, 40), width=P(3))
d.ellipse([sx - sr, sy - sr, sx + sr, sy + sr], fill=(255, 213, 64), outline=(230, 170, 20), width=P(2))
d.ellipse([sx - P(11), sy - P(8), sx - P(4), sy - P(1)], fill=(70, 50, 20))
d.ellipse([sx + P(4), sy - P(8), sx + P(11), sy - P(1)], fill=(70, 50, 20))
d.arc([sx - P(13), sy - P(6), sx + P(13), sy + P(16)], 20, 160, fill=(70, 50, 20), width=P(3))

# ---- Mây ----
for cxp, cyp, sc in [(P(150), P(40), 1.0), (P(360), P(36), 1.2), (P(430), P(80), 0.8)]:
    for dx, dy, r in [(-18, 0, 16), (0, -6, 20), (18, 0, 16), (0, 6, 14)]:
        d.ellipse([cxp + P(dx) - P(r), cyp + P(dy) - P(r), cxp + P(dx) + P(r), cyp + P(dy) + P(r)],
                  fill=(255, 255, 255))

# ---- Confetti ----
conf = [(P(110), P(70), (231, 76, 60)), (P(300), P(28), (46, 204, 113)),
        (P(250), P(60), (241, 196, 15)), (P(400), P(120), (52, 152, 219)),
        (P(80), P(120), (155, 89, 182)), (P(360), P(150), (231, 76, 60))]
for x, y, c in conf:
    d.rectangle([x, y, x + P(8), y + P(5)], fill=c)

# ---- Cây dừa (phải) ----
tx, ty = P(442), P(150)
d.line([(tx, ty), (tx - P(6), P(250))], fill=(120, 80, 40), width=P(7))
for a in (-50, -20, 15, 50, 90):
    ar = math.radians(a)
    d.line([(tx, ty), (tx + P(46) * math.cos(ar), ty - P(30) + P(40) * math.sin(ar))],
           fill=(39, 174, 96), width=P(6))

# ---- Thuyền Going Merry (dưới-trái, trên biển) ----
bx, by = P(70), P(232)
d.polygon([(bx - P(46), by), (bx + P(46), by), (bx + P(34), by + P(26)), (bx - P(34), by + P(26))],
          fill=(196, 120, 60), outline=(120, 70, 30))
d.line([(bx, by - P(56)), (bx, by)], fill=(120, 70, 30), width=P(4))
d.polygon([(bx + P(3), by - P(54)), (bx + P(40), by - P(30)), (bx + P(3), by - P(18))], fill=(255, 255, 255))
# cờ Jolly Roger nhỏ trên thuyền
d.polygon([(bx - P(2), by - P(56)), (bx - P(30), by - P(50)), (bx - P(2), by - P(42))], fill=(20, 20, 20))
d.ellipse([bx - P(22), by - P(54), bx - P(12), by - P(44)], fill=(255, 255, 255))

# ---- Biển hiệu STRAW HAT PIRATES (trái) ----
sgx, sgy = P(20), P(150)
d.line([(sgx + P(10), sgy), (sgx + P(10), sgy + P(86))], fill=(120, 80, 40), width=P(5))
for i, txt in enumerate(["STRAW HAT", "PIRATES", "GOING MERRY"]):
    yy = sgy + i * P(26)
    d.rectangle([sgx, yy, sgx + P(96), yy + P(20)], fill=(222, 184, 120), outline=(150, 110, 60), width=S)
    ctext(d, sgx + P(48), yy + P(10), txt, font(P(9)), (90, 60, 30))

# ---- Nón rơm Luffy (trên khung giấy) ----
hx, hy = W // 2, P(56)
d.ellipse([hx - P(58), hy - P(8), hx + P(58), hy + P(20)], fill=(245, 205, 90), outline=(200, 150, 40), width=P(2))
d.pieslice([hx - P(34), hy - P(34), hx + P(34), hy + P(24)], 180, 360, fill=(250, 215, 110), outline=(200, 150, 40), width=P(2))
d.rectangle([hx - P(34), hy + P(2), hx + P(34), hy + P(10)], fill=(200, 60, 50))

# ---- KHUNG GIẤY DA (giữa) — để TRỐNG cho firmware vẽ giờ/ngày ----
px0, py0, px1, py1 = P(96), P(84), P(384), P(262)
d.rounded_rectangle([px0, py0, px1, py1], radius=P(22), fill=(245, 232, 200), outline=(196, 158, 96), width=P(4))
# viền chấm đỏ bên trong (kiểu phiếu kho báu)
ix0, iy0, ix1, iy1 = px0 + P(14), py0 + P(14), px1 - P(14), py1 - P(14)
dash = P(10)
for x in range(ix0, ix1, dash * 2):
    d.line([(x, iy0), (min(x + dash, ix1), iy0)], fill=(214, 40, 40), width=P(2))
    d.line([(x, iy1), (min(x + dash, ix1), iy1)], fill=(214, 40, 40), width=P(2))
for y in range(iy0, iy1, dash * 2):
    d.line([(ix0, y), (ix0, min(y + dash, iy1))], fill=(214, 40, 40), width=P(2))
    d.line([(ix1, y), (ix1, min(y + dash, iy1))], fill=(214, 40, 40), width=P(2))
# bánh lái nhỏ giữa khung (vạch ngăn giờ/ngày)
wx, wy, wr = W // 2, P(180), P(12)
d.ellipse([wx - wr, wy - wr, wx + wr, wy + wr], outline=(180, 130, 70), width=P(3))
for k in range(8):
    a = k * math.pi / 4
    d.line([(wx + wr * math.cos(a), wy + wr * math.sin(a)),
            (wx + (wr + P(5)) * math.cos(a), wy + (wr + P(5)) * math.sin(a))], fill=(180, 130, 70), width=P(2))
# 2 sao vàng cạnh ngày
for ssx in (P(150), P(330)):
    d.regular_polygon((ssx, P(214), P(8)), 5, fill=(241, 196, 15))

# ---- Kho báu (góc dưới-phải) ----
chx, chy = P(420), P(280)
d.rectangle([chx - P(26), chy - P(14), chx + P(26), chy + P(18)], fill=(150, 90, 40), outline=(90, 55, 25), width=P(2))
d.rectangle([chx - P(26), chy - P(14), chx + P(26), chy - P(6)], fill=(120, 72, 32))
for gx in (-14, 0, 14):
    d.ellipse([chx + P(gx) - P(6), chy - P(22), chx + P(gx) + P(6), chy - P(10)], fill=(255, 210, 60))

img = img.resize((480, 320), Image.LANCZOS)
img.save("/tmp/onepiece_clock_480x320.jpg", quality=92)
print("saved /tmp/onepiece_clock_480x320.jpg")
