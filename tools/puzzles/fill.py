"""Fill a crossword grid with common words.

    python tools/puzzles/fill.py TEMPLATE [--seed N] [--top 30000] [--tries 300]

TEMPLATE is a text file of grid rows: '#' black, '.' open, a letter = fixed
(theme words are typed into the template). Prints the filled grid and its
words, ready to paste into a puzzle source; the clues are written by hand.

Words: ENABLE (public domain), kept to the commonest by a Google Books
count list (data/, fetched once; build-time only), less a block list.
"""
import argparse
import random
import sys
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / "data"
ENABLE_URL = "https://raw.githubusercontent.com/dolph/dictionary/master/enable1.txt"
FREQ_URL = "https://norvig.com/google-books-common-words.txt"

# Not for a family game, or no fun to clue.
BLOCKED = set("""
abo abos boche boches cunt cunts dago dagoes dagos darkey darkeys darkies darky fag faggot faggots
fagot fagots fags fuck fucked fucker fuckers fucking fucks gook gooks honkey honkeys honkie honkies
honky jew jewed jewing jews kike kikes kraut krauts nigger niggers piss pissed pisser pissers pisses
pissing rapist rapists redskin redskins shit shits shitted shitting shitty spic spick spicks spics
squaw squaws twat twats wetback wetbacks wop wops rape raped rapes raping negro negroes whore whores
slut sluts bitch bitches bastard bastards damn damned hell arse arses ass asses tit tits anus penis
vagina sex sexes sexed sexy semen sperm incest molest nazi nazis slave slaves suicide suicides
cancer cancers tumor tumors aids murder murders murdered killer killers kill kills killed corpse
corpses abort aborts aborted abortion nude nudes porn erotic orgy orgies pimp pimps hooker hookers
dick dicks cock cocks crap craps turd turds fart farts pee pees poo poop urine feces enema enemas
""".split()) - {"craps"}           # (the dice game is welcome here)


def fetch(url, path):
    if not path.exists():
        DATA.mkdir(parents=True, exist_ok=True)
        print(f"downloading {url}", file=sys.stderr)
        urllib.request.urlretrieve(url, path)
    return path


def load_words(top):
    """{length: [(word, rank), ...]}: ENABLE words among the `top` commonest, capitals."""
    enable = set(fetch(ENABLE_URL, DATA / "enable1.txt").read_text().split())
    out, rank = {}, 0
    for ln in fetch(FREQ_URL, DATA / "gbooks_common.txt").read_text().splitlines():
        w = ln.split()[0].lower()
        rank += 1
        if rank > top:
            break
        if w in enable and w not in BLOCKED and 3 <= len(w) <= 15:
            out.setdefault(len(w), []).append((w.upper(), rank))
    return out


