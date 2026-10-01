"""Draw a blank crossword grid: symmetric black squares, every word three
letters or more, every letter in two words, all in one piece.

    python tools/puzzles/template.py [--size 13] [--seed N] [--longest 8] [--black N] [--theme WORD ...]

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


def threes(g, n):
    """Runs of exactly three open cells."""
    k3 = 0
    for lines in (g, list(zip(*g))):
        for row in lines:
            k = 0
            for ch in list(row) + ["#"]:
                if ch == "#":
                    k3 += k == 3
                    k = 0
                else:
                    k += 1
    return k3


def make(n, seed, maxlen, theme=(), min_black=0):
    rng = random.Random(seed)
    for _ in range(2000):
        g = [["."] * n for _ in range(n)]
        # Theme words: across, in rows placed symmetrically.
        rows = []
        if theme:
            mid = n // 2
            places = [mid] if len(theme) % 2 else []
            k = 3
            while len(places) < len(theme):
                places += [k, n - 1 - k]
                k += 2
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
                # What is left of the row beside the word, if too short for
                # a word, goes black too.
                for a, b in ((0, c0 - 1), (c0 + len(w) + 1, n)):
                    if 0 < b - a < 3:
                        for c in range(a, b):
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
        # More black squares, if asked for: each where it makes the fewest
        # three-letter words (of a few places tried).
        while good and sum(r.count("#") for r in g) < min_black:
            opts = []
            for _ in range(60):
                r, c = rng.randrange(n), rng.randrange(n)
                if g[r][c] != "." or g[n - 1 - r][n - 1 - c] != ".":
                    continue
                g[r][c] = g[n - 1 - r][n - 1 - c] = "#"
                if runs_ok(g, n) and connected(g, n):
                    opts.append((threes(g, n), rng.random(), r, c))
                g[r][c] = g[n - 1 - r][n - 1 - c] = "."
                if len(opts) >= 8:
                    break
            if not opts:
                good = False
                break
            _, _, r, c = min(opts)
            g[r][c] = g[n - 1 - r][n - 1 - c] = "#"
        if good and longest(g, n)[0] <= maxlen and connected(g, n):
            return ["".join(r) for r in g]
    return None


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=13)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--longest", type=int, default=8)
    ap.add_argument("--theme", nargs="*", default=[])
    ap.add_argument("--black", type=int, default=0, help="at least this many black squares")
    a = ap.parse_args()
    g = make(a.size, a.seed, a.longest, [w.upper() for w in a.theme], a.black)
    if not g:
        raise SystemExit("no grid found")
    print("\n".join(g))
