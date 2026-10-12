#!/usr/bin/env python3
"""Emit Content/Textures/digits.png -- a 10-glyph damage-number atlas.

One row of `0`..`9`, each glyph 8x8 with a 1px shadow, so a damage number is a
strip of digit sprites the battle script spawns and fades. Pixel font so it
matches the 16px art at nearest filtering.

    python3 Example/2DRpgPrototype/Tools/make_digits.py
"""
from __future__ import annotations

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
CELL = 8

# 5x7 glyphs, MSB = leftmost of the 5 columns.
GLYPHS = {
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "3": ["11111", "00010", "00100", "00010", "00001", "10001", "01110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
}

FILL = (255, 255, 255, 255)
SHADOW = (40, 20, 25, 255)


def main() -> None:
    image = Image.new("RGBA", (CELL * 10, CELL), (0, 0, 0, 0))
    pixels = image.load()
    for i, digit in enumerate("0123456789"):
        rows = GLYPHS[digit]
        ox = i * CELL + 1
        # Shadow pass first, then fill, so the fill is never overdrawn.
        for y, row in enumerate(rows):
            for x, bit in enumerate(row):
                if bit == "1":
                    pixels[ox + x + 1, y + 1] = SHADOW
        for y, row in enumerate(rows):
            for x, bit in enumerate(row):
                if bit == "1":
                    pixels[ox + x, y] = FILL
    out = ROOT / "Content/Textures/digits.png"
    image.save(out)
    print(f"wrote {out} ({image.width}x{image.height})")


if __name__ == "__main__":
    main()
