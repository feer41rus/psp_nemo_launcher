#!/usr/bin/env python3
"""Nemo Launcher logo (variant G, chosen by the author 2026-10-25; the "N" replaced by the NCL "C"
glyph in dark, 2026-10-27): accent square with the glyph, "Nemo" white, "Launcher" grey.
Writes the large master, the icon, the small screen asset and the README logo.

Usage: make_nemo_logo.py FONT_DIR OUT_DIR ASSET_PNG GLYPH_PNG [README_PNG]
"""
import sys
from PIL import Image, ImageDraw, ImageFont

ACC, WHITE, MUT, DARK = (111, 176, 255), (233, 238, 245), (140, 155, 176), (15, 20, 28)
GLYPH = Image.open(sys.argv[4]).getchannel("A")  # glyph shape as an alpha mask


def logo(scale):
    s = scale
    bold = ImageFont.truetype(sys.argv[1] + "/Inter-SemiBold.ttf", 40 * s)
    reg = ImageFont.truetype(sys.argv[1] + "/Inter-Regular.ttf", 26 * s)
    w = 80 * s + 20 * s + int(max(bold.getlength("Nemo"), reg.getlength("Launcher"))) + 4 * s
    im = Image.new("RGBA", (w, 80 * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, 80 * s - 1, 80 * s - 1), radius=18 * s, fill=ACC)
    gw = 62 * s                                   # glyph width, centred in the square
    gh = round(GLYPH.height * gw / GLYPH.width)
    gm = GLYPH.resize((gw, gh), Image.LANCZOS)
    im.paste(Image.new("RGBA", gm.size, DARK + (255,)), ((80 * s - gw) // 2, (80 * s - gh) // 2), gm)
    d.text((100 * s, 22 * s), "Nemo", font=bold, fill=WHITE, anchor="lm")
    d.text((101 * s, 62 * s), "Launcher", font=reg, fill=MUT, anchor="lm")
    return im


def scaled(h):
    return master.resize((round(master.width * h / master.height), h), Image.LANCZOS)


master = logo(8)
master.save(sys.argv[2] + "/nemo_logo.png")
master.crop((0, 0, 80 * 8, 80 * 8)).save(sys.argv[2] + "/nemo_icon.png")
scaled(44).save(sys.argv[3])
if len(sys.argv) > 5:
    scaled(120).save(sys.argv[5])
print("nemo logo:", master.size, "asset height 44")
