// Host tests: the puzzle decoder against the Python reference (expect.h is
// written by run_tests.py from tools/puzzles/cwformat.py), the rules and
// the score, saving, and damaged data.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "../../src/game/Puzzle.h"
#include "../../src/game/Game.h"
#include "../../src/game/PuzzleData.h"
#include "../../src/pack/Pack.h"

struct XWord { int num, start, len, down; const char *clue; };
struct XPuzzle { const char *title; int n, diff; const char *grid; std::vector<XWord> words; };
#include "build/expect.h"       // static const std::vector<XPuzzle> EXPECT

static long checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t rngState = 12345;
static uint32_t rnd() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return rngState; }

static bool open(int i) { return pack::select(0) && pack::open((uint8_t)i); }

// A second, plain implementation of the word finder.
static int wordAtRef(int c, bool down) {
    int n = puz::n;
    if (!puz::sol[c]) return -1;
    int st = down ? n : 1;
    while (down ? c >= n && puz::sol[c - n] : c % n && puz::sol[c - 1]) c -= st;
    for (int w = 0; w < puz::nWords; w++)
        if (puz::wStart[w] == c && puz::isDown((uint8_t)w) == down) return w;
    return -1;
}

static void testDecode() {
    CHECK(pack::select(0));
    CHECK(pack::puzzles() == EXPECT.size());
    CHECK(!strcmp(pack::name(), "BUILT-IN"));
    for (size_t i = 0; i < EXPECT.size(); i++) {
        const XPuzzle &x = EXPECT[i];
        uint8_t size, diff;
        char title[puz::TITLE_MAX + 1];
        CHECK(pack::peek((uint8_t)i, size, diff, title));
        CHECK(size == x.n && diff == x.diff && !strcmp(title, x.title));
        CHECK(open((int)i));
        CHECK(puz::n == x.n && puz::difficulty == x.diff && !strcmp(puz::title, x.title));
        int whites = 0;
        for (int c = 0; c < x.n * x.n; c++) {
            char want = x.grid[c];
            CHECK(want == '#' ? puz::sol[c] == 0 : puz::sol[c] == want - 'A' + 1);
            whites += want != '#';
            CHECK(puz::cell[c] == 0);
        }
        CHECK(puz::whites == whites);
        CHECK(puz::nWords == x.words.size());
        for (size_t w = 0; w < x.words.size() && w < puz::nWords; w++) {
            const XWord &xw = x.words[w];
            CHECK(puz::wNum[w] == xw.num && puz::wStart[w] == xw.start && puz::wLen[w] == xw.len);
            CHECK(puz::isDown((uint8_t)w) == (xw.down != 0));
            char clue[puz::CLUE_MAX];
            puz::clue((uint8_t)w, clue);
            CHECK(!strcmp(clue, xw.clue));
            for (int k = 0; k < xw.len; k++) {
                int c = puz::cellOf((uint8_t)w, (uint8_t)k);
                CHECK(puz::wordAt((uint8_t)c, xw.down) == w);
            }
        }
        for (int c = 0; c < x.n * x.n; c++)
            for (int d = 0; d < 2; d++) {
                int ref = wordAtRef(c, d);
                CHECK(puz::wordAt((uint8_t)c, d) == (ref < 0 ? puz::NONE : ref));
            }
    }
}

// What a word's points are multiplied by: three for the jackpot word.
static unsigned J(int w) { return w == game::st.jackpot ? game::JACKPOT_TIMES : 1; }

// Type a word's letters; returns the last letter's events.
static game::Events typeWord(int w, const char *wrongAt = nullptr) {
    game::Events e{};
    for (int k = 0; k < puz::wLen[w]; k++) {
        int c = puz::cellOf((uint8_t)w, (uint8_t)k);
        uint8_t v = puz::sol[c];
        if (wrongAt && wrongAt[k] == 'x') v = (uint8_t)(v % 26 + 1);
        if (puz::cell[c] & puz::LOCKED) continue;
        e = game::place((uint8_t)c, v);
    }
    return e;
}

