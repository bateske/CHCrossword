// Sound and the status LED.
//
// CHBlackjack's piezo sequencer: effects are short step lists (3 bytes a
// step) kept inside the piezo's 1-4 kHz sweet spot. There is no music
// player here - the fanfares are effects too.
#pragma once
#include <stdint.h>

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Key, Rub, Open, Close, Coin, Wrong, Reveal, Cross,
    Solved, Star, Title,
    COUNT
};

namespace audio {

bool begin(bool on);
void setOn(bool on);
void sfx(Sfx s);
// One note (a word's tiles locking, each a step up the scale).
void note(uint16_t hz, uint8_t ms);
bool playing();                     // an effect is sounding
void update();                      // once per frame: LED patterns

// Status LED (PB9): short patterns for wins.
enum Led : uint8_t { LED_OFF, LED_BLINK, LED_TRIPLE, LED_PARTY };
void led(Led pattern);

}  // namespace audio
