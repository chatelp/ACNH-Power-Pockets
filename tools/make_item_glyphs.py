#!/usr/bin/env python3
"""Item glyphs for ACNH Power Pockets (docs/design/GLYPHES.md).

Draws assets/item_glyphs.png from scratch with simple shapes (no image is read
or traced). One 24x24 cell per glyph, left to right, in the order of the
"glyph <index> <name> [tint]" table of assets/item_glyphs.txt, on 3 rows:

  row 0 (y 0)   full color: the glyph with its default colors
  row 1 (y 24)  tint mask: the parts to recolor, white (light) and gray 200 (shade)
  row 2 (y 48)  overlay: outlines, untinted details and halo, the tint parts left out

For an item with a known color (assets/item_glyphs.txt "color" table) and a
"tint" glyph, the app draws row 1 with SDL_SetTextureColorMod(color), then row 2.
Otherwise it draws row 0. Rows 1 and 2 are empty for glyphs without tint.

Glyphs are drawn at 16x, given a cream "sticker" halo, then downsampled, so they
read on the cream list background (#F1EBDD) and on the dark-green selection
(#3F7A2E).

Usage:
  python3 tools/make_item_glyphs.py              write assets/item_glyphs.png
  python3 tools/make_item_glyphs.py --coverage   rule and color coverage on items.txt
  python3 tools/make_item_glyphs.py --preview X  also write a x4 control sheet to X
Needs Pillow.
"""
import math
import re
import sys
import collections
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
RULES = ROOT / "assets" / "item_glyphs.txt"
ITEMS = ROOT / "assets" / "items.txt"
OUT = ROOT / "assets" / "item_glyphs.png"
FONT = ROOT / "assets" / "font.ttf"

S = 16            # supersampling factor
U = 24            # cell size in units / output pixels
W = U * S

# ------------------------------------------------------------------ palette
# Charter tokens (docs/design/CHARTE.md 2.1) plus muted object colors.
OUTL = (0x5C, 0x44, 0x30, 255)    # wood.dark: every outline
WOOD = (0x7A, 0x5C, 0x3E, 255)
WOOD_L = (0xC2, 0x8E, 0x5C, 255)
CREAM = (0xF8, 0xF5, 0xEC, 255)
SAND = (0xE6, 0xDC, 0xC3, 255)
GOLD = (0xF2, 0xB7, 0x05, 255)
AMBER = (0xE8, 0xA3, 0x3D, 255)
YELLOW = (0xF2, 0xC1, 0x4E, 255)
RED = (0xD9, 0x53, 0x4A, 255)
GREEN = (0x6F, 0xB7, 0x39, 255)
GREEN_L = (0x9B, 0xD3, 0x6A, 255)
GREEN_D = (0x3F, 0x7A, 0x2E, 255)
BLUE = (0x5F, 0xA8, 0xD3, 255)
BLUE_D = (0x3D, 0x7E, 0xA6, 255)
SKY = (0xBF, 0xE3, 0xF2, 255)
METAL = (0xB4, 0xBE, 0xC6, 255)
METAL_D = (0x7E, 0x8A, 0x94, 255)
PINK = (0xF2, 0x9B, 0xB0, 255)
PURPLE = (0x9B, 0x7B, 0xC8, 255)
BONE = (0xEE, 0xE2, 0xC6, 255)
STONE = (0xA8, 0xA2, 0x98, 255)
INK = (0x3B, 0x2A, 0x1A, 255)
ORANGE = (0xF0, 0x8A, 0x2E, 255)
BROWN = (0x9A, 0x6B, 0x3F, 255)

# Tint markers: parts drawn with these colors become the recolorable mask.
T = (255, 0, 255, 255)            # tint, light
TS = (200, 0, 200, 255)           # tint, shade (x 200/255)
SHADE = 200

OW = 1.3          # outline width, in units


def px(v):
    return int(round(v * S))


