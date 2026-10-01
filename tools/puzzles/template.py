"""Draw a blank crossword grid: symmetric black squares, every word three
letters or more, every letter in two words, all in one piece.

    python tools/puzzles/template.py [--size 13] [--seed N] [--longest 8] [--theme WORD ...]

Prints a template for tools/puzzles/fill.py. Theme words are laid across in
symmetric places (the first through the middle row if its length is odd).
"""
import argparse
import random


def runs_ok(g, n):
    """No run of one or two open cells in any row or column."""
    for lines in (g, list(zip(*g))):
        for row in lines:
            k = 0
            for ch in list(row) + ["#"]:
                if ch == "#":
                    if 0 < k < 3:
                        return False
                    k = 0
                else:
                    k += 1
    return True


def connected(g, n):
    whites = [(r, c) for r in range(n) for c in range(n) if g[r][c] != "#"]
    if not whites:
        return False
    ws = set(whites)
    seen, todo = {whites[0]}, [whites[0]]
    while todo:
        r, c = todo.pop()
        for q in ((r + 1, c), (r - 1, c), (r, c + 1), (r, c - 1)):
            if q in ws and q not in seen:
                seen.add(q)
                todo.append(q)
    return len(seen) == len(whites)


def longest(g, n):
    """(length, cells) of the longest run of open cells without a fixed letter in it."""
    best = (0, [])
    for d in range(2):
        for a in range(n):
            cells = []
            for b in range(n + 1):
                r, c = (a, b) if d == 0 else (b, a)
                if b < n and g[r][c] != "#":
                    cells.append((r, c))
                else:
                    if len(cells) > best[0] and all(g[r][c] == "." for r, c in cells):
                        best = (len(cells), cells)
                    cells = []
    return best


def make(n, seed, maxlen, theme=()):
    rng = random.Random(seed)
    for _ in range(2000):
        g = [["."] * n for _ in range(n)]
        # Theme words: across, in rows placed symmetrically.
        rows = []
        if theme:
            mid = n // 2
            places = [mid] if len(theme) % 2 else []
            k = 2 if n >= 13 else 1
            while len(places) < len(theme):
                places += [k, n - 1 - k]
                k += 3
            ok = True
            for w, r in zip(theme, places):
                c0 = (n - len(w)) // 2 if r == mid else (0 if r < mid else n - len(w))
                if len(w) > n:
                    ok = False
                    break
                for i, ch in enumerate(w):
                    g[r][c0 + i] = ch
                for c in (c0 - 1, c0 + len(w)):
                    if 0 <= c < n:
                        g[r][c] = "#"
                        g[n - 1 - r][n - 1 - c] = "#"
            if not ok or not runs_ok(g, n):
                raise SystemExit("theme words do not fit a symmetric layout")
        good = True
        for _ in range(400):
            ln, cells = longest(g, n)
            if ln <= maxlen:
                break
            # A black square somewhere in the longest run (not near its ends).
            opts = [rc for i, rc in enumerate(cells) if 3 <= i <= ln - 4]
            if not opts:
                good = False
                break
            rng.shuffle(opts)
            for r, c in opts:
                if g[r][c] != "." or g[n - 1 - r][n - 1 - c] != ".":
                    continue
                g[r][c] = g[n - 1 - r][n - 1 - c] = "#"
                if runs_ok(g, n) and connected(g, n):
                    break
                g[r][c] = g[n - 1 - r][n - 1 - c] = "."
            else:
                good = False
                break
        if good and longest(g, n)[0] <= maxlen and connected(g, n):
            return ["".join(r) for r in g]
    return None


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=13)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--longest", type=int, default=8)
    ap.add_argument("--theme", nargs="*", default=[])
    a = ap.parse_args()
    g = make(a.size, a.seed, a.longest, [w.upper() for w in a.theme])
    if not g:
        raise SystemExit("no grid found")
    print("\n".join(g))
