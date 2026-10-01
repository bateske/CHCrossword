// One frame of the game: input, logic, drawing, the DMA flush.
#pragma once
#include <stdint.h>

namespace frame {

void begin();
bool run();                         // true if a frame was due and ran

}  // namespace frame
