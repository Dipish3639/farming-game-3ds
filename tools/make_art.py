#!/usr/bin/env python3
"""Paints all of Tiny Harvest's art and writes it as C data.

Everything is drawn from code with a fixed, hue-shifted palette: shaded
spheres and leaves with ordered dithering, selective outlines, and
hand-placed pixel details. Output:

  source/art.c, source/art.h   indexed images + palette for the game
  art/preview_*.png            enlarged previews for reviewing the art

Run from the repository root:  python3 tools/make_art.py
"""
import math
import os
import random

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def hexc(h):
    return (int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16), 255)


# ---------------------------------------------------------------------------
# Palette: ramps run dark -> light, shadows lean cool, highlights lean warm.

RAMPS = {
    "outline": ["#24151f"],
    "grass": ["#173d38", "#1f5a3e", "#2f7a3f", "#4a9a3f", "#72b84a", "#a6d65a", "#d4ec7a"],
    "leaf": ["#14392f", "#1d5a3a", "#2f803c", "#55a840", "#86c94a", "#bce46a"],
    "soil": ["#2a1620", "#40221f", "#5c3524", "#7a4b2c", "#9a6638"],
    "wet": ["#170c16", "#24141c", "#341e20", "#462a24", "#5a3a2c"],
    "wood": ["#2e1a1f", "#4f2c26", "#73422e", "#99603a", "#bf8550", "#e0b070"],
    "stone": ["#2f2c40", "#4a4760", "#6e6c88", "#9a99b2", "#c8c8da", "#eeeef6"],
    "red": ["#4a1428", "#7a1e30", "#b52d33", "#e0503c", "#f88a5a", "#ffc49a"],
    "orange": ["#5a2318", "#94401c", "#d0671f", "#f0952e", "#ffc35a", "#ffe6a0"],
    "yellow": ["#6a4a14", "#a87a1e", "#e0b52a", "#f8de4a", "#fff590", "#fffcd8"],
    "purple": ["#2a1640", "#4a2468", "#76369a", "#a45ec8", "#d095ea", "#f0d0ff"],
    "cream": ["#5e5060", "#8c7f88", "#b8ada8", "#ddd3c4", "#f6efe0", "#ffffff"],
    "skin": ["#5a2e2e", "#94523f", "#cc7f5a", "#eeac80", "#ffd6b0"],
    "blue": ["#18203f", "#24386a", "#305a9a", "#4a86c8", "#78b8e8", "#bce4ff"],
    "pink": ["#5a1a3a", "#9a2e5a", "#d8508a", "#f888b0", "#ffc0d6"],
    "roof": ["#3a1424", "#681e2c", "#9a2e34", "#c84a3e", "#e8785a", "#ffaa80"],
    "mount": ["#3a3a6a", "#4e5288", "#6870a4", "#8a94c0", "#b4bedc", "#e4ecf8"],
    "forest": ["#152c3a", "#1e4040", "#2a5a46", "#3a7448"],
    "gold": ["#5a3a10", "#a06a14", "#e0a420", "#ffd84a", "#fff4a8", "#ffffff"],
    "brown": ["#2e1a1a", "#4a2a22", "#6e4030", "#956042", "#bc8a5c"],
}
RAMPS = {k: [hexc(c) for c in v] for k, v in RAMPS.items()}
OUT = RAMPS["outline"][0]

# Colour -> its darkest ramp tone, for selective outlines.
DARKEST = {}
for name, ramp in RAMPS.items():
    for c in ramp:
        DARKEST.setdefault(c, ramp[0])

BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0
LIGHT = np.array([-0.55, -0.7, 0.45])
LIGHT = LIGHT / np.linalg.norm(LIGHT)


def shade(ramp, v, x, y, dither=1.0):
    """Pick a ramp colour for brightness v (0..1) with ordered dithering."""
    n = len(ramp)
    f = v * (n - 1) + (BAYER[y % 4][x % 4] - 0.5) * dither
    return ramp[int(min(n - 1, max(0, round(f))))]


class Img:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.a = np.zeros((h, w, 4), np.uint8)

    def put(self, x, y, c):
        x, y = int(x), int(y)
        if 0 <= x < self.w and 0 <= y < self.h:
            self.a[y, x] = c

    def get(self, x, y):
        return tuple(int(v) for v in self.a[y, x])

    def solid(self, x, y):
        return 0 <= x < self.w and 0 <= y < self.h and self.a[y, x, 3] > 0

    def paste(self, other, ox, oy):
        for y in range(other.h):
            for x in range(other.w):
                if other.a[y, x, 3]:
                    self.put(ox + x, oy + y, other.a[y, x])

    def flipped(self):
        o = Img(self.w, self.h)
        o.a = self.a[:, ::-1].copy()
        return o

    def outline(self, color=None, selective=True):
        """Add a 1px outline. Selective: top edges take the darkest tone of
        the colour they border, everything else the true outline colour."""
        src = self.a.copy()
        solid = src[:, :, 3] > 0
        for y in range(self.h):
            for x in range(self.w):
                if solid[y, x]:
                    continue
                below = y + 1 < self.h and solid[y + 1, x]
                others = any(0 <= x + dx < self.w and 0 <= y + dy < self.h and solid[y + dy, x + dx]
                             for dx, dy in ((1, 0), (-1, 0), (0, -1)))
                if not below and not others:
                    continue
                if color is not None:
                    self.a[y, x] = color
                elif selective and below and not others:
                    self.a[y, x] = DARKEST.get(tuple(int(v) for v in src[y + 1, x]), OUT)
                else:
                    self.a[y, x] = OUT
        return self

    def save(self, path, scale=4):
        Image.fromarray(self.a, "RGBA").resize((self.w * scale, self.h * scale), Image.NEAREST).save(path)


# ---------------------------------------------------------------------------
# Painting primitives


def ball(img, cx, cy, rx, ry, ramp, spec=True, ambient=0.18, dither=1.0, tint=None):
    """Lit ellipsoid (fruit, bushes, clouds)."""
    for y in range(int(cy - ry - 1), int(cy + ry + 2)):
        for x in range(int(cx - rx - 1), int(cx + rx + 2)):
            nx, ny = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
            d = nx * nx + ny * ny
            if d > 1:
                continue
            nz = math.sqrt(1 - d)
            lam = max(0.0, nx * LIGHT[0] + ny * LIGHT[1] + nz * LIGHT[2])
            v = ambient + (1 - ambient) * lam
            v = v * 0.92 + 0.08 * (1 - d)
            r = ramp if tint is None else tint(x, y, ramp)
            c = shade(r, min(1, v), x, y, dither)
            if spec:
                rz = 2 * lam * nz - LIGHT[2]
                if rz > 0.965:
                    c = r[-1]
            img.put(x, y, c)


def leaf(img, x0, y0, x1, y1, width, ramp, vein=True, curl=0.0, edge=True):
    """Pointed leaf from (x0,y0) base to (x1,y1) tip, lit from the upper left."""
    ax, ay = x1 - x0, y1 - y0
    length = math.hypot(ax, ay) or 1
    ux, uy = ax / length, ay / length
    px_, py_ = -uy, ux
    xs = [x0, x1]
    ys = [y0, y1]
    for y in range(int(min(ys) - width - 2), int(max(ys) + width + 3)):
        for x in range(int(min(xs) - width - 2), int(max(xs) + width + 3)):
            rx, ry = x + 0.5 - x0, y + 0.5 - y0
            t = (rx * ux + ry * uy) / length
            if t < 0 or t > 1:
                continue
            s = rx * px_ + ry * py_ - curl * math.sin(math.pi * t) * width
            half = width * math.sin(math.pi * min(1, t * 1.15)) ** 0.7
            if abs(s) > half:
                continue
            side = (s / (half + 1e-6))
            facing = px_ * LIGHT[0] + py_ * LIGHT[1]
            v = 0.55 + 0.38 * side * (1 if facing > 0 else -1) + 0.3 * t - 0.12
            if vein and abs(s) < 0.6 and 0.08 < t < 0.85:
                v -= 0.32
            c = shade(ramp, max(0, min(1, v)), x, y)
            if edge and half - abs(s) < 0.85 and img.solid(x, y):
                c = ramp[1]  # dark rim where it overlaps other leaves
            img.put(x, y, c)


def line(img, x0, y0, x1, y1, c, width=1):
    n = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
    for i in range(n + 1):
        t = i / n
        x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
        for dx in range(width):
            img.put(round(x - width / 2 + dx + 0.5), round(y), c)


def stem(img, x0, y0, x1, y1, ramp, width=1, bend=0.0):
    """Slightly curved stalk, darker on its right side."""
    n = int(abs(y1 - y0) + abs(x1 - x0)) * 2 + 1
    for i in range(n + 1):
        t = i / n
        x = x0 + (x1 - x0) * t + bend * math.sin(math.pi * t)
        y = y0 + (y1 - y0) * t
        for dx in range(width):
            img.put(round(x - width / 2 + dx), round(y), ramp[2] if dx < width - 1 or width == 1 else ramp[1])


