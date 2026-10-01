// A small sound sequencer for the CHGame piezo (from CHBlackjack).
//
// Effects play on TIM1 channel 2 (PB10), stepped by the core's 1 kHz
// SysTick hook (osSystickHandler), so no other timer is used. One pin plays
// one note at a time; a higher-priority effect is never cut off by a lower
// one. tools/audio/preview.py renders every effect to WAV from this file.
#pragma GCC optimize("Os", "no-ipa-sra")
#include <Arduino.h>
#include "Audio.h"

#ifdef CHSIM
// The simulator is silent: same interface, no hardware. It remembers the
// last effect so scripts and tests can check what would have sounded.
namespace audio {
static bool simOn = true;
uint8_t simLast = 0xFF;
bool begin(bool on) { simOn = on; return true; }
void setOn(bool on) { simOn = on; }
void sfx(Sfx s) { if (simOn) simLast = (uint8_t)s; }
void note(uint16_t, uint8_t) {}
bool playing() { return false; }
void update() {}
void led(Led) {}
}
#else

// An effect step in three bytes: the pitch and the pitch it sweeps to (0:
// none) in 20 Hz units, and its length in 2 ms units; pitch 0 = a rest.
// (20 Hz is under half a percent of these pitches: no ear hears it on a
// piezo, and the tables are half the size.)
struct Step { uint8_t hz, endHz, ms; };

#define S(hz, end, ms) { (uint8_t)(((hz) + 10) / 20), (uint8_t)(((end) + 10) / 20), (uint8_t)(((ms) + 1) / 2) }
#define REST(ms)       { 0, 0, (uint8_t)(((ms) + 1) / 2) }

static const Step CURSOR[]  = { S(2100, 0, 10) };
static const Step SELECT[]  = { S(1700, 0, 18), S(2600, 0, 30) };
static const Step DENY[]    = { S(900, 650, 70) };
// A letter set down: a knock and a higher tick. Rubbed out: a slide down.
static const Step KEY[]     = { S(2400, 1100, 14), REST(8), S(3300, 0, 16) };
static const Step RUB[]     = { S(2000, 1200, 30) };
static const Step OPEN[]    = { S(1500, 2600, 40) };
static const Step CLOSE[]   = { S(2600, 1500, 40) };
static const Step COIN[]    = { S(2800, 0, 10), S(3700, 0, 28) };
// A wrong word: a buzzer (tones alternating high and low read as noise on
// a piezo).
static const Step WRONG[]   = {
    S(700, 0, 8), S(3200, 0, 6), S(600, 0, 8), S(2800, 0, 6), S(520, 0, 10), S(2400, 0, 6),
    S(480, 0, 12), S(600, 420, 160) };
static const Step REVEAL[]  = { S(2637, 0, 70), S(1976, 0, 70), S(1568, 0, 120) };
// One letter, two words.
static const Step CROSS[]   = {
    S(1568, 0, 45), S(2093, 0, 45), S(2637, 0, 45), S(3136, 0, 45), S(4186, 0, 60),
    S(3136, 0, 30), S(4186, 0, 30), S(3136, 0, 30), S(4186, 0, 120) };
// CHBlackjack's BLACKJACK fanfare and its win tune, back to back.
static const Step SOLVED[]  = {
    S(1568, 0, 50), S(2093, 0, 50), S(2637, 0, 50), S(3136, 0, 90),
    S(2093, 0, 40), S(2637, 0, 40), S(2093, 0, 40), S(2637, 0, 40),
    S(3136, 0, 40), S(4186, 0, 40), S(3136, 0, 40), S(4186, 0, 40),
    S(2000, 4200, 220), REST(80),
    S(2093, 0, 110), S(2637, 0, 110), S(3136, 0, 110), S(4186, 0, 220), REST(60),
    S(3520, 0, 110), S(4186, 0, 330) };
static const Step STAR[]    = { S(2637, 0, 40), S(3520, 0, 40), S(4186, 0, 90) };
static const Step TITLE[]   = {
    S(1568, 0, 90), S(2093, 0, 90), S(2637, 0, 90), S(3136, 0, 180), REST(40),
    S(2637, 0, 90), S(3136, 0, 360) };

struct SfxDef { const Step *steps; uint8_t n, prio; };
#define DEF(a, p) { a, (uint8_t)(sizeof(a) / sizeof(a[0])), p }
static const SfxDef DEFS[(int)Sfx::COUNT] = {
    DEF(CURSOR, 0), DEF(SELECT, 1), DEF(DENY, 1), DEF(KEY, 0), DEF(RUB, 0), DEF(OPEN, 1),
    DEF(CLOSE, 1), DEF(COIN, 1), DEF(WRONG, 3), DEF(REVEAL, 2), DEF(CROSS, 3),
    DEF(SOLVED, 4), DEF(STAR, 3), DEF(TITLE, 2),
};

// --- Sequencer state (shared with the 1 kHz interrupt) ----------------------
static volatile const Step *fxSteps = nullptr;
static volatile uint8_t fxN = 0, fxI = 0, fxPrio = 0;
static volatile uint16_t fxT = 0;

