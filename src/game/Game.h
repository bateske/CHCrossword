// A puzzle in play: entering letters, what locks, the score and the clock.
//
// With checking on (the default), a word locks the moment it is complete
// and right - gold tiles, points, a streak that multiplies them - and a
// complete wrong word costs a little and ends the streak. One word of every
// puzzle, always the same one, is its jackpot: it pays three times over. With checking
// off there are no locks: the grid is judged only when every cell is right.
// No graphics in here: the host tests build this file as it is.
#pragma once
#include <stdint.h>
#include "Puzzle.h"

namespace game {

// --- The scoring table --------------------------------------------------------
constexpr uint16_t PER_LETTER = 10;         // a locked word: 10 a letter, times the multiplier
constexpr uint8_t  STREAK_STEP = 3;         // every 3 words in a row: +1 to the multiplier
constexpr uint8_t  MULT_MAX = 5;
constexpr uint16_t QUICK_TICKS = 8 * 60;    // a lock within 8 s of the last one...
constexpr uint16_t QUICK_PER_LETTER = 5;    // ... is worth this much more a letter
constexpr uint16_t CROSS_BONUS = 50;        // one letter completing two words, times the multiplier
constexpr uint8_t  JACKPOT_TIMES = 3;       // the jackpot word pays this many times over
constexpr uint16_t WRONG_COST = 20;
constexpr uint16_t REVEAL_COST = 50;
constexpr uint16_t CHECK_COST = 25;
constexpr uint16_t SOLVED_BONUS = 500;
constexpr uint16_t CLEAN_BONUS = 250;       // no letter revealed
constexpr uint16_t PERFECT_BONUS = 1000;    // ... and not one wrong word
constexpr uint8_t  PAR_PER_CELL = 6;        // seconds a cell: under par earns 2 points a second

struct State {
    uint32_t ticks;             // frames on the clock (60 a second)
    uint32_t lastLock;          // the clock at the last lock
    uint16_t score;
    uint8_t streak;             // words locked in a row without a mistake
    uint8_t misses, reveals;    // wrong words; letters revealed
    uint8_t checking;           // 1: words lock as they are completed
    uint8_t solved;
    uint8_t jackpot;            // the puzzle's jackpot word (puz::NONE with checking off)
};
extern State st;

// What entering a letter did.
struct Events {
    uint8_t nLock, lock[2];     // words that locked
    uint8_t nWrong, wrong[2];   // words completed wrong
    uint16_t points[2];         // what each lock was worth
    uint16_t bonus;             // the CROSS bonus
    bool quick, cross, solved, changed;
    bool jackpot;               // one of the locks was the jackpot word
};

// The end of a solved puzzle.
struct Result {
    uint16_t seconds, par;
    uint16_t timeBonus, extra;  // extra: the solved, clean and perfect bonuses
    uint16_t score;             // the final score (the bonuses included)
    uint8_t stars;              // 1..3
    bool clean, perfect;
};

void start(bool checking);              // after puz::load: an empty grid, the clock at 0
inline void tick() { if (!st.solved) st.ticks++; }
inline uint16_t seconds() { return (uint16_t)(st.ticks / 60 > 0xFFFF ? 0xFFFF : st.ticks / 60); }
uint8_t multiplier();

// Enter a letter (1..26; 0 rubs out). A locked cell is left alone.
Events place(uint8_t cell, uint8_t letter);
// Show a cell's letter, at a price. Nothing happens on a cell already right and locked.
Events reveal(uint8_t cell);
// Checking off: rub out a word's wrong letters, at a price. Returns how many.
uint8_t checkWord(uint8_t w);
Result finish();                        // call once, when solved

// A game in progress, for the save page. Cells are packed five bits each
// (0 empty, 1..26 a letter, 27 the revealed right letter).
struct Record {
    uint32_t packId;
    uint32_t ticks;
    uint16_t score;
    uint8_t source, index;              // which pack (0 built-in) and puzzle
    uint8_t streak, misses, reveals, checking;
    uint8_t cursor, down;
    uint8_t cells[141];
};
void save(Record &r);
void load(const Record &r);             // after puz::load of the same puzzle

}  // namespace game
