// The simulator's SD card: the file named by $CHCW_CARD (a WORDS.DIC from
// tools/dict/build_sd.py) stands in for the card's blocks, the file's block
// k at "LBA" k. With the variable unset there is no card, which is how the
// scripts in tools/scripts run unless they say otherwise. The debug
// protocol's X command pulls the card out and puts it back.
#include <stdio.h>
#include <stdlib.h>
#include "sim.h"
#include "src/sd/SdSpi.h"

static FILE *s_card;
static bool s_out;                  // pulled out

static FILE *card() {
    if (!s_card && getenv("CHCW_CARD")) s_card = fopen(getenv("CHCW_CARD"), "rb");
    return s_out ? nullptr : s_card;
}

uint32_t sim_cardBlocks() {
    FILE *f = card();
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    return (uint32_t)(ftell(f) / 512);
}

void sim_cardEject(bool out) { s_out = out; }

namespace sd {

bool init() { return card() != nullptr; }

bool read(uint32_t lba, uint8_t *dst) {
    FILE *f = card();
    if (!f || fseek(f, (long)lba * 512, SEEK_SET)) return false;
    sim_advance(900);               // about what a polled block read takes on the board
    return fread(dst, 1, 512, f) == 512;
}

}  // namespace sd
