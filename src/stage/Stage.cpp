#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed (hot pixel loops live in Draw/Mask)
#include <Arduino.h>
#include <string.h>
#include <CHGfx.h>
#include "../../config.h"
#include "Stage.h"
#include "../gfx/Palette.h"
#include "../gfx/Draw.h"
#include "../gfx/Fmt.h"
#include "../fx/Fx.h"
#include "../audio/Audio.h"
#include "../assets/Assets.h"

namespace stage {

using namespace puz;

uint8_t cur, key;
bool down, board, stepAll, viewClose, peek;

// --- Layout -------------------------------------------------------------------
static uint8_t cs;                  // cell pitch: 8, or 7 for the 14s and 15s
static uint8_t box;                 // the grid's square: 104 or 105 px
// The camera. Tiles are zs pixels apart - cs for the whole grid, twice
// that in the close-up, and the sizes between while it whips from one to
// the other - and the grid's corner is drawn at (-camX, -camY).
static uint8_t zs;
static int camX, camY;
static uint8_t dealT;               // the tiles being laid out as a puzzle starts
static uint8_t bobT;                // the glove over the cursor, bobbing
static const int PANEL_H = 42;      // the letter board
static const int KEY_W = 14, KEY_H = 13;

static uint8_t curWord = NONE;
static char clueText[CLUE_MAX];
static uint8_t clueOf = NONE;

static uint8_t boardT;              // 0 shut .. 8 up
static uint8_t pokeT;               // the glove pressing a key

// --- The show -------------------------------------------------------------------
static const uint8_t POP = 4;       // frames between a locking word's tiles
struct Lock { uint8_t w, t, on, cross; uint16_t points; };
static Lock locks[2];
static uint8_t wrongW[2], wrongN, wrongT;
static uint8_t solvedT;             // 0: not yet; counts up through the last show
struct Float { int16_t x, y; int16_t v; uint8_t t, c; };
static Float floats[3];
static uint16_t shownScore;
static uint8_t lastMult, comboT;
static bool dirty;                  // something on screen changed
static uint16_t drawnSecond;

void invalidate() { dirty = true; }

// Notes for a locking word's tiles: up a major pentatonic scale.
static const uint16_t SCALE[12] = {1047, 1175, 1319, 1568, 1760, 2093, 2349, 2637, 3136, 3520, 4186, 4186};

// M and W five columns wide in the small letters: in three columns they are
// an H with its bar a row out of place.
static const uint8_t WIDE_M[5] = {0x1F, 0x02, 0x04, 0x02, 0x1F};
static const uint8_t WIDE_W[5] = {0x1F, 0x08, 0x04, 0x08, 0x1F};

// A small letter (1..26) centred on column x.
static void letter(int x, int y, uint8_t v, uint8_t ink) {
    if (v == 13 || v == 23) glyph(x - 2, y, v == 13 ? WIDE_M : WIDE_W, 5, ink);
    else glyph(x - 1, y, FONT35['A' + v - 1 - FONT35_FIRST], 3, ink);
}

// A large letter (the close-up's and the keys'), centred in w pixels from
// x, its capitals' top at y: a drop shadow a pixel down and right, then its
// half-ink pixels in `mid` - a tone between the letter's colour and that of
// the tile it is on, which is all the anti-aliasing sixteen colours allow -
// and its ink.
static void bigLetter(int x, int y, int w, uint8_t v, uint8_t ink, uint8_t mid, uint8_t shadow) {
    const uint16_t *g = TILEFONT;
    while (--v) g += 1 + 2 * (g[0] >> 8);           // (the glyphs are of different heights)
    uint8_t rows = (uint8_t)(g[0] >> 8);
    x += (w - (g[0] & 255)) / 2;
    glyph16(x + 1, y + 1, g + 1, rows, shadow);
    glyph16(x, y, g + 1 + rows, rows, mid);
    glyph16(x, y, g + 1, rows, ink);
}

// A tile as the close-up draws them (and the keys): its face, an edge of
// shade along the bottom and right, and the corners taken off.
static void tile(int x, int y, int w, int h, uint8_t face, uint8_t edge, uint8_t behind) {
    gfx_fillRect(x, y, w, h, face);
    gfx_hline(x, y + h - 1, w, edge);
    gfx_vline(x + w - 1, y, h, edge);
    gfx_pixel(x, y, behind);
    gfx_pixel(x + w - 1, y, behind);
    gfx_pixel(x, y + h - 1, behind);
    gfx_pixel(x + w - 1, y + h - 1, behind);
}

static uint8_t shadeOf(uint8_t face);

void bigTile(int x, int y, uint8_t face, uint8_t v) {
    uint8_t edge = shadeOf(face);
    tile(x, y, 15, 15, face, edge, FELT);
    if (v) bigLetter(x, y + 2, 14, v, INK, edge, edge);
}

int bigText(int x, int y, const char *s, uint8_t ink, uint8_t mid, uint8_t shadow, bool draw) {
    int x0 = x;
    for (; *s; s++) {
        if (*s == ' ') { x += 4; continue; }
        const uint16_t *g = TILEFONT;
        for (uint8_t v = (uint8_t)(*s - 'A'); v; v--) g += 1 + 2 * (g[0] >> 8);
        uint8_t w = (uint8_t)g[0];
        if (draw) bigLetter(x, y, w, (uint8_t)(*s - 'A' + 1), ink, mid, shadow);
        x += w + 1;
    }
    return x - x0;
}

static void cellXY(uint8_t c, int &x, int &y) {
    x = (c % n) * zs - camX;
    y = (c / n) * zs - camY;
}

// The close-up is wanted while typing, and when asked for (B held; or all
// the time, with B held for the whole grid, if the options say so).
static bool wantClose() { return !game::st.solved && !dealT && (board || viewClose != peek); }

// Where the camera should be: the cursor in the middle of what shows of the
// grid, the grid's edges kept to the window's (a grid smaller than the
// window is centred).
static void camTarget(int &tx, int &ty) {
    int size = n * zs, w = box, h = box - (PANEL_H * boardT) / 8;
    tx = (cur % n) * zs + zs / 2 - w / 2;
    ty = (cur / n) * zs + zs / 2 - h / 2;
    if (tx > size - w) tx = size - w;
    if (ty > size - h) ty = size - h;
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if (size < w) tx = -(w - size) / 2;
    if (size < h) ty = -(h - size) / 2;
}

static void refresh() {
    curWord = wordAt(cur, down);
    if (curWord == NONE) {
        uint8_t other = wordAt(cur, !down);
        if (other != NONE) { down = !down; curWord = other; }
    }
    if (curWord != clueOf) {
        clueOf = curWord;
        if (curWord != NONE) clue(curWord, clueText);
    }
    dirty = true;
}

void enter() {
    cs = n > 13 ? 7 : 8;
    box = n > 13 ? 105 : 104;
    cur = wStart[0];
    down = false;
    board = peek = false;
    key = 0;
    zs = cs;
    boardT = pokeT = 0;
    camTarget(camX, camY);
    dealT = (uint8_t)(n * 4 + 6);
    clueOf = NONE;
    memset(locks, 0, sizeof locks);
    memset(floats, 0, sizeof floats);
    wrongT = solvedT = comboT = 0;
    shownScore = game::st.score;
    lastMult = game::multiplier();
    dirty = true;
    refresh();
}

void setCursor(uint8_t c, bool d) {
    if (c < n * n && sol[c]) cur = c;
    down = d;
    refresh();
}

uint8_t word() { return curWord; }

bool move(int dx, int dy) {
    int r = cur / n, c = cur % n;
    for (;;) {
        r += dy; c += dx;
        if (r < 0 || c < 0 || r >= n || c >= n) return false;
        if (sol[r * n + c]) break;
    }
    cur = (uint8_t)(r * n + c);
    refresh();
    return true;
}

void flip() {
    if (wordAt(cur, !down) == NONE) return;
    down = !down;
    refresh();
}

// The first cell of a word still to be filled (its first cell if none).
static uint8_t firstOpen(uint8_t w) {
    for (uint8_t k = 0; k < wLen[w]; k++)
        if (!(cell[cellOf(w, k)] & LETTER)) return cellOf(w, k);
    return wStart[w];
}

static bool done(uint8_t w) { return game::st.checking ? wordLocked(w) : wordFull(w); }

void nextClue() {
    uint8_t w = curWord == NONE ? 0 : curWord;
    for (uint8_t k = 0; k < nWords; k++) {
        w = (uint8_t)(w + 1 == nWords ? 0 : w + 1);
        if (!done(w)) break;
    }
    down = isDown(w);
    cur = firstOpen(w);
    refresh();
}

void openBoard() {
    if (curWord == NONE || game::st.solved) return;
    board = true;
    dirty = true;
    audio::sfx(Sfx::Open);
}

void closeBoard() {
    if (!board) return;
    board = false;
    audio::sfx(Sfx::Close);
}

void moveKey(int dx, int dy) {
    int c = key % KEY_COLS + dx, r = key / KEY_COLS + dy;
    c = (c + KEY_COLS) % KEY_COLS;
    r = (r + 3) % 3;
    key = (uint8_t)(r * KEY_COLS + c);
    dirty = true;
}

static void addFloat(int x, int y, int v, uint8_t c) {
    Float *f = &floats[0];
    for (auto &g : floats) if (g.t < f->t) f = &g;
    f->x = (int16_t)x; f->y = (int16_t)y; f->v = (int16_t)v; f->t = 50; f->c = c;
}

// Where a word's middle is on screen.
static void wordMid(uint8_t w, int &x, int &y) {
    cellXY(cellOf(w, wLen[w] / 2), x, y);
    x += zs / 2; y += zs / 2;
}

static void show(const game::Events &e) {
    for (uint8_t i = 0; i < e.nLock; i++) {
        Lock &l = locks[i];
        l.w = e.lock[i]; l.t = 0; l.on = 1; l.cross = e.cross;
        l.points = (uint16_t)(e.points[i] + (i == 0 ? e.bonus : 0));
    }
    dirty = true;
    if (e.jackpot) {
        fx::banner("JACKPOT!", fx::B_RAINBOW, 40, 80);
        fx::fountain(30, box, 12);
        fx::fountain(74, box, 12);
        audio::sfx(Sfx::Cross);
        audio::led(audio::LED_PARTY);
    } else if (e.cross) {
        fx::banner("CROSS!", fx::B_RAINBOW, 40, 60);
        audio::sfx(Sfx::Cross);
        audio::led(audio::LED_TRIPLE);
    }
    if (e.nWrong) {
        wrongN = e.nWrong;
        wrongW[0] = e.wrong[0]; wrongW[1] = e.wrong[1];
        wrongT = 36;
        if (!e.nLock) {
            int x, y;
            wordMid(e.wrong[0], x, y);
            addFloat(x, y, -(int)game::WRONG_COST, RED);
            fx::shake(8, 2);
            audio::sfx(Sfx::Wrong);
        }
    }
    if (e.solved) closeBoard();
}

// On along the word after a letter: to the next empty cell (or just the
// next cell), round to the word's start if the end is reached with gaps
// left; a full word shuts the board.
static void advance() {
    uint8_t w = curWord;
    if (w == NONE) return;
    if (wordFull(w)) { closeBoard(); return; }
    uint8_t k = (uint8_t)((cur - wStart[w]) / step(w));
    for (uint8_t i = 1; i <= wLen[w]; i++) {
        uint8_t j = (uint8_t)((k + i) % wLen[w]);
        uint8_t c = cellOf(w, j);
        if (cell[c] & LOCKED) continue;
        if (stepAll && j > k) { cur = c; break; }
        if (!(cell[c] & LETTER)) { cur = c; break; }
    }
    refresh();
}

void press() {
    pokeT = 6;
    if (key == KEY_DEL) { rubOut(); return; }
    game::Events e = game::place(cur, (uint8_t)(key + 1));
    audio::sfx(Sfx::Key);
    show(e);
    if (board) advance();
}

void rubOut() {
    uint8_t w = curWord;
    if (w == NONE) return;
    if (!(cell[cur] & LETTER) || (cell[cur] & LOCKED)) {
        // Back to the letter before, if there is one that can go.
        uint8_t k = (uint8_t)((cur - wStart[w]) / step(w));
        for (;;) {
            if (!k) { closeBoard(); return; }
            k--;
            if (!(cell[cellOf(w, k)] & LOCKED)) break;
        }
        cur = cellOf(w, k);
        refresh();
    }
    if (cell[cur] & LETTER) {
        game::place(cur, 0);
        audio::sfx(Sfx::Rub);
    }
    dirty = true;
}

void reveal() {
    game::Events e = game::reveal(cur);
    if (!e.changed) { audio::sfx(Sfx::Deny); return; }
    audio::sfx(Sfx::Reveal);
    int x, y;
    cellXY(cur, x, y);
    addFloat(x + zs / 2, y, -(int)game::REVEAL_COST, RED);
    show(e);
}

void checkWord() {
    if (curWord == NONE) return;
    uint8_t bad = game::checkWord(curWord);
    int x, y;
    wordMid(curWord, x, y);
    addFloat(x, y, -(int)game::CHECK_COST, RED);
    if (bad) { fx::shake(8, 2); audio::sfx(Sfx::Wrong); }
    else audio::sfx(Sfx::Coin);
    dirty = true;
    // (Checking off never ends a puzzle here: the last right letter does.)
}

bool busy() { return locks[0].on || locks[1].on || solvedT; }
bool solvedShown() { return solvedT > 170; }

void update() {
    // Anything in motion means a new frame to draw.
    int lo, hi;
    if ((board ? boardT < 8 : boardT) || pokeT || wrongT || comboT || solvedT || dealT ||
        fx::activeRows(lo, hi) || shownScore != game::st.score || game::seconds() != drawnSecond)
        dirty = true;
    if (board && boardT < 8) boardT++;
    if (!board && boardT) boardT--;
    // The tiles are laid out, a diagonal at a time, as a puzzle starts.
    if (dealT) {
        dealT--;
        if ((dealT & 3) == 0) audio::note((uint16_t)(2600 - dealT * 12), 12);
    }
    // The camera whips between the whole grid and the close-up in a few
    // ticks, held on the cursor; at rest it pans after the cursor.
    uint8_t want = (uint8_t)(wantClose() ? cs * 2 : cs);
    int tx, ty;
    if (zs != want) {
        zs = (uint8_t)(zs < want ? (zs + 2 > want ? want : zs + 2) : (zs - 2 < want ? want : zs - 2));
        camTarget(camX, camY);
        dirty = true;
    } else {
        camTarget(tx, ty);
        if (tx != camX || ty != camY) {
            int dx = tx - camX, dy = ty - camY;
            camX += dx > 6 ? 6 : dx < -6 ? -6 : dx;
            camY += dy > 6 ? 6 : dy < -6 ? -6 : dy;
            dirty = true;
        }
    }
    if (!(++bobT & 15)) dirty = true;
    if (pokeT) pokeT--;
    if (wrongT) wrongT--;
    if (comboT) comboT--;

    // Locking words: a tile every POP frames, each a note up the scale,
    // then what the word was worth floats off it.
    for (auto &l : locks) {
        if (!l.on) continue;
        dirty = true;
        uint8_t len = wLen[l.w];
        if (l.t % POP == 0 && l.t / POP < len) {
            uint8_t k = (uint8_t)(l.t / POP);
            int x, y;
            cellXY(cellOf(l.w, k), x, y);
            fx::burst(fx::SPARK, x + zs / 2, y + zs / 2, 3, 14, FX_B);
            if (!l.cross) {
                uint8_t s = (uint8_t)(k + game::multiplier() - 1);
                audio::note(SCALE[s > 11 ? 11 : s], 60);
            }
        }
        l.t++;
        if (l.t == len * POP + 2) {
            int x, y;
            wordMid(l.w, x, y);
            addFloat(x, y, l.points, FX_B);
            if (!l.cross && &l == &locks[0]) audio::sfx(Sfx::Coin);
        }
        if (l.t >= len * POP + 8) l.on = 0;
    }
    // A step up the multiplier: the sign says so.
    uint8_t m = game::multiplier();
    if (m > lastMult) {
        comboT = 60;
        if (!fx::bannerActive()) {
            char text[10];
            memcpy(text, "COMBO X2!", 10);
            text[7] = (char)('0' + m);
            fx::banner(text, fx::B_GOLD, 40, 46);
        }
    }
    lastMult = m;

    for (auto &f : floats) if (f.t) { dirty = true; f.t--; if (f.t & 1) f.y--; }
    // The score counts up to where it is.
    uint16_t s = game::st.score;
    if (shownScore < s) shownScore = (uint16_t)(shownScore + (s - shownScore + 7) / 8);
    else shownScore = s;

    // Solved: once the last word has locked, the whole grid goes off.
    if (game::st.solved && !locks[0].on && !locks[1].on) {
        if (!solvedT) {
            fx::banner("SOLVED!", fx::B_RAINBOW, 50, 150);
            peek = false;
            audio::sfx(Sfx::Solved);
            audio::led(audio::LED_PARTY);
        }
        if (solvedT < 250) solvedT++;
        if (solvedT < 120 && solvedT % 12 == 1)
            fx::fountain(20 + (int)(fx::rnd() % 70), 104, 10);
    }
}

// --- Drawing --------------------------------------------------------------------
// Is cell c one of word w's?
static bool inWord(uint8_t c, uint8_t w) {
    if (w == NONE || c < wStart[w]) return false;
    uint8_t st = step(w), d = (uint8_t)(c - wStart[w]);
    return d % st == 0 && d / st < wLen[w] && (isDown(w) || c / n == wStart[w] / n);
}

// How a cell looks this frame: its tile's colour, its letter's, and whether
// it is popping (drawn a pixel larger all round).
static uint8_t look(uint8_t c, uint32_t frame, uint8_t &ink, bool &grow) {
    // (The jackpot word's tiles are a warmer ivory until it is won.)
    uint8_t v = cell[c], face = inWord(c, curWord) ? CYAN : inWord(c, game::st.jackpot) ? SKIN : WHITE;
    ink = (v & REVEALED) ? WINE : INK;
    grow = false;
    bool lockedNow = v & LOCKED, shine = false;
    // A locking word's tiles turn gold one after another, each with a pop,
    // and the word shines once the last has.
    for (auto &l : locks) {
        if (!l.on || !inWord(c, l.w)) continue;
        int since = (int)l.t - ((c - wStart[l.w]) / step(l.w)) * POP;
        if (since < 0) lockedNow = false;
        else if (since < 5) { grow = since < 3; face = WHITE; lockedNow = false; }
        else if (l.t >= wLen[l.w] * POP) shine = true;
    }
    if (lockedNow) face = shine ? FX_B : GOLD;
    if (wrongT && (wrongT & 4) && !(v & LOCKED))
        for (uint8_t i = 0; i < wrongN; i++)
            if (inWord(c, wrongW[i])) { face = RED; ink = WHITE; }
    if (solvedT) {
        // A wave from the top left corner, then the rainbow all over.
        int wave = (int)solvedT * 2 - (c / n + c % n) * 3;
        if (wave >= 0 && wave < 8) { face = WHITE; grow = true; }
        else if (wave >= 8 && ((c / n + c % n + (frame >> 3)) & 3) == 0) face = FX_A;
    } else if (c == cur) {
        face = FX_B;
    }
    return face;
}

// Not yet laid out (the deal runs corner to corner)?
static bool undealt(uint8_t r, uint8_t col) { return dealT && (r + col) * 2 > n * 4 + 6 - dealT; }

// The edge a tile's colour takes in the close-up.
static uint8_t shadeOf(uint8_t face) {
    switch (face) {
        case WHITE: return SILVER;
        case CYAN:  return BLUE;
        case RED:   return WINE;
        case GOLD: case SKIN: return WOOD;
        default:    return GOLD;
    }
}

// The close-up: each tile with a shaded edge and clipped corners, the
// number of the word that starts on it in its corner while it is empty,
// and its letter in the large face.
static void drawClose(uint32_t frame) {
    uint8_t num = 0;
    for (uint16_t c = 0; c < n * n; c++) {
        if (!sol[c]) continue;
        uint8_t r = (uint8_t)(c / n), col = (uint8_t)(c % n);
        bool starts = ((col == 0 || !sol[c - 1]) && col + 1 < n && sol[c + 1]) ||
                      ((r == 0 || !sol[c - n]) && r + 1 < n && sol[c + n]);
        if (starts) num++;
        int x = col * zs - camX, y = r * zs - camY, t = zs - 1;
        if (x <= -zs || y <= -zs || x >= box || y >= box || undealt(r, col)) continue;
        uint8_t ink;
        bool grow;
        uint8_t face = look((uint8_t)c, frame, ink, grow), edge = shadeOf(face);
        if (grow) { x--; y--; t += 2; }
        tile(x, y, t, t, face, edge, FELT_DK);
        uint8_t v = cell[c] & LETTER;
        if (v) {
            // (White on a wrong word's red; black on everything else.)
            bigLetter(x, y + t - 13, t - 1, v, ink, face == RED ? SKIN : edge, edge);
        } else if (starts) {
            char b[4];
            fmtInt(b, num);
            text35(x + 2, y + 2, b, edge);
        }
    }
}

// The whole grid (and the sizes between it and the close-up). Tiles of one
// colour side by side are painted as one bar and the gaps between them
// ruled in afterwards: a third of the fills that a tile at a time takes (a
// full grid is the frame's biggest cost).
static void drawFlat(uint32_t frame) {
    uint8_t inks[MAX_N];
    for (uint8_t r = 0; r < n; r++) {
        int y = r * zs - camY;
        if (y <= -zs || y >= box) continue;
        uint8_t runFace = 0, runFrom = 0;
        bool run = false;
        uint16_t pops = 0;                  // columns of this row that are popping
        for (uint8_t col = 0; col <= n; col++) {
            uint8_t c = (uint8_t)(r * n + col), face = 0;
            bool open = col < n && sol[c] && !undealt(r, col), grow = false;
            if (open) {
                face = look(c, frame, inks[col], grow);
                if (grow) pops |= (uint16_t)(1u << col);
            }
            if (run && (!open || face != runFace)) {
                gfx_fillRect(runFrom * zs - camX, y, (col - runFrom) * zs - 1, zs - 1, runFace);
                run = false;
            }
            if (open && !run) { run = true; runFace = face; runFrom = col; }
        }
        // The gaps, then the popping tiles over them, then the letters.
        for (uint8_t col = 1; col < n; col++)
            if (sol[r * n + col] && sol[r * n + col - 1]) gfx_vline(col * zs - camX - 1, y, zs - 1, FELT_DK);
        for (uint8_t col = 0; col < n; col++) {
            uint8_t c = (uint8_t)(r * n + col);
            if (!sol[c] || undealt(r, col)) continue;
            int x = col * zs - camX;
            if (pops >> col & 1) gfx_fillRect(x - 1, y - 1, zs + 1, zs + 1, WHITE);
            if (cell[c] & LETTER) letter(x + (zs - 1) / 2, y + (zs - 6) / 2, cell[c] & LETTER, inks[col]);
        }
    }
}

static void drawGrid(uint32_t frame) {
    gfx_fillRect(0, 0, box, box, FELT_DK);
    if (zs == cs * 2) drawClose(frame);
    else drawFlat(frame);
    // The cursor: a rainbow frame round its tile, over its neighbours' edges
    // (its corners left off, as the close-up's tiles' are), and the glove
    // pointing down at it from above - or, where that would take the glove
    // off the top of the screen, turned over and pointing up from below.
    if (!game::st.solved && !dealT) {
        int x, y;
        cellXY(cur, x, y);
        roundRect(x - 1, y - 1, zs + 1, zs + 1, 1, FX_A);
        int bob = (bobT >> 4) & 1, h = HAND[1], tip = zs > cs ? 3 : 1;
        x += (zs - 1) / 2 - HAND_TIP;
        if (y + tip - h - 1 >= 0) sprite4(HAND, x, y + tip - h - bob);
        else sprite4(HAND, x, y + zs - 1 - tip + bob, nullptr, -256);
    }
}

static void drawHud(uint32_t frame) {
    int x0 = box;
    gfx_fillRect(x0, 0, 128 - x0, box, INK);
    gfx_vline(x0, 0, box, GOLD);
    int x = x0 + 3;
    char buf[12], *p;
    text35(x, 3, "TIME", SILVER);
    uint32_t s = game::seconds();
    if (s > 5999) s = 5999;
    p = buf;
    *p++ = (char)('0' + s / 600); *p++ = (char)('0' + s / 60 % 10); *p++ = ':';
    *p++ = (char)('0' + s % 60 / 10); *p++ = (char)('0' + s % 10); *p = 0;
    text35(x, 10, buf, WHITE);
    text35(x, 22, "SCORE", SILVER);
    fmtInt(buf, shownScore);
    text35(x, 29, buf, shownScore != game::st.score ? FX_B : GOLD);
    if (game::st.checking) {
        text35(x, 41, "COMBO", SILVER);
        buf[0] = 'X'; buf[1] = (char)('0' + game::multiplier()); buf[2] = 0;
        bool hot = comboT && (frame & 4);
        text35x2(x + 1, 48, buf, hot ? WHITE : game::multiplier() > 1 ? FX_B : SILVER);
        // The streak towards the next step: three pips.
        uint8_t pips = game::multiplier() >= game::MULT_MAX ? 3 : game::st.streak % game::STREAK_STEP;
        for (uint8_t i = 0; i < 3; i++) gfx_fillRect(x + 1 + i * 6, 61, 4, 2, i < pips ? GOLD : NAVY);
        text35(x, 69, "WORDS", SILVER);
        p = fmtInt(buf, lockedWords());
        *p++ = '/';
        fmtInt(p, nWords);
        text35(x, 76, buf, WHITE);
    }
    text35(x, 88, down ? "DOWN" : "ACR.", CYAN);
    // On the jackpot word: what it pays.
    if (curWord != NONE && curWord == game::st.jackpot && !wordLocked(curWord)) {
        text35(x, 96, "*X3*", FX_A);
    }
}

static void drawBoard(uint32_t frame) {
    if (!boardT) return;
    int y0 = box - (PANEL_H * boardT) / 8;
    gfx_fillRect(0, y0, 128, box - y0, NAVY);
    gfx_hline(0, y0, 128, GOLD);
    for (uint8_t k = 0; k < KEYS; k++) {
        int x = 1 + (k % KEY_COLS) * KEY_W, y = y0 + 2 + (k / KEY_COLS) * KEY_H;
        bool on = k == key;
        if (on && pokeT > 3) y++;
        // Felt-green tiles with white letters; the one under the glove gold.
        uint8_t ink = on ? INK : WHITE, shadow = on ? WOOD : INK;
        tile(x, y, KEY_W - 1, KEY_H - 1, on ? FX_B : FELT, on ? WOOD : FELT_DK, NAVY);
        if (k == KEY_DEL) {
            // Rub out: a red triangle pointing back, its lower half in shade.
            for (int r = -3; r <= 3; r++) {
                int in = r < 0 ? -2 * r : 2 * r;
                gfx_hline(x + 4 + in, y + 6 + r, 7 - in, shadow);
                gfx_hline(x + 3 + in, y + 5 + r, 7 - in, r > 0 ? WINE : RED);
            }
        } else {
            bigLetter(x, y + 1, KEY_W - 2, (uint8_t)(k + 1), ink, on ? WOOD : FELT_LT, shadow);
        }
    }
    // The glove comes down on a key as it is pressed.
    if (pokeT) {
        int x = 1 + (key % KEY_COLS) * KEY_W + 7, y = y0 + 2 + (key / KEY_COLS) * KEY_H + 4;
        sprite4(HAND, x - HAND_TIP, y - HAND[1] - (pokeT > 3 ? 0 : (4 - pokeT) * 3), nullptr);
    }
}

// The clue, wrapped by words into three lines of 31 (the first shorter by
// the clue's number). tools/puzzles/cwformat.py wraps the same way when it
// checks that every clue fits.
static void drawClue() {
    int y0 = box;
    gfx_fillRect(0, y0, 128, 128 - y0, INK);
    gfx_hline(0, y0, 128, GOLD);
    if (curWord == NONE) return;
    char id[8], *p = fmtInt(id, wNum[curWord]);
    *p++ = isDown(curWord) ? 'D' : 'A';
    *p = 0;
    uint8_t idLen = (uint8_t)(p - id);
    text35(2, y0 + 3, id, FX_B);
    uint8_t line = 0, col = 0, room = (uint8_t)(31 - idLen - 1);
    int x0 = 2 + (idLen + 1) * 4;
    const char *s = clueText;
    while (*s && line < 3) {
        const char *e = s;
        while (*e && *e != ' ') e++;
        uint8_t wl = (uint8_t)(e - s);
        if (col && col + 1 + wl > room) { line++; col = 0; room = 31; x0 = 2; continue; }
        if (col) col++;
        if (wl > room - col) wl = (uint8_t)(room - col);
        char tmp[32];
        memcpy(tmp, s, wl);
        tmp[wl] = 0;
        text35(x0 + col * 4, y0 + 3 + line * 7, tmp, WHITE);
        col = (uint8_t)(col + wl);
        s += wl;
        if (*s == ' ') s++;
        else if (*s) { line++; col = 0; room = 31; x0 = 2; }      // a word cut at the line's end
    }
}

static void drawFloats() {
    for (auto &f : floats) {
        if (!f.t || (f.t < 10 && (f.t & 1))) continue;
        char buf[8], *p = buf;
        if (f.v > 0) *p++ = '+';
        fmtInt(p, f.v);
        int w = text35Width(buf), x = f.x - w / 2, y = f.y;
        if (x < 1) x = 1;
        if (x + w > 126) x = 126 - w;
        if (y < 1) y = 1;
        for (int8_t d = -1; d <= 1; d++) { text35(x + d, y, buf, INK); text35(x, y + d, buf, INK); }
        text35(x, y, buf, f.c);
    }
}

bool render(uint32_t frame) {
    if (!dirty) return false;
    dirty = false;
    drawnSecond = game::seconds();
    drawGrid(frame);
    fx::applyShake(0, box - 1);
    drawHud(frame);
    drawBoard(frame);
    drawClue();
    drawFloats();
    fx::drawParticles(2);
    fx::drawBanner();
    return true;
}

}  // namespace stage
