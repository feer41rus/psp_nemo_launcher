#!/usr/bin/env python3
"""README collage (assets/branding/collage_ru.png / collage_en.png) from Nemo screenshots.

Screenshots (480x272, from SELECT+START on the PSP) go to docs/collage/ under the names in SHOTS
(name_ru.png / name_en.png for the two collages, name.png for both);
cc.png is made by build/render_cc_preview over a game screenshot. A missing screenshot is cut from the
previous collage (assets/branding/collage_ru.png) when it has that screen, else left as a dark frame.
    python3 tools/make_collage.py                    # draft into build/collage/
    python3 tools/make_collage.py assets/branding    # the README images
"""
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "docs", "collage")
FONT_R = os.path.join(ROOT, "assets/fonts/src/Inter-Regular.ttf")
FONT_B = os.path.join(ROOT, "assets/fonts/src/Inter-SemiBold.ttf")
LOGO = os.path.join(ROOT, "assets/branding/nemo_logo_readme.png")
OLD = os.path.join(SRC, "old_collage_ru.png")   # the 1.1.14 collage: fallback for missing screenshots
W, H = 1680, 1500
ACCENT = (111, 176, 255)

# name: (file in docs/collage, box in the old collage to fall back on or None)
SHOTS = {
    "home": ("home.png", (642, 92, 1598, 632)),
    "card": ("card.png", (602, 726, 1078, 994)),
    "dlc": ("dlc.png", None),
    "batinfo": ("batinfo.png", None),
    "cc": ("cc.png", None),
    "music": ("music.png", (82, 1068, 558, 1336)),
    "eq": ("eq.png", None),
    "battery": ("battery.png", None),
}

TEXT = {
    "ru": {
        "subtitle": "Современный лаунчер для PSP на ARK-5",
        "about": ["Заменяет «Custom Launcher» ARK и не трогает", "саму прошивку: игры, плагины и обновления",
                  "остаются за ARK, а Nemo делает для них", "удобный дом."],
        "bullets": ["Главная и Библиотека с обложками", "PSP ISO/CSO, PSN, homebrew, PS1 и UMD",
                    "Музыка: MP3, FLAC, эквалайзер", "Control Center по HOME в играх",
                    "Установка и удаление DLC", "Аккумулятор: состояние и тест",
                    "Wi‑Fi, FTP-сервер и USB", "Настройки XMB и ARK, разгон"],
        "home": "Главная", "card": "Карточка игры", "dlc": "Карточка игры и DLC", "cc": "Control Center в игре",
        "music": "Музыка: сейчас играет", "eq": "Эквалайзер", "battery": "Тест аккумулятора",
        "batinfo": "Подробнее об аккумуляторе",
        "footer": "PSP 1000 / 2000 / 3000 / Go / E1000 · прошивка 6.61 + ARK-5 · GPL-3.0",
    },
    "en": {
        "subtitle": "A modern launcher for PSP on ARK-5",
        "about": ["Replaces ARK's \"Custom Launcher\" and leaves", "the firmware alone: games, plugins and updates",
                  "stay with ARK, and Nemo gives them", "a comfortable home."],
        "bullets": ["Home and Library with covers", "PSP ISO/CSO, PSN, homebrew, PS1 and UMD",
                    "Music: MP3, FLAC, equalizer", "Control Center on HOME in games",
                    "Install and delete DLC", "Battery: status and capacity test",
                    "Wi-Fi, FTP server and USB", "XMB and ARK settings, overclock"],
        "home": "Home", "card": "Game card", "dlc": "Game card and DLC", "cc": "Control Center in a game",
        "music": "Music: now playing", "eq": "Equalizer", "battery": "Battery test",
        "batinfo": "Battery details",
        "footer": "PSP 1000 / 2000 / 3000 / Go / E1000 · firmware 6.61 + ARK-5 · GPL-3.0",
    },
}

GRID = ["dlc", "cc", "music", "eq", "battery", "batinfo"]


