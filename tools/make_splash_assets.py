#!/usr/bin/env python3
"""Splash screen pictures (1.2.3), pre-scaled so the PSP never scales big textures:
assets/splash_c.png   - the Nemo "C" glyph, white with alpha (tinted in code), 40 px high;
assets/splash_ncl.png - the full NCL logo (white N and L, red C), 160 px wide."""
import os
import sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GLYPH = os.path.join(ROOT, "assets/branding/logo_c_glyph.png")
NCL = os.path.join(ROOT, "..", "Logo", "Logo full ncl.png")


def fit(im, w=None, h=None):
    im = im.crop(im.getbbox())
    if w:
        h = round(im.height * w / im.width)
    else:
        w = round(im.width * h / im.height)
    return im.resize((w, h), Image.LANCZOS)


def main():
    c = fit(Image.open(GLYPH).convert("RGBA"), h=40)
    white = Image.new("RGBA", c.size, (255, 255, 255, 0))
    white.putalpha(c.getchannel("A"))
    white.save(os.path.join(ROOT, "assets/splash_c.png"), optimize=True)
    fit(Image.open(NCL).convert("RGBA"), w=160).save(os.path.join(ROOT, "assets/splash_ncl.png"), optimize=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