def mound(img, cx, by, w, h, ramp):
    ball(img, cx, by - h / 2, w / 2, h / 2, ramp, spec=False, ambient=0.35)


def poly(img, pts, c):
    ys = [p[1] for p in pts]
    for y in range(int(min(ys)), int(max(ys)) + 1):
        xs = []
        n = len(pts)
        for i in range(n):
            (xa, ya), (xb, yb) = pts[i], pts[(i + 1) % n]
            if (ya <= y + 0.5 < yb) or (yb <= y + 0.5 < ya):
                xs.append(xa + (y + 0.5 - ya) * (xb - xa) / (yb - ya))
        xs.sort()
        for i in range(0, len(xs) - 1, 2):
            for x in range(int(math.ceil(xs[i] - 0.5)), int(math.floor(xs[i + 1] - 0.5)) + 1):
                img.put(x, y, c(x, y) if callable(c) else c)


def sparkle(img, x, y, c):
    img.put(x, y, c)
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        img.put(x + dx, y + dy, c)


# ---------------------------------------------------------------------------
# Crops: 5 stages each (seed, sprout, young, grown, ripe), 22 px wide.

CW = 22
G, L = RAMPS["leaf"], RAMPS["leaf"]


def seed_stage(h=30):
    img = Img(CW, h)
    mound(img, 11, h - 1, 14, 5, RAMPS["soil"])
    for x, y in ((8, h - 4), (12, h - 5), (14, h - 3)):
        img.put(x, y, RAMPS["cream"][4])
        img.put(x + 1, y, RAMPS["cream"][2])
    return img.outline()


def sprout(h=30):
    img = Img(CW, h)
    mound(img, 11, h - 1, 12, 4, RAMPS["soil"])
    stem(img, 11, h - 3, 11, h - 8, G)
    leaf(img, 11, h - 8, 6, h - 12, 2.2, L, vein=False)
    leaf(img, 11, h - 8, 16, h - 13, 2.2, L, vein=False)
    return img.outline()


def turnip(stage):
    if stage == 0:
        return seed_stage()
    if stage == 1:
        return sprout()
    img = Img(CW, 30)
    big = {2: 0.6, 3: 1.0, 4: 1.0}[stage]
    base = 23
    if stage == 4:
        # bulb: purple shoulders fading to white
        def tint(x, y, r):
            return RAMPS["purple"] if y < base + 1 else RAMPS["cream"]
        ball(img, 11, base + 1.5, 6, 5, RAMPS["cream"], tint=tint)
        img.put(11, 29, RAMPS["cream"][2])
    for ang, ln in ((-58, 10), (36, 12), (-30, 14), (12, 15), (-8, 13)):
        a = math.radians(ang - 90)
        ln *= big
        leaf(img, 11, base - 1, 11 + math.cos(a) * ln, base - 1 + math.sin(a) * ln, 2.6 * (0.7 + 0.3 * big), L)
    return img.outline()


def carrot(stage):
    if stage == 0:
        return seed_stage()
    if stage == 1:
        return sprout()
    img = Img(CW, 30)
    k = 0.6 if stage == 2 else 1.0
    base = 25
    for ang in (-50, -32, -16, 0, 16, 32, 50):
        a = math.radians(ang - 90)
        ln = 17 * k * (1 - abs(ang) / 160)
        x1, y1 = 11 + math.cos(a) * ln, base + math.sin(a) * ln
        stem(img, 11, base, x1, y1, G)
        for i in range(2, int(ln), 2):  # feathery fronds
            t = i / ln
            fx, fy = 11 + math.cos(a) * ln * t, base + math.sin(a) * ln * t
            img.put(fx - 1, fy, shade(L, 0.75, int(fx), int(fy)))
            img.put(fx + 1, fy - 1, shade(L, 0.5, int(fx), int(fy)))
    if stage == 4:
        poly(img, [(7, base - 1), (15, base - 1), (12, 29), (10, 29)],
             lambda x, y: shade(RAMPS["orange"], 0.85 - (x - 7) * 0.07 - (y - base) * 0.03, x, y))
        for y in (base + 1, base + 3):
            img.put(9, y, RAMPS["orange"][1])
    else:
        mound(img, 11, 29, 10, 4, RAMPS["soil"])
    return img.outline()


def strawberry(stage):
    if stage == 0:
        return seed_stage()
    if stage == 1:
        return sprout()
    img = Img(CW, 30)
    k = 0.7 if stage == 2 else 1.0
    for cx, cy, r in ((6, 22, 4.5), (16, 22, 4.5), (11, 18, 5), (8, 15, 4), (14, 15, 4)):
        if k < 1 and cy < 17:
            continue
        ball(img, cx, cy + (1 - k) * 6, r * k, r * 0.85 * k, L, spec=False)
    for cx, cy in ((8, 19), (14, 19), (11, 14)):  # leaf veins
        if k == 1:
            img.put(cx, cy, L[1])
    if stage == 3:
        for fx, fy in ((6, 18), (15, 14), (12, 21)):
            for dx, dy in ((0, -1), (1, 0), (0, 1), (-1, 0)):
                img.put(fx + dx, fy + dy, RAMPS["cream"][5])
            img.put(fx, fy, RAMPS["yellow"][3])
    if stage == 4:
        for bx, by in ((5, 24), (11, 25), (17, 23), (14, 18)):
            ball(img, bx, by, 2.6, 3.0, RAMPS["red"])
            img.put(bx - 1, by - 3, L[3])
            img.put(bx + 1, by - 3, L[3])
            img.put(bx - 1, by, RAMPS["yellow"][4])
            img.put(bx + 1, by + 1, RAMPS["yellow"][4])
    return img.outline()


def tomato(stage):
    if stage == 0:
        return seed_stage(36)
    if stage == 1:
        return sprout(36)
    img = Img(CW, 36)
    top = {2: 18, 3: 4, 4: 4}[stage]
    # wooden stake
    for y in range(top - 1, 35):
        img.put(11, y, RAMPS["wood"][3])
        img.put(12, y, RAMPS["wood"][2])
    stem(img, 10, 34, 10, top + 2, G, bend=-1.5)
    for i, y in enumerate(range(top + 3, 32, 5)):
        side = -1 if i % 2 else 1
        leaf(img, 10, y + 2, 10 + side * 8, y - 2, 3.0, L)
    if stage >= 3:
        ramp = RAMPS["red"] if stage == 4 else RAMPS["leaf"]
        for tx, ty in ((5, 14), (16, 19), (6, 25), (15, 29)):
            ball(img, tx, ty, 3.2, 3.0, ramp, ambient=0.25)
            img.put(tx, ty - 3, L[2])
    return img.outline()


def pumpkin(stage):
    if stage == 0:
        return seed_stage()
    if stage == 1:
        return sprout()
    img = Img(CW, 30)
    k = 0.65 if stage == 2 else 1.0
    for cx, cy, r in ((5, 22, 5), (17, 21, 5), (11, 16, 6)):
        ball(img, cx, cy + (1 - k) * 8, r * k, r * 0.7 * k, L, spec=False)
        img.put(cx, int(cy + (1 - k) * 8), L[1])
    if stage == 4:
        cx, cy = 11, 24
        ball(img, cx, cy, 8.5, 5.5, RAMPS["orange"])
        for rx in (-4, 0, 4):  # ribs
            for y in range(cy - 4, cy + 5):
                x = cx + rx + (1 if rx > 0 and abs(y - cy) < 3 else (-1 if rx < 0 and abs(y - cy) < 3 else 0))
                if img.solid(x, y):
                    img.put(x, y, RAMPS["orange"][1] if rx >= 0 else RAMPS["orange"][2])
        stem(img, 11, cy - 5, 12, cy - 8, RAMPS["brown"], width=2)
    elif stage == 3:
        ball(img, 11, 25, 4, 3, RAMPS["leaf"])
    return img.outline()


def sunflower(stage):
    if stage == 0:
        return seed_stage(40)
    if stage == 1:
        return sprout(40)
    img = Img(CW, 40)
    top = {2: 22, 3: 9, 4: 9}[stage]
    stem(img, 11, 39, 11, top + 4, G, width=2, bend=0.8)
    for y, side in ((33, -1), (28, 1), (22, -1), (17, 1)):
        if y > top + 4:
            leaf(img, 11, y, 11 + side * 9, y - 3, 3.0, L)
    if stage == 3:
        ball(img, 11, top + 2, 4, 3.5, L)
        img.put(10, top - 1, L[4])
    elif stage == 4:
        cx, cy = 11, top + 1
        for i in range(14):
            a = i / 14 * math.tau
            leaf(img, cx + math.cos(a) * 3, cy + math.sin(a) * 3, cx + math.cos(a) * 9.5, cy + math.sin(a) * 9.5,
                 2.0, RAMPS["yellow"], vein=False)
        ball(img, cx, cy, 4.5, 4.5, RAMPS["brown"], spec=False)
        for (x, y) in ((cx - 1, cy - 1), (cx + 1, cy), (cx, cy + 2), (cx - 2, cy + 1), (cx + 2, cy - 2)):
            img.put(x, y, RAMPS["brown"][0])
    return img.outline()


