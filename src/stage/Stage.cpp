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
bool down, board, stepAll;

// --- Layout -------------------------------------------------------------------
static uint8_t cs;                  // cell pitch: 8, or 7 for the 14s and 15s
static uint8_t box;                 // the grid's square: 104 or 105 px
static uint8_t off;                 // a smaller grid is centred in it
static const int PANEL_H = 42;      // the letter board
static const int KEY_W = 14, KEY_H = 13;

static uint8_t curWord = NONE;
static char clueText[CLUE_MAX];
static uint8_t clueOf = NONE;

static int scroll;                  // px the grid is moved up while the board is open
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

// M and W five columns wide for the grid and the keys: in three columns
// they are an H with its bar a row out of place.
static const uint8_t WIDE_M[5] = {0x1F, 0x02, 0x04, 0x02, 0x1F};
static const uint8_t WIDE_W[5] = {0x1F, 0x08, 0x04, 0x08, 0x1F};

// A letter (1..26) centred on column x.
static void letter(int x, int y, uint8_t v, uint8_t ink) {
    if (v == 13 || v == 23) glyph(x - 2, y, v == 13 ? WIDE_M : WIDE_W, 5, ink);
    else glyph(x - 1, y, FONT35['A' + v - 1 - FONT35_FIRST], 3, ink);
}

static void cellXY(uint8_t c, int &x, int &y) {
    x = off + (c % n) * cs;
    y = off + (c / n) * cs - scroll;
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
    off = (uint8_t)((box - n * cs) / 2);
    cur = wStart[0];
    down = false;
    board = false;
    key = 0;
    scroll = 0;
    boardT = pokeT = 0;
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
    x += cs / 2; y += cs / 2;
}

static void show(const game::Events &e) {
    for (uint8_t i = 0; i < e.nLock; i++) {
        Lock &l = locks[i];
        l.w = e.lock[i]; l.t = 0; l.on = 1; l.cross = e.cross;
        l.points = (uint16_t)(e.points[i] + (i == 0 ? e.bonus : 0));
    }
    dirty = true;
    if (e.cross) {
        fx::banner("CROSS!", fx::B_RAINBOW, 52, 60);
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
    addFloat(x + cs / 2, y, -(int)game::REVEAL_COST, RED);
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
    // The board slides; the grid follows the cursor above it.
    int target = 0;
    if (board) {
        int view = box - PANEL_H;
        int y = off + (cur / n) * cs + cs / 2;
        if (curWord != NONE && isDown(curWord)) y += cs;      // a little of what is ahead
        target = y - view / 2;
        int most = off * 2 + n * cs - view;
        if (target > most) target = most;
        if (target < 0) target = 0;
    }
    // Anything in motion means a new frame to draw.
    int lo, hi;
    if ((board ? boardT < 8 : boardT) || scroll != target || pokeT || wrongT || comboT || solvedT ||
        fx::activeRows(lo, hi) || shownScore != game::st.score || game::seconds() != drawnSecond)
        dirty = true;
    if (board && boardT < 8) boardT++;
    if (!board && boardT) boardT--;
    for (uint8_t i = 0; i < 3; i++) {
        if (scroll < target) scroll++;
        if (scroll > target) scroll--;
    }
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
            fx::burst(fx::SPARK, x + cs / 2, y + cs / 2, 3, 14, FX_B);
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
            fx::banner(text, fx::B_GOLD, 52, 46);
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
    uint8_t v = cell[c], face = inWord(c, curWord) ? CYAN : WHITE;
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

// The grid. Tiles of one colour side by side are painted as one bar and
// the gaps between them ruled in afterwards: a third of the fills that a
// tile at a time takes (a full grid is the frame's biggest cost).
static void drawGrid(uint32_t frame) {
    gfx_fillRect(0, 0, box, box, FELT_DK);
    uint8_t inks[MAX_N];
    uint16_t pops = 0;                      // columns of this row that are popping
    for (uint8_t r = 0; r < n; r++) {
        int y = off + r * cs - scroll;
        if (y <= -cs || y >= box) continue;
        uint8_t runFace = 0, runFrom = 0;
        bool run = false;
        pops = 0;
        for (uint8_t col = 0; col <= n; col++) {
            uint8_t c = (uint8_t)(r * n + col), face = 0;
            bool open = col < n && sol[c], grow = false;
            if (open) {
                face = look(c, frame, inks[col], grow);
                if (grow) pops |= (uint16_t)(1u << col);
            }
            if (run && (!open || face != runFace)) {
                gfx_fillRect(off + runFrom * cs, y, (col - runFrom) * cs - 1, cs - 1, runFace);
                run = false;
            }
            if (open && !run) { run = true; runFace = face; runFrom = col; }
        }
        // The gaps, then the popping tiles over them, then the letters.
        for (uint8_t col = 1; col < n; col++)
            if (sol[r * n + col] && sol[r * n + col - 1]) gfx_vline(off + col * cs - 1, y, cs - 1, FELT_DK);
        for (uint8_t col = 0; col < n; col++) {
            uint8_t c = (uint8_t)(r * n + col);
            if (!sol[c]) continue;
            int x = off + col * cs;
            if (pops >> col & 1) gfx_fillRect(x - 1, y - 1, cs + 1, cs + 1, WHITE);
            if (cell[c] & LETTER) letter(x + (cs == 8 ? 3 : 2), y + (cs == 8 ? 1 : 0), cell[c] & LETTER, inks[col]);
        }
    }
    // The cursor's frame, over its neighbours' edges.
    if (!game::st.solved) {
        int x, y;
        cellXY(cur, x, y);
        gfx_rect(x - 1, y - 1, cs + 1, cs + 1, INK);
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
    text35(x, 90, down ? "DOWN" : "ACR.", CYAN);
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
        gfx_fillRect(x, y, KEY_W - 1, KEY_H - 1, on ? FX_B : SKIN);
        gfx_hline(x, y + KEY_H - 2, KEY_W - 1, on ? WOOD : SILVER);
        if (k == 12 || k == 22) {
            const uint8_t *g = k == 12 ? WIDE_M : WIDE_W;
            for (int i = 0; i < 5; i++)
                for (int r = 0; r < 5; r++)
                    if (g[i] >> r & 1) gfx_fillRect(x + 2 + 2 * i, y + 1 + 2 * r, 2, 2, INK);
        } else {
            char s[2] = {(char)(k == KEY_DEL ? '<' : 'A' + k), 0};
            text35x2(x + 4, y + 1, s, k == KEY_DEL ? RED : INK);
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
