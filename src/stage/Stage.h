// The play screen: the grid, the clue under it, the score beside it, the
// letter board that slides up to type on, and the show when a word locks.
//
// The whole grid is in view (8 px cells up to 13 x 13, 7 px for 14 and 15)
// until the camera whips in to the close-up, twice the size, round the
// cursor: tiles with edges, the words' numbers in their corners, letters in
// a large face. It does while the letter board is up, and while B is held. The screens (src/states) own the
// buttons; this owns the cursor and what it does.
#pragma once
#include <stdint.h>
#include "../game/Game.h"

namespace stage {

constexpr uint8_t KEYS = 27, KEY_COLS = 9, KEY_DEL = 26;     // A..Z and rub out

extern uint8_t cur;                 // the cell under the cursor
extern bool down;                   // the way the word being worked on runs
extern bool board;                  // the letter board is up
extern uint8_t key;                 // the key under the glove
extern bool stepAll;                // typing moves cell by cell (else on to the next empty one)
extern bool viewClose;              // the option: play in the close-up
extern bool peek;                   // B held: the other view for as long as it is

void enter();                       // a puzzle was loaded (or a saved one put back): onto the screen
void setCursor(uint8_t cell, bool down);
uint8_t word();                     // the word under the cursor (puz::NONE: none)

bool move(int dx, int dy);          // the cursor to the next open cell that way
void flip();                        // across <-> down
void nextClue();                    // the cursor to the next word not done yet
void openBoard();
void closeBoard();
void moveKey(int dx, int dy);
void press();                       // the key under the glove: a letter, or rub out
void rubOut();                      // the letter under the cursor, else the one before
void reveal();                      // the cursor's letter (the pause menu)
void checkWord();                   // checking off: rub out the word's wrong letters

// The close-up's art for other screens: a tile with a letter (1..26; 0 for
// none) on it, and a line of capitals in the large face (returns its width;
// draw = false only measures).
void bigTile(int x, int y, uint8_t face, uint8_t letter);
int bigText(int x, int y, const char *s, uint8_t ink, uint8_t mid, uint8_t shadow, bool draw = true);

void update();                      // once per logic tick
void invalidate();                  // draw the next frame whatever (an overlay changed)
// Draws the screen if anything on it changed since it was last drawn.
// False: nothing did, and the framebuffer is as it was (the palette still
// animates what is there).
bool render(uint32_t frame);
bool busy();                        // a word is locking, or the puzzle's last show is on
bool solvedShown();                 // the end-of-puzzle show has had its time

}  // namespace stage
