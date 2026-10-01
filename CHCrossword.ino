// CHCrossword - a crossword for the CHGame handheld (CH32X035, 128x128
// ST7735, piezo), in the look of CHBlackjack and the casino games after it.
//
// The puzzles are packed in src/game/PuzzleData.cpp (from tools/puzzles),
// with more in packs on the SD card (src/pack, src/sd); the rules and the
// score are in src/game, and everything you see and hear in src/stage and
// src/states.
#include "config.h"
#include <CHGfx.h>
#include "src/CHGame.h"
#include "src/Frame.h"

void setup() {
    arduboy.boot();
    gfx_begin(GFX_DIV2, GFX_12BPP);
    frame::begin();
    arduboy.setFrameRate(CHCW_FPS);
}

void loop() {
    frame::run();
}