class Filler:
    def __init__(self, rows, words, rng):
        self.n = n = len(rows)
        self.rng = rng
        self.cell = [list(r) for r in rows]
        white = lambda r, c: 0 <= r < n and 0 <= c < n and rows[r][c] != "#"
        self.slots = []                       # lists of (r, c)
        for r in range(n):
            for c in range(n):
                if not white(r, c):
                    continue
                if not white(r, c - 1) and white(r, c + 1):
                    k = c
                    while white(r, k):
                        k += 1
                    self.slots.append([(r, j) for j in range(c, k)])
                if not white(r - 1, c) and white(r + 1, c):
                    k = r
                    while white(k, c):
                        k += 1
                    self.slots.append([(j, c) for j in range(r, k)])
        self.at = {}                          # cell -> [(slot, position)]
        for s, cells in enumerate(self.slots):
            for i, rc in enumerate(cells):
                self.at.setdefault(rc, []).append((s, i))
        # Per length: the words, and for each position and letter a bit mask
        # of the words that have it there.
        self.words, self.mask, self.all = {}, {}, {}
        for ln in {len(s) for s in self.slots}:
            ws = words.get(ln, [])
            self.words[ln] = ws
            self.all[ln] = (1 << len(ws)) - 1
            m = [[0] * 26 for _ in range(ln)]
            for k, (w, _) in enumerate(ws):
                for i, ch in enumerate(w):
                    m[i][ord(ch) - 65] |= 1 << k
            self.mask[ln] = m
        self.used = set()
        self.nodes = 0

    def cands(self, s):
        cells = self.slots[s]
        ln = len(cells)
        m = self.all[ln]
        for i, (r, c) in enumerate(cells):
            ch = self.cell[r][c]
            if ch != ".":
                m &= self.mask[ln][i][ord(ch) - 65]
                if not m:
                    break
        return m

    def filled(self, s):
        return all(self.cell[r][c] != "." for r, c in self.slots[s])

    def solve(self, limit):
        self.nodes += 1
        if self.nodes > limit:
            return False
        # The open slot with the fewest candidates.
        best, bm, bn = None, 0, 1 << 60
        for s in range(len(self.slots)):
            if self.filled(s):
                continue
            m = self.cands(s)
            k = bin(m).count("1")
            if k == 0:
                return False
            if k < bn:
                best, bm, bn = s, m, k
        if best is None:
            return True
        cells = self.slots[best]
        ws = self.words[len(cells)]
        picks = [k for k in range(len(ws)) if bm >> k & 1 and ws[k][0] not in self.used]
        # Common words first, with some shuffle so seeds differ.
        picks.sort(key=lambda k: ws[k][1] * self.rng.uniform(0.5, 2.0))
        for k in picks:
            w = ws[k][0]
            before = [self.cell[r][c] for r, c in cells]
            for (r, c), ch in zip(cells, w):
                self.cell[r][c] = ch
            self.used.add(w)
            self.placed.add(best)
            ok = True
            for rc in cells:                  # every crossing word still possible?
                for s, _ in self.at[rc]:
                    if s == best:
                        continue
                    m = self.cands(s)
                    if not m:
                        ok = False
                    elif self.filled(s):
                        cw = "".join(self.cell[r][c] for r, c in self.slots[s])
                        if cw in self.used and s not in self.placed:
                            ok = False
                if not ok:
                    break
            if ok and self.solve(limit):
                return True
            self.used.discard(w)
            self.placed.discard(best)
            for (r, c), ch in zip(cells, before):
                self.cell[r][c] = ch
            if self.nodes > limit:
                return False
        return False

    def run(self, limit):
        self.placed = {s for s in range(len(self.slots)) if self.filled(s)}
        for s in list(self.placed):
            self.used.add("".join(self.cell[r][c] for r, c in self.slots[s]))
        if not self.solve(limit):
            return None
        # Crossing words completed along the way must be distinct too.
        ws = ["".join(self.cell[r][c] for r, c in cells) for cells in self.slots]
        if len(set(ws)) != len(ws):
            return None
        return ["".join(r) for r in self.cell]


def fill(rows, top=30000, seed=1, tries=300, limit=1500):
    words = load_words(top)
    for t in range(tries):
        f = Filler(rows, words, random.Random(seed * 1000 + t))
        g = f.run(limit)
        if g:
            rank = {w: r for ws in words.values() for w, r in ws}
            got = ["".join(g[r][c] for r, c in cells) for cells in f.slots]
            return g, sum(rank.get(w, top) for w in got) // len(got)
    return None, 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("template")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--top", type=int, default=30000)
    ap.add_argument("--tries", type=int, default=300)
    a = ap.parse_args()
    rows = [ln.strip().upper() for ln in Path(a.template).read_text().splitlines()
            if ln.strip() and not ln.startswith("//")]
    g, score = fill(rows, a.top, a.seed, a.tries)
    if not g:
        raise SystemExit("no fill found: try another seed, a larger --top, or an easier template")
    sys.path.insert(0, str(HERE))
    from cwformat import answers
    print("\n".join(g))
    across, down = answers(g)
    print("across:")
    for num, w in across:
        print(f"{num}. {w}")
    print("down:")
    for num, w in down:
        print(f"{num}. {w}")
    print(f"// mean word rank {score}", file=sys.stderr)


if __name__ == "__main__":
    main()
