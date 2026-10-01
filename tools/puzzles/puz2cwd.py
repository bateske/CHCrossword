"""Turn Across Lite .puz files into a pack for the SD card.

    python tools/puzzles/puz2cwd.py OUT.CWD FILE.puz [FILE.puz ...] [--name NAME] [--difficulty N]

For your own puzzle files. Copy OUT.CWD into a folder named CHCW on the
card. A pack holds up to 32 puzzles; a puzzle must be square, 5x5 to 15x15,
with at most 96 words, no rebus squares and not scrambled (locked).

What the game cannot show is changed to fit: clues become capitals, accented
letters lose their accents, characters outside the game's font are dropped,
a run of underscores becomes one, and a clue too long for the clue box (three
lines of 31) is cut short with "...". Each such change is reported.
"""
import argparse
import sys
import unicodedata
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import cwformat as cw  # noqa: E402

MAGIC = b"ACROSS&DOWN\0"


class NotForUs(Exception):
    pass


def plain(text):
    """Text as the game can show it."""
    t = unicodedata.normalize("NFKD", cw.clean_clue(text))
    return "".join(ch for ch in t if ch in cw.ALPHA).strip()


def fit(num, text):
    """The clue cut down until it fits the clue box."""
    if cw.clue_fits(num, text):
        return text, False
    words = text.split(" ")
    while words and not cw.clue_fits(num, " ".join(words) + "..."):
        words.pop()
    return " ".join(words) + "...", True


def read_puz(data, difficulty=2):
    """-> (puzzle as cwformat has them, notes about what was changed)."""
    at = data.find(MAGIC)
    if at < 2:
        raise NotForUs("not an Across Lite file")
    base = at - 2
    if len(data) < base + 0x34:
        raise NotForUs("the file is cut short")
    w, h = data[base + 0x2C], data[base + 0x2D]
    count = int.from_bytes(data[base + 0x2E:base + 0x30], "little")
    if int.from_bytes(data[base + 0x32:base + 0x34], "little"):
        raise NotForUs("the puzzle is scrambled (locked)")
    if w != h or not cw.MIN_N <= w <= cw.MAX_N:
        raise NotForUs(f"{w}x{h}: the game takes square grids from {cw.MIN_N} to {cw.MAX_N}")
    pos = base + 0x34
    sol = data[pos:pos + w * h].decode("latin-1")
    pos += 2 * w * h                                    # the solution, then the solver's state
    strings = data[pos:].split(b"\0")
    if len(strings) < 3 + count:
        raise NotForUs("the file is cut short")
    rest = b"\0".join(strings[3 + count:])
    if b"GRBS" in rest and any(rest[rest.index(b"GRBS") + 8:rest.index(b"GRBS") + 8 + w * h]):
        raise NotForUs("it has rebus squares (several letters in a cell)")
    if any(ch not in ".:" and not ("A" <= ch <= "Z") for ch in sol):
        raise NotForUs("its solution has something other than letters")
    grid = ["".join("#" if ch in ".:" else ch for ch in sol[r * w:(r + 1) * w]) for r in range(h)]
    notes = []
    title = plain(strings[0].decode("latin-1"))[:cw.TITLE_MAX].strip() or "UNTITLED"
    p = {"title": title, "difficulty": difficulty, "grid": grid, "across": {}, "down": {}}
    across, down = cw.numbering(grid)
    if len(across) + len(down) != count:
        raise NotForUs(f"{count} clues for {len(across) + len(down)} words")
    clues = [s.decode("latin-1") for s in strings[3:3 + count]]
    # The file's clues are in numbering order, across before down at a number.
    order = sorted([(num, 0) for num, *_ in across] + [(num, 1) for num, *_ in down])
    for (num, is_down), raw in zip(order, clues):
        text = plain(raw) or "?"
        if text != cw.clean_clue(raw):
            notes.append(f"{num}{'D' if is_down else 'A'}: some characters could not be kept")
        text, was_cut = fit(num, text)
        if was_cut:
            notes.append(f"{num}{'D' if is_down else 'A'}: cut to fit the clue box")
        p["down" if is_down else "across"][num] = text
    problems = cw.check(p, strict=False)
    if problems:
        raise NotForUs("; ".join(problems))
    return p, notes


def write_puz(p):
    """A puzzle as a minimal .puz file (for the tools' own tests: the
    checksums are left zero, which readers of the layout do not mind)."""
    g = p["grid"]
    n = len(g)
    across, down = cw.numbering(g)
    order = sorted([(num, 0) for num, *_ in across] + [(num, 1) for num, *_ in down])
    clues = [p["down" if d else "across"][num] for num, d in order]
    head = bytearray(0x34)
    head[2:14] = MAGIC
    head[0x18:0x1C] = b"1.3\0"
    head[0x2C], head[0x2D] = n, n
    head[0x2E:0x30] = len(clues).to_bytes(2, "little")
    head[0x30] = 1
    sol = "".join(g).replace("#", ".")
    state = "".join("." if ch == "." else "-" for ch in sol)
    strings = [p["title"], "", ""] + clues + [""]
    return bytes(head) + sol.encode("latin-1") + state.encode("latin-1") + b"".join(s.encode("latin-1") + b"\0" for s in strings)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("files", nargs="+")
    ap.add_argument("--name", help="the pack's name on the puzzle list (11 characters; default: the file's)")
    ap.add_argument("--difficulty", type=int, default=2, help="1 easy .. 5 master, shown on the list (default 2)")
    a = ap.parse_args()
    blobs = []
    for f in a.files:
        try:
            p, notes = read_puz(Path(f).read_bytes(), a.difficulty)
            blob = cw.encode(p)
        except (NotForUs, cw.Bad) as e:
            print(f"{Path(f).name}: left out: {e}")
            continue
        print(f"{Path(f).name}: {p['title']}, {len(p['grid'])}x{len(p['grid'])}, {len(blob)} bytes")
        for note in notes:
            print(f"    {note}")
        blobs.append(blob)
    if not blobs:
        raise SystemExit("nothing to pack")
    if len(blobs) > cw.MAX_COUNT:
        raise SystemExit(f"a pack holds {cw.MAX_COUNT} puzzles: make several")
    name = plain(a.name or Path(a.out).stem)
    Path(a.out).write_bytes(cw.make_pack(name, blobs))
    print(f"{a.out}: {len(blobs)} puzzles")


if __name__ == "__main__":
    main()
