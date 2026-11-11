#!/usr/bin/env python3
"""Generates assets/ui_icons.png: one 24x24 icon every 24 px (docs/design/CHARTE.md 4.3).

Index: 0 star, 1 check, 2 warn, 3 lock, 4 lock_light, 5 cross.
Needs Pillow. Drawn at 8x then downsampled for clean anti-aliasing.
Usage: python3 tools/make_icons.py [out.png]
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw

S = 8                    # supersampling
CELL = 24 * S
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "assets" / "ui_icons.png"

FAV = (0xF2, 0xB7, 0x05, 255)
WOOD_DARK = (0x5C, 0x44, 0x30, 255)
CREAM = (0xF8, 0xF5, 0xEC, 255)
WARN = (0xF2, 0xC1, 0x4E, 255)
INK = (0x3B, 0x2A, 0x1A, 255)
TEXT2 = (0x7A, 0x5C, 0x3E, 255)
ERROR = (0xC0, 0x39, 0x2B, 255)

img = Image.new("RGBA", (CELL * 6, CELL), (0, 0, 0, 0))
d = ImageDraw.Draw(img)


def p(cell, x, y):
    """Point in 24-unit icon coordinates -> supersampled pixels."""
    return (cell * CELL + x * S, y * S)


# 0 star
star = [(12, 2), (14.9, 8.6), (22, 9.3), (16.6, 14), (18.2, 21), (12, 17.3), (5.8, 21), (7.4, 14), (2, 9.3), (9.1, 8.6)]
d.polygon([p(0, *q) for q in star], fill=FAV, outline=WOOD_DARK, width=int(1.8 * S))

# 1 check
d.line([p(1, 4.5, 12.5), p(1, 9.5, 17.5), p(1, 19.5, 6.5)], fill=CREAM, width=int(3.2 * S), joint="curve")

# 2 warn
d.polygon([p(2, 12, 2.5), p(2, 22.5, 21), p(2, 1.5, 21)], fill=WARN, outline=INK, width=int(1.6 * S))
d.rectangle([p(2, 11, 9), p(2, 13.2, 15.5)], fill=INK)
d.rectangle([p(2, 11, 17), p(2, 13.2, 19.2)], fill=INK)


def lock(cell, color):
    d.rounded_rectangle([p(cell, 4.5, 10.5), p(cell, 19.5, 21.5)], radius=2 * S, fill=color)
    d.arc([p(cell, 7.5, 3), p(cell, 16.5, 15)], start=180, end=360, fill=color, width=int(2.4 * S))
    d.line([p(cell, 7.5 + 1.2, 9), p(cell, 7.5 + 1.2, 11)], fill=color, width=int(2.4 * S))
    d.line([p(cell, 16.5 - 1.2, 9), p(cell, 16.5 - 1.2, 11)], fill=color, width=int(2.4 * S))


lock(3, TEXT2)
lock(4, CREAM)

# 5 cross
d.line([p(5, 6, 6), p(5, 18, 18)], fill=ERROR, width=int(3 * S))
d.line([p(5, 18, 6), p(5, 6, 18)], fill=ERROR, width=int(3 * S))

img = img.resize((24 * 6, 24), Image.LANCZOS)
img.save(OUT)
print("wrote", OUT, img.size)
