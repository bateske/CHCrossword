// CHCrossword build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the CHGame core 0.2.4+ and its default Peripherals setting ("Game",
// which compiles out Serial1/tone/HardwareTimer: ~4 KB of flash). It is
// built with Optimize set to "Smallest + LTO" and, for release, USB set to
// "Upload only" (no Serial: ~0.6 KB); it fits without either.
#pragma once

#define CHCW_VERSION     "0.1"

// Serial debug protocol: screenshots, input injection, lockstep, perf.
// Off in normal builds. tools/device.py turns it on with
// --build-property build.extra_flags, and leaves USB at "Serial" for it.
#ifndef CHCW_DEBUG
#ifdef CHSIM
#define CHCW_DEBUG       1       // the simulator is driven through the protocol
#else
#define CHCW_DEBUG       0
#endif
#endif

// A device debug build carries the ~3 KB protocol and USB Serial, which the
// whole game no longer leaves room for: CHCW_LEAN leaves saving, the
// options screen and all but the first three built-in puzzles out of it
// (puzzles are started with the protocol's G command). tools/device.py sets it for debug builds: -DCHCW_LEAN=1.
#ifndef CHCW_LEAN
#define CHCW_LEAN        0
#endif

// Section profiler (dbg::prof + the T command). Opt-in: costs flash.
#ifndef CHCW_PROFILE
#define CHCW_PROFILE     0
#endif

// Frame rate the game logic is paced for.
#define CHCW_FPS         60
