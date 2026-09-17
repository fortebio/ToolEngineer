#!/usr/bin/env python3
"""Tải Google Noto Animated Emoji và chuyển thành GIF cảm xúc cho thiết bị VIMATE.

Nguồn: Noto Emoji Animation (Google) — https://googlefonts.github.io/noto-emoji-animation/
Giấy phép: CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/) — ghi nguồn là đủ.
Mỗi emoji tải thẳng qua HTTP, không cần clone repo:
    https://fonts.gstatic.com/s/e/notoemoji/latest/<codepoint>/512.gif

Vì sao ép mạnh (mặc định 160 px / 6 khung / 32 màu):
- Bộ GIF nằm ở CẢ HAI nơi: SPIFFS `emo_spiffs` 512 KB (dùng được ~448 KB) và embed trong
  app (main/CMakeLists.txt GIF_EMBED_FILES). Noto có gradient tô bóng nên GIF nặng gấp
  3–6 lần bộ sticker thỏ cũ; đo 11/09/2026: 240px/8f/64c ≈ 90 KB/file, 160px/6f/32c
  ≈ 27 KB/file (~350 KB cả bộ) — chỉ mức sau là vừa SPIFFS mà không ăn vào app.
- MỘT bảng màu chung cho mọi khung (quantize một lần trên dải ghép các khung): bảng màu
  cục bộ từng khung làm file to gấp đôi, và decoder on-device (main/util/gif/gifdec.c)
  đòi global colour table.
- Player on-device (main/util/gif/lvgl_gif.c) kẹp delay ≥ 150 ms/khung, nên nhiều hơn
  6–8 khung chỉ tốn chỗ chứ không mượt hơn.
- Không dither: decoder palette-based + phóng 3× nearest-neighbour làm hạt dither thành
  nhiễu lấm tấm.

Chạy từ thư mục firmware-vimate-p4:
    python tools/fetch_noto_gifs.py --preview /tmp/noto_preview.png
"""

from __future__ import annotations

import argparse
import io
import sys
import urllib.request
from pathlib import Path

from PIL import Image, ImageSequence

NOTO_URL = "https://fonts.gstatic.com/s/e/notoemoji/latest/{cp}/512.gif"

# 13 cảm xúc đang có GIF trong firmware (main/ui/display.c s_embedded_gifs[]).
MAPPING = {
    "neutral": "1f642",      # 🙂 slightly smiling — màn "Sẵn sàng", thân thiện hơn 😐
    "happy": "1f60a",        # 😊 smiling face with smiling eyes
    "sad": "1f622",          # 😢 crying face (một giọt) — "crying" là 😭 riêng
    "angry": "1f620",        # 😠 angry face
    "confused": "1f615",     # 😕 confused face
    "surprised": "1f62e",    # 😮 face with open mouth — "shocked" là 😱 riêng
    "sleepy": "1f634",       # 😴 sleeping face
    "embarrassed": "1f633",  # 😳 flushed face
    "thinking": "1f914",     # 🤔 thinking face
    "relaxed": "1f60c",      # 😌 relieved face
    "funny": "1f602",        # 😂 face with tears of joy — "laughing" là 😆 riêng
    "delicious": "1f60b",    # 😋 face savoring food
    "loving": "1f60d",       # 😍 smiling face with heart-eyes
}

# 8 cảm xúc còn lại server có thể gửi (server/internal/gateway/emotion.go), hiện là ảnh
# tĩnh PNG. Bật bằng --all21; muốn firmware dùng thì còn phải thêm vào GIF_EMBED_FILES
# (main/CMakeLists.txt) và EMBED_GIF()/s_embedded_gifs[] (main/ui/display.c).
MAPPING_EXTRA = {
    "laughing": "1f606",     # 😆
    "crying": "1f62d",       # 😭
    "shocked": "1f631",      # 😱
    "winking": "1f609",      # 😉
    "cool": "1f60e",         # 😎
    "kissy": "1f618",        # 😘
    "confident": "1f60f",    # 😏
    "silly": "1f61c",        # 😜
}

MAX_TOTAL_BYTES = 440 * 1024   # SPIFFS emo_spiffs 512 KB, dùng được ~448 KB
MAX_FILE_BYTES = 460 * 1024    # cap runtime trong display.c apply_emotion


def sample_indices(total: int, max_frames: int) -> list[int]:
    if total <= max_frames:
        return list(range(total))
    if max_frames <= 1:
        return [0]
    return [round(i * (total - 1) / (max_frames - 1)) for i in range(max_frames)]


def fetch(cp: str, cache_dir: Path | None) -> bytes:
    if cache_dir:
        cached = cache_dir / f"{cp}.gif"
        if cached.exists():
            return cached.read_bytes()
    with urllib.request.urlopen(NOTO_URL.format(cp=cp), timeout=60) as resp:
        data = resp.read()
    if cache_dir:
        cache_dir.mkdir(parents=True, exist_ok=True)
        (cache_dir / f"{cp}.gif").write_bytes(data)
    return data


