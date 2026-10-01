"""The CHCrossword puzzle format: sources, validation, encoding, decoding.

A puzzle source (tools/puzzles/src/*.txt):

    title: LUCKY NUMBERS
    difficulty: 2              # 1 easy .. 5 hard
    grid:
    CHIPS#ACE#...
    ...                        # one row a line, '#' = black square
    across:
    1. Clue text
    6. ...
    down:
    1. ...

A puzzle blob (the same bytes in flash and in a .CWD pack on the SD card):

    byte 0      bits 0-3 size N (5..15), bit 4 asymmetric mask, bits 5-7 difficulty
    then an MSB-first bit stream:
      title     Huffman string
      mask      1 = black, row-major: ceil(N*N/2) bits (the other half is the
                first turned half a turn) or N*N bits if asymmetric
      solution  5 bits a white cell (0 = A), row-major
      clues     Huffman strings: across in number order, then down
    padded to a byte, then CRC-16 (CCITT-FALSE, little-endian) of all before it

A pack:

    0   "CHCW"
    4   u8 version (1)   5  u8 codec (1)   6  u8 count (1..32)   7  u8 flags (0)
    8   char name[12] (NUL padded)
    20  u32 id (FNV-1a of the blobs)
    24  u16 crc16 of bytes 0..23 and the index     26  u16 0
    28  count x u32: offset << 12 | length (offset from the start of the pack)
    ... the blobs

The Huffman code (codec 1) is fixed - tools/puzzles/huff.json - so a pack
made today still reads after the game is rebuilt.
"""
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent

ALPHA = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,'-?!:;\"&/()_$%+*#="
END = len(ALPHA)
NSYM = END + 1
MAX_CODE = 12
MIN_N, MAX_N = 5, 15
MAX_WORDS = 96
MAX_BLOB = 2048
MAX_COUNT = 32
TITLE_MAX = 20
LINE = 31                       # characters across the clue bar
LINES = 3
VERSION, CODEC = 1, 1


class Bad(Exception):
    pass


