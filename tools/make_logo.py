#!/usr/bin/env python3
"""Logo of ACNH Power Pockets: a stitched patch pocket with a lightning bolt,
a ticket and a fruit peeking out (docs/design/CHARTE.md palette).

Drawn from scratch with simple shapes; it replaces the upstream leaf logo.
Writes (Pillow needed):
  icon.png, assets/icon.png            256x256 RGB  (SD card copy shown in the app header)
  icon_150.png, assets/icon_150.png    256x256 RGBA (pocket on a transparent background)
  icon.jpg                             256x256 JPEG (embedded in the .nro by the Makefile)
  banner.png, assets/banner.png        848x208 RGBA
Usage: python3 tools/make_logo.py
"""
import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT = ROOT / "assets" / "font.ttf"
S = 4                                   # supersampling

LEAF = (0x7A, 0xC1, 0x43, 255); DOTS = (0x6F, 0xB7, 0x39, 255)
CREAM = (0xF8, 0xF5, 0xEC, 255)
WOOD = (0x7A, 0x5C, 0x3E, 255); WOOD_D = (0x5C, 0x44, 0x30, 255); WOOD_L = (0xC2, 0x8E, 0x5C, 255)
YELLOW = (0xF2, 0xC1, 0x4E, 255); RED = (0xD9, 0x53, 0x4A, 255); GREEN = (0x6F, 0xB7, 0x39, 255)
INK = (0x3B, 0x2A, 0x1A, 255)


class Pen:
    """Draws in 256-unit coordinates, scaled by k and offset by (ox, oy), supersampled by S."""

    def __init__(self, im, k=1.0, ox=0, oy=0):
        self.d = ImageDraw.Draw(im)
        self.k, self.ox, self.oy = k, ox, oy

    def p(self, x, y):
        return ((self.ox + x * self.k) * S, (self.oy + y * self.k) * S)

    def w(self, v):
        return max(1, int(round(v * self.k * S)))


def dots(im, w, h):
    d = ImageDraw.Draw(im)
    for y in range(0, h, 32):
        for x in range(0, w + 32, 32):
            cx = x + (16 if (y // 32) % 2 else 0) + 8
            cy = y + 8
            d.ellipse([(cx - 4) * S, (cy - 4) * S, (cx + 4) * S, (cy + 4) * S], fill=DOTS)


def pocket(pen):
    d, p, w = pen.d, pen.p, pen.w
    top, bot, l, r = 70, 222, 46, 210
    cx, ry = (l + r) / 2, 52

    def outline_pts():
        pts = [p(l, top), p(r, top), p(r, bot - ry)]
        for k in range(1, 30):
            a = math.radians(6 * k)
            pts.append(p(cx + (r - l) / 2 * math.cos(a), bot - ry + ry * math.sin(a)))
        pts.append(p(l, bot - ry))
        return pts

    def stitches():
        for x in range(l + 18, r - 14, 14):
            d.line([p(x, top + 14), p(x + 7, top + 14)], fill=WOOD_L, width=w(4))

    # items peeking out (drawn first, the pocket front covers their bottom)
    d.rounded_rectangle([p(64, 30), p(116, 82)], radius=w(8), fill=YELLOW, outline=WOOD_D, width=w(7))   # ticket
    d.line([p(100, 34), p(100, 78)], fill=WOOD_D, width=w(3))
    d.line([p(159, 40), p(162, 22)], fill=WOOD_D, width=w(6))                                           # fruit stem
    d.ellipse([p(132, 34), p(186, 88)], fill=RED, outline=WOOD_D, width=w(7))                           # fruit
    d.ellipse([p(164, 15), p(190, 31)], fill=GREEN, outline=WOOD_D, width=w(4))                         # whole oval leaf
    d.ellipse([p(143, 45), p(155, 57)], fill=(0xF0, 0x8C, 0x82, 255))                                   # highlight
    # pocket
    d.polygon(outline_pts(), fill=CREAM, outline=WOOD_D, width=w(9))
    stitches()
    # lightning bolt ("power")
    s, bx, by = 2.3, 128, 150
    bolt = [(-6, -22), (10, -22), (2, -4), (12, -4), (-8, 24), (-2, 2), (-12, 2)]
    d.polygon([p(bx + x * s, by + y * s) for x, y in bolt], fill=YELLOW, outline=WOOD_D, width=w(6))


def icon(transparent=False):
    im = Image.new("RGBA", (256 * S, 256 * S), (0, 0, 0, 0) if transparent else LEAF)
    if not transparent:
        dots(im, 256, 256)
    pocket(Pen(im))
    return im.resize((256, 256), Image.LANCZOS)


def banner():
    W, H = 848, 208
    im = Image.new("RGBA", (W * S, H * S), LEAF)
    dots(im, W, H)
    d = ImageDraw.Draw(im)
    # cream card behind the pocket and the words, like the app panel
    d.rounded_rectangle([24 * S, 20 * S, (W - 24) * S, (H - 20) * S], radius=24 * S, fill=CREAM,
                        outline=WOOD_D, width=6 * S)
    pocket(Pen(im, k=0.62, ox=34, oy=24))
    big = ImageFont.truetype(str(FONT), 76 * S)
    small = ImageFont.truetype(str(FONT), 26 * S)
    d.text((214 * S, 98 * S), "Power Pockets", font=big, fill=INK, anchor="ls")
    d.text((218 * S, 146 * S), "Pocket & Bells editor for ACNH", font=small, fill=WOOD, anchor="ls")
    return im.resize((W, H), Image.LANCZOS)


def main():
    ic = icon()
    rgb = ic.convert("RGB")
    for path in (ROOT / "icon.png", ROOT / "assets" / "icon.png"):
        rgb.save(path)
    rgb.save(ROOT / "icon.jpg", quality=95, subsampling=0)
    tr = icon(transparent=True)
    for path in (ROOT / "icon_150.png", ROOT / "assets" / "icon_150.png"):
        tr.save(path)
    bn = banner()
    for path in (ROOT / "banner.png", ROOT / "assets" / "banner.png"):
        bn.save(path)
    print("wrote icon.png, icon.jpg, icon_150.png, banner.png (+ assets/ copies)")


if __name__ == "__main__":
    main()
