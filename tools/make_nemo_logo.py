#!/usr/bin/env python3
"""Nemo Launcher logo (variant G, chosen by the author 2026-10-25): accent square with "N",
"Nemo" white, "Launcher" grey. Writes the large master, the icon and the small screen asset.

Usage: make_nemo_logo.py FONT_DIR OUT_DIR ASSET_PNG
"""
import sys
from PIL import Image, ImageDraw, ImageFont

ACC, WHITE, MUT, DARK = (111, 176, 255), (233, 238, 245), (140, 155, 176), (15, 20, 28)


def logo(scale):
    s = scale
    bold = ImageFont.truetype(sys.argv[1] + "/Inter-SemiBold.ttf", 40 * s)
    reg = ImageFont.truetype(sys.argv[1] + "/Inter-Regular.ttf", 26 * s)
    big = ImageFont.truetype(sys.argv[1] + "/Inter-SemiBold.ttf", 60 * s)
    w = 80 * s + 20 * s + int(max(bold.getlength("Nemo"), reg.getlength("Launcher"))) + 4 * s
    im = Image.new("RGBA", (w, 80 * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, 80 * s - 1, 80 * s - 1), radius=18 * s, fill=ACC)
    d.text((40 * s, 40 * s), "N", font=big, fill=DARK, anchor="mm")
    d.text((100 * s, 22 * s), "Nemo", font=bold, fill=WHITE, anchor="lm")
    d.text((101 * s, 62 * s), "Launcher", font=reg, fill=MUT, anchor="lm")
    return im


master = logo(8)
master.save(sys.argv[2] + "/nemo_logo.png")
master.crop((0, 0, 80 * 8, 80 * 8)).save(sys.argv[2] + "/nemo_icon.png")
h = 44
master.resize((round(master.width * h / master.height), h), Image.LANCZOS).save(sys.argv[3])
print("nemo logo:", master.size, "asset height", h)