# ---------------------------------------------------------------------------
# Huffman
# ---------------------------------------------------------------------------
def huff_lengths(freq):
    """Code lengths for the NSYM symbols, none longer than MAX_CODE."""
    import heapq
    f = [max(1, int(v)) for v in freq]
    while True:
        heap = [(v, i, (i,)) for i, v in enumerate(f)]
        heapq.heapify(heap)
        lens = [0] * NSYM
        k = NSYM
        while len(heap) > 1:
            a = heapq.heappop(heap)
            b = heapq.heappop(heap)
            for s in a[2] + b[2]:
                lens[s] += 1
            heapq.heappush(heap, (a[0] + b[0], k, a[2] + b[2]))
            k += 1
        if max(lens) <= MAX_CODE:
            return lens
        f = [v + max(f) // 2000 + 1 for v in f]          # flatten the tail and try again


def load_lengths():
    lens = json.loads((HERE / "huff.json").read_text())["lengths"]
    if len(lens) != NSYM:
        raise Bad("huff.json does not match the alphabet")
    return lens


def canonical(lens):
    """-> (codes {symbol: (code, length)}, counts per length 1..MAX_CODE, symbols in code order)."""
    order = sorted(range(NSYM), key=lambda s: (lens[s], s))
    codes, code, prev = {}, 0, 0
    for s in order:
        code <<= lens[s] - prev
        prev = lens[s]
        codes[s] = (code, lens[s])
        code += 1
    counts = [sum(1 for v in lens if v == n) for n in range(1, MAX_CODE + 1)]
    return codes, counts, order


class BitWriter:
    def __init__(self):
        self.bits = []

    def put(self, value, n):
        self.bits += [(value >> (n - 1 - i)) & 1 for i in range(n)]

    def bytes(self):
        b = self.bits + [0] * (-len(self.bits) % 8)
        return bytes(sum(v << (7 - k) for k, v in enumerate(b[i:i + 8])) for i in range(0, len(b), 8))


class BitReader:
    def __init__(self, data, pos=0):
        self.data, self.pos = data, pos

    def get(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.data[self.pos >> 3] >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v


def put_text(w, text, codes):
    for ch in text:
        w.put(*codes[ALPHA.index(ch)])
    w.put(*codes[END])


def get_text(r, counts, order):
    out = []
    while True:
        code = first = index = 0
        for n in range(MAX_CODE):
            code |= r.get(1)
            c = counts[n]
            if code - first < c:
                s = order[index + code - first]
                break
            index += c
            first = (first + c) << 1
            code <<= 1
        else:
            raise Bad("bad Huffman code")
        if s == END:
            return "".join(out)
        out.append(ALPHA[s])


def crc16(data):
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


# ---------------------------------------------------------------------------
# Puzzles
# ---------------------------------------------------------------------------
def numbering(grid):
    """-> (across, down): lists of (number, row, col, length) in number order."""
    n = len(grid)
    white = lambda r, c: 0 <= r < n and 0 <= c < n and grid[r][c] != "#"
    across, down, num = [], [], 0
    for r in range(n):
        for c in range(n):
            if not white(r, c):
                continue
            a = not white(r, c - 1) and white(r, c + 1)
            d = not white(r - 1, c) and white(r + 1, c)
            if a or d:
                num += 1
            if a:
                k = c
                while white(r, k):
                    k += 1
                across.append((num, r, c, k - c))
            if d:
                k = r
                while white(k, c):
                    k += 1
                down.append((num, r, c, k - r))
    return across, down


def answers(grid):
    across, down = numbering(grid)
    return ([(num, "".join(grid[r][c + i] for i in range(ln))) for num, r, c, ln in across],
            [(num, "".join(grid[r + i][c] for i in range(ln))) for num, r, c, ln in down])


def clean_clue(text):
    """What the game can show: capitals and the characters of its 3x5 font."""
    t = text.strip().upper()
    for a, b in (("’", "'"), ("‘", "'"), ("“", '"'), ("”", '"'), ("—", "-"),
                 ("–", "-"), ("…", "..."), ("___", "_"), ("__", "_")):
        t = t.replace(a, b)
    return re.sub(r"\s+", " ", t)


def wrap(text, first=LINE):
    """Greedy word wrap as the game does it: -> lines (the first one shorter
    by the clue's number). A word longer than a line is cut."""
    lines, cur, room = [], "", first
    for word in text.split(" "):
        while True:
            need = len(word) + (1 if cur else 0)
            if len(cur) + need <= room:
                cur += (" " if cur else "") + word
                break
            if cur:
                lines.append(cur)
                cur, room = "", LINE
                continue
            lines.append(word[:room])
            word = word[room:]
            room = LINE
            if not word:
                break
    if cur:
        lines.append(cur)
    return lines


def clue_fits(num, text):
    return len(wrap(text, LINE - len(str(num)) - 2)) <= LINES


def parse(path):
    """A source file -> {"title", "difficulty", "grid", "across": {num: clue}, "down": {...}}."""
    p = {"title": "", "difficulty": 2, "grid": [], "across": {}, "down": {}, "name": Path(path).stem}
    sec = None
    for raw in Path(path).read_text(encoding="utf-8").splitlines():
        ln = raw.rstrip()
        if not ln.strip() or ln.lstrip().startswith("//"):
            continue
        low = ln.strip().lower()
        if low in ("grid:", "across:", "down:"):
            sec = low[:-1]
            continue
        if sec is None:
            key, _, val = ln.partition(":")
            key = key.strip().lower()
            val = val.split("#")[0].strip() if key == "difficulty" else val.strip()
            if key == "title":
                p["title"] = val.upper()
            elif key == "difficulty":
                p["difficulty"] = int(val)
            else:
                raise Bad(f"{path}: unknown line {ln!r}")
        elif sec == "grid":
            p["grid"].append(ln.strip().upper())
        else:
            m = re.match(r"\s*(\d+)[.)]\s*(.*)", ln)
            if not m:
                raise Bad(f"{path}: not a clue: {ln!r}")
            p[sec][int(m.group(1))] = clean_clue(m.group(2))
    return p


def check(p, strict=True):
    """Problems with a puzzle, as a list of strings. strict: the rules for
    the built-in puzzles (symmetry, checked letters, no short words)."""
    out = []
    g = p["grid"]
    n = len(g)
    if not MIN_N <= n <= MAX_N:
        return [f"size {n} is not {MIN_N}..{MAX_N}"]
    if any(len(r) != n for r in g):
        return ["grid is not square"]
    if any(ch != "#" and not ("A" <= ch <= "Z") for r in g for ch in r):
        return ["grid has something other than letters and #"]
    if not 0 <= p["difficulty"] <= 7:
        out.append("difficulty is not 0..7")
    if not p["title"] or len(p["title"]) > TITLE_MAX or any(ch not in ALPHA for ch in p["title"]):
        out.append(f"title missing, over {TITLE_MAX} characters, or not in the font")
    across, down = numbering(g)
    if len(across) + len(down) > MAX_WORDS:
        out.append(f"{len(across) + len(down)} words: over {MAX_WORDS}")
    for name, words, clues in (("across", across, p["across"]), ("down", down, p["down"])):
        nums = [w[0] for w in words]
        for num in nums:
            if num not in clues or not clues[num]:
                out.append(f"{num} {name}: no clue")
        for num in clues:
            if num not in nums:
                out.append(f"{num} {name}: a clue for no word")
        for num, text in clues.items():
            bad = sorted({ch for ch in text if ch not in ALPHA})
            if bad:
                out.append(f"{num} {name}: cannot show {''.join(bad)!r}")
            elif not clue_fits(num, text):
                out.append(f"{num} {name}: clue does not fit {LINES} lines: {text!r}")
    if strict:
        if any(g[r][c] == "#" and g[n - 1 - r][n - 1 - c] != "#" for r in range(n) for c in range(n)):
            out.append("black squares are not symmetric")
        for name, words in (("across", across), ("down", down)):
            for num, r, c, ln in words:
                if ln < 3:
                    out.append(f"{num} {name}: a {ln}-letter word")
        ina = {(r, c + i) for _, r, c, ln in across for i in range(ln)}
        ind = {(r + i, c) for _, r, c, ln in down for i in range(ln)}
        for r in range(n):
            for c in range(n):
                if g[r][c] != "#" and ((r, c) not in ina or (r, c) not in ind):
                    out.append(f"row {r + 1} column {c + 1}: letter is not in two words")
        whites = [(r, c) for r in range(n) for c in range(n) if g[r][c] != "#"]
        seen, todo = {whites[0]}, [whites[0]]
        while todo:
            r, c = todo.pop()
            for q in ((r + 1, c), (r - 1, c), (r, c + 1), (r, c - 1)):
                if q in set(whites) and q not in seen:
                    seen.add(q)
                    todo.append(q)
        if len(seen) != len(whites):
            out.append("the grid is in more than one piece")
        a, d = answers(g)
        words = [w for _, w in a + d]
        for w in sorted({w for w in words if words.count(w) > 1}):
            out.append(f"{w} is in the grid twice")
        for name, ws, clues in (("across", a, p["across"]), ("down", d, p["down"])):
            for num, w in ws:
                if re.search(rf"\b{w}\b", clues.get(num, "")):
                    out.append(f"{num} {name}: the clue contains its answer")
    return out


def encode(p, lens=None):
    codes, _, _ = canonical(lens or load_lengths())
    g = p["grid"]
    n = len(g)
    flat = "".join(g)
    sym = all((flat[i] == "#") == (flat[n * n - 1 - i] == "#") for i in range(n * n))
    w = BitWriter()
    put_text(w, p["title"], codes)
    for i in range(n * n if not sym else (n * n + 1) // 2):
        w.put(1 if flat[i] == "#" else 0, 1)
    for ch in flat:
        if ch != "#":
            w.put(ord(ch) - 65, 5)
    across, down = numbering(g)
    for num, *_ in across:
        put_text(w, p["across"][num], codes)
    for num, *_ in down:
        put_text(w, p["down"][num], codes)
    body = bytes([n | (0 if sym else 16) | (p["difficulty"] << 5)]) + w.bytes()
    blob = body + crc16(body).to_bytes(2, "little")
    if len(blob) > MAX_BLOB:
        raise Bad(f"{p.get('name', '?')}: {len(blob)} bytes, over {MAX_BLOB}")
    return blob


def decode(blob, lens=None):
    """The reference decoder (the game's is src/game/Puzzle.cpp)."""
    _, counts, order = canonical(lens or load_lengths())
    if len(blob) < 4 or crc16(blob[:-2]) != int.from_bytes(blob[-2:], "little"):
        raise Bad("bad CRC")
    n = blob[0] & 15
    r = BitReader(blob, 8)
    title = get_text(r, counts, order)
    cells = n * n
    if blob[0] & 16:
        black = [r.get(1) for _ in range(cells)]
    else:
        half = [r.get(1) for _ in range((cells + 1) // 2)]
        black = [half[i] if i < len(half) else half[cells - 1 - i] for i in range(cells)]
    flat = "".join("#" if b else chr(65 + r.get(5)) for b in black)
    grid = [flat[i * n:(i + 1) * n] for i in range(n)]
    across, down = numbering(grid)
    p = {"title": title, "difficulty": blob[0] >> 5, "grid": grid, "across": {}, "down": {}}
    for num, *_ in across:
        p["across"][num] = get_text(r, counts, order)
    for num, *_ in down:
        p["down"][num] = get_text(r, counts, order)
    return p


def fnv1a(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def make_pack(name, blobs):
    if not 1 <= len(blobs) <= MAX_COUNT:
        raise Bad(f"a pack holds 1..{MAX_COUNT} puzzles")
    name = name.upper()[:11]                   # (and a NUL: the game reads it as a string)
    off = 28 + 4 * len(blobs)
    index = b""
    for b in blobs:
        index += ((off << 12) | len(b)).to_bytes(4, "little")
        off += len(b)
    body = b"".join(blobs)
    head = (b"CHCW" + bytes([VERSION, CODEC, len(blobs), 0]) + name.encode("ascii").ljust(12, b"\0")
            + fnv1a(body).to_bytes(4, "little"))
    return head + crc16(head + index).to_bytes(2, "little") + b"\0\0" + index + body


def read_pack(data):
    if data[:4] != b"CHCW" or data[4] != VERSION or data[5] != CODEC:
        raise Bad("not a CHCW pack of this version")
    count = data[6]
    index = data[28:28 + 4 * count]
    if crc16(data[:24] + index) != int.from_bytes(data[24:26], "little"):
        raise Bad("bad pack header CRC")
    out = []
    for i in range(count):
        e = int.from_bytes(index[4 * i:4 * i + 4], "little")
        out.append(data[e >> 12:(e >> 12) + (e & 0xFFF)])
    return data[8:20].rstrip(b"\0").decode("ascii"), out
