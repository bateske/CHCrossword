"""Draw the close-up's letters: capitals rasterized from DejaVu Serif Bold.

    python tools/tilefont.py [--ttf PATH] [--size 12]

Writes tools/art/tilefont.txt (which tools/assets.py packs, and which can
be touched up by hand afterwards: this only needs running again to start
over from the typeface). DejaVu is freely redistributable (see NOTICE).
Each glyph there: a line "= A", then its rows from the capitals' top, '#'
set and '.' clear. Q's tail runs below the baseline; J's hook is brought
up to stand on it.
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
FONTS = ["C:/Windows/Fonts/DejaVuSerif-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
         "/Library/Fonts/DejaVuSerif-Bold.ttf"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ttf")
    ap.add_argument("--size", type=int, default=12)
    a = ap.parse_args()
    path = a.ttf or next((f for f in FONTS if Path(f).exists()), None)
    if not path:
        raise SystemExit("DejaVuSerif-Bold.ttf not found: pass --ttf")
    font = ImageFont.truetype(path, a.size)
    base = 20

    def ink(ch):
        im = Image.new("1", (40, 40), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text((10, base), ch, font=font, fill=1, anchor="ls")
        return im

    top = ink("H").getbbox()[1]                      # the capitals' top row
    out = ["# The close-up's letters (tools/tilefont.py: DejaVu Serif Bold, size %d)." % a.size,
           "# Rows from the capitals' top; the baseline is row %d." % (base - top), ""]
    for ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        im = ink(ch)
        x0, y0, x1, y1 = im.getbbox()
        out.append(f"= {ch}")
        rows = ["".join("#" if im.getpixel((x, y)) else "." for x in range(x0, x1)) for y in range(top, y1)]
        if ch == "J":                                # its hook brought up to the baseline: a tile has no room below
            rows = rows[:base - top - 2] + rows[-2:]
        out += rows
        out.append("")
    (HERE / "art" / "tilefont.txt").write_text("\n".join(out), newline="\n")
    print("tools/art/tilefont.txt written")


if __name__ == "__main__":
    main()