def shot(name, lang, old):
    path, box = SHOTS[name]
    stem, ext = os.path.splitext(path)
    for p in (os.path.join(SRC, f"{stem}_{lang}{ext}"), os.path.join(SRC, path)):
        if os.path.exists(p):
            return Image.open(p).convert("RGB").resize((480, 272), Image.LANCZOS)
    if box and old is not None:
        return old.crop(box).resize((480, 272), Image.LANCZOS)
    return Image.new("RGB", (480, 272), (20, 27, 38))


def rounded(im, r):
    m = Image.new("L", im.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, im.size[0] - 1, im.size[1] - 1), r, fill=255)
    out = Image.new("RGBA", im.size)
    out.paste(im, (0, 0), m)
    return out


def tile(canvas, im, x, y, w, h, caption, font):
    d = ImageDraw.Draw(canvas)
    shadow = Image.new("RGBA", (w + 40, h + 40), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((20, 24, w + 20, h + 24), 18, fill=(0, 0, 0, 120))
    canvas.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(10)), (x - 20, y - 20))
    canvas.alpha_composite(rounded(im.resize((w, h), Image.LANCZOS).convert("RGBA"), 16), (x, y))
    d.rounded_rectangle((x - 1, y - 1, x + w, y + h), 16, outline=(255, 255, 255, 230), width=3)
    d.text((x + 4, y + h + 18), caption, font=font, fill=(235, 240, 248))


def background():
    bg = Image.new("RGBA", (W, H))
    top, mid, bot = (15, 19, 30), (22, 28, 48), (38, 28, 62)
    px = bg.load()
    for yy in range(H):
        t = yy / (H - 1)
        a, b, k = (top, mid, t * 2) if t < 0.5 else (mid, bot, (t - 0.5) * 2)
        c = tuple(int(a[i] + (b[i] - a[i]) * k) for i in range(3)) + (255,)
        for xx in range(W):
            px[xx, yy] = c
    return bg


def build(lang, old):
    t = TEXT[lang]
    c = background()
    d = ImageDraw.Draw(c)
    f = lambda path, size: ImageFont.truetype(path, size)
    logo = Image.open(LOGO).convert("RGBA")
    icon = logo.crop((0, 0, 120, 120)).resize((120, 120), Image.LANCZOS)
    c.alpha_composite(icon, (80, 100))
    d.text((232, 98), "Nemo", font=f(FONT_B, 64), fill=(255, 255, 255))
    d.text((232, 170), "Launcher", font=f(FONT_R, 40), fill=(200, 210, 225))
    d.text((80, 262), t["subtitle"], font=f(FONT_B, 25), fill=ACCENT)
    for i, line in enumerate(t["about"]):
        d.text((80, 314 + i * 31), line, font=f(FONT_R, 21), fill=(200, 208, 220))
    for i, b in enumerate(t["bullets"]):
        y = 462 + i * 30
        d.ellipse((80, y + 8, 92, y + 20), fill=ACCENT)
        d.text((104, y), b, font=f(FONT_R, 21), fill=(235, 240, 248))
    cap = f(FONT_B, 21)
    tile(c, shot("home", lang, old), 642, 92, 956, 540, t["home"], cap)
    for i, name in enumerate(GRID):
        x, y = 82 + (i % 3) * 520, 760 + (i // 3) * 340
        tile(c, shot(name, lang, old), x, y, 476, 268, t[name], cap)
    foot = f(FONT_R, 20)
    tw = d.textlength(t["footer"], font=foot)
    d.text(((W - tw) / 2, H - 58), t["footer"], font=foot, fill=(150, 160, 178))
    return c.convert("RGB")


def main():
    import sys
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "collage")   # assets/branding when final
    os.makedirs(out_dir, exist_ok=True)
    old = Image.open(OLD).convert("RGB") if os.path.exists(OLD) else None
    for lang in TEXT:
        im = build(lang, old)
        out = os.path.join(out_dir, f"collage_{lang}.png")
        im.save(out, optimize=True)
        print(out)


if __name__ == "__main__":
    main()
