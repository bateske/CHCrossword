"""Fill a crossword grid with common words.

    python tools/puzzles/fill.py TEMPLATE [--seed N] [--top 30000] [--tries 300]

TEMPLATE is a text file of grid rows: '#' black, '.' open, a letter = fixed
(theme words are typed into the template). Prints the filled grid and its
words, ready to paste into a puzzle source; the clues are written by hand.

Words: ENABLE (public domain), kept to the commonest by the wordfreq
package's figures (`pip install wordfreq`, or `pip install --target
tools/puzzles/data/pylib wordfreq`; build-time only), less a block list.
"--top N" keeps roughly the N commonest words of the language.
"""
import argparse
import math
import random
import sys
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / "data"
ENABLE_URL = "https://raw.githubusercontent.com/dolph/dictionary/master/enable1.txt"

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
dick dicks cock cocks crap turd turds fart farts pee pees poo poop urine feces enema enemas scum
""".split())


def fetch(url, path):
    if not path.exists():
        DATA.mkdir(parents=True, exist_ok=True)
        print(f"downloading {url}", file=sys.stderr)
        urllib.request.urlretrieve(url, path)
    return path


def load_words(top):
    """{length: [(word, rank), ...]}: the ENABLE words of 3 to 15 letters
    that are common enough, capitals, commonest first. rank is 0 for the
    commonest word there is, about `top` for the rarest let in."""
    if (DATA / "pylib").exists():
        sys.path.insert(0, str(DATA / "pylib"))
    from wordfreq import zipf_frequency
    # A word of Zipf frequency z is about the 10^(7.2 - z)th commonest.
    floor = 7.2 - math.log10(top)
    out = {}
    for w in fetch(ENABLE_URL, DATA / "enable1.txt").read_text().split():
        if not 3 <= len(w) <= 15 or w in BLOCKED:
            continue
        z = zipf_frequency(w, "en")
        if z >= floor:
            out.setdefault(len(w), []).append((w.upper(), int(10 ** (7.2 - z))))
    for ws in out.values():
        ws.sort(key=lambda x: (x[1], x[0]))
    return out


ENDINGS = {"S", "ES", "ED", "D", "ER", "R", "RS", "ERS", "EST", "ING", "INGS", "LY", "Y", "N", "EN", "TH",
           "AL", "NESS", "FUL", "LESS", "MENT"}
Y_ENDINGS = {"IER", "IES", "IED", "ILY", "IEST"}


def related(a, b):
    """Two answers too alike to share a grid: one is the other with an
    ending (ACE and ACES, AIM and AIMING, TEN and TENTH), or they are one
    word with two endings (RENTS and RENTED, EASY and EASIER)."""
    if len(a) > len(b):
        a, b = b, a
    k = 0
    while k < len(a) and a[k] == b[k]:
        k += 1
    ra, rb = a[k:], b[k:]
    if not ra:
        return rb in ENDINGS
    if ra == "Y" and rb in Y_ENDINGS:
        return k >= 3
    return k >= 4 and ra in ENDINGS and rb in ENDINGS


class Filler:
    """Each slot keeps the set of words it can still take (a bit mask over
    the words of its length). Placing a word narrows the slots it crosses;
    the slot with the fewest words left goes next, and of its words the ones
    that leave its crossings the most room are tried first."""

    def __init__(self, rows, words, rng):
        self.n = n = len(rows)
        self.rng = rng
        self.rows = rows
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
        at = {}
        for s, cells in enumerate(self.slots):
            for i, rc in enumerate(cells):
                at.setdefault(rc, []).append((s, i))
        # cross[s]: (position in s, other slot, position in it)
        self.cross = [[] for _ in self.slots]
        for rc, uses in at.items():
            if len(uses) == 2:
                (s, i), (t, j) = uses
                self.cross[s].append((i, t, j))
                self.cross[t].append((j, s, i))
        self.words, self.mask = {}, {}
        for ln in {len(s) for s in self.slots}:
            ws = words.get(ln, [])
            self.words[ln] = ws
            m = [[0] * 26 for _ in range(ln)]
            for k, (w, _) in enumerate(ws):
                for i, ch in enumerate(w):
                    m[i][ord(ch) - 65] |= 1 << k
            self.mask[ln] = m
        self.same = {}                        # slots of each length (no word twice)
        for s, cells in enumerate(self.slots):
            self.same.setdefault(len(cells), []).append(s)
        self.dom = []
        self.word = [None] * len(self.slots)
        self.fixed = {}
        for s, cells in enumerate(self.slots):
            ln = len(cells)
            m = (1 << len(self.words[ln])) - 1
            letters = [rows[r][c] for r, c in cells]
            for i, ch in enumerate(letters):
                if ch != ".":
                    m &= self.mask[ln][i][ord(ch) - 65]
            if all(ch != "." for ch in letters):
                self.fixed[s] = "".join(letters)        # a theme word: anything goes
                self.word[s] = self.fixed[s]
                m = 0
            self.dom.append(m)
        self.nodes = 0

    def place(self, s, k, trail):
        """Word k into slot s; False if a crossing slot is left with nothing."""
        ln = len(self.slots[s])
        w = self.words[ln][k][0]
        for other in self.word:
            if other is not None and related(w, other):
                return False
        self.word[s] = w
        for i, t, j in self.cross[s]:
            if self.word[t] is not None:
                continue
            d = self.dom[t] & self.mask[len(self.slots[t])][j][ord(w[i]) - 65]
            if d != self.dom[t]:
                trail.append((t, self.dom[t]))
                self.dom[t] = d
            if not d:
                return False
        bit = 1 << k
        for t in self.same[ln]:
            if t != s and self.word[t] is None and self.dom[t] & bit:
                trail.append((t, self.dom[t]))
                self.dom[t] &= ~bit
                if not self.dom[t]:
                    return False
        return True

    def undo(self, s, trail):
        self.word[s] = None
        for t, d in reversed(trail):
            self.dom[t] = d

    def room(self, s, w):
        """How much choice word w would leave the tightest slot it crosses."""
        least = 1 << 30
        for i, t, j in self.cross[s]:
            if self.word[t] is None:
                k = (self.dom[t] & self.mask[len(self.slots[t])][j][ord(w[i]) - 65]).bit_count()
                if k < least:
                    least = k
                    if not k:
                        break
        return least

    def solve(self, limit):
        self.nodes += 1
        if self.nodes > limit:
            return False
        best, bn = None, 1 << 60
        for s in range(len(self.slots)):
            if self.word[s] is None:
                k = self.dom[s].bit_count()
                if k == 0:
                    return False
                if k < bn:
                    best, bn = s, k
        if best is None:
            return True
        ws = self.words[len(self.slots[best])]
        # The commonest 30 or so candidates (a few skipped at random, so
        # seeds differ), tried in the order that leaves the crossings most room.
        picks, m = [], self.dom[best]
        while m and len(picks) < 30:
            low = m & -m
            m ^= low
            if bn <= 30 or self.rng.random() < 0.7:
                picks.append(low.bit_length() - 1)
        scored = sorted(((self.room(best, ws[k][0]), -k) for k in picks), reverse=True)
        for room, negk in scored[:8]:
            if not room:
                break
            trail = []
            if self.place(best, -negk, trail) and self.solve(limit):
                return True
            self.undo(best, trail)
            if self.nodes > limit:
                return False
        return False

    def run(self, limit):
        # Theme words narrow their crossings before anything is placed.
        for s, w in self.fixed.items():
            for i, t, j in self.cross[s]:
                if self.word[t] is None:
                    self.dom[t] &= self.mask[len(self.slots[t])][j][ord(w[i]) - 65]
        if not self.solve(limit):
            return None
        if len(set(self.word)) != len(self.word):
            return None
        g = [list(r) for r in self.rows]
        for s, cells in enumerate(self.slots):
            for (r, c), ch in zip(cells, self.word[s]):
                g[r][c] = ch
        return ["".join(r) for r in g]


def fill(rows, top=30000, seed=1, tries=300, limit=1500):
    words = load_words(top)
    rank = {w: r for ws in words.values() for w, r in ws}
    for t in range(tries):
        f = Filler(rows, words, random.Random(seed * 1000 + t))
        g = f.run(limit)
        if g:
            return g, sum(rank.get(w, top) for w in f.word) // len(f.word)
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
