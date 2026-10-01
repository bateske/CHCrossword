"""CHCrossword asset pipeline.

    python tools/assets.py         # build src/assets/*, write previews to build/assets/

Sources, all in tools/art/:
  * hand.png, else hand.txt: the pointing glove.
  * font.txt: the display font (the title, banners, headings).
  * tilefont.txt: the close-up's letters (from tools/tilefont.py).

Outputs:
  src/assets/Assets.h / Assets.cpp   - generated, do not edit
  build/assets/*.png                 - previews on the real palette
"""
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
ART = HERE / "art"
GEN = ART / "gen"
OUT_H = ROOT / "src" / "assets" / "Assets.h"
OUT_C = ROOT / "src" / "assets" / "Assets.cpp"
PREVIEW = ROOT / "build" / "assets"

# Must match src/gfx/Palette.cpp.
PALETTE = [0x000, 0xFFF, 0x042, 0x173, 0x4B5, 0xBBC, 0xE12, 0x702,
           0xFC2, 0x741, 0x26E, 0x125, 0xFB8, 0x6EF, 0xF0F, 0xFC2]
# Letters used in tools/art/*.txt. ' ' / '.' = transparent.
LETTER = {"k": 0, "w": 1, "d": 2, "f": 3, "g": 4, "s": 5, "r": 6, "m": 7,
          "y": 8, "b": 9, "u": 10, "n": 11, "p": 12, "c": 13, "x": 14, "z": 15}
TRANSPARENT = 16
NAMES = ["INK", "WHITE", "FELT_DK", "FELT", "FELT_LT", "SILVER", "RED", "WINE",
         "GOLD", "WOOD", "BLUE", "NAVY", "SKIN", "CYAN", "FX_A", "FX_B"]


def rgb(i):
    c = PALETTE[i]
    return ((c >> 8) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17)


def save_png(path, img):
    """Rows of palette indices -> a palette-exact RGBA PNG (what load_png reads)."""
    h, w = len(img), len(img[0])
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for y in range(h):
        for x in range(w):
            if img[y][x] != TRANSPARENT:
                im.putpixel((x, y), rgb(img[y][x]) + (255,))
    path.parent.mkdir(parents=True, exist_ok=True)
    im.save(path)


def load_png(path):
    """Palette-exact PNG -> rows of palette indices; alpha 0 is transparent."""
    lut = {rgb(i): i for i in range(15)}          # FX_B (15) shares GOLD's colour
    im = Image.open(path).convert("RGBA")
    rows = []
    for y in range(im.height):
        row = []
        for x in range(im.width):
            r, g, b, a = im.getpixel((x, y))
            if a == 0:
                row.append(TRANSPARENT)
            elif (r, g, b) in lut and a == 255:
                row.append(lut[(r, g, b)])
            else:
                raise SystemExit(f"{path.name} ({x},{y}): #{r:02X}{g:02X}{b:02X} alpha {a} is not a palette colour")
        rows.append(row)
    return rows


def load_art(name):
    """tools/art/<name>.txt: palette letters, one row per line; '#' starts a comment line.
    Several images may follow each other, separated by a blank line."""
    imgs, cur = [], []
    for ln in (ART / f"{name}.txt").read_text().splitlines():
        if ln.startswith("#"):
            continue
        if not ln.strip():
            if cur:
                imgs.append(cur)
                cur = []
            continue
        cur.append(ln.rstrip())
    if cur:
        imgs.append(cur)
    out = []
    for rows in imgs:
        w = max(len(r) for r in rows)
        out.append([[TRANSPARENT if ch in " ." else LETTER[ch] for ch in r.ljust(w)] for r in rows])
    return out


def source(name):
    """tools/art/<name>.png if someone has drawn one, else the letters in <name>.txt."""
    png = ART / f"{name}.png"
    return load_png(png) if png.exists() else load_art(name)[0]


def load_font():
    """tools/art/font.txt -> {char: {"w", "top", "rows"}} (rows of 0/1, all-clear rows trimmed)."""
    font, cur = {}, None
    for ln in (ART / "font.txt").read_text().splitlines():
        if not ln.strip() or ln.startswith("# "):           # (glyph rows are only '#' and '.')
            continue
        if ln.startswith("= "):
            parts = ln[2:].split()
            cur = {"top": int(parts[1]) if len(parts) > 1 else 0, "rows": []}
            font[parts[0]] = cur
            continue
        cur["rows"].append([1 if ch == "#" else 0 for ch in ln.rstrip()])
    for c, g in font.items():
        w = max(len(r) for r in g["rows"])
        g["rows"] = [r + [0] * (w - len(r)) for r in g["rows"]]
        while g["rows"] and not any(g["rows"][0]):
            g["rows"].pop(0)
            g["top"] += 1
        while g["rows"] and not any(g["rows"][-1]):
            g["rows"].pop()
        g["w"] = w
    return font


def pack_font(font):
    """The glyphs one after another, each a three-byte header - its
    character, width, top row << 4 | rows - then its rows' bits packed
    tightly (MSB first); a zero byte ends them. (The game finds a glyph by
    walking the list: no index to carry.)"""
    data = []
    for c in sorted(font):
        g = font[c]
        data += [ord(c), g["w"], (g["top"] << 4) | len(g["rows"])]
        bits = [v for row in g["rows"] for v in row]
        for i in range(0, len(bits), 8):
            chunk = bits[i:i + 8] + [0] * (8 - len(bits[i:i + 8]))
            data.append(sum(b << (7 - k) for k, b in enumerate(chunk)))
    return data + [0]


