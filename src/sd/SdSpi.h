// SdSpi - a read-only SPI-mode microSD block driver (MIT, see NOTICE).
//
// HypeRunner's clean-room driver cut down to what a dictionary lookup needs:
// identify the card, read one 512-byte block. No writing, no DMA (CHGfx owns
// DMA1 channel 3 and its interrupt), no CRC (the dictionary file checks its
// own blocks).
//
// BUS RULE: SPI1 is shared with the LCD. Call these only after gfx_wait(),
// before the next flush. SPI1 is handed back exactly as CHGfx left it.
#pragma once
#include <stdint.h>

namespace sd {

bool init();                                // false: no card, or not one this can read
bool read(uint32_t lba, uint8_t *dst);      // one block; false: the card did not deliver

}  // namespace sd
