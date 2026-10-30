#!/usr/bin/env python3
"""Nemo's Library backgrounds (1.3.8), drawn by code: no photos or film frames. Their meaning is in
assets/backgrounds/README.md. Renders at 2x (960x544), adds the matte coat, saves 480x272 JPEGs:
    python3 tools/make_library_bgs.py assets/backgrounds          # the six pictures Nemo embeds
    python3 tools/make_library_bgs.py out --raw                   # also the uncoated 2x renders
"""
import math, random, sys, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

W, H = 960, 544
OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)


def grad(c1, c2, vertical=True):
    t = np.linspace(0, 1, H if vertical else W)
    a, b = np.array(c1, float), np.array(c2, float)
    g = a[None, :] * (1 - t[:, None]) + b[None, :] * t[:, None]
    if vertical:
        return np.repeat(g[:, None, :], W, axis=1)
    return np.repeat(g[None, :, :], H, axis=0)


def radial(cx, cy, r, color, power=2.0):
    y, x = np.mgrid[0:H, 0:W]
    d = np.sqrt((x - cx) ** 2 + (y - cy) ** 2) / r
    k = np.clip(1 - d, 0, 1) ** power
    return k[:, :, None] * np.array(color, float)[None, None, :]


def glow_layer(draw_fn, blur=(0, 3, 12, 30), weights=(1.0, 0.8, 0.6, 0.5)):
    base = Image.new("RGB", (W, H), (0, 0, 0))
    draw_fn(ImageDraw.Draw(base))
    acc = np.zeros((H, W, 3), float)
    for b, w in zip(blur, weights):
        im = base.filter(ImageFilter.GaussianBlur(b)) if b else base
        acc += np.asarray(im, float) * w
    return acc


def to_img(arr):
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))


def grain(arr, amount=6, seed=1):
    rng = np.random.default_rng(seed)
    return arr + rng.normal(0, amount, arr.shape[:2])[:, :, None]


def vignette(arr, strength=0.55):
    y, x = np.mgrid[0:H, 0:W]
    d = np.sqrt(((x - W / 2) / (W / 2)) ** 2 + ((y - H / 2) / (H / 2)) ** 2) / math.sqrt(2)
    return arr * (1 - strength * d ** 2)[:, :, None]


def branch_paths(seed, start, length, spread, depth, step=6):
    """One path that keeps forking: every choice opens a new road."""
    rnd = random.Random(seed)
    paths = []

    def walk(x, y, ang, d, n):
        pts = [(x, y)]
        for i in range(n):
            ang += rnd.uniform(-0.05, 0.05)
            ang *= 0.97                      # keep the roads inside the frame
            if y < 60: ang += 0.04
            if y > H - 60: ang -= 0.04
            x += math.cos(ang) * step
            y += math.sin(ang) * step
            pts.append((x, y))
            if d < depth and i > 6 and rnd.random() < 0.07:
                walk(x, y, ang + rnd.uniform(-spread, spread), d + 1, int(n * rnd.uniform(0.5, 0.9)) - i)
        paths.append((pts, d))

    walk(*start, 0.0, 0, length)
    return paths