def pack_span4(img, trans=TRANSPARENT):
    """Colour image -> w, h, then per row: n, then n bytes of (len-1)<<4 | colour
    (colour 15 = skip). Trailing transparency is implicit."""
    h, w = len(img), len(img[0])
    out = [w, h]
    for row in img:
        runs, x = [], 0
        while x < w:
            c = row[x]
            s = x
            while x < w and row[x] == c and x - s < 16:
                x += 1
            runs.append((x - s, c))
        while runs and runs[-1][1] == trans:
            runs.pop()
        out.append(len(runs))
        for n, c in runs:
            out.append(((n - 1) << 4) | (15 if c == trans else c))
    return out


def remapped(img, table):
    return [[v if v == TRANSPARENT else table[v] for v in row] for row in img]


def preview(name, imgs, scale=8, bg=3):
    """Images side by side on the felt."""
    h = max(len(i) for i in imgs)
    w = sum(len(i[0]) + 2 for i in imgs) + 2
    im = Image.new("RGB", (w, h + 4), rgb(bg))
    x0 = 2
    for img in imgs:
        for y in range(len(img)):
            for x in range(len(img[0])):
                if img[y][x] != TRANSPARENT:
                    im.putpixel((x0 + x, 2 + y), rgb(img[y][x]))
        x0 += len(img[0]) + 2
    im.resize((w * scale, (h + 4) * scale), Image.NEAREST).save(PREVIEW / f"{name}.png")


def c_array(name, data, per_line=20):
    lines = [f"const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), per_line):
        lines.append("    " + ", ".join(str(v) for v in data[i:i + per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    PREVIEW.mkdir(parents=True, exist_ok=True)
    decls, defs = [], []
    total = 0
    # The pointing hand (span4).
    hand = source("hand")
    data = pack_span4(hand)
    defs.append(c_array("HAND", data))
    tip = [x for x, v in enumerate(hand[-1]) if v != TRANSPARENT]
    # (Turned over, for pointing at the far side from below, by drawing it
    # with a negative scale: no second copy.)
    decls.append(f"extern const uint8_t HAND[{len(data)}];                            // span4, fingertip on the bottom row\n"
                 f"constexpr uint8_t HAND_TIP = {(tip[0] + tip[-1]) // 2};                           // its column")
    total += len(data)
    preview("hand", [hand, hand[::-1]])
    # ... and turned a quarter turn, pointing right: the title menu's.
    side = [[hand[c][r] for c in range(len(hand))] for r in range(len(hand[0]))]
    data = pack_span4(side)
    defs.append(c_array("HAND_SIDE", data))
    decls.append(f"extern const uint8_t HAND_SIDE[{len(data)}];                       // the glove pointing right, {len(side[0])} x {len(side)}")
    total += len(data)

    # The display font.
    font = load_font()
    data = pack_font(font)
    defs.append(c_array("FONT", data))
    decls.append(f"extern const uint8_t FONT[{len(data)}];                         // the display font (tools/art/font.txt): per glyph its\n"
                 "                                                            // character, width, top << 4 | rows, the rows' bits; 0 ends")
    total += len(data)

    # The close-up's letters, one after another from A: width | rows << 8,
    # the rows of ink, then the rows of half ink (the leftmost pixel in bit 15).
    glyphs, cur = {}, None
    for ln in (ART / "tilefont.txt").read_text().splitlines():
        if ln.startswith("= "):
            cur = glyphs.setdefault(ln[2], [])
        elif ln.strip() and not ln.startswith("# "):
            cur.append(ln.strip())
    words = []
    for ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        rows = glyphs[ch]
        w = max(len(r) for r in rows)
        assert w <= 16 and len(rows) <= 11, ch
        words.append(w | len(rows) << 8)
        for mark in "#+":
            words += [sum(1 << (15 - i) for i, v in enumerate(r) if v == mark) for r in rows]
    defs.append(f"const uint16_t TILEFONT[{len(words)}] = {{\n" +
                "".join("    " + ", ".join(f"0x{v:04X}" for v in words[i:i + 12]) + ",\n"
                        for i in range(0, len(words), 12)) + "};")
    decls.append(f"extern const uint16_t TILEFONT[{len(words)}];                     // the close-up's letters (tools/art/tilefont.txt), A to Z:\n"
                 "                                                            // width | rows << 8, the rows of ink, the rows of half ink")
    total += 2 * len(words)

    # The palette, for whoever edits the art.
    sw = Image.new("RGB", (16 * 24, 24))
    for i in range(16):
        for y in range(24):
            for x in range(24):
                sw.putpixel((i * 24 + x, y), rgb(i))
    sw.save(PREVIEW / "palette.png")

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text("// Generated by tools/assets.py - do not edit.\n#pragma once\n#include <stdint.h>\n\n"
                     + "\n".join(decls) + "\n", newline="\n")
    OUT_C.write_text("// Generated by tools/assets.py - do not edit.\n"
                     "// The art is in tools/art/ (see tools/assets.py for how to redraw it).\n"
                     "#include \"Assets.h\"\n\n" + "\n\n".join(defs) + "\n", newline="\n")
    print(f"assets: {total} bytes of data")


if __name__ == "__main__":
    main()