CROPS = [
    # name, painter, seed cost, sell price, grow seconds, unlock price
    ("Turnip", turnip, 2, 6, 20, 0),
    ("Carrot", carrot, 5, 15, 30, 40),
    ("Strawberry", strawberry, 10, 32, 40, 150),
    ("Tomato", tomato, 20, 64, 55, 400),
    ("Pumpkin", pumpkin, 45, 140, 75, 1000),
    ("Sunflower", sunflower, 80, 300, 100, 2500),
]


# ---------------------------------------------------------------------------
# Produce icons (32x32) for the seed cards and harvest pops


def icon_turnip():
    img = Img(32, 32)
    for ang, ln in ((-35, 11), (0, 13), (35, 11)):
        a = math.radians(ang - 90)
        leaf(img, 16, 12, 16 + math.cos(a) * ln, 12 + math.sin(a) * ln, 3.4, L)

    def tint(x, y, r):
        return RAMPS["purple"] if y < 19 else RAMPS["cream"]
    ball(img, 16, 20, 10, 9, RAMPS["cream"], tint=tint)
    line(img, 16, 29, 16, 31, RAMPS["cream"][2])
    return img.outline()


def icon_carrot():
    img = Img(32, 32)
    for ang, ln in ((-40, 9), (-15, 11), (10, 10)):
        a = math.radians(ang - 60)
        stem(img, 21, 10, 21 + math.cos(a) * ln, 10 + math.sin(a) * ln, G, width=2)
    poly(img, [(14, 8), (26, 14), (8, 30), (6, 28)],
         lambda x, y: shade(RAMPS["orange"], 0.95 - 0.045 * (x + y - 20) * 0.5 - max(0, (x - y + 2)) * 0.0, x, y))
    for t in (0.3, 0.5, 0.7):  # rings
        x, y = 20 + (7 - 20) * t, 11 + (29 - 11) * t
        img.put(round(x), round(y), RAMPS["orange"][1])
        img.put(round(x) + 1, round(y), RAMPS["orange"][1])
    return img.outline()


def icon_strawberry():
    img = Img(32, 32)
    ball(img, 16, 18, 10, 10, RAMPS["red"])
    poly(img, [(7, 20), (25, 20), (16, 31)], lambda x, y: shade(RAMPS["red"], 0.55 - (y - 20) * 0.03 - (x - 16) * 0.02, x, y))
    for x, y in ((11, 15), (16, 13), (21, 15), (13, 20), (19, 20), (16, 25), (10, 21), (22, 21)):
        img.put(x, y, RAMPS["yellow"][4])
        img.put(x, y + 1, RAMPS["red"][1])
    for ang in (-150, -110, -70, -30):
        a = math.radians(ang)
        leaf(img, 16, 9, 16 + math.cos(a) * 7, 9 - math.sin(a) * -3 + 1, 2.0, L, vein=False)
    stem(img, 16, 9, 17, 4, G, width=2)
    return img.outline()


def icon_tomato():
    img = Img(32, 32)
    ball(img, 16, 18, 12, 10.5, RAMPS["red"])
    for a in range(5):
        ang = a / 5 * math.tau - math.pi / 2
        leaf(img, 16, 9, 16 + math.cos(ang) * 6, 9 + math.sin(ang) * 3, 1.6, L, vein=False, edge=False)
    stem(img, 16, 9, 16, 5, G, width=2)
    return img.outline()


def icon_pumpkin():
    img = Img(32, 32)
    ball(img, 16, 19, 14, 10, RAMPS["orange"])
    for rx in (-8, -3, 3, 8):
        for y in range(11, 28):
            dy = abs(y - 19) / 10
            x = 16 + rx * (1 - 0.25 * dy * dy)
            if img.solid(round(x), y):
                img.put(round(x), y, RAMPS["orange"][1] if rx > 0 else RAMPS["orange"][2])
    stem(img, 16, 10, 18, 4, RAMPS["brown"], width=3)
    leaf(img, 17, 8, 25, 5, 2.5, L)
    return img.outline()


def icon_sunflower():
    img = Img(32, 32)
    cx, cy = 16, 16
    for i in range(16):
        a = i / 16 * math.tau
        leaf(img, cx + math.cos(a) * 5, cy + math.sin(a) * 5, cx + math.cos(a) * 15, cy + math.sin(a) * 15, 2.8,
             RAMPS["yellow"], vein=False)
    ball(img, cx, cy, 7, 7, RAMPS["brown"], spec=False)
    for i in range(18):
        a = i * 2.39996
        r = 1.5 * math.sqrt(i)
        img.put(round(cx + math.cos(a) * r), round(cy + math.sin(a) * r), RAMPS["brown"][0])
    return img.outline()


ICONS = [icon_turnip, icon_carrot, icon_strawberry, icon_tomato, icon_pumpkin, icon_sunflower]

# ---------------------------------------------------------------------------
# The farmer (hand pixelled), 18x25

FARMER_PAL = {
    "o": OUT, "k": OUT,
    "H": RAMPS["yellow"][2], "h": RAMPS["yellow"][3], "J": RAMPS["yellow"][1],
    "r": RAMPS["red"][2],
    "b": RAMPS["brown"][2], "B": RAMPS["brown"][1],
    "s": RAMPS["skin"][3], "S": RAMPS["skin"][2], "t": RAMPS["skin"][4],
    "p": RAMPS["pink"][3],
    "w": RAMPS["cream"][4], "W": RAMPS["cream"][3],
    "u": RAMPS["blue"][3], "U": RAMPS["blue"][2], "V": RAMPS["blue"][4],
    "g": RAMPS["gold"][3],
    "n": RAMPS["wood"][2], "N": RAMPS["wood"][1],
}

HAT_FRONT = [
    "......oooooo......",
    "....ooHhhhhHoo....",
    "...oHhhhhhhhhHo...",
    "...oHHhhhhhhHHo...",
    ".ooJrrrrrrrrrrJoo.",
    "oJHHHHHHHHHHHHHHJo",
    ".ooJJJJJJJJJJJJoo.",
]
FACE_FRONT = [
    "...obbssssssbbo...",
    "...obskssssksbo...",
    "...ospssssssspo...",
    "...oSssskksssSo...",
    "....oSSssssSSo....",
]
FACE_BACK = [
    "...obbbbbbbbbbo...",
    "...obBbbbbbbBbo...",
    "...obbBbbbbBbbo...",
    "...oBbbbbbbbbBo...",
    "....oSSBBBBSSo....",
]
BODY_FRONT = [
    "...owwwwSSwwwwo...",
    "..owuuwwwwwwuuwo..",
    "..owuuuuuuuuuuwo..",
    "..osuuguuuuguuso..",
    "..oSuuuuVVuuuuSo..",
    "...oUuuuuuuuuUo...",
    "...oUUuuuuuuUUo...",
]
BODY_BACK = [
    "...owwwwwwwwwwo...",
    "..owuuwwwwwwuuwo..",
    "..owwuuwwwwuuwwo..",
    "..oswwuuuuuuwwso..",
    "..oSuuuuuuuuuuSo..",
    "...oUuuuuuuuuUo...",
    "...oUUuuuuuuUUo...",
]
LEGS_STAND = [
    "...ouuuuoouuuuo...",
    "...oUUUo..oUUUo...",
    "...onnno..onnno...",
    "...oNNNo..oNNNo...",
    "....ooo....ooo....",
    "..................",
]
LEGS_WALK = [
    "...ouuuuoouuuuo...",
    "...oUUUo..oUUUo...",
    "...onnno..oUUUo...",
    "...oNNNo..onnno...",
    "....ooo...oNNNo...",
    "...........ooo....",
]
SIDE = [
    ".....oooooo.......",
    "...ooHhhhhhHoo....",
    "..oHhhhhhhhhhHo...",
    "..oJrrrrrrrrrrJo..",
    "oJHHHHHHHHHHHHHHHo",
    ".ooJJJJJJJJJJJJJo.",
    "...obbbbbssssso...",
    "...obbbbsssksso...",
    "...obbbsssssspo...",
    "...oBbbssssskso...",
    "....oBSSSSSSo.....",
    ".....owwwwwwo.....",
    "....owuuwwwwwo....",
    "....owuuuuuuuo....",
    "....owuuguuuuo....",
    "....owuuuuuusso...",
    "....oUuuuuuuuo....",
    "....oUUuuuuuUo....",
    "....oUUUUUUUUo....",
]
SIDE_STAND = [
    "....oUUUooUUUo....",
    "....onnnooonnno...",
    "....oNNNNooNNNNo..",
    ".....oooo..oooo...",
    "..................",
    "..................",
]
SIDE_WALK = [
    "...oUUUo..oUUUo...",
    "..onnno....onnno..",
    "..oNNNNo...oNNNNo.",
    "...oooo.....oooo..",
    "..................",
    "..................",
]


