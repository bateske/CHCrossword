// CHSd 1.0.0 (generated: edit CHSd/host/sd_host.cpp, then run CHSd/tools/vendor.py)
// The simulator's SD card (CHSd's stand-in for src/sd/SdSpi.cpp): the file
// named by $CHCW_CARD is in the slot. A FAT image (*.img, e.g. from CHSd's
// tools/fatimg.py) is served as the card's blocks; any other file is put on
// a pretend FAT16 card of its own (VCard.h), so the game's FAT code runs in
// the simulator just as on the board. With the variable unset there is no
// card. The debug protocol's eject command pulls it out and puts it back.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim.h"
#include "VCard.h"
#include "src/sd/SdSpi.h"

static FILE *s_card;
static bool s_out, s_image;         // pulled out; a whole image, not a file to wrap
static vcard::Card s_vc;

static FILE *card() {
    const char *path = getenv("CHCW_CARD");
    if (!s_card && path && (s_card = fopen(path, "rb"))) {
        size_t n = strlen(path);
        s_image = n > 4 && (!strcmp(path + n - 4, ".img") || !strcmp(path + n - 4, ".IMG"));
        fseek(s_card, 0, SEEK_END);
        s_vc.setup(path, (uint32_t)ftell(s_card));
    }
    return s_out ? nullptr : s_card;
}

uint32_t sim_cardBlocks() {
    FILE *f = card();
    if (!f) return 0;
    if (!s_image) return s_vc.blocks();
    fseek(f, 0, SEEK_END);
    return (uint32_t)(ftell(f) / 512);
}

void sim_cardEject(bool out) { s_out = out; }

namespace sd {

bool init() { return card() != nullptr; }

bool read(uint32_t lba, uint8_t *dst) {
    FILE *f = card();
    if (!f || lba >= sim_cardBlocks()) return false;
    sim_advance(900);               // about what a polled block read takes on the board
    long k = s_image ? (long)lba : s_vc.read(lba, dst);
    if (k < 0) return true;
    memset(dst, 0, 512);            // (a file's last block, past its end)
    return !fseek(f, k * 512, SEEK_SET) && fread(dst, 1, 512, f) > 0;
}

}  // namespace sd
