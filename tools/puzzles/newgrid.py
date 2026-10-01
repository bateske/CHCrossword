"""Make a new filled grid: try many blank grids and fills, keep the best.

    python tools/puzzles/newgrid.py [--size 13] [--seed N] [--theme WORD ...]
                                    [--top 8000] [--rounds 30] [--longest 7] [--black 34]

The best is the one with the commonest words and the fewest three-letter
ones. Prints the grid and its words (as tools/puzzles/fill.py does), ready
to become a puzzle source once the clues are written. Words listed in
tools/puzzles/avoid.txt are never used.
"""
import argparse
import random
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import fill as filler  # noqa: E402
import template  # noqa: E402
from cwformat import answers  # noqa: E402


def avoid():
    path = HERE / "avoid.txt"
    if not path.exists():
        return set()
    return {w.strip().upper() for ln in path.read_text().splitlines() for w in ln.split("#")[0].split()}


def shape(rows):
    """(black squares, words, three-letter words)."""
    across, down = answers([r.replace(".", "A") for r in rows])
    words = [w for _, w in across + down]
    return sum(r.count("#") for r in rows), len(words), sum(1 for w in words if len(w) == 3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=13)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--theme", nargs="*", default=[])
    ap.add_argument("--top", type=int, default=8000)
    ap.add_argument("--rounds", type=int, default=30)
    ap.add_argument("--longest", type=int, default=7)
    ap.add_argument("--black", type=int, default=34, help="at least this many black squares")
    ap.add_argument("--max-threes", type=int, default=24)
    ap.add_argument("--max-black", type=int, default=40)
    ap.add_argument("--seconds", type=float, default=120)
    a = ap.parse_args()
    theme = [w.upper() for w in a.theme]
    bad = avoid()
    words = {ln: [(w, r) for w, r in ws if w not in bad] for ln, ws in filler.load_words(a.top).items()}
    rank = {w: r for ws in words.values() for w, r in ws}
    best, t0, made = None, time.time(), 0
    for k in range(100000):
        if made >= a.rounds or time.time() - t0 > a.seconds:
            break
        rows = template.make(a.size, a.seed * 100000 + k, a.longest, theme, a.black)
        if not rows:
            continue
        black, count, threes = shape(rows)
        if threes > a.max_threes or black > a.max_black:
            continue
        g = None
        for t in range(4):
            f = filler.Filler(rows, words, random.Random(a.seed * 7919 + k * 31 + t))
            g = f.run(3000)
            if g:
                break
        if not g:
            continue
        made += 1
        across, down = answers(g)
        ws = [w for _, w in across + down]
        score = sum(rank.get(w, 0) for w in ws) / len(ws) + 400 * threes
        if best is None or score < best[0]:
            best = (score, g, black, count, threes)
    if not best:
        raise SystemExit("nothing found: allow more three-letter words or black squares, or a larger --top")
    score, g, black, count, threes = best
    print("\n".join(g))
    across, down = answers(g)
    print("across:")
    for num, w in across:
        print(f"{num}. {w}")
    print("down:")
    for num, w in down:
        print(f"{num}. {w}")
    print(f"// {made} grids tried; this one: {count} words, {threes} of three letters, {black} black squares, "
          f"score {score:.0f}", file=sys.stderr)


if __name__ == "__main__":
    main()