def grid_img(rows):
    w = max(len(r) for r in rows)
    img = Img(w, len(rows))
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch in FARMER_PAL:
                img.put(x, y, FARMER_PAL[ch])
    return img


def farmer_frames():
    """front stand/walk, back stand/walk, side stand/walk (facing right)."""
    front = HAT_FRONT + FACE_FRONT + BODY_FRONT
    back = HAT_FRONT + FACE_BACK + BODY_BACK
    side = SIDE[:6] + SIDE[6:11] + SIDE[11:]
    frames = [
        grid_img(front + LEGS_STAND), grid_img(front + LEGS_WALK),
        grid_img(back + LEGS_STAND), grid_img(back + LEGS_WALK),
        grid_img(side + SIDE_STAND), grid_img(side + SIDE_WALK),
    ]
    for f in frames:
        for (k, v) in []:
            pass
    return frames


# ---------------------------------------------------------------------------
# Small props and UI icons


def watering_can():
    img = Img(16, 12)
    S = RAMPS["blue"]
    for y in range(4, 11):
        for x in range(4, 13):
            img.put(x, y, shade(S, 0.85 - (x - 4) * 0.06 - (y - 4) * 0.03, x, y))
    for x in range(5, 12):
        img.put(x, 4, S[4])
    line(img, 4, 7, 0, 3, S[2], 2)  # spout
    img.put(0, 2, S[4])
    img.put(1, 2, S[4])
    for x in range(6, 11):  # handle
        img.put(x, 1, S[1])
    img.put(5, 2, S[1])
    img.put(11, 2, S[1])
    img.put(5, 3, S[1])
    img.put(11, 3, S[1])
    return img.outline()


def seed_pouch():
    img = Img(12, 12)
    ball(img, 6, 7.5, 5, 4.5, RAMPS["brown"], spec=False, ambient=0.35)
    for x in range(4, 9):
        img.put(x, 3, RAMPS["brown"][3])
    img.put(5, 2, RAMPS["brown"][2])
    img.put(7, 2, RAMPS["brown"][2])
    img.put(6, 3, RAMPS["red"][3])
    return img.outline()


