#pragma GCC optimize("Os", "no-ipa-sra")
#include <string.h>
#include "Game.h"

namespace game {

using namespace puz;

State st;

void start(bool checking) {
    memset(&st, 0, sizeof st);
    st.checking = checking;
    // The jackpot word: one of the longer words, picked by the puzzle's own
    // letters, so it is the same every time the puzzle is played.
    st.jackpot = NONE;
    if (!checking) return;
    uint32_t h = 0, longest = 0;
    for (uint16_t c = 0; c < n * n; c++) h = h * 31 + sol[c];
    for (uint8_t w = 0; w < nWords; w++) if (wLen[w] > longest) longest = wLen[w];
    uint32_t want = longest > 6 ? 6 : longest, count = 0;
    for (uint8_t w = 0; w < nWords; w++) count += wLen[w] >= want;
    uint32_t pick = h % count;
    for (uint8_t w = 0; w < nWords; w++)
        if (wLen[w] >= want && !pick--) { st.jackpot = w; break; }
}

uint8_t multiplier() {
    uint8_t m = (uint8_t)(1 + st.streak / STREAK_STEP);
    return m > MULT_MAX ? MULT_MAX : m;
}

static void add(int32_t v) {
    int32_t s = (int32_t)st.score + v;
    st.score = (uint16_t)(s < 0 ? 0 : s > 0xFFFF ? 0xFFFF : s);
}

static bool hasRevealed(uint8_t w) {
    for (uint8_t k = 0; k < wLen[w]; k++)
        if (cell[cellOf(w, k)] & REVEALED) return true;
    return false;
}

static bool allRight() {
    for (uint16_t c = 0; c < n * n; c++)
        if ((cell[c] & LETTER) != sol[c]) return false;
    return true;
}

// The words through a cell, now that its letter changed: lock the complete
// right ones, charge for the complete wrong ones.
static void judge(uint8_t c, Events &e, bool charge) {
    if (!st.checking) {
        e.solved = allRight();
        st.solved = e.solved;
        return;
    }
    bool quick = lockedWords() && st.ticks - st.lastLock <= QUICK_TICKS;
    for (uint8_t d = 0; d < 2; d++) {
        uint8_t w = wordAt(c, d);
        if (w == NONE || wordLocked(w) || !wordFull(w)) continue;
        if (wordRight(w)) {
            uint32_t p = (uint32_t)PER_LETTER * wLen[w] * multiplier();
            if (quick) p += (uint32_t)QUICK_PER_LETTER * wLen[w];
            if (hasRevealed(w)) p /= 2;
            if (w == st.jackpot) { p *= JACKPOT_TIMES; e.jackpot = true; }
            lockWord(w);
            e.points[e.nLock] = (uint16_t)p;
            e.lock[e.nLock++] = w;
            add((int32_t)p);
        } else if (charge) {
            e.wrong[e.nWrong++] = w;
        }
    }
    if (e.nLock) {
        e.quick = quick;
        if (e.nLock == 2) {
            e.cross = true;
            e.bonus = (uint16_t)(CROSS_BONUS * multiplier());
            add(e.bonus);
        }
        if (st.streak < 250) st.streak = (uint8_t)(st.streak + e.nLock);
        st.lastLock = st.ticks;
    } else if (e.nWrong) {
        // One charge however many words the letter spoiled.
        add(-(int32_t)WRONG_COST);
        st.streak = 0;
        if (st.misses < 255) st.misses++;
    }
    e.solved = lockedWords() == nWords;
    st.solved = e.solved;
}

Events place(uint8_t c, uint8_t letter) {
    Events e;
    memset(&e, 0, sizeof e);
    if (st.solved || !sol[c] || (cell[c] & LOCKED) || (cell[c] & LETTER) == letter) return e;
    cell[c] = letter;
    e.changed = true;
    if (letter) judge(c, e, true);
    return e;
}

Events reveal(uint8_t c) {
    Events e;
    memset(&e, 0, sizeof e);
    if (st.solved || !sol[c] || (cell[c] & LOCKED)) return e;
    cell[c] = (uint8_t)(sol[c] | REVEALED);
    e.changed = true;
    add(-(int32_t)REVEAL_COST);
    st.streak = 0;
    if (st.reveals < 255) st.reveals++;
    judge(c, e, false);
    return e;
}

uint8_t checkWord(uint8_t w) {
    uint8_t bad = 0;
    for (uint8_t k = 0; k < wLen[w]; k++) {
        uint8_t c = cellOf(w, k);
        if ((cell[c] & LETTER) && (cell[c] & LETTER) != sol[c]) { cell[c] = 0; bad++; }
    }
    add(-(int32_t)CHECK_COST);
    if (bad && st.misses < 255) st.misses++;
    return bad;
}

Result finish() {
    Result r;
    memset(&r, 0, sizeof r);
    r.seconds = seconds();
    r.par = (uint16_t)(whites * PAR_PER_CELL);
    r.clean = st.reveals == 0;
    r.perfect = r.clean && st.misses == 0;
    r.timeBonus = r.seconds < r.par ? (uint16_t)((r.par - r.seconds) * 2) : 0;
    r.extra = (uint16_t)(SOLVED_BONUS + (r.perfect ? PERFECT_BONUS : r.clean ? CLEAN_BONUS : 0));
    add(r.timeBonus);
    add(r.extra);
    r.score = st.score;
    r.stars = 1;
    if (r.clean && r.seconds < r.par) r.stars = 2;
    if (r.clean && st.misses <= 3 && (uint32_t)r.seconds * 3 < (uint32_t)r.par * 2) r.stars = 3;
    return r;
}

// --- The save page ------------------------------------------------------------
static const uint8_t REVEALED_CODE = 27;

void save(Record &r) {
    r.ticks = st.ticks;
    r.score = st.score;
    r.streak = st.streak;
    r.misses = st.misses;
    r.reveals = st.reveals;
    r.checking = st.checking;
    memset(r.cells, 0, sizeof r.cells);
    for (uint16_t c = 0; c < n * n; c++) {
        uint32_t v = (cell[c] & REVEALED) ? REVEALED_CODE : (cell[c] & LETTER);
        uint32_t bit = c * 5u;
        uint32_t word = v << (11 - (bit & 7));          // 5 bits within a 16-bit window
        r.cells[bit >> 3] |= (uint8_t)(word >> 8);
        if ((bit >> 3) + 1 < sizeof r.cells) r.cells[(bit >> 3) + 1] |= (uint8_t)word;
    }
}

void load(const Record &r) {
    start(r.checking);              // (the jackpot word with it: it comes from the puzzle)
    st.ticks = r.ticks;
    st.score = r.score;
    st.streak = r.streak;
    st.misses = r.misses;
    st.reveals = r.reveals;
    for (uint16_t c = 0; c < n * n; c++) {
        uint32_t bit = c * 5u;
        uint32_t word = (uint32_t)r.cells[bit >> 3] << 8;
        if ((bit >> 3) + 1 < sizeof r.cells) word |= r.cells[(bit >> 3) + 1];
        uint8_t v = (uint8_t)((word >> (11 - (bit & 7))) & 31);
        if (!sol[c] || v > REVEALED_CODE) v = 0;
        cell[c] = v == REVEALED_CODE ? (uint8_t)(sol[c] | REVEALED) : v;
    }
    // Locks are not stored: a complete, right word is locked.
    clearLocks();
    if (st.checking)
        for (uint8_t w = 0; w < nWords; w++)
            if (wordFull(w) && wordRight(w)) lockWord(w);
    st.solved = st.checking ? lockedWords() == nWords : allRight();
}

}  // namespace game