static void testRules() {
    CHECK(open(0));
    game::start(true);
    CHECK(game::multiplier() == 1 && game::st.score == 0);
    // A first word: 10 a letter.
    game::Events e = typeWord(0);
    CHECK(e.nLock == 1 && e.lock[0] == 0 && e.points[0] == 10 * puz::wLen[0] * J(0) && !e.cross && !e.solved);
    CHECK(puz::wordLocked(0) && game::st.streak == 1 && game::st.score == 10 * puz::wLen[0] * J(0));
    CHECK(e.jackpot == (J(0) > 1));
    // A locked cell cannot be changed.
    e = game::place(puz::wStart[0], 5);
    CHECK(!e.changed);
    CHECK((puz::cell[puz::wStart[0]] & puz::LETTER) == puz::sol[puz::wStart[0]]);
    // A second within eight seconds: the quick bonus.
    uint16_t before = game::st.score;
    e = typeWord(1);
    CHECK(e.nLock == 1 && e.quick && e.points[0] == 15 * puz::wLen[1] * J(1));
    CHECK(game::st.score == before + 15 * puz::wLen[1] * J(1));
    // After a long think: no quick bonus; the third word in a row makes it x2 for the next.
    game::st.ticks += 20 * 60;
    e = typeWord(2);
    CHECK(!e.quick && e.points[0] == 10 * puz::wLen[2] * J(2) && game::st.streak == 3 && game::multiplier() == 2);
    game::st.ticks += 20 * 60;
    e = typeWord(3);
    CHECK(e.points[0] == 20 * puz::wLen[3] * J(3));
    // A wrong word (in an empty grid, so nothing else completes): 20 off,
    // the streak gone, the letters stay.
    CHECK(open(0));
    game::start(true);
    game::st.score = 100;
    game::st.streak = 5;
    e = typeWord(4, "x..............");
    CHECK(e.nLock == 0 && e.nWrong == 1 && e.wrong[0] == 4 && game::st.streak == 0 && game::st.misses == 1);
    CHECK(game::st.score == 80 && puz::wordFull(4) && !puz::wordLocked(4));
    // Put right, it locks at x1.
    e = game::place(puz::cellOf(4, 0), puz::sol[puz::cellOf(4, 0)]);
    CHECK(e.nLock == 1 && e.points[0] == 10 * puz::wLen[4] * J(4) && game::st.streak == 1);
    // Rubbing out an unlocked letter.
    int free = -1;
    for (int c = 0; c < puz::n * puz::n; c++)
        if (puz::sol[c] && !puz::cell[c]) { free = c; break; }
    CHECK(free >= 0);
    game::place((uint8_t)free, 3);
    CHECK((puz::cell[free] & puz::LETTER) == 3 || puz::sol[free] == 3);
    if (!(puz::cell[free] & puz::LOCKED)) {
        e = game::place((uint8_t)free, 0);
        CHECK(e.changed && puz::cell[free] == 0);
    }
    // A reveal: 50 off, the streak gone, the word worth half.
    CHECK(open(0));
    game::start(true);
    game::st.score = 500;
    e = game::reveal(puz::wStart[0]);
    CHECK(e.changed && game::st.score == 450 && game::st.reveals == 1 && (puz::cell[puz::wStart[0]] & puz::REVEALED));
    e = typeWord(0);
    CHECK(e.nLock == 1 && e.points[0] == 5 * puz::wLen[0] * J(0));
    // The score never goes below nothing.
    CHECK(open(0));
    game::start(true);
    typeWord(4, "x..............");
    CHECK(game::st.score == 0);
}

// Whole puzzles, a letter at a time in random order: every word locks
// exactly once, crosses are counted when one letter ends two words, and the
// last letter solves it.
static void testWhole(bool checking) {
    for (size_t i = 0; i < EXPECT.size(); i++) {
        CHECK(open((int)i));
        game::start(checking);
        int cells = puz::n * puz::n;
        std::vector<int> order;
        for (int c = 0; c < cells; c++) if (puz::sol[c]) order.push_back(c);
        for (size_t k = order.size(); k > 1; k--) std::swap(order[k - 1], order[rnd() % k]);
        // Every puzzle has a jackpot word, one of its longer ones, and it is
        // the same one each time; none with checking off.
        if (checking) {
            int longest = 0;
            for (int w = 0; w < puz::nWords; w++) if (puz::wLen[w] > longest) longest = puz::wLen[w];
            uint8_t jp = game::st.jackpot;
            CHECK(jp < puz::nWords && puz::wLen[jp] >= (longest > 6 ? 6 : longest));
            game::start(true);
            CHECK(game::st.jackpot == jp);
        } else {
            CHECK(game::st.jackpot == puz::NONE);
        }
        int locks = 0, crosses = 0, jackpots = 0;
        uint32_t sum = 0;
        for (size_t k = 0; k < order.size(); k++) {
            game::st.ticks += 60;
            game::Events e = game::place((uint8_t)order[k], puz::sol[order[k]]);
            CHECK(e.changed && e.nWrong == 0);
            locks += e.nLock;
            crosses += e.cross;
            jackpots += e.jackpot;
            sum += e.points[0] + e.points[1] + e.bonus;
            CHECK(e.solved == (k + 1 == order.size()));
            CHECK(e.cross == (e.nLock == 2));
        }
        CHECK(game::st.solved);
        if (checking) {
            CHECK(locks == puz::nWords && puz::lockedWords() == puz::nWords && game::st.score == sum);
            CHECK(game::st.streak == locks && jackpots == 1);
        } else {
            CHECK(locks == 0 && game::st.score == 0);
        }
        uint16_t before = game::st.score;
        game::Result r = game::finish();
        CHECK(r.perfect && r.clean && r.seconds == order.size());
        CHECK(r.par == puz::whites * 6 && r.timeBonus == (r.par - r.seconds) * 2);
        CHECK(r.score == before + r.timeBonus + 1500 && r.stars == 3);
        (void)crosses;
    }
}

