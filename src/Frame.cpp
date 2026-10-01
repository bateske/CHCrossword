#pragma GCC optimize("Os", "no-ipa-sra")
#include <Arduino.h>
#include <CHGfx.h>
#include "../config.h"
#include "Frame.h"
#include "CHGame.h"
#include "gfx/Palette.h"
#include "states/Screens.h"
#include "debug/Debug.h"
#include "audio/Audio.h"

namespace frame {

void begin() {
    dbg::paintStack();
    pal::init();
    screens::begin();
}

// Logic runs while the previous frame is still going out over DMA; drawing
// waits for it (one framebuffer), then the new frame is sent.
bool run() {
    dbg::poll();
    if (!arduboy.nextFrame()) return false;
    dbg::markUpdateStart();
    // Logic runs at a fixed 60 Hz. If a heavy frame made drawing fall
    // behind, catch up (up to three ticks) before drawing again.
    uint8_t ticks = 0;
    do {
        arduboy.pollButtons();
        pal::tick();
        audio::update();
        screens::update();
    } while (++ticks < 3 && arduboy.nextFrame());
    gfx_wait();
    pal::commit();
    dbg::markRenderStart();
    screens::render(arduboy.frameCount);
    dbg::markRenderEnd();
    gfx_flushAsync();
    return true;
}

}  // namespace frame
