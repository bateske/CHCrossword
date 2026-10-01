// Where puzzles come from: the built-in pack in flash, and packs on the SD
// card (files *.CWD in the folder CHCW; made by tools/puzzles/build_pack.py
// or tools/puzzles/puz2cwd.py). A pack's layout is in tools/puzzles/cwformat.py.
//
// BUS RULE: the card shares SPI1 with the LCD. scan(), select(), peek() and
// open() may read the card: call them only after gfx_wait(), from update -
// never while drawing (they borrow CHGfx's chunk scratch as a sector buffer,
// which the lettering masks use during render).
#pragma once
#include <stdint.h>

namespace pack {

constexpr uint8_t MAX_PACKS = 9;        // the built-in one and eight from the card

enum Card : uint8_t { CARD_NONE, CARD_UNREADABLE, CARD_EXFAT, CARD_EMPTY, CARD_OK };

// Looks at the card afresh: which packs are on it. Pack 0 is always the
// built-in one.
void scan();
Card card();
uint8_t count();                        // packs, the built-in one included

// Makes pack p the current one (reads its header from the card). False: it
// could not be read (the card was pulled: scan() again).
bool select(uint8_t p);
uint8_t current();
const char *name();                     // the current pack's
uint8_t puzzles();
uint32_t id();
// Puzzle i of the current pack: its size, difficulty and title, for the list.
bool peek(uint8_t i, uint8_t &size, uint8_t &diff, char *title);
// Puzzle i into puz (puz::load). A card puzzle is copied to RAM first; the
// card is not needed again while it is played.
bool open(uint8_t i);
// The pack with this id made current (a saved game's). False: not there.
bool find(uint32_t packId);

}  // namespace pack