static void testSave() {
    for (int round = 0; round < 200; round++) {
        int i = (int)(rnd() % EXPECT.size());
        CHECK(open(i));
        bool checking = rnd() & 1;
        game::start(checking);
        int cells = puz::n * puz::n;
        for (int k = 0; k < (int)(rnd() % 300); k++) {
            int c = (int)(rnd() % cells);
            if (!puz::sol[c]) continue;
            uint32_t what = rnd() % 10;
            if (what < 6) game::place((uint8_t)c, puz::sol[c]);
            else if (what < 8) game::place((uint8_t)c, (uint8_t)(rnd() % 27));
            else if (what < 9) game::reveal((uint8_t)c);
            game::st.ticks += rnd() % 500;
            if (game::st.solved) break;
        }
        if (game::st.solved) continue;
        uint8_t cellsWere[puz::MAX_CELLS];
        memcpy(cellsWere, puz::cell, sizeof cellsWere);
        game::State was = game::st;
        uint8_t lockedWas = puz::lockedWords();
        game::Record r;
        game::save(r);
        CHECK(open(i));
        game::load(r);
        CHECK(game::st.ticks == was.ticks && game::st.score == was.score && game::st.streak == was.streak);
        CHECK(game::st.misses == was.misses && game::st.reveals == was.reveals && game::st.checking == was.checking);
        CHECK(puz::lockedWords() == lockedWas && !game::st.solved);
        CHECK(!memcmp(cellsWere, puz::cell, (size_t)cells));
    }
    CHECK(sizeof(game::Record) == 160);
}

// Damaged blobs: a flipped bit is refused by the CRC; and with the CRC put
// right again the decoder must still not run off anything (UBSan and the
// bounds in the decoder are the test: it may load nonsense or refuse).
static void testDamage() {
    const uint8_t *pk = BUILTIN;
    uint32_t e = pk[28] | pk[29] << 8 | pk[30] << 16 | (uint32_t)pk[31] << 24;
    uint32_t off = e >> 12, len = e & 0xFFF;
    std::vector<uint8_t> blob(pk + off, pk + off + len);
    CHECK(puz::load(blob.data(), (uint16_t)len));
    for (int round = 0; round < 3000; round++) {
        std::vector<uint8_t> b = blob;
        int flips = 1 + (int)(rnd() % 4);
        for (int k = 0; k < flips; k++) b[rnd() % (len - 2)] ^= (uint8_t)(1u << (rnd() % 8));
        if (round % 2 == 0) {
            CHECK(!puz::load(b.data(), (uint16_t)len));
        } else {
            uint16_t c = puz::crc16(b.data(), len - 2);
            b[len - 2] = (uint8_t)c; b[len - 1] = (uint8_t)(c >> 8);
            if (puz::load(b.data(), (uint16_t)len)) {
                CHECK(puz::n >= puz::MIN_N && puz::n <= puz::MAX_N && puz::nWords <= puz::MAX_WORDS);
                char clue[puz::CLUE_MAX];
                for (int w = 0; w < puz::nWords; w++) {
                    puz::clue((uint8_t)w, clue);
                    CHECK(strlen(clue) < puz::CLUE_MAX);
                    CHECK(puz::cellOf((uint8_t)w, (uint8_t)(puz::wLen[w] - 1)) < puz::n * puz::n);
                }
            }
        }
        // Truncated.
        uint16_t cut = (uint16_t)(rnd() % len);
        puz::load(blob.data(), cut);
    }
    // Random bytes with a good CRC.
    for (int round = 0; round < 3000; round++) {
        uint32_t n = 4 + rnd() % 600;
        std::vector<uint8_t> b(n);
        for (auto &v : b) v = (uint8_t)rnd();
        uint16_t c = puz::crc16(b.data(), n - 2);
        b[n - 2] = (uint8_t)c; b[n - 1] = (uint8_t)(c >> 8);
        if (puz::load(b.data(), (uint16_t)n)) {
            char clue[puz::CLUE_MAX];
            for (int w = 0; w < puz::nWords; w++) puz::clue((uint8_t)w, clue);
        }
    }
}

int main() {
    testDecode();
    testRules();
    testWhole(true);
    testWhole(false);
    testSave();
    testDamage();
    printf("%ld checks, %ld failed\n", checks, fails);
    return fails ? 1 : 0;
}
