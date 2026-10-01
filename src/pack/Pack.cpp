#pragma GCC optimize("Os", "no-ipa-sra")
#include <string.h>
#include "Pack.h"
#include "../game/Puzzle.h"
#include "../game/PuzzleData.h"
#include "../sd/SdSpi.h"
#include "../sd/Fat.h"
#ifndef CHTEST
#include <CHGfx.h>
#endif

namespace pack {

// A pack's header (cwformat.py): "CHCW", version, codec, count, flags,
// name[12], id, crc16, 0, then count entries of offset << 12 | length.
static const uint8_t HEAD = 28;
static const uint8_t VERSION = 1, CODEC = 1, MAX_COUNT = 32;
static const uint16_t MAX_BLOB = 2048;
static const uint8_t MAX_RUNS = 4;              // a pack in more pieces than this is passed over

static Card state;
static uint8_t nPacks = 1, cur;
static fat::File files[MAX_PACKS - 1];          // the card's packs
static fat::Run runs[MAX_RUNS];                 // where the current one is
static uint8_t nRuns;
alignas(4) static uint8_t head[HEAD + 4 * MAX_COUNT];   // its header and index
alignas(4) static uint8_t blob[MAX_BLOB];       // the card puzzle in play

// A sector buffer, and room for two (a puzzle's heading across a boundary):
// CHGfx's chunk scratch, idle between gfx_wait() and the next flush.
#ifdef CHTEST
alignas(4) static uint8_t scratchBuf[1024];
static uint8_t *scratch() { return scratchBuf; }
#else
static uint8_t *scratch() { return gfx_chunkScratch(); }
#endif

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static const uint8_t *hdr() { return cur ? head : BUILTIN; }

// h: a pack's first bytes, the whole index included.
static bool goodHeader(const uint8_t *h) {
    if (memcmp(h, "CHCW", 4) || h[4] != VERSION || h[5] != CODEC || !h[6] || h[6] > MAX_COUNT || h[19]) return false;
    // The CRC covers the header up to itself, and the index.
    uint8_t tmp[24 + 4 * MAX_COUNT];
    memcpy(tmp, h, 24);
    memcpy(tmp + 24, h + HEAD, 4u * h[6]);
    return puz::crc16(tmp, 24 + 4u * h[6]) == (h[24] | h[25] << 8);
}

// Sector k of the current card pack into dst.
static bool sector(uint32_t k, uint8_t *dst) { return fat::read(runs, nRuns, k, dst); }

// The card went away (or gave rubbish): back to the built-in pack.
static bool lost() {
    state = CARD_NONE;
    nPacks = 1;
    cur = 0;
    return false;
}

// A card pack's extents and header. False if it is not a pack this build
// reads (the card itself may be fine).
static bool load(const fat::File &f) {
    uint8_t *b = scratch();
    int8_t n = fat::runs(f, runs, MAX_RUNS, b);
    if (n <= 0) return false;
    nRuns = (uint8_t)n;
    if (f.size < HEAD + 4u || !sector(0, b)) return false;
    memcpy(head, b, sizeof head);
    if (!goodHeader(head)) return false;
    // Every puzzle must lie inside the file.
    for (uint8_t i = 0; i < head[6]; i++) {
        uint32_t e = le32(head + HEAD + 4 * i), len = e & 0xFFF;
        if (len < 4 || len > MAX_BLOB || (e >> 12) + len > f.size) return false;
    }
    return true;
}

void scan() {
    cur = 0;
    nPacks = 1;
    state = CARD_NONE;
    if (!sd::init()) return;
    uint8_t *b = scratch();
    int8_t rc = fat::mount(b);
    if (rc) { state = rc == fat::E_EXFAT ? CARD_EXFAT : rc == fat::E_READ ? CARD_NONE : CARD_UNREADABLE; return; }
    state = CARD_EMPTY;
    fat::File dir;
    if (fat::folder("CHCW       ", dir, b)) return;
    for (uint8_t k = 0; k < 64 && nPacks < MAX_PACKS; k++) {
        fat::File f;
        rc = fat::match(dir, "????????CWD", k, f, nullptr, b);
        if (rc) break;
        if (!load(f)) continue;
        files[nPacks - 1] = f;
        nPacks++;
    }
    if (nPacks > 1) state = CARD_OK;
}

Card card() { return state; }
uint8_t count() { return nPacks; }

bool select(uint8_t p) {
    if (p >= nPacks) return false;
    cur = p;
    if (!p) return goodHeader(BUILTIN);
    if (!load(files[p - 1])) return lost();
    return true;
}

uint8_t current() { return cur; }
const char *name() { return (const char *)hdr() + 8; }
uint8_t puzzles() { return hdr()[6]; }
uint32_t id() { return le32(hdr() + 20); }

static void entry(uint8_t i, uint32_t &off, uint32_t &len) {
    uint32_t e = le32(hdr() + HEAD + 4 * i);
    off = e >> 12;
    len = e & 0xFFF;
}

bool peek(uint8_t i, uint8_t &size, uint8_t &diff, char *title) {
    if (i >= puzzles()) return false;
    uint32_t off, len;
    entry(i, off, len);
    if (!cur) return puz::peek(BUILTIN + off, (uint16_t)len, size, diff, title);
    // The sector the puzzle starts in, and the next if it runs on.
    uint8_t *b = scratch();
    uint32_t at = off & 511, have = 512 - at;
    if (!sector(off >> 9, b)) return lost();
    if (len > have) {
        if (!sector((off >> 9) + 1, b + 512)) return lost();
        have += 512;
    }
    return puz::peek(b + at, (uint16_t)(len < have ? len : have), size, diff, title);
}

bool open(uint8_t i) {
    if (i >= puzzles()) return false;
    uint32_t off, len;
    entry(i, off, len);
    if (!cur) return puz::load(BUILTIN + off, (uint16_t)len);
    uint8_t *b = scratch();
    for (uint32_t got = 0; got < len;) {
        uint32_t at = (off + got) & 511, take = 512 - at;
        if (take > len - got) take = len - got;
        if (!sector((off + got) >> 9, b)) return lost();
        memcpy(blob + got, b + at, take);
        got += take;
    }
    return puz::load(blob, (uint16_t)len);
}

bool find(uint32_t packId) {
    for (uint8_t p = 0; p < nPacks; p++)
        if (select(p) && id() == packId) return true;
    select(0);
    return false;
}

}  // namespace pack
