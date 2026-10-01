// Serial debug protocol (CHCW_DEBUG builds only).
//
// Line-based ASCII over the USB CDC port. Unknown input is ignored and never
// answered, so it cannot confuse chgame-upload.
//
//   ?          -> "CHCW <ver>"
//   S          -> "FB <frame> 8224\n" then 8192 framebuffer + 32 palette bytes
//   K <hex>    hold these buttons (ORed with the real ones); K alone releases
//   L1 / L0    lockstep on / off (on = the game only advances on N)
//   N <k>      run k frames, then answer "OK <frame>"
//   P          -> "PERF rnd=<us> max=<us> late=<n> frames=<n> stk=<bytes>" (render time, late
//                 frames, the stack's high-water mark; then they start again)
//   T          -> "PROF <slot>=<us> ..." section timings from dbg::prof()
//   B          -> reboot into the CHGame bootloader
//   anything else is offered to the game's hook (start a game, stack the dice, ...)
#pragma once
#include "../../config.h"
#include <stdint.h>

namespace dbg {

#if CHCW_DEBUG
void poll();
void paintStack();                  // at boot: for the stack high-water mark in P
void markUpdateStart();
void markRenderStart();
void markRenderEnd();
void print(const char *s);
// Profiling (CHCW_PROFILE builds): prof(i) charges the time since the
// previous prof() to slot i; the T command reports and resets the averages.
#if CHCW_PROFILE
void profStart();
void prof(uint8_t slot);
#else
inline void profStart() {}
inline void prof(uint8_t) {}
#endif
uint32_t parseNum(const char *&p, uint8_t base);   // skips leading spaces/commas
// Game-specific commands: return true if handled.
extern bool (*hook)(char cmd, const char *args);
#else
inline void poll() {}
inline void paintStack() {}
inline void markUpdateStart() {}
inline void markRenderStart() {}
inline void markRenderEnd() {}
inline void print(const char *) {}
inline void profStart() {}
inline void prof(uint8_t) {}
#endif

}  // namespace dbg