# 1. "Ветвящиеся судьбы": one line of light from the left forks into many futures.
def v_branches():
    bg = grad((10, 14, 34), (22, 10, 40), vertical=False)
    bg += radial(120, 300, 520, (60, 40, 120), 1.6)
    paths = branch_paths(7, (40, 300), 170, 0.6, 6)
    cols = [(255, 220, 160), (140, 200, 255), (255, 120, 160), (200, 160, 255), (120, 255, 220)]

    def draw(d):
        for pts, depth in paths:
            c = cols[depth % len(cols)]
            f = max(0.35, 1 - depth * 0.12)
            d.line(pts, fill=tuple(int(v * f) for v in c), width=max(1, 4 - depth // 2))
            for p in pts[-1:]:
                d.ellipse([p[0] - 4, p[1] - 4, p[0] + 4, p[1] + 4], fill=c)

    bg += glow_layer(draw) * 0.9
    return vignette(grain(bg, 4))


# 2. "Перрон": the station of choices — rails splitting into many tracks toward a dawn horizon.
def v_rails():
    bg = grad((18, 20, 44), (60, 34, 52))
    bg += radial(W / 2, 250, 700, (120, 70, 60), 2.4)
    hy = 250  # horizon

    def draw(d):
        rnd = random.Random(3)
        n = 13
        for i in range(n):
            t = (i - (n - 1) / 2) / ((n - 1) / 2)
            x_far = W / 2 + t * 40
            x_near = W / 2 + t * 1100
            # each track leaves the main line at its own distance: a fork
            fork = rnd.uniform(0.15, 0.6)
            pts = []
            for k in range(60):
                s = k / 59
                yy = hy + (H - hy) * s ** 1.6
                if s < fork:
                    xx = W / 2 + (x_far - W / 2) * 0.2 + t * 60 * s
                else:
                    u = (s - fork) / (1 - fork)
                    xx = x_far + (x_near - x_far) * (u ** 1.8) * s
                pts.append((xx, yy))
            warm = abs(t) < 0.2
            col = (255, 200, 140) if warm else (120, 150, 230)
            d.line(pts, fill=col, width=2)
        for k in range(60):  # sleepers glow on the horizon
            x = rnd.uniform(0, W)
            d.ellipse([x, hy - 1, x + 2, hy + 1], fill=(255, 210, 170))
        d.line([(0, hy), (W, hy)], fill=(255, 180, 140), width=1)

    bg += glow_layer(draw, weights=(0.9, 0.7, 0.5, 0.4))
    return vignette(grain(bg, 4))


# 3. "Три жизни": red, blue and yellow — three possible lives as soft overlapping orbits.
def v_three_lives():
    bg = grad((12, 12, 22), (20, 16, 30))
    bg += radial(250, 260, 420, (150, 30, 50), 1.4)
    bg += radial(520, 300, 420, (30, 70, 170), 1.4)
    bg += radial(760, 240, 400, (170, 130, 30), 1.4)

    def draw(d):
        for cx, cy, col in ((250, 260, (255, 90, 110)), (520, 300, (110, 170, 255)), (760, 240, (255, 210, 110))):
            for r in range(60, 330, 34):
                d.ellipse([cx - r, cy - r * 0.62, cx + r, cy + r * 0.62], outline=tuple(int(v * 0.55) for v in col),
                          width=1)
        d.line([(0, 280), (W, 270)], fill=(240, 230, 220), width=1)

    bg += glow_layer(draw, weights=(0.7, 0.6, 0.5, 0.3))
    return vignette(grain(bg, 5))


# 4. "Лист на ветру": possibilities as drifting points of light, a single brighter one — Nemo.
def v_particles():
    bg = grad((8, 18, 30), (14, 30, 40))
    bg += radial(700, 160, 600, (30, 90, 110), 1.5)
    rnd = random.Random(11)

    def draw(d):
        for _ in range(420):
            x, y = rnd.uniform(0, W), rnd.uniform(0, H)
            r = rnd.choice((1, 1, 1, 2, 2, 3, 5))
            c = rnd.choice(((120, 220, 230), (180, 240, 255), (90, 160, 200), (255, 220, 170)))
            f = rnd.uniform(0.3, 1.0)
            d.ellipse([x - r, y - r, x + r, y + r], fill=tuple(int(v * f) for v in c))
        # a drifting trail
        pts = [(80 + i * 8, 420 - 140 * math.sin(i / 18) - i * 1.4) for i in range(100)]
        d.line(pts, fill=(255, 225, 180), width=2)
        x, y = pts[-1]
        d.ellipse([x - 7, y - 7, x + 7, y + 7], fill=(255, 240, 210))

    bg += glow_layer(draw, weights=(0.9, 0.8, 0.6, 0.45))
    return vignette(grain(bg, 4))


# 5. "Марс 2092": a red planet horizon under stars with constellations of choices.
def v_mars():
    bg = grad((6, 8, 20), (30, 12, 20))
    rnd = random.Random(5)
    y, x = np.mgrid[0:H, 0:W]
    cx, cy, R = W * 0.62, H + 900, 1220
    d = np.sqrt((x - cx) ** 2 + (y - cy) ** 2)
    planet = (d < R)[:, :, None]
    tex = np.asarray(Image.fromarray((np.random.default_rng(2).random((68, 120)) * 255).astype(np.uint8)).resize(
        (W, H), Image.BICUBIC).filter(ImageFilter.GaussianBlur(6)), float) / 255
    pcol = np.stack([150 + 70 * tex, 50 + 30 * tex, 30 + 20 * tex], -1)
    shade = np.clip((R - d) / 260, 0, 1)[:, :, None]
    bg = bg * (1 - planet) + planet * pcol * (0.35 + 0.65 * shade)
    rim = np.exp(-((d - R) / 10) ** 2)[:, :, None] * np.array((255, 140, 90))[None, None, :]
    bg += rim * 0.8 + radial(cx, H - 60, 600, (90, 30, 20), 2)

    def draw(dr):
        stars = [(rnd.uniform(0, W), rnd.uniform(0, H * 0.6)) for _ in range(260)]
        for sx, sy in stars:
            f = rnd.uniform(0.3, 1)
            dr.point((sx, sy), fill=(int(255 * f), int(240 * f), int(230 * f)))
        nodes = [(120, 90), (230, 140), (330, 80), (420, 160), (540, 110), (300, 210), (480, 230), (640, 60)]
        edges = [(0, 1), (1, 2), (1, 5), (2, 3), (3, 4), (3, 6), (4, 7), (5, 6)]
        for a, b in edges:
            dr.line([nodes[a], nodes[b]], fill=(140, 170, 255), width=1)
        for nx, ny in nodes:
            dr.ellipse([nx - 3, ny - 3, nx + 3, ny + 3], fill=(220, 230, 255))

    bg += glow_layer(draw, blur=(0, 2, 8), weights=(1.0, 0.6, 0.4))
    return vignette(grain(bg, 4), 0.4)


# 6. "Лига": many controller paths — a quiet grid of crossing lines with a few lit intersections.
def v_grid():
    bg = grad((12, 14, 24), (16, 20, 34))
    bg += radial(300, 200, 700, (40, 50, 110), 1.6)
    rnd = random.Random(21)

    def draw(d):
        for i in range(-20, 40):
            x0 = i * 48
            d.line([(x0, 0), (x0 + 300, H)], fill=(50, 60, 100), width=1)
            d.line([(x0 + 300, 0), (x0, H)], fill=(40, 48, 84), width=1)
        for _ in range(16):
            i, j = rnd.randint(0, 22), rnd.randint(0, 11)
            x = i * 48 - 150 + j * 24
            y = j * 45
            c = rnd.choice(((255, 120, 150), (120, 200, 255), (255, 220, 140), (160, 255, 200)))
            d.ellipse([x - 4, y - 4, x + 4, y + 4], fill=c)
        # one walked route through the grid
        pts, x, y = [], 40, 500
        for k in range(18):
            pts.append((x, y))
            x += 48
            y += -45 if rnd.random() < 0.7 else 45
        d.line(pts, fill=(255, 230, 190), width=2)

    bg += glow_layer(draw, weights=(0.9, 0.7, 0.5, 0.4))
    return vignette(grain(bg, 4))


def matte(img):
    """Frosted matte coat: blur, pull toward the UI's dark blue, soften contrast, add fine grain."""
    a = np.asarray(img.filter(ImageFilter.GaussianBlur(5)), float)
    ui = np.array((14, 19, 27), float)
    a = a * 0.55 + ui * 0.45
    lum = a.mean(axis=2, keepdims=True)
    a = lum + (a - lum) * 0.85
    a = grain(a, 3, seed=9)
    return to_img(a)


# The order is the LibBg enum in src/core/libbg.h.
VARIANTS = [
    ("branches", v_branches), ("rails", v_rails), ("three_lives", v_three_lives),
    ("particles", v_particles), ("mars", v_mars), ("grid", v_grid),
]
for name, fn in VARIANTS:
    raw = to_img(fn())
    if "--raw" in sys.argv:
        raw.save(f"{OUT}/{name}_raw.png")
    matte(raw).resize((480, 272), Image.LANCZOS).save(f"{OUT}/{name}.jpg", quality=90, optimize=True)
    print(name)
