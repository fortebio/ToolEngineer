#!/usr/bin/env python3
"""Generate small transparent Cherry GIFs for the embedded emotion overlay.

The ESP firmware GIF decoder is palette based and runs on constrained memory.
Avoid external GIF palette optimizers here because aggressive dithering can
produce noisy colors on-device. Pillow's RGBA-to-GIF path keeps the Cherry
palette stable enough for the small emotion overlay.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


MAPPING = {
    "neutral": "Neutral.gif",
    "happy": "Happy.gif",
    "sad": "Sad.gif",
    "angry": "Angry.gif",
    "confused": "Confused.gif",
    "surprised": "Surprised.gif",
    "sleepy": "Sleepy.gif",
    "embarrassed": "Embarrassed.gif",
    "thinking": "Thinking.gif",
    "relaxed": "Relaxed.gif",
    "funny": "Funny.gif",
    "delicious": "Delicious.gif",
    "loving": "Loving.gif",
}


def sample_indices(total: int, max_frames: int) -> list[int]:
    if total <= max_frames:
        return list(range(total))
    if max_frames <= 1:
        return [0]
    return [round(i * (total - 1) / (max_frames - 1)) for i in range(max_frames)]


def normalized_frame(src: Image.Image, size: int, alpha_threshold: int) -> Image.Image:
    frame = src.convert("RGBA").resize((size, size), Image.Resampling.LANCZOS)
    r, g, b, a = frame.split()
    a = a.point(lambda value: 255 if value >= alpha_threshold else 0)
    frame.putalpha(a)
    return frame


def frame_duration_ms(gif: Image.Image, indices: list[int]) -> int:
    durations: list[int] = []
    for idx in indices:
        gif.seek(idx)
        durations.append(max(20, int(gif.info.get("duration", 80))))
    total = sum(durations)
    return max(40, round(total / max(1, len(indices))))


def convert_one(src: Path, dst: Path, size: int, max_frames: int, alpha_threshold: int) -> None:
    with Image.open(src) as gif:
        indices = sample_indices(getattr(gif, "n_frames", 1), max_frames)
        frames: list[Image.Image] = []
        for idx in indices:
            gif.seek(idx)
            frames.append(normalized_frame(gif, size, alpha_threshold))
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
    parser.add_argument("--src", default="Emotion/cherry", help="Source Cherry GIF directory")
    parser.add_argument("--dst", default="firmware-vimate/spiffs_emo_image", help="Output firmware GIF directory")
    parser.add_argument("--size", type=int, default=64, help="Output square size in pixels")
    parser.add_argument("--max-frames", type=int, default=12, help="Maximum frames per GIF")
    parser.add_argument("--alpha-threshold", type=int, default=24, help="Alpha threshold for 1-bit GIF transparency")
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
