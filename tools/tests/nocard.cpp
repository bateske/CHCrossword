// No card in the slot, for the tests that are not about the card.
#include "../../src/sd/SdSpi.h"

namespace sd {
bool init() { return false; }
bool read(uint32_t, uint8_t *) { return false; }
}  // namespace sd
