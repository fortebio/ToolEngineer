#!/usr/bin/env python3
"""Generate transparent Chuppy GIFs for the VIMATE edu device.

The ST7796S SPI LCD cannot afford LVGL alpha+scale transforms every GIF frame.
Generate GIFs at the display size instead, keep transparency clean, and let
firmware render RGB565A8 frames 1:1.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


MAPPING = {
    "neutral": "Happy Bunny Sticker by Tonton Friends.gif",
    "happy": "Bunny Cheer Sticker by Tonton Friends.gif",
    "sad": "Sad Cry Sticker by Tonton Friends.gif",
    "angry": "Angry Bunny Sticker by Tonton Friends.gif",
    "confused": "Bunny What Sticker by Tonton Friends.gif",
    "surprised": "Surprise Wow Sticker by Tonton Friends.gif",
    "sleepy": "Sleep Love Sticker by Tonton Friends.gif",
    "embarrassed": "Bunny Sigh Sticker by Tonton Friends.gif",
    "thinking": "Bunny What Sticker by Tonton Friends.gif",
    "relaxed": "Thanks Nod Sticker by Tonton Friends.gif",
    "funny": "Laugh Lol Sticker by Tonton Friends.gif",
    "delicious": "Hungry Meal Sticker by Tonton Friends.gif",
    "loving": "Bunny Love Sticker by Tonton Friends.gif",
}


def sample_indices(total: int, max_frames: int) -> list[int]:
    if total <= max_frames:
        return list(range(total))
    if max_frames <= 1:
        return [0]
    return [round(i * (total - 1) / (max_frames - 1)) for i in range(max_frames)]


def normalized_frame(src: Image.Image, size: int, alpha_threshold: int) -> Image.Image:
    frame = src.convert("RGBA")
    frame.thumbnail((size, size), Image.Resampling.LANCZOS)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.alpha_composite(frame, ((size - frame.width) // 2, (size - frame.height) // 2))
    r, g, b, a = out.split()
    a = a.point(lambda value: 255 if value >= alpha_threshold else 0)
    out.putalpha(a)
    return out


def frame_duration_ms(gif: Image.Image, indices: list[int]) -> int:
    durations: list[int] = []
    for idx in indices:
        gif.seek(idx)
        durations.append(max(50, int(gif.info.get("duration", 100))))
    return max(80, round(sum(durations) / max(1, len(durations))))


def convert_one(src: Path, dst: Path, size: int, max_frames: int, alpha_threshold: int) -> None:
    with Image.open(src) as gif:
        indices = sample_indices(getattr(gif, "n_frames", 1), max_frames)
        frames = [normalized_frame(gif.seek(idx) or gif, size, alpha_threshold) for idx in indices]
        duration = frame_duration_ms(gif, indices)

    dst.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        dst,
        save_all=True,
        append_images=frames[1:],
        duration=duration,
        loop=0,
        disposal=2,
        transparency=0,
        optimize=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--src", default="Emotion/chuppy", help="Source Chuppy GIF directory")
    parser.add_argument("--dst", default="firmware-vimate/spiffs_emo_image", help="Output firmware GIF directory")
    parser.add_argument("--size", type=int, default=128, help="Output square size in pixels")
    parser.add_argument("--max-frames", type=int, default=10, help="Maximum frames per GIF")
    parser.add_argument("--alpha-threshold", type=int, default=24, help="Alpha threshold for GIF transparency")
    args = parser.parse_args()

    src_dir = Path(args.src)
    dst_dir = Path(args.dst)
    total_size = 0
    for key, filename in MAPPING.items():
        src = src_dir / filename
        if not src.exists():
            raise FileNotFoundError(src)
        dst = dst_dir / f"{key}.gif"
        convert_one(src, dst, args.size, args.max_frames, args.alpha_threshold)
        size = dst.stat().st_size
        total_size += size
        print(f"{key:12s} {size:7d} bytes -> {dst}")
    print(f"total {total_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
