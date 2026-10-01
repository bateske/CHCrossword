#pragma GCC optimize("Os", "no-ipa-sra")
#include <string.h>
#include "Pack.h"
#include "../game/Puzzle.h"
#include "../game/PuzzleData.h"

namespace pack {

// A pack's header (cwformat.py): "CHCW", version, codec, count, flags,
// name[12], id, crc16, 0, then count entries of offset << 12 | length.
static const uint8_t HEAD = 28;
static const uint8_t VERSION = 1, CODEC = 1, MAX_COUNT = 32;

static uint8_t cur;
static uint8_t head[HEAD + 4 * MAX_COUNT];      // the current card pack's header and index

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static const uint8_t *hdr() { return cur ? head : BUILTIN; }

static bool goodHeader(const uint8_t *h) {
    if (memcmp(h, "CHCW", 4) || h[4] != VERSION || h[5] != CODEC || !h[6] || h[6] > MAX_COUNT) return false;
    // The CRC covers the header up to itself, and the index.
    uint8_t tmp[24 + 4 * MAX_COUNT];
    memcpy(tmp, h, 24);
    memcpy(tmp + 24, h + HEAD, 4u * h[6]);
    return puz::crc16(tmp, 24 + 4u * h[6]) == (h[24] | h[25] << 8);
}

void scan() { cur = 0; }
Card card() { return CARD_NONE; }
uint8_t count() { return 1; }

bool select(uint8_t p) {
    if (p) return false;
    cur = 0;
    return goodHeader(BUILTIN);
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
    return puz::peek(BUILTIN + off, (uint16_t)len, size, diff, title);
}

bool open(uint8_t i) {
    if (i >= puzzles()) return false;
    uint32_t off, len;
    entry(i, off, len);
    return puz::load(BUILTIN + off, (uint16_t)len);
}

bool find(uint32_t packId) {
    return select(0) && id() == packId;
}

}  // namespace pack
