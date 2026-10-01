// Saving options, best times and a puzzle in progress.
//
// CHGame has no EEPROM, but its bootloader only erases the flash pages a new
// sketch occupies, so the last pages of the application region survive
// re-uploads. Two pages are used in turn, each record carrying a sequence
// number and a CRC, so a power cut mid-write can only lose the newest save.
// If the sketch ever grows into those pages, saving switches itself off
// rather than overwrite code. (From CHBlackjack, with its own magic: the
// games share the pages, and each ignores the others' records.)
#pragma once
#include <stdint.h>
#include "../game/Game.h"

struct Options {
    uint8_t sound;      // 0 off, 1 on
    uint8_t felt;       // table colour theme (pal::Theme)
    uint8_t check;      // 0: words lock as they are completed; 1: no checking
    uint8_t skip;       // 0: typing steps over filled cells; 1: it goes cell by cell
    uint8_t view;       // 0: the whole grid, B held for the close-up; 1: the other way round
    uint8_t pad[3];
};

// What has been solved, and how well.
struct Progress {
    static const uint8_t BUILTIN = 20, CARD = 2;
    // A built-in puzzle's best: seconds (14 bits; 0 = not solved yet) with the
    // stars above them, and the best score in hundreds.
    struct Best { uint8_t timeLo, timeHi, score; } best[BUILTIN];
    // The last packs played from the card: which of their puzzles are solved.
    struct Card { uint32_t solved; uint16_t id, pad; } card[CARD];
};

namespace save {

bool available();                   // false: image too big, or a write failed
bool load(Options &o, Progress &p, bool &hasGame);
bool loadGame(game::Record &g);     // the saved puzzle in progress
// Call after gfx_wait(): the page is built in CHGfx's chunk scratch.
bool store(const Options &o, const Progress &p, const game::Record *g);

}  // namespace save