static bool started = false, running = false;
static uint16_t lastHz = 0;
static Step oneNote;                 // audio::note's
static uint8_t ledPattern = 0;
static uint16_t ledT = 0;

extern "C" volatile uint32_t CFGHR_tmpB;        // GPIOB CFGHR is write-only: go through the shadow

static void pb10(uint32_t nibble) {
    uint32_t v = (CFGHR_tmpB & ~(15u << 8)) | (nibble << 8);
    CFGHR_tmpB = v;
    GPIOB->CFGHR = v;
}

static void hwInit() {
    RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM1;
    AFIO->PCFR1 = (AFIO->PCFR1 & ~(7u << 15)) | (1u << 15);   // TIM1 partial remap: CH2 on PB10
    GPIOB->BCR = 1u << 10;
    pb10(11u);                                                // alternate-function push-pull
    TIM1->CTLR1 = 0; TIM1->CTLR2 = 0; TIM1->SMCFGR = 0; TIM1->DMAINTENR = 0;
    TIM1->CCER = 0;
    TIM1->CHCTLR1 = 0x6800;                                   // CH2 PWM1 + preload
    TIM1->CHCTLR2 = 0;
    TIM1->PSC = 47;                                           // 1 MHz
    TIM1->RPTCR = 0;
    TIM1->ATRLR = 999;
    TIM1->CH2CVR = 0;
    TIM1->CNT = 0;
    TIM1->BDTR = 0x8000;                                      // MOE
    TIM1->CCER = 0x10;
    TIM1->SWEVGR = 1;
    TIM1->INTFR = 0;
    running = false; lastHz = 0;
}

static void tone(uint16_t hz) {
    if (hz == lastHz) return;
    lastHz = hz;
    if (!hz) {
        TIM1->CH2CVR = 0; TIM1->SWEVGR = 1; TIM1->CTLR1 = 0; TIM1->INTFR = 0;
        running = false;
        return;
    }
    uint32_t period = (1000000u + hz / 2u) / hz;
    if (period < 2) period = 2;
    running = true;
    TIM1->CTLR1 = 0;
    TIM1->ATRLR = period - 1;
    TIM1->CH2CVR = period / 2;
    TIM1->SWEVGR = 1;
    TIM1->INTFR = 0;
    TIM1->CTLR1 = 0x81;
}

extern "C" void osSystickHandler(void) {
    if (!started) return;
    const Step *s = (const Step *)fxSteps;
    if (!s) { tone(0); return; }
    const Step &st = s[fxI];
    uint16_t hz = (uint16_t)(st.hz * 20), ms = (uint16_t)(st.ms * 2);
    if (hz && st.endHz) hz = (uint16_t)(hz + ((int32_t)st.endHz * 20 - hz) * fxT / ms);
    if (++fxT >= ms) {
        fxT = 0;
        if (++fxI >= fxN) { fxSteps = nullptr; fxPrio = 0; }
    }
    tone(hz);
}

namespace audio {

bool begin(bool on) {
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOB;
    uint32_t v = (CFGHR_tmpB & ~(15u << 4)) | (3u << 4);          // PB9 LED: push-pull output
    CFGHR_tmpB = v;
    GPIOB->CFGHR = v;
    GPIOB->BCR = 1u << 9;
    if (!on) { started = false; lastHz = 1; tone(0); return true; }
    if (!started) hwInit();
    started = true;
    return true;
}

void setOn(bool on) { begin(on); }

void sfx(Sfx s) {
    const SfxDef &d = DEFS[(int)s];
    if (!started) return;
    if (fxSteps && d.prio < fxPrio) return;
    __disable_irq();
    fxSteps = d.steps; fxN = d.n; fxI = 0; fxT = 0; fxPrio = d.prio;
    __enable_irq();
}

// A note of a locking word: it gives way only to the fanfares.
void note(uint16_t hz, uint8_t ms) {
    if (!started || (fxSteps && fxPrio > 2)) return;
    __disable_irq();
    oneNote.hz = (uint8_t)((hz + 10) / 20); oneNote.endHz = 0; oneNote.ms = (uint8_t)(ms / 2);
    fxSteps = &oneNote; fxN = 1; fxI = 0; fxT = 0; fxPrio = 2;
    __enable_irq();
}

bool playing() { return fxSteps != nullptr; }

void led(Led p) { ledPattern = p; ledT = 0; }

void update() {
    if (!ledPattern) return;
    ledT++;
    bool on = false;
    switch (ledPattern) {
        case LED_BLINK:  on = ledT < 12; if (ledT > 12) ledPattern = 0; break;
        case LED_TRIPLE: on = (ledT % 16) < 8; if (ledT > 48) ledPattern = 0; break;
        case LED_PARTY:  on = (ledT % 8) < 4; if (ledT > 240) ledPattern = 0; break;
    }
    if (on && ledPattern) GPIOB->BSHR = 1u << 9;
    else GPIOB->BCR = 1u << 9;
}

}  // namespace audio
#endif  // CHSIM