def convert(raw: bytes, size: int, max_frames: int, colors: int, alpha_threshold: int) -> tuple[bytes, Image.Image, int, int]:
    """Trả về (bytes GIF, khung đầu RGBA để preview, số khung nguồn, delay ms)."""
    src = Image.open(io.BytesIO(raw))
    frames = [f.convert("RGBA") for f in ImageSequence.Iterator(src)]
    durations = [max(50, int(f.info.get("duration", 100))) for f in ImageSequence.Iterator(Image.open(io.BytesIO(raw)))]
    n = len(frames)
    indices = sample_indices(n, max_frames)
    small = [frames[i].resize((size, size), Image.Resampling.LANCZOS) for i in indices]

    # Một bảng màu chung: ghép mọi khung thành một dải rồi quantize đúng một lần.
    # Nền dải = trắng để màu nền không chiếm chỗ trong bảng (pixel trong suốt sẽ bị
    # thay bằng index 0 ở dưới).
    strip = Image.new("RGB", (size * len(small), size), (255, 255, 255))
    for k, f in enumerate(small):
        strip.paste(f.convert("RGB"), (k * size, 0), f.getchannel("A"))
    pal_img = strip.quantize(colors=colors - 1, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    palette = pal_img.getpalette()[: 3 * (colors - 1)]

    out: list[Image.Image] = []
    for f in small:
        alpha = f.getchannel("A").point(lambda v: 255 if v >= alpha_threshold else 0)
        q = f.convert("RGB").quantize(palette=pal_img, dither=Image.Dither.NONE).point(lambda p: p + 1)
        q.putpalette([0, 0, 0] + palette)
        q.paste(0, mask=alpha.point(lambda v: 255 - v))  # index 0 = trong suốt
        out.append(q)

    # Delay đều = trung bình nguồn, tối thiểu 150 ms (player kẹp ở đó).
    duration = max(150, round(sum(durations) / max(1, len(durations))))
    buf = io.BytesIO()
    out[0].save(
        buf,
        format="GIF",
        save_all=True,
        append_images=out[1:],
        duration=duration,
        loop=0,
        disposal=2,
        transparency=0,
        optimize=True,
    )
    return buf.getvalue(), small[0], n, duration


def check_gif(data: bytes, size: int) -> None:
    """Đúng những gì decoder on-device cần: GIF89a, đúng kích thước, có global colour table."""
    if data[:6] not in (b"GIF89a", b"GIF87a"):
        raise ValueError("không phải GIF")
    w = int.from_bytes(data[6:8], "little")
    h = int.from_bytes(data[8:10], "little")
    if (w, h) != (size, size):
        raise ValueError(f"kích thước {w}x{h} != {size}x{size}")
    if not data[10] & 0x80:
        raise ValueError("thiếu global colour table — gifdec.c sẽ từ chối")


def write_preview(tiles: list[tuple[str, Image.Image]], path: Path, size: int) -> None:
    cols = 5
    rows = (len(tiles) + cols - 1) // cols
    pad = 8
    sheet = Image.new("RGBA", (cols * (size + pad) + pad, rows * (size + pad) + pad), (238, 238, 238, 255))
    for i, (_, img) in enumerate(tiles):
        x = pad + (i % cols) * (size + pad)
        y = pad + (i // cols) * (size + pad)
        sheet.alpha_composite(img, (x, y))
    sheet.convert("RGB").save(path)


def main() -> int:
    # Console Windows mặc định cp1252 → in tiếng Việt có dấu là UnicodeEncodeError.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dst", default="spiffs_emo_image", help="Thư mục GIF của firmware")
    parser.add_argument("--size", type=int, default=160, help="Cạnh vuông đầu ra (px)")
    parser.add_argument("--max-frames", type=int, default=6, help="Số khung tối đa mỗi GIF")
    parser.add_argument("--colors", type=int, default=32, help="Số màu bảng màu chung (kể cả index 0 trong suốt)")
    parser.add_argument("--alpha-threshold", type=int, default=24, help="Ngưỡng alpha 1-bit")
    parser.add_argument("--all21", action="store_true", help="Sinh thêm 8 cảm xúc chưa có trong firmware")
    parser.add_argument("--cache", default="", help="Thư mục cache GIF 512px đã tải (tuỳ chọn)")
    parser.add_argument("--preview", default="", help="Xuất contact sheet PNG để xem trước")
    parser.add_argument("--dry-run", action="store_true", help="Chỉ đo size, không ghi GIF")
    args = parser.parse_args()

    if not 2 <= args.colors <= 256:
        parser.error("--colors phải trong [2, 256]")

    mapping = dict(MAPPING)
    if args.all21:
        mapping.update(MAPPING_EXTRA)

    dst_dir = Path(args.dst)
    cache_dir = Path(args.cache) if args.cache else None
    tiles: list[tuple[str, Image.Image]] = []
    total = 0
    worst = 0
    for key, cp in mapping.items():
        raw = fetch(cp, cache_dir)
        data, first, n_src, duration = convert(raw, args.size, args.max_frames, args.colors, args.alpha_threshold)
        check_gif(data, args.size)
        n_out = min(n_src, args.max_frames)
        total += len(data)
        worst = max(worst, len(data))
        tiles.append((key, first))
        flag = "  !! > cap 460KB" if len(data) > MAX_FILE_BYTES else ""
        print(f"{key:12s} U+{cp.upper():6s} src {len(raw) // 1024:5d} KB/{n_src:3d} khung -> "
              f"{len(data):6d} B ({len(data) / 1024:5.1f} KB) {n_out} khung @ {duration} ms{flag}")
        if not args.dry_run:
            dst_dir.mkdir(parents=True, exist_ok=True)
            (dst_dir / f"{key}.gif").write_bytes(data)

    print(f"tổng {total} B ({total / 1024:.1f} KB) / ngân sách SPIFFS {MAX_TOTAL_BYTES // 1024} KB; "
          f"file lớn nhất {worst / 1024:.1f} KB")
    if args.preview:
        write_preview(tiles, Path(args.preview), args.size)
        print(f"preview -> {args.preview}")

    ok = True
    if total > MAX_TOTAL_BYTES:
        print(f"LỖI: tổng {total} B vượt {MAX_TOTAL_BYTES} B — hạ --size/--max-frames/--colors", file=sys.stderr)
        ok = False
    if worst > MAX_FILE_BYTES:
        print(f"LỖI: có file vượt cap runtime {MAX_FILE_BYTES} B", file=sys.stderr)
        ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