class G:
    """One glyph canvas with helpers in 24-unit coordinates."""

    def __init__(self):
        self.im = Image.new("RGBA", (W, W), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def pts(self, pts):
        return [(px(x), px(y)) for x, y in pts]

    def poly(self, pts, fill, outline=OUTL, w=OW):
        self.d.polygon(self.pts(pts), fill=fill, outline=outline, width=px(w) if outline else 0)

    def ell(self, x0, y0, x1, y1, fill, outline=OUTL, w=OW):
        self.d.ellipse([px(x0), px(y0), px(x1), px(y1)], fill=fill, outline=outline, width=px(w) if outline else 0)

    def circ(self, cx, cy, r, fill, outline=OUTL, w=OW):
        self.ell(cx - r, cy - r, cx + r, cy + r, fill, outline, w)

    def rect(self, x0, y0, x1, y1, fill, r=1.5, outline=OUTL, w=OW):
        self.d.rounded_rectangle([px(x0), px(y0), px(x1), px(y1)], radius=px(r), fill=fill,
                                 outline=outline, width=px(w) if outline else 0)

    def line(self, pts, fill, w=1.4):
        self.d.line(self.pts(pts), fill=fill, width=px(w), joint="curve")
        for x, y in (pts[0], pts[-1]):          # round caps
            r = w / 2
            self.d.ellipse([px(x - r), px(y - r), px(x + r), px(y + r)], fill=fill)

    def stick(self, pts, fill, w=2.4):
        """Outlined thick line (handles, stems)."""
        self.line(pts, OUTL, w + 2 * OW * 0.85)
        self.line(pts, fill, w)

    def arc(self, box, a0, a1, fill, w=1.4):
        self.d.arc([px(box[0]), px(box[1]), px(box[2]), px(box[3])], a0, a1, fill=fill, width=px(w))

    def text(self, s, cx, cy, size, fill):
        f = ImageFont.truetype(str(FONT), px(size))
        self.d.text((px(cx), px(cy)), s, font=f, fill=fill, anchor="mm")


def leaf(g, cx, cy, length, width, angle, fill, outline=OUTL, w=OW):
    """Whole oval leaf (rotated ellipse). Never a bitten leaf."""
    a = math.radians(angle)
    pts = []
    for k in range(24):
        t = 2 * math.pi * k / 24
        x, y = length / 2 * math.cos(t), width / 2 * math.sin(t)
        pts.append((cx + x * math.cos(a) - y * math.sin(a), cy + x * math.sin(a) + y * math.cos(a)))
    g.poly(pts, fill, outline, w)


def petal(g, cx, cy, r0, r1, angle, width, fill, outline=OUTL, w=OW):
    """Pointed petal from radius r0 to r1 around (cx, cy)."""
    a = math.radians(angle)
    mid = (r0 + r1) / 2
    n = (math.cos(a + math.pi / 2), math.sin(a + math.pi / 2))
    p0 = (cx + r0 * math.cos(a), cy + r0 * math.sin(a))
    p1 = (cx + r1 * math.cos(a), cy + r1 * math.sin(a))
    pm = (cx + mid * math.cos(a), cy + mid * math.sin(a))
    g.poly([p0, (pm[0] + n[0] * width / 2, pm[1] + n[1] * width / 2), p1,
            (pm[0] - n[0] * width / 2, pm[1] - n[1] * width / 2)], fill, outline, w)


def sparkle(g, cx, cy, r, fill):
    k = r * 0.32
    g.poly([(cx, cy - r), (cx + k, cy - k), (cx + r, cy), (cx + k, cy + k),
            (cx, cy + r), (cx - k, cy + k), (cx - r, cy), (cx - k, cy - k)], fill)


# ==================================================================== glyphs
# Each g_<name> draws one generic object. Comments note what was avoided on
# purpose (GLYPHES.md, "Droit d'auteur"). Tintable parts use T / TS.

def g_unknown(g):
    g.circ(12, 12, 9, SAND)
    g.text("?", 12, 12.6, 14, OUTL)


# ------------------------------------------------------------------ tools
def g_axe(g):
    g.stick([(6.5, 21), (15.5, 5)], WOOD_L)
    g.poly([(12.5, 2.8), (20.8, 5.2), (21.2, 11.8), (16.8, 10.6), (14.6, 8.6)], METAL)
    g.line([(18.6, 6.2), (19.6, 10.4)], CREAM, 1.0)


def g_net(g):
    g.stick([(5, 21.5), (11.8, 12.6)], WOOD_L)
    g.ell(9.5, 2.5, 21.5, 14.5, CREAM)
    for i in range(1, 4):
        g.line([(9.8 + 3 * i, 3.6), (9.8 + 3 * i, 13.4)], SAND, 0.8)
        g.line([(10.6, 2.5 + 3 * i), (20.4, 2.5 + 3 * i)], SAND, 0.8)
    g.ell(9.5, 2.5, 21.5, 14.5, None)


def g_fishing_rod(g):
    g.stick([(4, 21.5), (18.5, 3.5)], WOOD, 1.8)
    g.line([(18.5, 3.5), (19.5, 15)], OUTL, 0.7)
    g.ell(16.8, 14, 22, 19.2, RED)
    g.poly([(16.8, 16.6), (22, 16.6), (21.6, 18.2), (17.2, 18.2)], CREAM, None)
    g.ell(16.8, 14, 22, 19.2, None)
    g.stick([(4, 21.5), (7.5, 17.2)], RED, 2.6)


def g_shovel(g):
    g.stick([(12, 4.5), (12, 13)], WOOD_L, 2.2)
    g.rect(7.5, 2, 16.5, 5.6, WOOD_L, 1.5)
    g.poly([(7.2, 12), (16.8, 12), (16.8, 17), (12, 22), (7.2, 17)], METAL)
    g.line([(12, 13.6), (12, 18.6)], CREAM, 1.0)


def g_watering_can(g):
    g.stick([(14.5, 13.5), (20.5, 7)], BLUE, 2.2)
    g.ell(18.4, 4.2, 23, 9.2, BLUE)
    g.rect(3.5, 9, 15.5, 20.5, BLUE, 3)
    g.arc((5, 4.5, 13, 13.5), 180, 360, OUTL, 3.6)
    g.arc((5, 4.5, 13, 13.5), 180, 360, BLUE_D, 1.6)
    g.line([(6, 12.5), (13, 12.5)], CREAM, 1.0)


def g_slingshot(g):
    g.stick([(12, 21.5), (12, 13)], WOOD_L, 2.6)
    g.stick([(12, 13.5), (6.5, 4.5)], WOOD_L, 2.4)
    g.stick([(12, 13.5), (17.5, 4.5)], WOOD_L, 2.4)
    g.d.line(g.pts([(6.5, 4.8), (12, 9.5), (17.5, 4.8)]), fill=RED, width=px(1.1), joint="curve")


def g_ladder(g):
    for y in (6, 11, 16):
        g.stick([(7.5, y), (16.5, y)], WOOD_L, 1.6)
    g.stick([(7.5, 2.5), (7.5, 21.5)], WOOD, 2.2)
    g.stick([(16.5, 2.5), (16.5, 21.5)], WOOD, 2.2)


def g_vaulting_pole(g):
    g.stick([(3.5, 20.5), (20.5, 3.5)], CREAM, 2.6)
    for t in (0.15, 0.4, 0.65, 0.9):
        x, y = 3.5 + 17 * t, 20.5 - 17 * t
        g.line([(x - 0.9, y + 0.9), (x + 0.9, y - 0.9)], RED, 2.4)


def g_wand(g):
    g.stick([(4.5, 20.5), (13.5, 11.5)], PURPLE, 2.2)
    sparkle(g, 16.2, 8.2, 6.4, YELLOW)          # 4-point sparkle, not a star
    g.circ(20.7, 16.7, 1.2, CREAM, OUTL, 0.9)


# ------------------------------------------------------------------ fruit
def fruit_leaf(g, x, y, ang=-25):
    g.line([(x - 1.4, y + 2.6), (x - 0.6, y - 0.6)], OUTL, 1.6)
    leaf(g, x + 2.6, y - 0.6, 5.4, 3, ang, GREEN)


def g_apple(g):
    g.ell(3.5, 6, 20.5, 22, RED)
    fruit_leaf(g, 12.8, 4.4)
    g.ell(6.6, 9.4, 9.6, 13.4, (0xF0, 0x8C, 0x82, 255), None)


def g_orange(g):
    g.circ(12, 13.5, 8.6, ORANGE)
    for x, y in ((8, 12), (11, 16.5), (15.5, 13), (14, 18.5), (9, 18)):
        g.circ(x, y, 0.55, (0xC8, 0x64, 0x14, 255), None)
    leaf(g, 15, 4.4, 6, 3.2, -20, GREEN)
    g.circ(12, 5.4, 1.1, GREEN_D, None)
    g.ell(6.2, 8.4, 9.2, 11.4, (0xF8, 0xB8, 0x70, 255), None)


def g_pear(g):
    col = (0xC9, 0xD8, 0x5B, 255)
    g.circ(12, 8.6, 4.4, col)
    g.circ(12, 15.4, 6.8, col)
    g.circ(12, 8.6, 3.1, col, None)                    # erase the inner outline
    g.ell(7.6, 10.5, 16.4, 16, col, None)
    g.line([(12, 4.6), (13.2, 1.8)], OUTL, 1.6)
    leaf(g, 16, 3.6, 5, 2.8, -20, GREEN)
    g.ell(8.2, 13.4, 10.6, 17, (0xE4, 0xEC, 0x9C, 255), None)


def g_peach(g):
    g.ell(3.5, 5.5, 20.5, 22, (0xF7, 0xA6, 0x8E, 255))
    g.arc((9.5, 5.5, 18.5, 22), 100, 250, (0xD9, 0x6A, 0x5A, 255), 1.2)
    g.line([(12, 6.4), (12.6, 3.4)], OUTL, 1.4)
    leaf(g, 8.2, 3.6, 6.4, 3.4, 200, GREEN)
    leaf(g, 15.8, 3.4, 6.4, 3.4, -20, GREEN)


def g_cherry(g):
    g.line([(7.5, 14), (12.5, 3.5)], OUTL, 1.4)
    g.line([(16.5, 15), (12.5, 3.5)], OUTL, 1.4)
    leaf(g, 16.2, 4.4, 6, 3, -10, GREEN)
    g.circ(7.5, 16.5, 4.6, RED)
    g.circ(16.5, 17.5, 4.6, RED)
    g.circ(6.2, 15.2, 1.0, (0xF0, 0x8C, 0x82, 255), None)
    g.circ(15.2, 16.2, 1.0, (0xF0, 0x8C, 0x82, 255), None)


def g_coconut(g):
    g.circ(12, 13, 9, BROWN)
    for x, y in ((10.4, 6.9), (13.6, 6.9), (12, 9.2)):     # the three pores, small and high
        g.circ(x, y, 0.8, INK, None)
    for a0 in (120, 150, 30, 60):
        g.arc((5, 6, 19, 20), a0, a0 + 18, (0xC2, 0x96, 0x68, 255), 1.0)
    g.arc((7, 11, 17, 21), 20, 160, (0xC2, 0x96, 0x68, 255), 1.0)


# ------------------------------------------------------------- vegetables

def g_turnip(g):
    leaf(g, 8.2, 6, 9, 4.4, -125, GREEN)
    leaf(g, 15.8, 6, 9, 4.4, -55, GREEN)
    leaf(g, 12, 4.6, 8.4, 4.4, -90, GREEN)
    g.ell(4.5, 8.5, 19.5, 21, CREAM)
    g.poly([(5.2, 12.5), (18.8, 12.5), (18, 10.6), (16, 9.2), (8, 9.2), (6, 10.6)], PURPLE, None)
    g.ell(4.5, 8.5, 19.5, 21, None)
    g.line([(12, 20.6), (12, 22.6)], OUTL, 1.2)


def g_tomato(g):
    g.ell(3, 6.5, 21, 21.5, RED)
    for k in range(5):
        petal(g, 12, 7.6, 0, 5, -90 + 72 * k + 36, 2.6, GREEN, OUTL, 0.9)
    g.line([(12, 7), (12.6, 3.4)], GREEN_D, 1.6)
    g.ell(6, 10.5, 9, 14, (0xF0, 0x8C, 0x82, 255), None)


def g_wheat(g):
    g.stick([(12, 22), (12, 6)], (0xE6, 0xC0, 0x66, 255), 1.2)
    for k in range(4):
        y = 6 + 3.4 * k
        leaf(g, 9.4, y + 1.2, 5, 2.8, -55, (0xF0, 0xCE, 0x6E, 255), OUTL, 1.0)
        leaf(g, 14.6, y + 1.2, 5, 2.8, -125, (0xF0, 0xCE, 0x6E, 255), OUTL, 1.0)
    leaf(g, 12, 3.6, 5, 2.8, -90, (0xF0, 0xCE, 0x6E, 255), OUTL, 1.0)


def g_sugarcane(g):
    for x, top in ((8, 3), (14, 5.5)):
        g.rect(x - 2, top, x + 2, 22, (0xB9, 0xD8, 0x6A, 255), 1)
        for y in range(int(top) + 4, 21, 5):
            g.line([(x - 1.6, y), (x + 1.6, y)], OUTL, 0.9)
    leaf(g, 18, 6, 8, 2.6, -40, GREEN)
    leaf(g, 4.6, 7, 7, 2.4, -130, GREEN)


def g_potato(g):
    g.poly([(3.5, 12), (6, 7), (12, 5.5), (18, 7), (21, 12), (19, 17.5), (12, 19.5), (5.5, 17.5)],
           (0xC9, 0x9A, 0x62, 255))
    for x, y in ((8, 10.5), (14.5, 9.5), (11, 14.5), (17, 14)):
        g.circ(x, y, 0.7, WOOD, None)


def g_carrot(g):
    leaf(g, 9, 5, 7, 3, -120, GREEN)
    leaf(g, 15, 5, 7, 3, -60, GREEN)
    leaf(g, 12, 4, 7, 3, -90, GREEN)
    g.poly([(6.5, 8), (17.5, 8), (12.8, 22), (11.2, 22)], ORANGE)
    for y, x0, x1 in ((11.5, 9, 12), (15, 12.5, 14.2), (18, 10.6, 12.4)):
        g.line([(x0, y), (x1, y)], (0xC8, 0x64, 0x14, 255), 0.9)


def g_pumpkin(g):
    for x0, x1 in ((2.5, 13), (11, 21.5)):
        g.ell(x0, 7, x1, 21, TS)
    g.ell(7.5, 6.5, 16.5, 21.5, T)
    g.arc((7.5, 6.5, 16.5, 21.5), 90, 270, OUTL, 1.0)
    g.arc((7.5, 6.5, 16.5, 21.5), 270, 450, OUTL, 1.0)
    g.rect(10.8, 3, 13.2, 8, GREEN_D, 0.8)


def g_bamboo_shoot(g):
    leaf(g, 8.5, 13.5, 15, 7, -105, (0xC9, 0xA4, 0x6A, 255))
    leaf(g, 15.5, 13.5, 15, 7, -75, (0xC9, 0xA4, 0x6A, 255))
    leaf(g, 12, 11, 17, 7.4, -90, (0xE0, 0xC2, 0x88, 255))
    leaf(g, 12, 4.4, 5, 3.4, -90, GREEN, OUTL, 1.0)
    g.rect(5, 19.5, 19, 22.5, (0xB0, 0x8A, 0x52, 255), 1.0)


# ---------------------------------------------------------------- flowers

def stem(g, top=13, bottom=22, leafs=True):
    g.stick([(12, top), (12, bottom)], GREEN, 1.6)
    if leafs:
        leaf(g, 15.4, 18.5, 6, 3, -30, GREEN)


def g_rose(g):
    stem(g, 12)
    g.circ(12, 8.5, 6.6, T)
    g.arc((7.6, 4.2, 16.4, 13), 200, 470, TS, 2.4)
    g.arc((9.5, 6, 14.5, 11), 30, 300, OUTL, 1.0)
    g.arc((7.6, 4.2, 16.4, 13), 200, 330, OUTL, 1.0)
    g.circ(12, 8.5, 6.6, None)


def g_tulip(g):
    stem(g, 12)
    leaf(g, 8, 17, 8, 3.2, -110, GREEN)
    g.poly([(6, 4), (9, 7), (12, 3), (15, 7), (18, 4), (17.5, 10.5), (15, 13.5), (9, 13.5), (6.5, 10.5)], T)
    g.poly([(9, 7), (12, 3), (15, 7), (14.5, 13.5), (9.5, 13.5)], TS, None)
    g.poly([(6, 4), (9, 7), (12, 3), (15, 7), (18, 4), (17.5, 10.5), (15, 13.5), (9, 13.5), (6.5, 10.5)], None)


def g_pansy(g):
    stem(g, 15)
    for cx, cy, r in ((8.4, 6.6, 4.2), (15.6, 6.6, 4.2), (6.6, 11.6, 4), (17.4, 11.6, 4), (12, 13.4, 4.2)):
        g.circ(cx, cy, r, T)
    g.circ(12, 10.2, 3.6, TS, None)
    g.circ(12, 10.2, 1.5, YELLOW, OUTL, 0.8)


def g_cosmos(g):
    stem(g, 14)
    for k in range(8):
        a = math.radians(-90 + 45 * k)
        leaf(g, 12 + 5 * math.cos(a), 9 + 5 * math.sin(a), 6.6, 4.4, -90 + 45 * k, T if k % 2 == 0 else TS, OUTL, 0.8)
    g.circ(12, 9, 2.4, YELLOW, OUTL, 0.9)


def g_lily(g):
    stem(g, 14)
    for k in range(6):
        a = -90 + 60 * k + 30
        petal(g, 12, 9.5, 0, 9, a, 6.4, T if k % 2 == 0 else TS, OUTL, 0.8)
    for k in (-1, 0, 1):
        g.line([(12, 9.5), (12 + 2.6 * k, 4.6)], OUTL, 0.7)
        g.circ(12 + 2.6 * k, 4.4, 0.8, AMBER, None)


def g_hyacinth(g):
    stem(g, 16)
    for row, (y, xs) in enumerate(((4, (12,)), (7, (9.5, 14.5)), (10, (8, 12, 16)), (13, (9.5, 14.5)))):
        for x in xs:
            g.circ(x, y, 2.3, T if (row + int(x)) % 2 else TS)


def g_windflower(g):
    stem(g, 15)
    for k in range(6):
        a = math.radians(-90 + 60 * k)
        g.circ(12 + 5 * math.cos(a), 9.5 + 5 * math.sin(a), 3.6, T)
    g.circ(12, 9.5, 3.4, INK)
    g.circ(12, 9.5, 1.2, CREAM, None)


def g_mum(g):
    stem(g, 15)
    g.circ(12, 9.5, 8, T)
    g.circ(12, 9.5, 4.2, TS, None)
    for k in range(16):
        a = math.radians(22.5 * k)
        r0 = 2 if k % 2 else 4.4
        g.line([(12 + r0 * math.cos(a), 9.5 + r0 * math.sin(a)),
                (12 + 7.2 * math.cos(a), 9.5 + 7.2 * math.sin(a))], OUTL, 0.7)
    g.circ(12, 9.5, 8, None)


def g_flower(g):
    stem(g, 13)
    for k in range(5):
        a = math.radians(-90 + 72 * k)
        g.circ(12 + 4.6 * math.cos(a), 9 + 4.6 * math.sin(a), 3.4, T)
    g.circ(12, 9, 2.6, YELLOW)


# ------------------------------------------------------------- vegetation
def g_tree(g):
    g.rect(10.2, 13, 13.8, 22, WOOD, 0.8)
    g.circ(12, 9.5, 8.2, GREEN)
    g.circ(9, 7.5, 2.2, GREEN_L, None)


def g_cedar(g):
    g.rect(10.4, 18, 13.6, 22.5, WOOD, 0.6)
    g.poly([(12, 9), (20, 19), (4, 19)], GREEN_D)
    g.poly([(12, 5.5), (18, 13.5), (6, 13.5)], GREEN_D)
    g.poly([(12, 1.8), (16, 8.5), (8, 8.5)], GREEN_D)


def g_palm(g):
    g.stick([(13, 22), (11.5, 15), (12.5, 8.5)], WOOD_L, 2.6)
    for cx, cy, ang in ((6.4, 8.6, 205), (8, 4.6, 235), (16, 4.6, 305), (17.6, 8.6, 335), (12, 3.6, 270)):
        leaf(g, cx, cy, 11, 4.6, ang, GREEN)
    g.circ(11, 10.2, 1.6, BROWN, OUTL, 0.9)
    g.circ(14, 10.4, 1.6, BROWN, OUTL, 0.9)


def g_bamboo(g):
    for x, top in ((8.5, 2.5), (15, 5)):
        g.rect(x - 2, top, x + 2, 22, (0x8C, 0xC6, 0x5A, 255), 1)
        for y in range(int(top) + 5, 21, 5):
            g.line([(x - 1.6, y), (x + 1.6, y)], GREEN_D, 1.0)
    leaf(g, 19.4, 8.5, 7, 2.6, -25, GREEN)
    leaf(g, 4.2, 12, 6.4, 2.4, -150, GREEN)


def g_bush(g):
    g.rect(3, 13, 21, 21, GREEN, 2)
    for cx, cy, r in ((7, 13.5, 5.6), (17, 13.5, 5.6), (12, 9.5, 6.8)):
        g.circ(cx, cy, r, GREEN)
    g.rect(3.7, 13.5, 20.3, 20.3, GREEN, 1.5, None)
    g.circ(9.4, 7.4, 1.6, GREEN_L, None)
    for x, y in ((7.5, 14.5), (13.5, 8.5), (16.8, 14.2), (11.4, 17.2), (12.6, 12.8)):
        g.circ(x, y, 2.0, T, OUTL, 0.9)


def g_weeds(g):
    for x0, x1, top, ang in ((6, 8, 7, -1), (9, 11.5, 3.5, 0), (13, 15, 5, 1), (16, 18, 9, 1)):
        g.poly([(x0, 21), (x1, 21), ((x0 + x1) / 2 + 2.5 * ang, top)], GREEN)
    g.ell(4, 19, 20, 22.5, GREEN_D)


def g_sapling(g):
    g.ell(4, 17, 20, 23, WOOD_L)
    g.stick([(12, 18), (12, 9)], GREEN_D, 1.4)
    g.ell(3.5, 5.5, 12.4, 10.5, GREEN)
    g.ell(11.6, 3.5, 20.5, 8.5, GREEN)


def g_seed_bag(g):
    top = [(4.5 + 1.875 * k, 4.5 if k % 2 else 3) for k in range(9)]
    g.poly([(4.5, 21.5), (19.5, 21.5)] + top[::-1], SAND)
    g.poly([(4.5, 21.5), (19.5, 21.5), (19.5, 18), (4.5, 18)], AMBER, None)
    g.poly([(4.5, 21.5), (19.5, 21.5)] + top[::-1], None)
    for k in range(5):                                   # printed flower, tinted
        a = math.radians(-90 + 72 * k)
        g.circ(12 + 2.6 * math.cos(a), 10.8 + 2.6 * math.sin(a), 2.1, T, OUTL, 0.9)
    g.circ(12, 10.8, 1.3, YELLOW, OUTL, 0.8)


def g_mushroom(g):
    # plain brown cap, no spots (spotted red caps evoke another franchise)
    g.rect(9, 11, 15, 21, CREAM, 2.5)
    g.poly([(2.5, 13), (5, 6.5), (12, 3), (19, 6.5), (21.5, 13)], WOOD_L)
    g.line([(5.2, 12.6), (18.8, 12.6)], OUTL, 1.2)


# --------------------------------------------------------------- materials
def g_wood(g):
    g.rect(3, 7, 18, 18, WOOD_L, 1.0)
    g.ell(13, 7, 21, 18, (0xE8, 0xC8, 0x96, 255))
    g.ell(15.2, 9.8, 18.8, 15.2, None, OUTL, 0.9)
    g.line([(5, 11), (11, 11)], WOOD, 0.9)
    g.line([(6, 14.5), (12, 14.5)], WOOD, 0.9)


def g_ore(g):
    g.poly([(3, 15), (6, 7), (13, 4), (20, 7.5), (21.5, 16), (15, 20.5), (7, 20)], STONE)
    g.poly([(11, 10), (15.5, 8.6), (17.4, 12.6), (13.4, 15.2), (10, 13.6)], GOLD, OUTL, 1.0)
    g.line([(6.5, 15.5), (9.5, 17.5)], (0xCF, 0xCA, 0xC0, 255), 1.0)


def g_star_shard(g):
    # a crystal shard, kept distinct from the favorite star badge
    g.poly([(12, 2), (17.5, 9), (12, 22), (6.5, 9)], (0xFF, 0xE8, 0x8A, 255))
    g.poly([(12, 2), (17.5, 9), (12, 9.8), (6.5, 9)], (0xFF, 0xF6, 0xC8, 255), None)
    g.poly([(12, 2), (17.5, 9), (12, 22), (6.5, 9)], None)
    sparkle(g, 19, 17.5, 3.2, CREAM)


def g_seasonal(g):
    def flake(color, main, branch):
        for k in range(3):
            a = math.radians(90 + 60 * k)
            dx, dy = 9 * math.cos(a), 9 * math.sin(a)
            g.line([(12 - dx, 12 - dy), (12 + dx, 12 + dy)], color, main)
        for k in range(6):
            a = math.radians(90 + 60 * k)
            ex, ey = 12 + 6 * math.cos(a), 12 + 6 * math.sin(a)
            for s in (-1, 1):
                b = a + s * math.radians(40)
                g.line([(ex, ey), (ex + 3 * math.cos(b), ey + 3 * math.sin(b))], color, branch)
    flake(OUTL, 3.4, 2.6)
    flake(BLUE, 1.6, 1.0)


def g_shell(g):
    pts = [(12, 21)]
    for k in range(0, 11):
        a = math.radians(200 + 14 * k)
        pts.append((12 + 10 * math.cos(a), 13.5 + 10 * math.sin(a)))
    g.poly(pts, PINK)
    for k in range(1, 5):
        a = math.radians(200 + 28 * k)
        g.line([(12, 20), (12 + 8.5 * math.cos(a), 13.5 + 8.5 * math.sin(a))], (0xC9, 0x6E, 0x86, 255), 0.9)
    g.rect(9.5, 19, 14.5, 22.5, PINK, 1.0)


# --------------------------------------------------------------- creatures
def g_fish(g):
    g.poly([(15.5, 12), (22, 6), (22, 18)], BLUE_D)
    g.ell(2.5, 6, 18, 18, BLUE)
    g.circ(7, 11.2, 1.4, INK, None)
    g.arc((9, 7, 15, 17), 300, 420, BLUE_D, 1.0)


def g_insect(g):
    for y, dx in ((10, 7.5), (13.5, 8.2), (17, 7.5)):
        g.line([(12 - dx, y + 1), (12, y)], OUTL, 1.2)
        g.line([(12 + dx, y + 1), (12, y)], OUTL, 1.2)
    g.line([(10.5, 4.6), (8, 1.8)], OUTL, 1.0)
    g.line([(13.5, 4.6), (16, 1.8)], OUTL, 1.0)
    g.ell(9, 3.4, 15, 8, INK)
    g.ell(6, 6.5, 18, 21.5, GREEN)
    g.line([(12, 7.2), (12, 21)], OUTL, 1.0)


def g_sea_creature(g):
    for s in (-1, 1):
        cx = 12 + s * 7.6
        g.ell(cx - 3, 3, cx + 3, 9, RED)
        g.line([(cx, 3.2), (cx, 6)], OUTL, 1.0)
        g.line([(12 + s * 6.4, 13), (12 + s * 7.6, 8.4)], OUTL, 1.6)
        for k in range(3):
            y = 15 + 2.3 * k
            g.line([(12 + s * 6.5, y), (12 + s * 10.2, y + 1.6)], OUTL, 1.2)
    g.ell(4.5, 10.5, 19.5, 21, RED)
    g.circ(9.6, 13.6, 1.0, INK, None)
    g.circ(14.4, 13.6, 1.0, INK, None)


def g_fossil(g):
    # a plain bone, not the game's buried-fossil marker
    for cx, cy in ((5.5, 6.5), (8, 4), (16, 20), (18.5, 17.5)):
        g.circ(cx, cy, 3, BONE)
    g.stick([(7, 6), (17, 18)], BONE, 3.6)
    for cx, cy in ((5.5, 6.5), (8, 4), (16, 20), (18.5, 17.5)):
        g.circ(cx, cy, 2.2, BONE, None)
    g.line([(7.6, 6.6), (16.4, 17.4)], BONE, 3.4)


# --------------------------------------------------------------------- art
def g_figurine(g):
    g.rect(5, 17, 19, 22, WOOD_L, 1.0)
    g.poly([(6.5, 17.4), (8, 12), (16, 12), (17.5, 17.4)], STONE)
    g.ell(8.8, 3.5, 15.2, 10.5, STONE)                  # plain bust, no character
    g.line([(7.5, 14.8), (16.5, 14.8)], (0xCF, 0xCA, 0xC0, 255), 1.0)


def g_painting(g):
    g.rect(2.5, 4, 21.5, 20, AMBER, 1.0)
    g.rect(5, 6.5, 19, 17.5, SKY, 0.4, OUTL, 0.9)
    g.poly([(5.4, 17.1), (9.5, 11.5), (13, 15), (15.5, 12.5), (18.6, 17.1)], GREEN, None)
    g.circ(16, 9.5, 1.5, YELLOW, None)


def g_photo(g):
    g.poly([(4, 5.5), (18.5, 3), (20.5, 18.5), (6, 21)], CREAM)
    g.poly([(6, 7.2), (17.2, 5.3), (18.6, 15.2), (7.4, 17.1)], PINK, None)
    g.circ(12.4, 9.4, 1.8, CREAM, None)                 # generic head and shoulders
    g.poly([(8.6, 16.6), (10, 12.8), (14.8, 12), (17.2, 15.4)], CREAM, None)


def g_poster(g):
    g.rect(4.5, 3.5, 19.5, 21.5, CREAM, 0.6)
    g.rect(7, 7, 17, 15.5, BLUE, 0.4, None)
    g.line([(7.5, 18), (16.5, 18)], OUTL, 1.0)
    g.poly([(15.5, 21.5), (19.5, 17.5), (19.5, 21.5)], SAND)
    g.circ(12, 3, 1.4, RED, OUTL, 0.9)


# --------------------------------------------------------------- furniture
def g_furniture(g):
    # generic decor item: a wooden cube seen from above
    g.poly([(12, 3), (21, 7.5), (12, 12), (3, 7.5)], (0xE0, 0xB4, 0x80, 255))
    g.poly([(3, 7.5), (12, 12), (12, 21.5), (3, 17)], WOOD_L)
    g.poly([(12, 12), (21, 7.5), (21, 17), (12, 21.5)], BROWN)


def g_chair(g):
    g.rect(6.5, 2.5, 17.5, 12.5, WOOD_L, 1.2)
    g.line([(10, 4.5), (10, 10.5)], WOOD, 1.0)
    g.line([(14, 4.5), (14, 10.5)], WOOD, 1.0)
    g.stick([(7.2, 14), (6.8, 22)], WOOD, 1.6)
    g.stick([(16.8, 14), (17.2, 22)], WOOD, 1.6)
    g.rect(4.5, 12, 19.5, 15.5, AMBER, 1.0)


def g_table(g):
    g.stick([(5.5, 11), (5.5, 21.5)], WOOD, 1.8)
    g.stick([(18.5, 11), (18.5, 21.5)], WOOD, 1.8)
    g.poly([(5, 6), (19, 6), (22, 10), (2, 10)], WOOD_L)
    g.rect(2, 9.6, 22, 12.4, BROWN, 0.6)


def g_bed(g):
    g.rect(2.5, 5, 7, 21, WOOD_L, 1.2)                  # headboard
    g.rect(5, 11, 21.5, 18, CREAM, 1.0)                 # mattress
    g.rect(6, 8.5, 11, 12.5, CREAM, 1.5)                # pillow
    g.poly([(11, 10.2), (21.5, 10.2), (21.5, 18), (11, 18)], T)   # blanket
    g.poly([(11, 15), (21.5, 15), (21.5, 18), (11, 18)], TS, None)
    g.poly([(11, 10.2), (21.5, 10.2), (21.5, 18), (11, 18)], None)
    g.stick([(20.5, 18.5), (20.5, 21)], WOOD, 1.4)


def g_sofa(g):
    g.rect(4, 5, 20, 13, T, 2.5)                        # back
    g.rect(2.5, 11, 21.5, 18, TS, 2)                    # seat
    g.rect(1.5, 9, 5.5, 18, T, 1.6)                     # arms
    g.rect(18.5, 9, 22.5, 18, T, 1.6)
    g.line([(12, 11.6), (12, 17.4)], OUTL, 0.9)
    g.stick([(4, 18.5), (4, 21.5)], WOOD, 1.4)
    g.stick([(20, 18.5), (20, 21.5)], WOOD, 1.4)


def g_storage(g):
    g.rect(4, 2.5, 20, 21, WOOD_L, 1.2)
    g.rect(6, 4.5, 18, 10.5, (0xE0, 0xB4, 0x80, 255), 0.6, OUTL, 1.0)
    g.rect(6, 12.5, 18, 18.5, (0xE0, 0xB4, 0x80, 255), 0.6, OUTL, 1.0)
    g.circ(12, 7.5, 0.9, OUTL, None)
    g.circ(12, 15.5, 0.9, OUTL, None)
    g.stick([(6, 21), (6, 22.5)], WOOD, 1.2)
    g.stick([(18, 21), (18, 22.5)], WOOD, 1.2)


def g_lamp(g):
    g.line([(12, 15), (12, 19.5)], OUTL, 2.6)
    g.rect(7, 19, 17, 22, WOOD_L, 1.0)
    g.poly([(7.5, 4), (16.5, 4), (20, 15), (4, 15)], YELLOW)
    g.line([(6.4, 13), (17.6, 13)], AMBER, 1.0)


def g_appliance(g):
    # a washing machine: box and round window
    g.rect(3.5, 2.5, 20.5, 21.5, CREAM, 1.8)
    g.line([(4.5, 6.5), (19.5, 6.5)], OUTL, 1.0)
    g.circ(7, 4.5, 0.8, BLUE_D, None)
    g.rect(13, 3.6, 18.6, 5.4, METAL, 0.4, None)
    g.circ(12, 14, 5.4, METAL)
    g.circ(12, 14, 3.6, SKY, OUTL, 0.9)


def g_instrument(g):
    # an acoustic guitar
    g.stick([(15, 9), (20, 2.5)], WOOD, 1.8)
    g.rect(18.4, 1.2, 22, 4.4, WOOD, 0.8)
    g.circ(8.8, 16.6, 5.8, AMBER)
    g.circ(12.6, 11.6, 4.2, AMBER)
    g.circ(8.8, 16.6, 5.8, None)
    g.circ(10.6, 14.2, 1.6, INK, None)
    g.line([(7.2, 19.4), (9.8, 17.8)], OUTL, 1.2)


def g_houseplant(g):
    leaf(g, 8, 8, 9, 4.6, -125, GREEN)
    leaf(g, 16, 8, 9, 4.6, -55, GREEN)
    leaf(g, 12, 6, 9, 4.6, -90, GREEN_L)
    g.poly([(6, 12.5), (18, 12.5), (16.5, 22), (7.5, 22)], (0xD0, 0x7A, 0x52, 255))
    g.rect(5, 11.5, 19, 14.5, (0xE0, 0x8E, 0x66, 255), 0.8)


def g_clock(g):
    g.stick([(7, 19), (5.5, 22)], WOOD, 1.4)
    g.stick([(17, 19), (18.5, 22)], WOOD, 1.4)
    g.circ(12, 11.5, 9, WOOD_L)
    g.circ(12, 11.5, 6.8, CREAM, OUTL, 1.0)
    g.line([(12, 11.5), (12, 6.8)], OUTL, 1.2)
    g.line([(12, 11.5), (15.4, 13.2)], OUTL, 1.2)


def g_outdoor(g):
    # a wooden signpost
    g.stick([(12, 9), (12, 22)], WOOD, 2.2)
    g.poly([(3, 3.5), (18, 3.5), (21.5, 7), (18, 10.5), (3, 10.5)], WOOD_L)
    g.line([(5.5, 7), (15, 7)], CREAM, 1.2)
    g.ell(6, 20, 18, 23, GREEN)


def g_rug(g):
    g.ell(2, 6, 22, 18, T)
    g.ell(5.5, 8.5, 18.5, 15.5, TS, OUTL, 0.9)
    g.ell(9, 10.4, 15, 13.6, CREAM, None)


def g_wallpaper(g):
    g.poly([(5, 6), (17, 6), (17, 19), (14.5, 21.5), (5, 21.5)], SKY)
    for x, y in ((8, 10), (13.5, 10), (10.7, 14), (8, 18), (13.5, 18)):
        g.circ(x, y, 1.3, BLUE_D, None)
    g.poly([(14.5, 21.5), (17, 19), (14.5, 19)], CREAM)
    g.rect(3, 2.5, 19, 7.5, GREEN, 2.5)
    g.ell(16.5, 2.5, 21.5, 7.5, GREEN_L)
    g.circ(19, 5, 0.8, OUTL, None)


def g_flooring(g):
    g.poly([(12, 4), (22, 11), (12, 18), (2, 11)], WOOD_L)
    for k in (1, 2, 3):
        f = k / 4
        g.line([(2 + 10 * f, 11 - 7 * f), (12 + 10 * f, 18 - 7 * f)], WOOD, 0.9)
    g.poly([(12, 4), (22, 11), (12, 18), (2, 11)], None)
    g.poly([(2, 11), (12, 18), (12, 20.5), (2, 13.5)], WOOD)
    g.poly([(12, 18), (22, 11), (22, 13.5), (12, 20.5)], BROWN)


def g_fence(g):
    for y in (9.5, 16):
        g.rect(2, y, 22, y + 2.6, WOOD, 0.6)
    for x in (4, 10, 16):
        g.poly([(x, 7), (x + 2, 3.5), (x + 4, 7), (x + 4, 21.5), (x, 21.5)], CREAM)


# ---------------------------------------------------------------- clothing
def g_shirt(g):
    body = [(8, 3.5), (12, 5.5), (16, 3.5), (21.5, 7), (19, 11.5), (17, 10.5), (17, 21), (7, 21),
            (7, 10.5), (5, 11.5), (2.5, 7)]
    g.poly(body, T)
    g.poly([(7, 17), (17, 17), (17, 21), (7, 21)], TS, None)
    g.poly(body, None)
    g.arc((9.5, 2, 14.5, 7.5), 20, 160, OUTL, 1.1)


def g_coat(g):
    body = [(8, 2.5), (16, 2.5), (20.5, 6), (21, 21.5), (3, 21.5), (3.5, 6)]
    g.poly(body, T)
    g.poly([(3, 6), (6.5, 6), (6.5, 21.5), (3, 21.5)], TS, None)
    g.poly([(17.5, 6), (21, 6), (21, 21.5), (17.5, 21.5)], TS, None)
    g.poly(body, None)
    g.poly([(8, 2.5), (12, 8.5), (16, 2.5)], CREAM, OUTL, 1.0)   # collar
    g.line([(12, 8.5), (12, 21.5)], OUTL, 1.0)
    for y in (11.5, 15, 18.5):
        g.circ(13.6, y, 0.8, OUTL, None)


def g_dress(g):
    g.poly([(9, 3), (15, 3), (15.8, 9), (20, 21), (4, 21), (8.2, 9)], T)
    g.poly([(8.2, 9), (15.8, 9), (16.6, 11), (7.4, 11)], TS, None)
    g.poly([(9, 3), (15, 3), (15.8, 9), (20, 21), (4, 21), (8.2, 9)], None)
    g.line([(8.4, 9.4), (15.6, 9.4)], OUTL, 1.1)
    g.line([(9.5, 3.2), (8.4, 1.6)], OUTL, 1.0)
    g.line([(14.5, 3.2), (15.6, 1.6)], OUTL, 1.0)


def g_outfit(g):
    # a one-piece outfit: jumpsuit with sleeves and belt
    body = [(8, 2.5), (16, 2.5), (21, 6.5), (19, 10), (17, 9), (17.5, 21.5), (12.8, 21.5), (12, 13),
            (11.2, 21.5), (6.5, 21.5), (7, 9), (5, 10), (3, 6.5)]
    g.poly(body, T)
    g.rect(6.8, 10.5, 17.2, 12.6, TS, 0, None)
    g.poly(body, None)
    g.line([(7, 10.4), (17, 10.4)], OUTL, 0.9)
    g.line([(7, 12.8), (17, 12.8)], OUTL, 0.9)
    g.rect(10.8, 10.6, 13.2, 12.6, YELLOW, 0.3, OUTL, 0.7)
    g.arc((9.5, 1, 14.5, 6.5), 20, 160, OUTL, 1.1)


def g_pants(g):
    body = [(5.5, 3), (18.5, 3), (20, 21.5), (14, 21.5), (12, 10), (10, 21.5), (4, 21.5)]
    g.poly(body, T)
    g.poly([(5.5, 3), (18.5, 3), (18.7, 6.2), (5.3, 6.2)], TS, None)
    g.poly(body, None)
    g.line([(5.4, 6.2), (18.6, 6.2)], OUTL, 1.0)
    g.line([(12, 6.4), (12, 10)], OUTL, 0.9)


def g_skirt(g):
    body = [(7, 4), (17, 4), (21.5, 19.5), (2.5, 19.5)]
    g.poly(body, T)
    for x0, x1 in ((5.5, 8.4), (11, 12.8), (15.6, 18.4)):
        g.poly([(x0 + 1.8, 7.5), (x1 + 0.4, 7.5), (x1 + 1.2, 19.5), (x0, 19.5)], TS, None)
    g.poly(body, None)
    g.rect(6.5, 3, 17.5, 7, TS, 0.6)


def g_hat(g):
    g.ell(1.5, 13, 22.5, 20, T)
    g.rect(6, 5, 18, 16.5, T, 4)
    g.rect(6, 12, 18, 15, TS, 0.4, None)
    g.rect(6, 5, 18, 16.5, None, 4)
    g.line([(6.6, 16.2), (17.4, 16.2)], OUTL, 1.0)


def g_cap(g):
    g.poly([(10, 15), (22, 15.5), (21, 18.5), (10, 18)], TS)      # visor
    pts = [(2.5, 16.5)]
    for k in range(0, 13):
        a = math.radians(180 + 15 * k)
        pts.append((11 + 8.5 * math.cos(a), 16.5 + 10 * math.sin(a)))
    g.poly(pts, T)
    g.line([(11, 6.6), (11, 16.4)], OUTL, 0.9)
    g.circ(11, 6.4, 1.2, TS, OUTL, 0.8)


def g_hair(g):
    # a hair bow / ribbon
    g.poly([(12, 11), (3, 4.5), (2.5, 17.5)], T)
    g.poly([(12, 11), (21, 4.5), (21.5, 17.5)], T)
    g.poly([(12, 11), (6, 8), (5.8, 14.2)], TS, None)
    g.poly([(12, 11), (18, 8), (18.2, 14.2)], TS, None)
    g.poly([(12, 11), (3, 4.5), (2.5, 17.5)], None)
    g.poly([(12, 11), (21, 4.5), (21.5, 17.5)], None)
    g.poly([(10.5, 12), (8, 21), (11, 20)], TS)
    g.poly([(13.5, 12), (16, 21), (13, 20)], TS)
    g.circ(12, 11, 2.6, T)


def g_glasses(g):
    g.line([(9.6, 12), (14.4, 12)], OUTL, 2.6)
    g.line([(9.6, 12), (14.4, 12)], T, 1.0)
    for cx in (6.2, 17.8):
        g.circ(cx, 13, 5.2, T, OUTL, 1.0)
        g.circ(cx, 13, 3.4, SKY, OUTL, 1.0)
        g.line([(cx - 1.8, 11.6), (cx - 0.4, 10.8)], CREAM, 1.0)


def g_mask(g):
    g.arc((0.5, 7, 7.5, 17), 90, 270, OUTL, 1.4)
    g.arc((16.5, 7, 23.5, 17), 270, 450, OUTL, 1.4)
    g.rect(3.5, 6.5, 20.5, 18.5, T, 4)
    for y in (10.5, 14.5):
        g.line([(6.5, y), (17.5, y)], TS, 1.2)
    g.rect(3.5, 6.5, 20.5, 18.5, None, 4)


def g_socks(g):
    body = [(8, 2.5), (15, 2.5), (15, 14), (19.5, 16.5), (19.5, 21.5), (10.5, 21.5), (8, 18)]
    g.poly(body, T)
    g.poly([(15, 14), (19.5, 16.5), (19.5, 21.5), (16, 21.5)], TS, None)
    g.poly(body, None)
    g.rect(7.4, 2, 15.6, 6, CREAM, 0.8)
    g.line([(8.4, 10), (14.6, 10)], CREAM, 1.4)


def g_shoes(g):
    body = [(2.5, 18), (3, 9), (9, 9), (11, 12.5), (19, 14), (21.5, 17), (21.5, 18)]
    g.poly(body, T)
    g.poly([(2.8, 9), (9, 9), (10, 11), (3, 11)], TS, None)
    g.poly(body, None)
    g.rect(2, 17.5, 22, 21, CREAM, 1.2)
    g.line([(7, 11.6), (9.5, 14)], CREAM, 1.0)


def g_boots(g):
    body = [(6, 2.5), (14, 2.5), (14, 13), (20.5, 15), (21, 19), (6, 19)]
    g.poly(body, T)
    g.poly([(6, 2.5), (14, 2.5), (14, 6), (6, 6)], TS, None)
    g.poly(body, None)
    g.rect(5.5, 18.5, 21.5, 22, WOOD, 1.0)


def g_bag(g):
    g.arc((7, 2.5, 17, 13), 180, 360, OUTL, 3.6)
    g.arc((7, 2.5, 17, 13), 180, 360, WOOD_L, 1.6)
    g.rect(3.5, 8, 20.5, 21.5, T, 2.5)
    g.rect(3.5, 8, 20.5, 12, TS, 0.6, None)
    g.rect(3.5, 8, 20.5, 21.5, None, 2.5)
    g.rect(9.5, 10.5, 14.5, 13.5, YELLOW, 0.8, OUTL, 0.9)


def g_backpack(g):
    g.arc((8, 1.5, 16, 8), 180, 360, OUTL, 3.0)
    g.rect(4.5, 4, 19.5, 21.5, T, 4)
    g.rect(7, 13, 17, 20, TS, 2, OUTL, 1.0)
    g.line([(7.5, 15.4), (16.5, 15.4)], OUTL, 0.9)
    g.rect(4.5, 4, 19.5, 21.5, None, 4)


def g_umbrella(g):
    g.stick([(12, 10), (12, 19)], WOOD, 1.4)
    g.arc((12, 16, 17, 22), 0, 180, OUTL, 1.6)
    pts = []
    for k in range(0, 19):
        a = math.radians(180 + 10 * k)
        pts.append((12 + 10 * math.cos(a), 11.5 + 8.5 * math.sin(a)))
    for k in range(4, -1, -1):
        x = 2 + 5 * k
        pts.append((x - 1.2 if k else x, 11.5))
        pts.append((x - 2.5, 13.2) if k else (x, 11.5))
    g.poly(pts, T)
    g.poly([(12, 3), (7.5, 4.4), (9.5, 11.5), (14.5, 11.5), (16.5, 4.4)], TS, None)
    g.poly(pts, None)
    g.line([(12, 3.2), (12, 11.6)], OUTL, 0.9)


# -------------------------------------------------------------------- misc
def g_recipe(g):
    g.rect(3.5, 2.5, 17.5, 21.5, CREAM, 1.0)
    for y in (7, 10.5, 14):
        g.line([(6.5, y), (14.5, y)], SAND, 1.2)
    g.stick([(12, 20.5), (20.5, 9)], YELLOW, 2.0)
    g.poly([(12.6, 19.7), (11, 22.6), (13.9, 21.2)], CREAM, OUTL, 0.8)


def g_music(g):
    g.line([(9, 6.5), (19.5, 3.8)], OUTL, 2.4)
    g.line([(9, 6.5), (9, 17)], OUTL, 1.6)
    g.line([(19.5, 3.8), (19.5, 14.5)], OUTL, 1.6)
    g.ell(3.5, 14.2, 10, 19.8, PURPLE)
    g.ell(14, 11.7, 20.5, 17.3, PURPLE)


def g_ticket(g):
    g.poly([(2, 6), (22, 6), (22, 10), (20.4, 12), (22, 14), (22, 18), (2, 18), (2, 14), (3.6, 12), (2, 10)], YELLOW)
    for y in (7.6, 10, 12.4, 14.8):
        g.line([(16, y), (16, y + 1)], OUTL, 0.8)
    g.line([(5.5, 10), (12.5, 10)], AMBER, 1.2)
    g.line([(5.5, 14), (10.5, 14)], AMBER, 1.2)


def g_bells(g):
    # a stack of coins (not a money sack)
    for y in (17, 13.5, 10):
        g.ell(4, y, 20, y + 5.5, GOLD)
    g.ell(7.5, 11, 16.5, 14.6, None, (0xC9, 0x8A, 0x00, 255), 0.9)


def g_present(g):
    g.rect(3.5, 9.5, 20.5, 21.5, RED, 1.0)
    g.rect(2.5, 7, 21.5, 11, RED, 1.0)
    g.rect(10.4, 7.2, 13.6, 21.3, YELLOW, 0, None)
    g.ell(6, 2.5, 12.2, 8, YELLOW)
    g.ell(11.8, 2.5, 18, 8, YELLOW)
    g.rect(2.5, 7, 21.5, 11, None, 1.0)


def g_food(g):
    for x in (9, 12.5, 16):
        g.arc((x - 2, 1.5, x + 2, 6.5), 270, 450, SAND, 1.0)
    g.ell(3, 7.5, 21, 13.5, CREAM)
    g.poly([(2, 11), (22, 11), (19.5, 18.5), (15, 21), (9, 21), (4.5, 18.5)], BLUE)
    g.line([(5, 15), (19, 15)], CREAM, 1.0)


def g_drink(g):
    g.line([(14, 6), (18.5, 1.8)], OUTL, 2.6)
    g.line([(14, 6), (18.5, 1.8)], RED, 1.2)
    g.poly([(5.5, 5), (18.5, 5), (16.5, 21.5), (7.5, 21.5)], SKY)
    g.poly([(6.4, 10), (17.6, 10), (16.6, 20.6), (7.4, 20.6)], AMBER, None)
    g.poly([(5.5, 5), (18.5, 5), (16.5, 21.5), (7.5, 21.5)], None)


def g_egg(g):
    g.ell(4.5, 2.5, 19.5, 21.5, SKY)
    pts = [(4.8 + 1.8 * k, 12 + (1.6 if k % 2 else -1.6)) for k in range(9)]
    g.line(pts, PINK, 1.6)
    g.ell(4.5, 2.5, 19.5, 21.5, None)


def g_kit(g):
    g.arc((8, 3, 16, 11), 180, 360, OUTL, 3.4)
    g.arc((8, 3, 16, 11), 180, 360, METAL_D, 1.6)
    g.rect(2.5, 7, 21.5, 20.5, RED, 2)
    g.line([(3.4, 12.5), (20.6, 12.5)], OUTL, 1.0)
    g.rect(10, 11, 14, 14, METAL, 0.6, OUTL, 0.8)


def g_document(g):
    g.poly([(4.5, 2.5), (14.5, 2.5), (19.5, 7.5), (19.5, 21.5), (4.5, 21.5)], CREAM)
    g.poly([(14.5, 2.5), (19.5, 7.5), (14.5, 7.5)], SAND)
    for y in (11, 14.5, 18):
        g.line([(7.5, y), (16.5, y)], BLUE_D, 1.1)


DRAW = {name[2:]: fn for name, fn in list(globals().items()) if name.startswith("g_") and callable(fn)}

# Default color of the tinted parts (row 0), per glyph.
DEFAULT_TINT = {
    "pumpkin": ORANGE, "rose": RED, "tulip": RED, "pansy": PURPLE, "cosmos": PINK, "lily": CREAM,
    "hyacinth": BLUE, "windflower": RED, "mum": YELLOW, "flower": PINK, "bush": PINK, "seed_bag": PINK,
    "bed": BLUE, "sofa": AMBER, "rug": RED, "shirt": BLUE, "coat": WOOD_L, "dress": PINK,
    "outfit": GREEN, "pants": BLUE_D, "skirt": PURPLE, "hat": YELLOW, "cap": RED, "hair": RED,
    "glasses": RED, "mask": CREAM, "socks": RED, "shoes": RED, "boots": BROWN, "bag": AMBER,
    "backpack": GREEN, "umbrella": GREEN,
}


# ------------------------------------------------------------------ render
def channel_eq(ch, v):
    return ch.point(lambda x: 255 if x == v else 0)


def render(name):
    """Returns (row0, row1, row2) 24x24 RGBA images and whether the glyph tints."""
    g = G()
    DRAW[name](g)
    im = g.im
    r, gr, b, a = im.split()
    g0 = channel_eq(gr, 0)
    m_t = ImageChops.multiply(ImageChops.multiply(channel_eq(r, 255), g0), channel_eq(b, 255))
    m_s = ImageChops.multiply(ImageChops.multiply(channel_eq(r, SHADE), g0), channel_eq(b, SHADE))
    m_all = ImageChops.lighter(m_t, m_s)
    tints = m_all.getbbox() is not None

    # halo from the whole silhouette
    sil = a.filter(ImageFilter.GaussianBlur(px(1.1) * 0.6)).point(lambda v: 255 if v > 18 else 0)
    sil = sil.filter(ImageFilter.GaussianBlur(S * 0.4))

    def with_halo(img, halo_alpha):
        base = Image.new("RGBA", img.size, CREAM[:3] + (0,))
        base.putalpha(halo_alpha)
        return Image.alpha_composite(base, img)

    def small(img):
        return img.resize((U, U), Image.LANCZOS)

    # row 0: markers replaced by the default color
    full = im.copy()
    if tints:
        c = DEFAULT_TINT.get(name, PINK)
        cs = tuple(int(v * SHADE / 255) for v in c[:3]) + (255,)
        full.paste(Image.new("RGBA", im.size, c), (0, 0), m_t)
        full.paste(Image.new("RGBA", im.size, cs), (0, 0), m_s)
    row0 = small(with_halo(full, sil))
    if not tints:
        empty = Image.new("RGBA", (U, U), (0, 0, 0, 0))
        return row0, empty, empty, False

    # row 1: white / gray mask of the tinted parts
    mask = Image.new("RGBA", im.size, (0, 0, 0, 0))
    mask.paste(Image.new("RGBA", im.size, (255, 255, 255, 255)), (0, 0), m_t)
    mask.paste(Image.new("RGBA", im.size, (SHADE, SHADE, SHADE, 255)), (0, 0), m_s)
    row1 = small(mask)

    # row 2: everything else; the halo is kept out of the tinted area
    over = im.copy()
    over.putalpha(ImageChops.subtract(a, m_all))
    halo2 = ImageChops.subtract(sil, m_all)
    row2 = small(with_halo(over, halo2))
    return row0, row1, row2, True


# ------------------------------------------------------------------- rules
def base_name(n):
    i = n.find(" (")
    if i > 0:
        n = n[:i]
    j = n.find(" with ")
    return n[:j] if j > 0 else n


def variant(n):
    i = n.find(" (")
    return n[i + 2:].rstrip(")") if i > 0 else ""


def parse_rules(path=RULES):
    """Returns (glyphs {name: (index, tint)}, colors [(name, hex)], rules, bad lines)."""
    glyphs, colors, rules, bad = {}, [], [], []
    for no, raw in enumerate(open(path, encoding="utf-8"), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if parts[0] == "glyph" and len(parts) in (3, 4) and parts[1].isdigit():
            glyphs[parts[2]] = (int(parts[1]), len(parts) == 4 and parts[3] == "tint")
            continue
        if parts[0] == "color" and len(parts) >= 3 and re.fullmatch(r"[0-9A-Fa-f]{6}", parts[1]):
            colors.append((" ".join(parts[2:]).lower(), parts[1].upper()))
            continue
        p2 = line.split(None, 1)
        if len(p2) < 2:
            bad.append((no, line))
            continue
        rules.append((no, p2[0], p2[1].strip()))
    return glyphs, colors, rules, bad


def rule_matches(rule, idx, low, base):
    if rule == "*":
        return True
    m = re.fullmatch(r"0[xX]([0-9A-Fa-f]{4})(?:-0[xX]([0-9A-Fa-f]{4}))?", rule)
    if m:
        a = int(m.group(1), 16)
        b = int(m.group(2), 16) if m.group(2) else a
        return a <= idx <= b
    r = rule.lower()
    if r[0] == "~":
        return r[1:].strip() in low
    if r[0] == "$":
        w = r[1:].strip()
        return base == w or base.endswith(" " + w) or base.endswith("-" + w)
    return low == r


def classify(items, rules):
    out, hits = [None] * len(items), collections.Counter()
    for i, n in enumerate(items):
        if not n.strip():
            continue
        low = n.lower()
        base = base_name(low)
        for no, g, rule in rules:
            if rule_matches(rule, i, low, base):
                out[i] = g
                hits[no] += 1
    return out, hits


def find_color(text, colors):
    """First color word of `text`, longest name first at each position."""
    t = text.lower()
    names = sorted(colors, key=lambda c: -len(c[0]))
    for pos in range(len(t)):
        if pos and t[pos - 1].isalnum():
            continue
        for name, hexv in names:
            end = pos + len(name)
            if t.startswith(name, pos) and (end == len(t) or not t[end].isalnum()):
                return name, hexv
    return None


def item_color(name, colors):
    """Color of an item: its (variant) first, else its base name (flowers, plants)."""
    v = variant(name)
    if v:
        c = find_color(v, colors)
        if c:
            return c
    return find_color(base_name(name), colors)


def load_items():
    return [l.rstrip("\r\n") for l in open(ITEMS, encoding="utf-8", errors="replace")]


def coverage():
    items = load_items()
    glyphs, colors, rules, bad = parse_rules()
    unknown_names = sorted({g for _, g, _ in rules} - set(glyphs))
    named = [i for i, n in enumerate(items) if n.strip()]
    explicit = [r for r in rules if r[2] != "*"]
    out_all, _ = classify(items, rules)
    out_exp, hits = classify(items, explicit)
    n = len(named)
    cls = sum(1 for i in named if out_exp[i] and out_exp[i] != "unknown")
    unk = sum(1 for i in named if (out_all[i] or "unknown") == "unknown")
    print(f"items with a name: {n}")
    print(f"recognized by a rule (glyph other than unknown): {cls} ({100 * cls / n:.1f} %)")
    print(f"shown as unknown '?': {unk} ({100 * unk / n:.1f} %)")
    tintable = [i for i in named if glyphs.get(out_all[i] or "unknown", (0, False))[1]]
    tinted = [i for i in tintable if item_color(items[i], colors)]
    print(f"items on a tint glyph: {len(tintable)} ({100 * len(tintable) / n:.1f} %), "
          f"with a known color: {len(tinted)} ({100 * len(tinted) / n:.1f} % of all, "
          f"{100 * len(tinted) / max(1, len(tintable)):.1f} % of tint glyphs)")
    print(f"glyphs: {len(glyphs)} ({sum(1 for v in glyphs.values() if v[1])} with tint), colors: {len(colors)}")
    print(f"invalid lines: {bad}  unknown glyph names: {unknown_names}")
    c = collections.Counter(out_all[i] or "unknown" for i in named)
    ct = collections.Counter(out_all[i] for i in tinted)
    print("\nitems per glyph (tinted with a color from the name):")
    for g, (k, t) in sorted(glyphs.items(), key=lambda x: x[1][0]):
        extra = f"  tinted {ct.get(g, 0)}" if t else ""
        print(f"  {k:2d} {g:14s} {c.get(g, 0):5d}{extra}")
    used = collections.Counter(item_color(items[i], colors)[0] for i in tinted)
    print("\ncolors used most:", ", ".join(f"{k} {v}" for k, v in used.most_common(15)))
    print("colors never used:", ", ".join(sorted(c for c, _ in colors if c not in used)) or "none")
    rest = collections.Counter((base_name(items[i].lower()).split() or ["?"])[-1]
                               for i in named if (out_all[i] or "unknown") == "unknown")
    print("\nlargest groups shown as unknown (last word of the name):")
    print("  " + ", ".join(f"{w} {k}" for w, k in rest.most_common(30)))
    dead = [(no, g, r) for no, g, r in explicit if hits[no] == 0]
    print(f"\nrules that match nothing: {len(dead)}")
    for no, g, r in dead:
        print(f"  line {no}: {g} {r}")


# ------------------------------------------------------------------- build
def build(preview=None):
    glyphs, colors, _, _ = parse_rules()
    order = sorted(glyphs.items(), key=lambda x: x[1][0])
    assert [v[0] for _, v in order] == list(range(len(order))), "glyph indexes must be 0..N-1"
    missing = [g for g, _ in order if g not in DRAW]
    assert not missing, f"no drawing for {missing}"
    sheet = Image.new("RGBA", (U * len(order), U * 3), (0, 0, 0, 0))
    cells = []
    for name, (k, tint_flag) in order:
        r0, r1, r2, tints = render(name)
        assert tints == tint_flag, f"{name}: 'tint' flag in item_glyphs.txt is {tint_flag}, drawing says {tints}"
        sheet.paste(r0, (k * U, 0))
        sheet.paste(r1, (k * U, U))
        sheet.paste(r2, (k * U, 2 * U))
        cells.append((name, r0, r1, r2, tints))
    sheet.save(OUT)
    print("wrote", OUT, sheet.size, f"{len(order)} glyphs, {sum(c[4] for c in cells)} with tint")
    if preview:
        write_preview(cells, colors, preview)


def tinted(r1, r2, hexv):
    c = tuple(int(hexv[i:i + 2], 16) for i in (0, 2, 4))
    rr, gg, bb, aa = r1.split()
    mod = Image.merge("RGBA", (rr.point(lambda v: v * c[0] // 255), gg.point(lambda v: v * c[1] // 255),
                               bb.point(lambda v: v * c[2] // 255), aa))
    return Image.alpha_composite(mod, r2)


def write_preview(cells, colors, path):
    """x4 on cream (#F1EBDD) and green (#3F7A2E), plus x1 with dark and light tints."""
    font = ImageFont.truetype(str(FONT), 13)
    cmap = dict(colors)
    samples = [cmap.get("navy blue", "2E3F6B"), cmap.get("white", "F4F1EA"), cmap.get("yellow", "F2C14E")]
    cols, cw, ch = 6, 210, 156
    rows = (len(cells) + cols - 1) // cols
    im = Image.new("RGBA", (cols * cw, rows * ch), (0xF8, 0xF5, 0xEC, 255))
    d = ImageDraw.Draw(im)
    for n, (name, r0, r1, r2, tints) in enumerate(cells):
        x, y = (n % cols) * cw, (n // cols) * ch
        for j, bg in enumerate(((0xF1, 0xEB, 0xDD, 255), (0x3F, 0x7A, 0x2E, 255))):
            bx = x + 6 + j * 102
            d.rectangle([bx, y + 6, bx + 98, y + 132], fill=bg)
            im.alpha_composite(r0.resize((96, 96), Image.NEAREST), (bx + 1, y + 7))
            vs = [r0] + ([tinted(r1, r2, h) for h in samples] if tints else [])
            for k, v in enumerate(vs):
                im.alpha_composite(v, (bx + 2 + k * 24, y + 106))
        d.text((x + 8, y + 136), f"{n} {name}" + (" (tint)" if tints else ""), font=font,
               fill=(0x5A, 0x40, 0x29, 255))
    im.save(path)
    print("wrote", path)


if __name__ == "__main__":
    args = sys.argv[1:]
    if args[:1] == ["--coverage"]:
        coverage()
    else:
        prev = args[args.index("--preview") + 1] if "--preview" in args else None
        build(prev)