def basket():
    img = Img(16, 12)
    for y in range(5, 11):
        for x in range(2 + (y - 5) // 3, 14 - (y - 5) // 3):
            img.put(x, y, RAMPS["wood"][4] if (x + y) % 3 else RAMPS["wood"][3])
    for x in range(2, 14):
        img.put(x, 5, RAMPS["wood"][5])
    for x in range(4, 12):  # handle arc
        y = 4 - int(3 * math.sin((x - 4) / 7 * math.pi))
        img.put(x, y, RAMPS["wood"][2])
    return img.outline()


def coin(size=12):
    img = Img(size, size)
    r = size / 2 - 0.6
    ball(img, size / 2, size / 2, r, r, RAMPS["gold"], ambient=0.35)
    c = size // 2
    for y in range(c - size // 4, c + size // 4):
        img.put(c - 1 if size > 10 else c, y, RAMPS["gold"][1])
    return img.outline(OUT)


def droplet():
    img = Img(9, 12)
    B = RAMPS["blue"]
    ball(img, 4.5, 7.5, 3.6, 3.6, B, ambient=0.4)
    for y in range(1, 6):
        w = (y - 1) * 0.7
        for x in range(round(4.5 - w), round(4.5 + w) + 1):
            img.put(x, y, B[3] if x <= 4 else B[2])
    img.put(3, 6, B[5])
    img.put(3, 7, B[5])
    return img.outline(OUT)


def lock_icon():
    img = Img(12, 14)
    for y in range(6, 13):
        for x in range(1, 11):
            img.put(x, y, shade(RAMPS["gold"], 0.8 - (y - 6) * 0.07 - x * 0.02, x, y))
    for y in range(1, 6):
        img.put(3, y, RAMPS["stone"][3])
        img.put(8, y, RAMPS["stone"][2])
    for x in range(4, 8):
        img.put(x, 0, RAMPS["stone"][4])
    img.put(5, 8, OUT)
    img.put(6, 8, OUT)
    img.put(5, 9, OUT)
    img.put(5, 10, OUT)
    return img.outline(OUT)


def star():
    img = Img(7, 7)
    for i in range(7):
        img.put(3, i, RAMPS["gold"][4])
        img.put(i, 3, RAMPS["gold"][4])
    img.put(3, 3, RAMPS["gold"][5])
    for d in ((2, 2), (4, 2), (2, 4), (4, 4)):
        img.put(*d, RAMPS["gold"][3])
    return img


# ---------------------------------------------------------------------------
# Raised planter bed, 64x44: wooden frame around soil (dry or watered)


def planter(wet):
    img = Img(64, 44)
    W = RAMPS["wood"]
    soil = RAMPS["wet"] if wet else RAMPS["soil"]
    rnd = random.Random(7)
    # soil bed: three lit furrows running across the bed
    for y in range(6, 35):
        for x in range(4, 60):
            ph = ((y - 8) % 9) / 9.0  # 0 at ridge top -> 1 in the trough
            v = 0.82 - 0.75 * ph if ph < 0.75 else 0.1 + (ph - 0.75) * 1.6
            v += (rnd.random() - 0.5) * 0.12 - (y - 6) * 0.004
            img.put(x, y, shade(soil, max(0, min(1, v)), x, y, dither=0.8))
    if wet:
        for _ in range(22):  # glossy wet highlights along the ridges
            x, y = rnd.randint(6, 56), rnd.choice((8, 17, 26)) + rnd.randint(0, 1)
            img.put(x, y, RAMPS["blue"][1])
            img.put(x + 1, y, RAMPS["mount"][0])
    # inner shadow under the top rim
    for x in range(4, 60):
        img.put(x, 6, soil[0])
        img.put(x, 7, soil[0] if x % 2 else soil[1])
    # rims: top plank, side posts
    for x in range(0, 64):
        for y in range(2, 6):
            img.put(x, y, shade(W, 0.85 - (y - 2) * 0.12 + (0.05 if (x * 7) % 13 == 0 else 0), x, y))
        img.put(x, 2, W[5])
    for y in range(2, 35):
        for x in list(range(0, 4)) + list(range(60, 64)):
            img.put(x, y, shade(W, (0.75 if x < 4 else 0.5) - (x % 4) * 0.08, x, y))
    # front face: two planks
    for y in range(35, 44):
        for x in range(0, 64):
            band = 0 if y < 39 else 1
            v = 0.62 - band * 0.18 - (y - 35 - band * 4) * 0.05
            if (x + band * 23) % 31 == 0:
                v -= 0.25  # plank seams
            img.put(x, y, shade(W, v, x, y))
        img.put(0, y, W[1])
        img.put(63, y, W[0])
    for x in range(64):
        img.put(x, 35, W[4])
        img.put(x, 39, W[1])
    for x in (2, 61):  # nails
        img.put(x, 37, RAMPS["stone"][4])
        img.put(x, 41, RAMPS["stone"][3])
    return img.outline(OUT)


# ---------------------------------------------------------------------------
# The farm scene (400x240). The sky is left transparent: the game paints a
# live sky behind it. Light sources are exported for night-time glows.

RAMPS["hill"] = [hexc(c) for c in ["#22404e", "#2c5658", "#3a6c62", "#4e846a", "#6a9c72"]]
RAMPS["plaster"] = [hexc(c) for c in ["#8a7068", "#b49a88", "#d8c0a4", "#ecdcc0", "#fff4e0"]]
for _n in ("hill", "plaster"):
    for _c in RAMPS[_n]:
        DARKEST.setdefault(_c, RAMPS[_n][0])

BED_X = [48, 128, 208, 288]
BED_Y = [116, 168]
LIGHTS = {}


def ridge(img, base, amp, freqs, ramp, seed, snow=None, vmin=0.25, vmax=0.9):
    rnd = random.Random(seed)
    phases = [rnd.random() * 10 for _ in freqs]
    tops = []
    for x in range(-1, img.w + 1):
        h = sum(a * math.sin(x * f + p) for (f, a), p in zip(freqs, phases))
        tops.append(base - amp * h)
    for x in range(img.w):
        top = tops[x + 1]
        slope = tops[x + 2] - tops[x]
        for y in range(max(0, int(top)), img.h):
            depth = y - top
            v = vmax - depth * 0.012 + slope * 0.18
            v = max(vmin, min(vmax, v))
            c = shade(ramp, v, x, y)
            if snow is not None and top < snow and depth < (snow - top) * 0.55 + 1:
                c = shade(RAMPS["cream"], 0.75 + slope * 0.15, x, y)
            img.put(x, y, c)


def grass_field(img, y0, rnd):
    Gr = RAMPS["grass"]
    for y in range(y0, img.h):
        for x in range(img.w):
            v = 0.66 - (y - y0) * 0.0016 + (rnd.random() - 0.5) * 0.16
            v += 0.05 * math.sin(x * 0.05 + y * 0.11) * math.sin(y * 0.07)
            img.put(x, y, shade(Gr, max(0, min(1, v)), x, y, dither=1.2))
    # tufts: little "v" blades, bigger toward the front
    for _ in range(900):
        x, y = rnd.randint(0, img.w - 1), rnd.randint(y0 + 2, img.h - 1)
        k = 1 + (y - y0) // 60
        dark, light = Gr[2], Gr[5]
        for i in range(k + 1):
            img.put(x - i, y - i, dark)
            img.put(x + i, y - i, dark)
        img.put(x, y - 1, light)


def flowers(img, x0, y0, x1, y1, n, rnd, palette):
    for _ in range(n):
        x, y = rnd.randint(x0, x1), rnd.randint(y0, y1)
        c = rnd.choice(palette)
        img.put(x, y + 1, RAMPS["grass"][1])
        for dx, dy in ((0, -1), (1, 0), (0, 1), (-1, 0)):
            img.put(x + dx, y + dy, c[3])
        img.put(x, y, RAMPS["yellow"][4] if c is not RAMPS["yellow"] else RAMPS["orange"][3])


def dirt(img, x0, y0, x1, y1, rnd, edge=True):
    D = RAMPS["soil"]
    for y in range(y0, y1):
        for x in range(x0, x1):
            if edge and (x - x0 < 2 or x1 - x < 3) and rnd.random() < 0.5:
                continue
            v = 0.82 - (y - y0) / max(1, y1 - y0) * 0.2 + (rnd.random() - 0.5) * 0.2
            img.put(x, y, shade(D, v, x, y))
    for _ in range((x1 - x0) * (y1 - y0) // 40):  # pebbles
        x, y = rnd.randint(x0 + 2, x1 - 3), rnd.randint(y0, y1 - 1)
        img.put(x, y, RAMPS["stone"][3])
        img.put(x + 1, y, RAMPS["stone"][2])


def cottage(img):
    W, Ro, Pl, St = RAMPS["wood"], RAMPS["roof"], RAMPS["plaster"], RAMPS["stone"]
    rnd = random.Random(11)
    # chimney behind the roof
    for y in range(4, 34):
        for x in range(92, 106):
            brick = ((y // 3) % 2 * 3 + x) % 6 == 0 or y % 3 == 0
            img.put(x, y, St[1] if brick else shade(St, 0.72 - (x - 92) * 0.03, x, y))
    for x in range(90, 108):
        img.put(x, 3, St[4])
        img.put(x, 4, St[3])
        img.put(x, 5, St[1])
    LIGHTS["chimney"] = (99, 2)
    # walls
    for y in range(56, 100):
        for x in range(12, 115):
            img.put(x, y, shade(Pl, 0.72 - (y - 56) * 0.004 + (rnd.random() - 0.5) * 0.12, x, y))
    for x in (12, 13, 46, 47, 79, 80, 113, 114):
        for y in range(56, 100):
            img.put(x, y, W[1] if x % 2 else W[2])
    for y in (56, 57, 78, 79):
        for x in range(12, 115):
            img.put(x, y, W[1] if y % 2 else W[2])
    for i in range(10):  # diagonal braces
        img.put(14 + i * 3 // 1 - i * 2, 58 + i * 2, W[2])
    # roof: shingled trapezoid
    for y in range(16, 60):
        t = (y - 16) / 44
        xl, xr = 30 - t * 28, 96 + t * 28
        for x in range(int(xl), int(xr) + 1):
            row = (y - 16) // 5
            off = (row % 2) * 4
            v = 0.82 - t * 0.25 - (x - xl) / (xr - xl) * 0.25
            if (y - 16) % 5 == 4:
                v -= 0.35  # shingle shadow line
            elif (x + off) % 8 == 0:
                v -= 0.22
            img.put(x, y, shade(Ro, max(0, v), x, y))
        img.put(int(xl), y, OUT)
        img.put(int(xr) + 1, y, OUT)
    for x in range(30, 97):
        img.put(x, 15, Ro[5])
        img.put(x, 14, OUT)
    for x in range(2, 126):
        img.put(x, 60, OUT)
        img.put(x, 59, Ro[1])
    for x in range(12, 115):  # shadow under the eaves
        img.put(x, 61, Pl[0])
        img.put(x, 62, Pl[1])
    # stone foundation
    for y in range(98, 113):
        for x in range(10, 117):
            seam = (y - 98) % 5 == 0 or (x + ((y - 98) // 5) * 7) % 14 == 0
            img.put(x, y, St[1] if seam else shade(St, 0.62 + (rnd.random() - 0.5) * 0.2, x, y))
    # door with arched top
    for y in range(76, 109):
        for x in range(55, 73):
            if y < 82 and (x - 64) ** 2 + (y - 82) ** 2 * 2.2 > 85:
                continue
            plank = (x - 55) % 5 == 0
            img.put(x, y, W[1] if plank else shade(W, 0.55 - (x - 55) * 0.02, x, y))
    for y in range(75, 109):
        for x in (54, 73):
            img.put(x, y, OUT)
    img.put(69, 93, RAMPS["gold"][4])
    img.put(69, 94, RAMPS["gold"][2])
    for x in range(52, 76):  # step
        img.put(x, 109, St[4])
        img.put(x, 110, St[3])
        img.put(x, 111, St[2])
    # windows
    for wx in (20, 87):
        for y in range(68, 88):
            for x in range(wx, wx + 20):
                frame = x in (wx, wx + 19) or y in (68, 87) or x == wx + 9 or x == wx + 10 or y == 77
                if frame:
                    img.put(x, y, W[3] if y == 68 else W[1])
                else:
                    v = 0.4 + (1 - (y - 68) / 20) * 0.35 + (0.3 if (x - wx) - (y - 68) in (3, 4, 13) else 0)
                    img.put(x, y, shade(RAMPS["blue"], v, x, y))
        LIGHTS["window%d" % (0 if wx == 20 else 1)] = (wx + 10, 78)
        # flower box
        for y in range(88, 93):
            for x in range(wx - 2, wx + 22):
                img.put(x, y, shade(W, 0.6 - (y - 88) * 0.1, x, y))
        for i in range(8):
            fx = wx - 1 + i * 3
            c = (RAMPS["pink"], RAMPS["yellow"], RAMPS["red"])[i % 3]
            img.put(fx, 86, RAMPS["leaf"][3])
            img.put(fx + 1, 87, RAMPS["leaf"][2])
            img.put(fx, 85, c[3])
            img.put(fx + 1, 85, c[2])
            img.put(fx, 84, c[4])
    # lantern beside the door
    for y in range(70, 73):
        img.put(77, y, OUT)
    for y in range(73, 80):
        for x in range(75, 80):
            img.put(x, y, RAMPS["gold"][3] if 1 <= x - 75 <= 3 and 1 <= y - 73 <= 5 else OUT)
    LIGHTS["lantern"] = (77, 76)


def windmill(img):
    St, W, Ro = RAMPS["stone"], RAMPS["wood"], RAMPS["roof"]
    rnd = random.Random(5)
    for y in range(36, 113):
        t = (y - 36) / 76
        xl, xr = 344 - t * 14, 376 + t * 14
        for x in range(int(xl), int(xr) + 1):
            u = (x - xl) / (xr - xl)
            course = (y // 4) % 2
            seam = y % 4 == 0 or (x + course * 5) % 10 == 0
            v = 0.85 - u * 0.55 + (rnd.random() - 0.5) * 0.1
            img.put(x, y, St[1] if seam and u < 0.95 else shade(St, v, x, y))
        img.put(int(xl) - 1, y, OUT)
        img.put(int(xr) + 1, y, OUT)
    # door and window
    for y in range(92, 113):
        for x in range(352, 368):
            if y < 96 and (x - 360) ** 2 > (y - 92) * 16 + 4:
                continue
            img.put(x, y, OUT if x in (352, 367) else shade(W, 0.5 - (x - 352) * 0.02, x, y))
    for y in range(58, 68):
        for x in range(356, 364):
            img.put(x, y, OUT if x in (356, 363) or y in (58, 67) else RAMPS["blue"][1])
    LIGHTS["mill"] = (360, 63)
    # conical cap
    for y in range(18, 40):
        t = (y - 18) / 21
        half = 6 + t * 18
        for x in range(int(360 - half), int(360 + half) + 1):
            u = (x - (360 - half)) / (2 * half)
            v = 0.85 - u * 0.6 - t * 0.1 - (0.3 if (y - 18) % 6 == 5 else 0)
            img.put(x, y, shade(Ro, max(0, v), x, y))
        img.put(int(360 - half) - 1, y, OUT)
        img.put(int(360 + half) + 1, y, OUT)
    for x in range(334, 387):
        img.put(x, 40, OUT)
        img.put(x, 39, Ro[1])
    for x in range(354, 367):
        img.put(x, 17, OUT)
    LIGHTS["hub"] = (360, 34)


def fence(img, x0, x1, y):
    W = RAMPS["wood"]
    for rail in (y + 4, y + 10):
        for x in range(x0, x1):
            img.put(x, rail - 1, OUT)
            img.put(x, rail, W[4])
            img.put(x, rail + 1, W[2])
            img.put(x, rail + 2, OUT)
    for x in range(x0, x1, 16):
        for yy in range(y, y + 16):
            for dx in range(4):
                c = OUT if dx in (0, 3) or yy == y else (W[3] if dx == 1 else W[2])
                img.put(x + dx, yy, c)
        img.put(x + 1, y + 1, W[5])
        img.put(x + 2, y + 1, W[4])


def lamp_post(img, x, y):
    for yy in range(y, y + 48):
        img.put(x - 1, yy, OUT)
        img.put(x, yy, RAMPS["stone"][2])
        img.put(x + 1, yy, RAMPS["stone"][1])
        img.put(x + 2, yy, OUT)
    for yy in range(y - 10, y):
        for xx in range(x - 4, x + 6):
            edge = xx in (x - 4, x + 5) or yy in (y - 10, y - 1)
            img.put(xx, yy, OUT if edge else RAMPS["gold"][3 if yy < y - 5 else 2])
    for xx in range(x - 5, x + 7):
        img.put(xx, y - 11, OUT)
        img.put(xx, y - 12, RAMPS["stone"][1])
    for yy in range(y + 46, y + 49):
        for xx in range(x - 3, x + 5):
            img.put(xx, yy, OUT if yy == y + 48 or xx in (x - 3, x + 4) else RAMPS["stone"][2])
    LIGHTS["lamp"] = (x + 1, y - 5)


def scarecrow(img, x, y):
    W = RAMPS["wood"]
    for yy in range(y + 10, y + 64):  # pole
        img.put(x, yy, W[3])
        img.put(x + 1, yy, W[2])
        img.put(x - 1, yy, OUT)
        img.put(x + 2, yy, OUT)
    for xx in range(x - 16, x + 18):  # arms
        img.put(xx, y + 21, OUT)
        img.put(xx, y + 22, W[3])
        img.put(xx, y + 23, OUT)
    # plaid shirt
    for yy in range(y + 18, y + 40):
        for xx in range(x - 8, x + 10):
            if yy > y + 36 and (xx - x) % 4 == 0:
                continue
            plaid = (xx // 3 + yy // 3) % 2
            img.put(xx, yy, shade(RAMPS["red"], 0.55 + plaid * 0.2 - (xx - x) * 0.02, xx, yy))
    for xx in (x - 17, x + 18):  # straw hands
        for k in range(3):
            img.put(xx + (k - 1) * (1 if xx > x else -1), y + 20 + k, RAMPS["yellow"][3])
    # burlap head
    ball(img, x + 0.5, y + 11, 7, 7, RAMPS["plaster"], spec=False)
    img.put(x - 2, y + 10, OUT)
    img.put(x + 3, y + 10, OUT)
    for xx in range(x - 2, x + 4):
        img.put(xx, y + 14, RAMPS["brown"][1] if xx % 2 else OUT)
    # hat
    for xx in range(x - 10, x + 12):
        img.put(xx, y + 5, RAMPS["yellow"][2])
        img.put(xx, y + 6, OUT)
    for yy in range(y - 2, y + 5):
        for xx in range(x - 5, x + 7):
            img.put(xx, yy, shade(RAMPS["yellow"], 0.75 - (xx - x) * 0.05, xx, yy))
    for xx in range(x - 5, x + 7):
        img.put(xx, y + 3, RAMPS["red"][2])
    tmp = Img(img.w, img.h)  # outline just the scarecrow area
    return img


def hay_bale(img, x, y):
    """Round bale lying on its side: straw cylinder with a rolled end."""
    Y = RAMPS["yellow"][:5]
    w, h = 26, 20
    rx = 6  # end-face half width
    for yy in range(y, y + h):  # barrel
        v_row = 0.75 - abs((yy - y) - h * 0.35) / h * 0.9
        for xx in range(x + rx, x + w):
            v = v_row - (xx - x) * 0.012 + (0.12 if (xx * 5 + yy * 3) % 11 == 0 else 0)
            img.put(xx, yy, shade(Y, max(0.05, v), xx, yy))
    for xx in range(x + rx, x + w):  # binding twine
        for yy in range(y, y + h):
            if (xx - x - rx) in (7, 15):
                img.put(xx, yy, RAMPS["brown"][2])
    cx, cy = x + rx, y + h / 2
    for yy in range(y, y + h):  # end face with a spiral
        for xx in range(x, x + 2 * rx + 1):
            dx, dy = (xx + 0.5 - cx) / rx, (yy + 0.5 - cy) / (h / 2)
            if dx * dx + dy * dy <= 1:
                r = math.sqrt(dx * dx + dy * dy)
                ang = math.atan2(dy, dx)
                band = (r * 4 - ang / math.tau) % 1
                img.put(xx, yy, shade(Y, 0.85 - r * 0.3 - (0.35 if band < 0.3 else 0), xx, yy))


def bush(img, x, y, r, rnd):
    for _ in range(7):
        ball(img, x + rnd.uniform(-r, r), y + rnd.uniform(-r * 0.4, r * 0.3), r * rnd.uniform(0.5, 0.75),
             r * rnd.uniform(0.45, 0.6), RAMPS["leaf"], spec=False)


def paint_scene():
    rnd = random.Random(42)
    img = Img(400, 240)
    far = Img(400, 240)
    ridge(far, 58, 20, [(0.011, 1.0), (0.031, 0.45), (0.083, 0.12)], RAMPS["mount"], 1, snow=48, vmin=0.4, vmax=0.95)
    img.paste(far, 0, 0)
    hills = Img(400, 240)
    ridge(hills, 86, 7, [(0.017, 1.0), (0.05, 0.4)], RAMPS["hill"], 2, vmin=0.2, vmax=0.85)
    img.paste(hills, 0, 0)
    # forest line
    for x in range(-6, 406, 7):
        r = rnd.uniform(5, 9)
        ball(img, x + rnd.uniform(-2, 2), 94 - rnd.uniform(0, 6), r, r * 0.9, RAMPS["forest"], spec=False, ambient=0.3)
    grass_field(img, 98, rnd)
    # worn walkways in front of the beds and a path to the door
    dirt(img, 40, 159, 360, 168, rnd)
    dirt(img, 40, 211, 360, 226, rnd)
    dirt(img, 54, 108, 76, 168, rnd)
    flowers(img, 0, 172, 40, 236, 40, rnd, [RAMPS["pink"], RAMPS["yellow"], RAMPS["cream"], RAMPS["purple"]])
    flowers(img, 360, 116, 399, 150, 14, rnd, [RAMPS["yellow"], RAMPS["cream"]])
    flowers(img, 120, 112, 330, 116, 20, rnd, [RAMPS["pink"], RAMPS["yellow"], RAMPS["cream"]])
    fence(img, 126, 318, 98)
    windmill(img)
    cottage(img)
    b = Img(400, 240)
    bush(b, 8, 108, 10, rnd)
    bush(b, 122, 106, 8, rnd)
    bush(b, 18, 222, 12, rnd)
    bush(b, 392, 232, 11, rnd)
    b.outline()
    img.paste(b, 0, 0)
    lamp_post(img, 22, 136)
    s2 = Img(400, 240)
    scarecrow(s2, 376, 140)
    hay_bale(s2, 366, 210)
    hay_bale(s2, 344, 220)
    s2.outline()
    img.paste(s2, 0, 0)
    return img


# ---------------------------------------------------------------------------
# Pixel font: proportional, 7px caps, lowercase with descenders (9 rows).
# Each glyph: list of rows (top = row 0); missing rows are blank.

FONT = {
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "B": ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
    "C": [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
    "D": ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "G": [".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"],
    "H": ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "I": ["###", ".#.", ".#.", ".#.", ".#.", ".#.", "###"],
    "J": ["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."],
    "K": ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "M": ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
    "N": ["#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    "Q": [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "V": ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
    "W": ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."],
    "X": ["#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"],
    "Y": ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
    "Z": ["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
    "a": ["....", "....", ".###", "...#", ".###", "#..#", ".###"],
    "b": ["#...", "#...", "###.", "#..#", "#..#", "#..#", "###."],
    "c": ["...", "...", ".##", "#..", "#..", "#..", ".##"],
    "d": ["...#", "...#", ".###", "#..#", "#..#", "#..#", ".###"],
    "e": ["....", "....", ".##.", "#..#", "####", "#...", ".###"],
    "f": ["..#", ".#.", "###", ".#.", ".#.", ".#.", ".#."],
    "g": ["....", "....", ".###", "#..#", "#..#", "#..#", ".###", "...#", "###."],
    "h": ["#...", "#...", "###.", "#..#", "#..#", "#..#", "#..#"],
    "i": ["#", ".", "#", "#", "#", "#", "#"],
    "j": ["..#", "...", "..#", "..#", "..#", "..#", "..#", "..#", "##."],
    "k": ["#...", "#...", "#..#", "#.#.", "##..", "#.#.", "#..#"],
    "l": ["#.", "#.", "#.", "#.", "#.", "#.", ".#"],
    "m": [".....", ".....", "####.", "#.#.#", "#.#.#", "#.#.#", "#.#.#"],
    "n": ["....", "....", "###.", "#..#", "#..#", "#..#", "#..#"],
    "o": ["....", "....", ".##.", "#..#", "#..#", "#..#", ".##."],
    "p": ["....", "....", "###.", "#..#", "#..#", "#..#", "###.", "#...", "#..."],
    "q": ["....", "....", ".###", "#..#", "#..#", "#..#", ".###", "...#", "...#"],
    "r": ["...", "...", "#.#", "##.", "#..", "#..", "#.."],
    "s": ["....", "....", ".###", "#...", ".##.", "...#", "###."],
    "t": [".#.", ".#.", "###", ".#.", ".#.", ".#.", "..#"],
    "u": ["....", "....", "#..#", "#..#", "#..#", "#..#", ".###"],
    "v": ["...", "...", "#.#", "#.#", "#.#", "#.#", ".#."],
    "w": [".....", ".....", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."],
    "x": ["...", "...", "#.#", "#.#", ".#.", "#.#", "#.#"],
    "y": ["....", "....", "#..#", "#..#", "#..#", "#..#", ".###", "...#", "###."],
    "z": ["....", "....", "####", "...#", ".##.", "#...", "####"],
    "0": [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."],
    "1": [".#.", "##.", ".#.", ".#.", ".#.", ".#.", "###"],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    "3": [".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###."],
    "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
    "5": ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
    "6": [".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."],
    "7": ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
    "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
    "9": [".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."],
    ".": [".", ".", ".", ".", ".", ".", "#"],
    ",": ["..", "..", "..", "..", "..", "..", ".#", "#."],
    ":": [".", ".", "#", ".", ".", ".", "#"],
    "!": ["#", "#", "#", "#", "#", ".", "#"],
    "?": [".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."],
    "'": ["#", "#"],
    "-": ["...", "...", "...", "###"],
    "+": [".....", ".....", "..#..", "..#..", "#####", "..#..", "..#.."],
    "/": ["....#", "...#.", "...#.", "..#..", ".#...", ".#...", "#...."],
    "(": [".#", "#.", "#.", "#.", "#.", "#.", ".#"],
    ")": ["#.", ".#", ".#", ".#", ".#", ".#", "#."],
    "%": ["##..#", "##.#.", "...#.", "..#..", ".#...", ".#.##", "#..##"],
    "&": [".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#"],
    "x": ["...", "...", "#.#", "#.#", ".#.", "#.#", "#.#"],
    ">": ["#..", ".#.", "..#", ".#.", "#.."],
    "<": ["..#", ".#.", "#..", ".#.", "..#"],
}
FONT[">"] = ["...", "#..", ".#.", "..#", ".#.", "#.."]
FONT["<"] = ["...", "..#", ".#.", "#..", ".#.", "..#"]


def font_tables():
    widths = [0] * 128
    rows = [[0] * 9 for _ in range(128)]
    widths[ord(" ")] = 3
    for ch, g in FONT.items():
        w = max(len(r) for r in g)
        widths[ord(ch)] = w
        for y, r in enumerate(g):
            for x, c in enumerate(r):
                if c == "#":
                    rows[ord(ch)][y] |= 1 << x
    return widths, rows


def text_img(text, scale=1, color=(255, 255, 255, 255)):
    widths, rows = font_tables()
    w = sum(widths[ord(c)] + 1 for c in text) - 1
    img = Img(w * scale, 9 * scale)
    x = 0
    for c in text:
        for y in range(9):
            for i in range(widths[ord(c)]):
                if rows[ord(c)][y] >> i & 1:
                    for sy in range(scale):
                        for sx in range(scale):
                            img.put((x + i) * scale + sx, y * scale + sy, color)
        x += widths[ord(c)] + 1
    return img


def logo():
    """'Tiny Harvest' title: gradient letters, thick outline, drop shadow, sprout."""
    words = text_img("Tiny Harvest", 4)
    pad = 6
    img = Img(words.w + pad * 2, words.h + pad * 2)
    mask = words.a[:, :, 3] > 0
    Y, O = RAMPS["yellow"], RAMPS["orange"]
    h = words.h
    # shadow
    for y in range(words.h):
        for x in range(words.w):
            if mask[y, x]:
                img.put(x + pad + 3, y + pad + 4, RAMPS["brown"][1])
    # fill with a vertical gradient and a glossy top band
    for y in range(words.h):
        for x in range(words.w):
            if not mask[y, x]:
                continue
            t = y / (28)  # cap height ~28px at 4x
            if t < 0.3:
                c = shade(Y, 0.95 - t, x, y)
            else:
                c = shade(O, 0.95 - (t - 0.3) * 0.9, x, y)
            if y > 0 and not mask[y - 1, x]:
                c = Y[5]
            img.put(x + pad, y + pad, c)
    img.outline(OUT)
    img.outline(OUT)
    # sprout growing out of the "i"
    sx = pad + (text_img("T").w + 1) * 4 + 1
    leaf(img, sx + 2, pad + 1, sx - 6, pad - 6 + 2, 3.0, RAMPS["leaf"])
    leaf(img, sx + 2, pad + 1, sx + 10, pad - 5, 3.0, RAMPS["leaf"])
    return img


# ---------------------------------------------------------------------------
# Bottom screen: wooden board + parchment card


def wood_board(w, h, seed=1):
    img = Img(w, h)
    W = RAMPS["wood"]
    rnd = random.Random(seed)
    for y in range(h):
        plank = y // 16
        pv = rnd.Random() if False else None
        for x in range(w):
            ly = y % 16
            v = 0.6 + 0.08 * math.sin(x * 0.07 + plank * 3 + math.sin(x * 0.013 + plank) * 4) + (plank % 3) * 0.05
            v += (rnd.random() - 0.5) * 0.06
            if ly == 0:
                v = 0.05
            elif ly == 1:
                v += 0.25
            elif ly == 15:
                v -= 0.2
            if (x + plank * 53) % 97 == 0 and ly > 1:
                v -= 0.35  # butt joints
            img.put(x, y, shade(W, max(0, min(1, v)), x, y))
    return img


def parchment(w, h, seed=2, ramp=None):
    img = Img(w, h)
    P = ramp or RAMPS["plaster"]
    rnd = random.Random(seed)
    for y in range(h):
        for x in range(w):
            e = min(x, y, w - 1 - x, h - 1 - y)
            v = 0.84 + (rnd.random() - 0.5) * 0.04 - max(0, 4 - e) * 0.07
            img.put(x, y, shade(P, v, x, y, dither=0.35))
    for x in range(1, w - 1):  # border
        img.put(x, 0, OUT)
        img.put(x, h - 1, OUT)
        img.put(x, h - 2, P[1])
    for y in range(1, h - 1):
        img.put(0, y, OUT)
        img.put(w - 1, y, OUT)
        img.put(w - 2, y, P[2])
    for x in range(1, w - 1):
        img.put(x, 1, P[4])
    return img


def contact_sheet(imgs, cols, path, scale=4, bg=(120, 170, 90, 255)):
    cw = max(i.w for i in imgs) + 4
    ch = max(i.h for i in imgs) + 4
    rows = (len(imgs) + cols - 1) // cols
    sheet = Img(cw * cols, ch * rows)
    sheet.a[:, :] = bg
    for k, im in enumerate(imgs):
        sheet.paste(im, (k % cols) * cw + 2 + (cw - 4 - im.w) // 2, (k // cols) * ch + ch - 2 - im.h)
    sheet.save(path, scale)


# ---------------------------------------------------------------------------
# Export


def all_images():
    """(C name, image) for every asset the game uses, in a fixed order."""
    out = [("SCENE", paint_scene()), ("PLANTER_DRY", planter(False)), ("PLANTER_WET", planter(True))]
    for c, (name, painter, *_) in enumerate(CROPS):
        for st in range(5):
            out.append(("CROP_%d_%d" % (c, st), painter(st)))
    for c, f in enumerate(ICONS):
        out.append(("ICON_%d" % c, f()))
    fr = farmer_frames()
    for n, im in zip(("FRONT0", "FRONT1", "BACK0", "BACK1", "SIDE0", "SIDE1"), fr):
        out.append(("FARMER_" + n, im))
    out += [("CAN", watering_can()), ("POUCH", seed_pouch()), ("BASKET", basket()), ("COIN", coin()),
            ("COIN_BIG", coin(16)), ("DROP", droplet()), ("LOCK", lock_icon()), ("STAR", star()),
            ("LOGO", logo()), ("BOARD", wood_board(320, 240)), ("CARD", parchment(98, 80)),
            ("HEADER", parchment(304, 32, 5)), ("FOOTER", parchment(304, 26, 6))]
    return out


def export():
    images = all_images()
    palette = [(0, 0, 0, 0)]
    index = {}
    pixels = bytearray()
    table = []
    for name, im in images:
        table.append((name, im.w, im.h, len(pixels)))
        for y in range(im.h):
            for x in range(im.w):
                r, g, b, a = (int(v) for v in im.a[y, x])
                if a == 0:
                    pixels.append(0)
                    continue
                key = (r, g, b)
                if key not in index:
                    index[key] = len(palette)
                    palette.append((r, g, b, 255))
                pixels.append(index[key])
    assert len(palette) <= 256, "too many colours: %d" % len(palette)
    ncolors = len(palette)
    palette += [(0, 0, 0, 0)] * (256 - len(palette))
    widths, rows = font_tables()

    h = ["// Generated by tools/make_art.py - do not edit.", "#pragma once", "#include <stdint.h>", "",
         "typedef struct {", "    uint16_t w, h;", "    uint32_t offset;", "} ArtImage;", "", "enum {"]
    h += ["    IMG_%s," % n for n, *_ in table]
    h += ["    IMG_COUNT", "};", "",
          "#define IMG_CROP(crop, stage) (IMG_CROP_0_0 + (crop) * 5 + (stage))",
          "#define NUM_CROP_KINDS %d" % len(CROPS), "#define NUM_BEDS %d" % (len(BED_X) * len(BED_Y)),
          "#define BED_COLS %d" % len(BED_X), "", "static const int BED_X[] = {%s};" % ", ".join(map(str, BED_X)),
          "static const int BED_Y[] = {%s};" % ", ".join(map(str, BED_Y)), ""]
    for k, (x, y) in sorted(LIGHTS.items()):
        h.append("#define LIGHT_%s_X %d" % (k.upper(), x))
        h.append("#define LIGHT_%s_Y %d" % (k.upper(), y))
    h += ["", "extern const uint32_t art_palette[%d];" % len(palette), "extern const ArtImage art_images[IMG_COUNT];",
          "extern const uint8_t art_pixels[%d];" % len(pixels), "extern const uint8_t font_width[128];",
          "extern const uint16_t font_rows[128][9];", ""]
    with open(os.path.join(ROOT, "source", "art.h"), "w") as f:
        f.write("\n".join(h))

    c = ["// Generated by tools/make_art.py - do not edit.", '#include "art.h"', ""]
    c.append("const uint32_t art_palette[%d] = {" % len(palette))
    for i in range(0, len(palette), 8):
        c.append("    " + ", ".join("0x%02X%02X%02X%02Xu" % (p_[3], p_[0], p_[1], p_[2]) for p_ in palette[i:i + 8]) + ",")
    c += ["};", "", "const ArtImage art_images[IMG_COUNT] = {"]
    c += ["    {%d, %d, %d}, // %s" % (w, hh, off, n) for n, w, hh, off in table]
    c += ["};", "", "const uint8_t art_pixels[%d] = {" % len(pixels)]
    for i in range(0, len(pixels), 40):
        c.append("    " + ",".join(str(v) for v in pixels[i:i + 40]) + ",")
    c += ["};", "", "const uint8_t font_width[128] = {"]
    c.append("    " + ",".join(str(w) for w in widths))
    c += ["};", "", "const uint16_t font_rows[128][9] = {"]
    for r in rows:
        c.append("    {" + ",".join(str(v) for v in r) + "},")
    c += ["};", ""]
    with open(os.path.join(ROOT, "source", "art.c"), "w") as f:
        f.write("\n".join(c))
    print("exported %d images, %d colours, %d KB of pixels" % (len(table), ncolors, len(pixels) // 1024))


def banner_and_icon():
    """HOME Menu banner (256x128) and app icon (48x48) for the .cia/.3dsx."""
    sc = paint_scene()
    b = Img(256, 128)
    for y in range(128):
        t = min(1.0, y / 100)
        b.a[y, :] = (int(90 + 100 * t), int(160 + 70 * t), int(232 + 18 * t), 255)
    crop = Img(256, 128)
    crop.a = sc.a[0:128, 72:328].copy()
    b.paste(crop, 0, 0)
    for k, bx in enumerate((BED_X[0] - 72, BED_X[1] - 72, BED_X[2] - 72, BED_X[3] - 72)):
        b.paste(planter(True), bx, BED_Y[0])
        for j, (px_, py_) in enumerate(((10, 24), (32, 24), (0, 35), (21, 35), (42, 35))):
            cr = CROPS[(k + 2) % len(CROPS)][1](4)
            b.paste(cr, bx + px_, BED_Y[0] + py_ - cr.h)
    lg = logo()
    b.paste(lg, (256 - lg.w) // 2, 18)
    Image.fromarray(b.a, "RGBA").convert("RGB").save(os.path.join(ROOT, "cia", "banner.png"))

    ic = Img(48, 48)
    for y in range(48):
        for x in range(48):
            r = 6
            cxr = min(max(x, r), 47 - r)
            cyr = min(max(y, r), 47 - r)
            if (x - cxr) ** 2 + (y - cyr) ** 2 > r * r:
                continue
            if y < 30:
                t = y / 30
                ic.put(x, y, (int(110 + 90 * t), int(180 + 50 * t), 240, 255))
            else:
                ic.put(x, y, shade(RAMPS["grass"], 0.7 - (y - 30) * 0.02, x, y))
    tur = icon_turnip()
    ic.paste(tur, 8, 9)
    ic.outline(OUT)
    Image.fromarray(ic.a, "RGBA").save(os.path.join(ROOT, "icon.png"))


if __name__ == "__main__":
    os.makedirs(os.path.join(ROOT, "art"), exist_ok=True)
    export()
    banner_and_icon()
    crops = [painter(s) for _, painter, *_ in CROPS for s in range(5)]
    contact_sheet(crops, 5, os.path.join(ROOT, "art", "preview_crops.png"))
    misc = [f() for f in ICONS] + farmer_frames() + [watering_can(), seed_pouch(), basket(), coin(), coin(16),
                                                     droplet(), lock_icon(), star()]
    contact_sheet(misc, 6, os.path.join(ROOT, "art", "preview_misc.png"))
    contact_sheet([planter(False), planter(True)], 2, os.path.join(ROOT, "art", "preview_planter.png"))
    sc = paint_scene()
    preview = Img(400, 240)
    for y in range(240):  # a daytime sky behind it, for the preview
        t = min(1.0, y / 120)
        preview.a[y, :] = (int(110 + 90 * t), int(170 + 50 * t), 240, 255)
    preview.paste(sc, 0, 0)
    for i, bx in enumerate(BED_X):
        for by in BED_Y:
            pl = planter((i + by) % 2 == 0)
            preview.paste(pl, bx, by)
            for k in range(3):
                cr = CROPS[(i + by // 50) % 6][1](4 if k != 1 else 3)
                preview.paste(cr, bx + 0 + k * 21, by + 32 - cr.h)
    f = farmer_frames()[2]
    preview.paste(f, 150, 142)
    preview.save(os.path.join(ROOT, "art", "preview_scene.png"), 2)
    ui = wood_board(320, 240)
    ui.paste(parchment(98, 78), 8, 44)
    lg = logo()
    ui.paste(lg, (320 - lg.w) // 2, 140)
    ui.paste(text_img("The quick brown fox jumps over 12 lazy dogs! Price: 45c, (3/4)", 1), 4, 4)
    ui.paste(text_img("ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789", 1), 4, 18)
    ui.save(os.path.join(ROOT, "art", "preview_ui.png"), 2)
