// The screens: title, setup, play (with pause and the result), options.
#pragma once
#include <stdint.h>

namespace screens {

void begin();
void update();                      // logic, before gfx_wait
void render(uint32_t frame);        // after gfx_wait

}  // namespace screens
