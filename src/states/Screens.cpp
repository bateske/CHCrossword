#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed (hot pixel loops live in Draw/Mask)
#include <Arduino.h>
#include <string.h>
#include <CHGfx.h>
#include "../../config.h"
#include "Screens.h"
#include "../CHGame.h"
#include "../gfx/Palette.h"
#include "../gfx/Draw.h"
#include "../gfx/Mask.h"
#include "../gfx/Fmt.h"
#include "../fx/Fx.h"
#include "../audio/Audio.h"
#include "../game/Puzzle.h"
#include "../game/Game.h"
#include "../pack/Pack.h"
#include "../stage/Stage.h"
#include "../save/Save.h"
#include "../debug/Debug.h"
#include "../assets/Assets.h"
#ifdef CHSIM
#include <sim.h>
#endif

namespace screens {

enum class Scr : uint8_t { Title, Select, Play, Options };
static Scr cur = Scr::Title, pending = Scr::Title;
static uint16_t t;                   // frames on this screen
static uint8_t fadeOut, fadeIn;
static uint8_t sel;                  // menu cursor

static Options opt;
static Progress prog;
static bool hasGame;                 // a saved puzzle is waiting

// The puzzle list.
static const uint8_t ROWS = 6;
struct Row { char title[puz::TITLE_MAX + 1]; uint8_t size, diff; };
static Row rows[ROWS];
static uint8_t listSel, listTop;
static bool listStale;               // the rows need reading again
static bool rescan;                  // look at the card again on the way into the list
static uint32_t lastPack;            // the pack last chosen there (its id)
static uint8_t playing;              // the puzzle in play: its place in the pack

// Play-screen overlays.
enum Overlay : uint8_t { NONE, PAUSE, RESULT };
static Overlay overlay;
static game::Result result;
static bool resultDone, newBest;
static uint8_t resultT;
static uint8_t bHeld;                // frames B has been down on the grid
static const uint8_t HOLD = 12;      // ... and how many make it a hold

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------
static void go(Scr s) {
    if (fadeOut) return;
    pending = s;
    fadeOut = 8;
}

static void enter(Scr s) {
    cur = s;
    t = 0;
    sel = 0;
    fadeIn = 8;
    fx::clear();
    stage::invalidate();
    if (s == Scr::Title) audio::sfx(Sfx::Title);
    if (s == Scr::Select) {
        listStale = true;
        if (rescan) {
            // The screen is black between fades: the moment to wake the card
            // (tens of milliseconds) and see which packs are on it.
            rescan = false;
            gfx_wait();
            pack::scan();
            if (!pack::find(lastPack)) pack::select(0);
            if (pack::id() != lastPack) listSel = listTop = 0;
        }
    }
}

// Save the options and the records, and the puzzle in play if there is one.
static void persist(bool withGame) {
    gfx_wait();                      // save builds its page in the chunk scratch
    game::Record r;
    if (withGame) {
        game::save(r);
        r.packId = pack::id();
        r.source = pack::current();
        r.index = playing;
        r.cursor = stage::cur;
        r.down = stage::down;
    }
    save::store(opt, prog, withGame ? &r : nullptr);
    hasGame = withGame;
}

static void applyOptions() {
    audio::setOn(opt.sound != 0);
    pal::setTheme(opt.felt);
    stage::stepAll = opt.skip != 0;
    stage::viewClose = opt.view != 0;
}

// ---------------------------------------------------------------------------
// Shared drawing (CHBlackjack's look)
// ---------------------------------------------------------------------------
static void feltBackdrop() {
    gfx_clear(FELT);
    dither(0, 0, 128, 6, FELT_DK, 0);
    dither(0, 122, 128, 6, FELT_DK, 1);
    dither(0, 0, 6, 128, FELT_DK, 0);
    dither(122, 0, 6, 128, FELT_DK, 1);
    gfx_rect(2, 2, 124, 124, GOLD);
}

// The display font's lettering with a gradient and an outline: the top
// rows FX_B, so the palette makes them shimmer.
static void heading(const char *text, int y) {
    int w = fontWidth(text);
    Mask m = maskBegin(w, FONT_H);
    maskFont(m, 0, 0, text);
    uint8_t r[FONT_H + 2];
    for (int i = 0; i < FONT_H + 2; i++) r[i] = i < 3 ? FX_B : (i < 8 ? GOLD : WOOD);
    maskDraw(m, 64 - w / 2, y, INK, INK, r);
}

static void centred35(int y, const char *s, uint8_t c) { text35(64 - text35Width(s) / 2, y, s, c); }
static void centred2(int y, const char *s, uint8_t c) { fontText(64 - fontWidth(s) / 2, y, s, c); }

static bool menuNav(uint8_t n) {
    if (arduboy.repeat(UP_BUTTON) && sel > 0) { sel--; audio::sfx(Sfx::Cursor); }
    if (arduboy.repeat(DOWN_BUTTON) && sel + 1 < n) { sel++; audio::sfx(Sfx::Cursor); }
    return arduboy.justPressed(A_BUTTON);
}

static void panel(int y, int h) {
    fillRound(10, y, 108, h, 3, NAVY);
    roundRect(10, y, 108, h, 3, GOLD);
}

// A seven-point-wide star, each pixel k x k.
static void star(int x, int y, uint8_t c, int k) {
    static const uint8_t COLS[7] = {0x04, 0x6C, 0x3C, 0x1F, 0x3C, 0x6C, 0x04};
    for (int i = 0; i < 7; i++)
        for (int r = 0; r < 7; r++)
            if (COLS[i] >> r & 1) gfx_fillRect(x + i * k, y + r * k, k, k, c);
}

static char *fmtTime(char *p, uint32_t s) {
    if (s > 5999) s = 5999;
    *p++ = (char)('0' + s / 600); *p++ = (char)('0' + s / 60 % 10); *p++ = ':';
    *p++ = (char)('0' + s % 60 / 10); *p++ = (char)('0' + s % 10); *p = 0;
    return p;
}

// ---------------------------------------------------------------------------
// The records
// ---------------------------------------------------------------------------
static uint16_t bestTime(uint8_t i) { return (uint16_t)(prog.best[i].timeLo | (prog.best[i].timeHi & 0x3F) << 8); }
static uint8_t bestStars(uint8_t i) { return prog.best[i].timeHi >> 6; }

static Progress::Card *cardSlot(bool make) {
    uint16_t id = (uint16_t)pack::id();
    for (auto &c : prog.card) if (c.id == id && (c.solved || !make)) return &c;
    if (!make) return nullptr;
    // A new pack takes the older slot.
    prog.card[1] = prog.card[0];
    prog.card[0].id = id;
    prog.card[0].solved = 0;
    return &prog.card[0];
}

// Stars and best time of puzzle i of the current pack (card packs: solved or not).
static uint8_t starsOf(uint8_t i, uint16_t &secs) {
    secs = 0;
    if (pack::current() == 0) {
        if (i >= Progress::BUILTIN) return 0;
        secs = bestTime(i);
        return secs ? bestStars(i) : 0;
    }
    Progress::Card *c = cardSlot(false);
    return c && (c->solved >> i & 1) ? 1 : 0;
}

static void record(const game::Result &r) {
    newBest = false;
    if (pack::current() == 0) {
        if (playing >= Progress::BUILTIN) return;
        Progress::Best &b = prog.best[playing];
        uint16_t old = bestTime(playing), secs = r.seconds > 0x3FFF ? 0x3FFF : r.seconds ? r.seconds : 1;
        uint8_t stars = bestStars(playing), score = (uint8_t)(r.score / 100 > 255 ? 255 : r.score / 100);
        newBest = !old || secs < old || score > b.score;
        if (!old || secs < old) old = secs;
        if (r.stars > stars) stars = r.stars;
        if (score > b.score) b.score = score;
        b.timeLo = (uint8_t)old;
        b.timeHi = (uint8_t)((old >> 8) | (stars << 6));
    } else if (playing < 32) {
        cardSlot(true)->solved |= 1ul << playing;
    }
}

// ---------------------------------------------------------------------------
// Title: tiles laid on the felt.
// ---------------------------------------------------------------------------
enum Item : uint8_t { I_PLAY, I_CONTINUE, I_OPTIONS };
static const char *const ITEM[3] = {"PLAY", "CONTINUE", "OPTIONS"};

static uint8_t titleItems(uint8_t *items) {
    uint8_t n = 0;
    if (hasGame) items[n++] = I_CONTINUE;
    items[n++] = I_PLAY;
#if !CHCW_LEAN
    items[n++] = I_OPTIONS;
#endif
    return n;
}

static bool startPuzzle(uint8_t i) {
    gfx_wait();
    if (!pack::open(i)) return false;
    playing = i;
    game::start(opt.check == 0);
    stage::enter();
    overlay = NONE;
    resultDone = false;
    bHeld = 0;
    go(Scr::Play);
    return true;
}

static bool continueGame() {
    game::Record r;
    gfx_wait();
    if (!save::loadGame(r)) return false;
    if (r.source) pack::scan();
    if (!pack::find(r.packId) || !pack::open(r.index)) return false;
    lastPack = r.packId;
    playing = r.index;
    game::load(r);
    stage::enter();
    stage::setCursor(r.cursor, r.down);
    overlay = NONE;
    resultDone = false;
    go(Scr::Play);
    return true;
}

// The title's show is the game in miniature, in the close-up's tiles: LUCKY
// across and ACE down through its C. The tiles are dealt blank; each word's
// clue comes up, its letters are typed in under the rainbow cursor, and it
// locks in gold with sparks; confetti; and it starts over.
static const uint16_t TITLE_LOOP = 380;
static const char TITLE_WORD[] = "LUCKYAE";
static const int8_t TITLE_AT[7][2] = {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}, {2, 0}, {2, 2}};
static const uint8_t TITLE_TYPE[7] = {44, 54, 64, 74, 84, 170, 184};       // when each letter is typed
static const uint8_t TITLE_LOCK[7] = {104, 108, 112, 116, 120, 204, 212};  // ... and when its tile locks
static const uint16_t TITLE_END = 340;                                    // the tiles cleared

static void titleUpdate() {
    uint8_t items[4], n = titleItems(items);
    if (sel >= n) sel = 0;
    if (menuNav(n)) {
        audio::sfx(Sfx::Select);
        switch (items[sel]) {
            case I_PLAY: rescan = true; go(Scr::Select); break;
            case I_CONTINUE:
                if (!continueGame()) { hasGame = false; audio::sfx(Sfx::Deny); }
                break;
#if !CHCW_LEAN
            case I_OPTIONS: go(Scr::Options); break;
#endif
        }
    }
    uint16_t phase = (uint16_t)(t % TITLE_LOOP);
    for (uint8_t i = 0; i < 7; i++) {
        int x = 31 + TITLE_AT[i][0] * 16, y = 38 + TITLE_AT[i][1] * 16;
        if (phase == TITLE_TYPE[i]) audio::sfx(Sfx::Key);
        if (phase == TITLE_LOCK[i]) {
            fx::burst(fx::SPARK, x, y, 6, 18, FX_B);
            audio::note((uint16_t)(1047 + i * 190), 60);
        }
    }
    if (phase == 122 || phase == 214) audio::sfx(Sfx::Coin);
    if (phase == 216 || phase == 236 || phase == 256) {
        fx::fountain(20, 124, 10);
        fx::fountain(108, 124, 10);
    }
}

static void titleRender(uint32_t frame) {
    feltBackdrop();
    fillRound(6, 4, 116, 22, 4, NAVY);
    roundRect(6, 4, 116, 22, 4, GOLD);
    heading("CROSSWORD", 9);
    uint16_t phase = (uint16_t)(t % TITLE_LOOP);
    int next = -1;                      // the tile the cursor is on
    for (uint8_t i = 0; i < 7; i++) {
        // The tiles drop in once, one after another.
        int since = (int)t - 4 - i * 4;
        if (since < 0) continue;
        int drop = since < 8 ? ((256 - fx::ease(fx::OUT_BOUNCE, since, 8)) * 20) >> 8 : 0;
        int x = 24 + TITLE_AT[i][0] * 16, y = 30 + TITLE_AT[i][1] * 16 - drop;
        uint8_t lock = i == 2 && phase >= TITLE_LOCK[6] ? TITLE_LOCK[5] + 4 : TITLE_LOCK[i];
        bool typed = phase >= TITLE_TYPE[i] && phase < TITLE_END;
        uint8_t face = WHITE;
        if (typed && phase >= lock) face = phase < lock + 6 ? FX_B : GOLD;
        else if (phase < TITLE_END && phase >= (i < 5 ? 30 : 150) && phase < TITLE_LOCK[i < 5 ? 4 : 6]) face = CYAN;
        stage::bigTile(x, y, face, typed ? (uint8_t)(TITLE_WORD[i] - 'A' + 1) : 0);
        if (!typed && next < 0 && t > 40 && phase >= 30 && (i < 5 || phase >= 150)) {
            next = i;
            roundRect(x - 1, y - 1, 17, 17, 1, FX_A);
        }
    }
    // The clue being solved.
    if (t > 40 && phase >= 30 && phase < TITLE_END) {
        bool second = phase >= 150;
        const char *id = second ? "2D" : "1A", *clue = second ? "HIGH CARD" : "FORTUNATE";
        int w = text35Width(clue) + 12, x = 64 - w / 2;
        text35(x, 80, id, FX_B);
        text35(x + 12, 80, clue, WHITE);
    }
    // The menu, in the large face; the glove on its side points at the choice.
    uint8_t items[4], n = titleItems(items);
    int y0 = 123 - n * 12;
    for (uint8_t i = 0; i < n; i++) {
        const char *text = ITEM[items[i]];
        int w = stage::bigText(0, 0, text, 0, 0, 0, false), x = 66 - w / 2, y = y0 + i * 12;
        bool on = i == sel;
        stage::bigText(x, y, text, on ? FX_B : WHITE, on ? WOOD : FELT_LT, INK);
        if (on) sprite4(HAND_SIDE, x - HAND_SIDE[0] - 3 + ((frame >> 4) & 1), y - 2);
    }
    fx::drawParticles(2);
}

// ---------------------------------------------------------------------------
// The puzzle list
// ---------------------------------------------------------------------------
static const char *const LEVEL[6] = {"", "EASY", "MEDIUM", "HARD", "EXPERT", "MASTER"};

static void readRows() {
    listStale = false;
    gfx_wait();
    for (uint8_t i = 0; i < ROWS; i++) {
        rows[i].size = 0;
        if (listTop + i < pack::puzzles()) pack::peek((uint8_t)(listTop + i), rows[i].size, rows[i].diff, rows[i].title);
    }
}

// Another pack (or back to the built-in one if the card has gone).
static void choosePack(uint8_t p) {
    gfx_wait();
    pack::select(p);
    lastPack = pack::id();
    listSel = listTop = 0;
    listStale = true;
}

static void selectUpdate() {
    if (pack::count() > 1) {
        int d = arduboy.justPressed(RIGHT_BUTTON) ? 1 : arduboy.justPressed(LEFT_BUTTON) ? -1 : 0;
        if (d) {
            choosePack((uint8_t)((pack::current() + pack::count() + d) % pack::count()));
            audio::sfx(Sfx::Coin);
        }
    }
    uint8_t n = pack::puzzles(), was = pack::current();
    if (arduboy.repeat(UP_BUTTON) && listSel > 0) { listSel--; audio::sfx(Sfx::Cursor); }
    if (arduboy.repeat(DOWN_BUTTON) && listSel + 1 < n) { listSel++; audio::sfx(Sfx::Cursor); }
    if (listSel >= n) listSel = 0;
    if (listSel < listTop) { listTop = listSel; listStale = true; }
    if (listSel >= listTop + ROWS) { listTop = (uint8_t)(listSel - ROWS + 1); listStale = true; }
    if (listStale) readRows();
    if (arduboy.justPressed(A_BUTTON)) {
        if (startPuzzle(listSel)) audio::sfx(Sfx::Select);
        else audio::sfx(Sfx::Deny);
    }
    // The card pulled out under us: the built-in list.
    if (pack::current() != was) { choosePack(0); readRows(); audio::sfx(Sfx::Deny); }
    if (arduboy.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
}

// Every star on the built-in puzzles, and the name that many earn.
static const char *const RANK[6] = {"ROOKIE", "REGULAR", "SHARP", "CARD SHARK", "HIGH ROLLER", "LEGEND"};
static const uint8_t RANK_AT[6] = {0, 8, 18, 30, 45, 60};

static uint8_t starTotal() {
    uint8_t total = 0;
    for (uint8_t i = 0; i < Progress::BUILTIN; i++)
        if (bestTime(i)) total = (uint8_t)(total + bestStars(i));
    return total;
}

static void stars(int x, int y, uint8_t n, uint8_t of) {
    for (uint8_t i = 0; i < of; i++) text35(x + i * 4, y, "*", i < n ? FX_B : FELT_DK);
}

static void selectRender(uint32_t frame) {
    feltBackdrop();
    heading("PUZZLES", 7);
    char buf[40], *p;
    // The pack, with arrows if there are others; or what is wrong with the card.
    static const char *const CARD[4] = {"", "CARD: CANNOT READ IT", "CARD: FORMAT IT AS FAT32", "CARD: NO PACKS IN CHCW"};
    if (pack::count() > 1) {
        int w = text35Width(pack::name()), bob = (frame >> 3) & 1;
        centred35(23, pack::name(), WHITE);
        text35(64 - w / 2 - 8 - bob, 23, "<", GOLD);
        text35(64 + w / 2 + 6 + bob, 23, ">", GOLD);
    } else if (pack::card() == pack::CARD_NONE || pack::card() == pack::CARD_OK) {
        centred35(23, pack::name(), FELT_LT);
    } else {
        centred35(23, CARD[pack::card()], SKIN);
    }
    for (uint8_t i = 0; i < ROWS; i++) {
        uint8_t k = (uint8_t)(listTop + i);
        if (!rows[i].size) continue;
        int y = 32 + i * 11;
        bool on = k == listSel;
        if (on) {
            fillRound(7, y - 3, 114, 11, 2, NAVY);
            roundRect(7, y - 3, 114, 11, 2, (frame & 16) ? FX_B : GOLD);
        }
        p = buf;
        *p++ = (char)('0' + (k + 1) / 10); *p++ = (char)('0' + (k + 1) % 10); *p = 0;
        text35(11, y, buf, on ? GOLD : SILVER);
        text35(22, y, rows[i].title, WHITE);
        uint16_t secs;
        uint8_t st = starsOf(k, secs);
        stars(106, y, st, 3);
    }
    if (listTop) text35(118, 29, "^", GOLD);
    // The chosen puzzle: its size and level, and your best.
    const Row &r = rows[listSel - listTop];
    if (r.size) {
        gfx_fillRect(3, 100, 122, 25, INK);
        gfx_hline(3, 99, 122, GOLD);
        p = fmtInt(buf, r.size); *p++ = 'X'; p = fmtInt(p, r.size);
        p = fmtStr(p, "  ");
        fmtStr(p, LEVEL[r.diff > 5 ? 5 : r.diff]);
        centred35(103, buf, CYAN);
        uint16_t secs;
        uint8_t st = starsOf(listSel, secs);
        if (pack::current() == 0 && secs) {
            p = fmtStr(buf, "BEST ");
            p = fmtTime(p, secs);
            p = fmtStr(p, "  ");
            p = fmtInt(p, prog.best[listSel].score * 100);
            fmtStr(p, "+");
            centred35(110, buf, GOLD);
        } else {
            centred35(110, st ? "SOLVED" : "NOT SOLVED YET", st ? GOLD : SILVER);
        }
        if (pack::current() == 0) {
            uint8_t total = starTotal(), rank = 0;
            while (rank < 5 && total >= RANK_AT[rank + 1]) rank++;
            p = fmtInt(buf, total);
            p = fmtStr(p, total == 1 ? " STAR: " : " STARS: ");
            fmtStr(p, RANK[rank]);
            centred35(117, buf, FX_B);
        }
    }
}

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
enum Pause : uint8_t { P_RESUME, P_REVEAL, P_CHECK, P_SAVE, P_GIVEUP };
static const char *const PAUSE_ITEM[5] = {"RESUME", "REVEAL LETTER", "CHECK WORD", "SAVE + QUIT", "GIVE UP"};

static uint8_t pauseItems(uint8_t *items) {
    uint8_t n = 0;
    items[n++] = P_RESUME;
    items[n++] = P_REVEAL;
    if (!game::st.checking) items[n++] = P_CHECK;
    items[n++] = P_SAVE;
    items[n++] = P_GIVEUP;
    return n;
}

static void playUpdate() {
    if (overlay == PAUSE) {
        uint8_t items[5], n = pauseItems(items), was = sel;
        if (arduboy.justPressed(B_BUTTON) || arduboy.justPressed(START_BUTTON)) {
            overlay = NONE;
            stage::invalidate();
            return;
        }
        bool chosen = menuNav(n);
        if (sel != was) stage::invalidate();
        if (!chosen) return;
        audio::sfx(Sfx::Select);
        overlay = NONE;
        stage::invalidate();
        switch (items[sel]) {
            case P_REVEAL: stage::reveal(); break;
            case P_CHECK:  stage::checkWord(); break;
            case P_SAVE:   persist(true); go(Scr::Title); break;
            case P_GIVEUP: persist(false); go(Scr::Select); break;
            default: break;
        }
        return;
    }
    if (overlay == RESULT) {
        if (resultT < 250) resultT++;
        stage::invalidate();
        // The score counts up, ticking.
        if (resultT < 30 && (resultT & 3) == 0) audio::note((uint16_t)(1500 + resultT * 60), 24);
        // The stars come in one at a time.
        for (uint8_t i = 0; i < result.stars; i++)
            if (resultT == 40 + i * 14) {
                audio::sfx(Sfx::Star);
                fx::burst(fx::STAR, 46 + i * 18, 93, 8, 30, GOLD);
            }
        if (resultT > 30 && arduboy.justPressed(A_BUTTON)) {
            audio::sfx(Sfx::Select);
            if (playing + 1 < pack::puzzles()) { listSel = (uint8_t)(playing + 1); }
            go(Scr::Select);
        }
        if (resultT > 30 && arduboy.justPressed(B_BUTTON)) { audio::sfx(Sfx::Select); go(Scr::Title); }
        stage::update();
        return;
    }
    game::tick();
    if (game::st.solved) {
        if (!resultDone) {
            // The puzzle is done: the records are written while the show starts.
            resultDone = true;
            result = game::finish();
            record(result);
            persist(false);
        }
        stage::update();
        if (stage::solvedShown()) { overlay = RESULT; resultT = 0; }
        return;
    }
    int dx = arduboy.repeat(RIGHT_BUTTON) ? 1 : arduboy.repeat(LEFT_BUTTON) ? -1 : 0;
    int dy = arduboy.repeat(DOWN_BUTTON) ? 1 : arduboy.repeat(UP_BUTTON) ? -1 : 0;
    if (stage::board) {
        if (dx || dy) { stage::moveKey(dx, dy); audio::sfx(Sfx::Cursor); }
        if (arduboy.justPressed(A_BUTTON)) stage::press();
        else if (arduboy.repeat(B_BUTTON, 24, 8)) stage::rubOut();
        else if (arduboy.justPressed(START_BUTTON)) stage::closeBoard();
        else if (arduboy.justPressed(SELECT_BUTTON)) stage::flip();
    } else {
        if (dx || dy) {
            if (stage::move(dx, dy)) audio::sfx(Sfx::Cursor);
        }
        // B: let go at once, the word turns; held, the camera goes to the
        // other view (the close-up, usually) until it is let go.
        if (arduboy.pressed(B_BUTTON)) {
            if (bHeld < HOLD && ++bHeld == HOLD) { stage::peek = true; audio::sfx(Sfx::Open); }
        } else if (bHeld) {
            if (bHeld < HOLD) { stage::flip(); audio::sfx(Sfx::Cursor); }
            else audio::sfx(Sfx::Close);
            bHeld = 0;
            stage::peek = false;
        }
        if (arduboy.justPressed(A_BUTTON)) { stage::openBoard(); bHeld = 0; stage::peek = false; }
        else if (arduboy.justPressed(SELECT_BUTTON)) { stage::nextClue(); audio::sfx(Sfx::Select); }
        else if (arduboy.justPressed(START_BUTTON)) {
            overlay = PAUSE;
            sel = 0;
            audio::sfx(Sfx::Select);
            stage::invalidate();
        }
    }
    stage::update();
}

static void playRender(uint32_t frame) {
    if (!stage::render(frame)) return;
    char buf[40], *p;
    if (overlay == PAUSE) {
        uint8_t items[5], n = pauseItems(items);
        int y0 = 48 - n * 7;
        fillRound(4, y0 - 7, 120, n * 14 + 28, 3, NAVY);
        roundRect(4, y0 - 7, 120, n * 14 + 28, 3, GOLD);
        for (uint8_t i = 0; i < n; i++) {
            int y = y0 + i * 14;
            if (i == sel) fillRound(8, y - 3, 112, 13, 3, INK);
            const char *s = PAUSE_ITEM[items[i]];
            text35x2(64 - text35x2Width(s) / 2, y - 1, s, i == sel ? FX_B : WHITE);
        }
        centred35(y0 + n * 14 + 4, "A TYPE  B TURN  SELECT NEXT", SILVER);
        centred35(y0 + n * 14 + 11, "HOLD B: THE OTHER VIEW", SILVER);
    } else if (overlay == RESULT) {
        panel(30, 88);
        // The puzzle's name, as large as it fits.
        if (fontWidth(puz::title) <= 102) centred2(34, puz::title, GOLD);
        else if (text35x2Width(puz::title) <= 102) text35x2(64 - text35x2Width(puz::title) / 2, 36, puz::title, GOLD);
        else centred35(39, puz::title, GOLD);
        p = fmtStr(buf, "TIME ");
        p = fmtTime(p, result.seconds);
        p = fmtStr(p, "  PAR ");
        fmtTime(p, result.par);
        centred35(51, buf, WHITE);
        // The score counts up.
        uint32_t shown = resultT >= 30 ? result.score : (uint32_t)result.score * resultT / 30;
        p = fmtStr(buf, "SCORE ");
        fmtInt(p, (int32_t)shown);
        centred2(60, buf, FX_B);
        centred35(77, result.perfect ? "PERFECT! +1000" : result.clean ? "NO LETTERS REVEALED +250" : "", CYAN);
        for (uint8_t i = 0; i < 3; i++) {
            bool on = i < result.stars && resultT >= 40 + i * 14;
            star(39 + i * 18, 86, on ? FX_B : INK, 2);
        }
        if (newBest && resultT > 90) centred35(103, "NEW BEST!", FX_A);
        centred35(110, "A NEXT   B MENU", SILVER);
        fx::drawParticles(2);
    }
}

// ---------------------------------------------------------------------------
// Options (device debug builds leave the screen out to fit the protocol)
// ---------------------------------------------------------------------------
#if !CHCW_LEAN
enum Opt : uint8_t { O_SOUND, O_FELT, O_CHECK, O_SKIP, O_VIEW, O_BACK, OPT_COUNT };
static const char *const OPT_TEXT[OPT_COUNT] = {
    "SOUND|OFF|ON", "FELT|GREEN|BLUE|RED|PURPLE", "CHECKING|ON|OFF", "TYPING|SKIPS|STEPS", "VIEW|WHOLE|CLOSE",
    "BACK",
};
static uint8_t &optByte(uint8_t i) { return ((uint8_t *)&opt)[i]; }

static uint8_t optField(const char *s, uint8_t k, char *buf) {
    uint8_t n = 0;
    for (;;) {
        const char *e = s;
        while (*e && *e != '|') e++;
        if (n == k) { uint8_t len = (uint8_t)(e - s); memcpy(buf, s, len); buf[len] = 0; }
        n++;
        if (!*e) return n;
        s = e + 1;
    }
}

static void optionsUpdate() {
    if (arduboy.repeat(UP_BUTTON)) { sel = (uint8_t)((sel + OPT_COUNT - 1) % OPT_COUNT); audio::sfx(Sfx::Cursor); }
    if (arduboy.repeat(DOWN_BUTTON)) { sel = (uint8_t)((sel + 1) % OPT_COUNT); audio::sfx(Sfx::Cursor); }
    int d = arduboy.justPressed(RIGHT_BUTTON) ? 1 : (arduboy.justPressed(LEFT_BUTTON) ? -1 : 0);
    if (arduboy.justPressed(A_BUTTON) && sel != O_BACK) d = 1;
    if (d && sel != O_BACK) {
        char tmp[12];
        uint8_t n = (uint8_t)(optField(OPT_TEXT[sel], 0, tmp) - 1);
        uint8_t &f = optByte(sel);
        f = (uint8_t)((f + n + d) % n);
        applyOptions();
        audio::sfx(Sfx::Coin);
    }
    if ((arduboy.justPressed(A_BUTTON) && sel == O_BACK) || arduboy.justPressed(B_BUTTON)) {
        audio::sfx(Sfx::Select);
        persist(hasGame);
        go(Scr::Title);
    }
}

static void optionsRender(uint32_t frame) {
    feltBackdrop();
    heading("OPTIONS", 8);
    for (uint8_t i = 0; i < OPT_COUNT; i++) {
        int y = 27 + i * 14;
        char label[12], value[12];
        optField(OPT_TEXT[i], 0, label);
        if (i == sel) {
            fillRound(8, y - 3, 112, 15, 3, NAVY);
            roundRect(8, y - 3, 112, 15, 3, (frame & 16) ? FX_B : GOLD);
        }
        if (i == O_BACK) { text35x2(64 - text35x2Width(label) / 2, y, label, i == sel ? GOLD : WHITE); continue; }
        text35x2(13, y, label, i == sel ? GOLD : WHITE);
        optField(OPT_TEXT[i], (uint8_t)(optByte(i) + 1), value);
        text35x2(116 - text35x2Width(value), y, value, i == sel ? WHITE : FELT_LT);
    }
    centred35(111, sel == O_VIEW ? (opt.view ? "HOLD B FOR THE WHOLE GRID" : "HOLD B FOR THE CLOSE-UP")
                   : opt.check ? "NO HELP: JUDGED WHEN FULL" : "RIGHT WORDS LOCK IN GOLD", SILVER);
    centred35(118, "3X5 FONT: PRESS PLAY ON TAPE", SILVER);
}
#endif

// ---------------------------------------------------------------------------
// Debug protocol hooks (tools/chsim/chdrive.py 'say')
// ---------------------------------------------------------------------------
#if CHCW_DEBUG
//   G <i> [pack]        start puzzle i of a pack (0, the built-in one, by default)
//   H                   STATE scr=<T|S|P|O> ov=<overlay> cur=<cell> down=<0|1> board=<0|1> key=<k>
//                       score= streak= locked= words= solved= sec= n= busy= jp=<the jackpot word>
//   W                   the next word not done: WORD <w> <cell> <down> <answer> <entered, . for none>
//   C <cell> <down>     put the cursor there
//   Z <k>               the grid filled in but for the cells of the last k words (no show)
//   U <seconds>         put the clock forward
//   J <T|S|O>           jump to title / the puzzle list / options
//   X <0|1>             (simulator) put the card in / pull it out
//   Q                   (simulator) time the calibration primitives
static bool debugHook(char cmd, const char *args) {
    char buf[120], *p;
    switch (cmd) {
        case 'G': {
            uint8_t i = (uint8_t)dbg::parseNum(args, 10), pk = (uint8_t)dbg::parseNum(args, 10);
            gfx_wait();
            if (pk) pack::scan();
            if (!pack::select(pk) || !startPuzzle(i)) return false;
            lastPack = pack::id();
            fadeOut = 1;
            return true;
        }
        case 'H': {
            static const char SCR[] = "TSPO";
            p = fmtStr(buf, "STATE scr=");
            *p++ = SCR[(int)cur];
            p = fmtInt(fmtStr(p, " ov="), overlay);
            p = fmtInt(fmtStr(p, " cur="), stage::cur);
            p = fmtInt(fmtStr(p, " down="), stage::down);
            p = fmtInt(fmtStr(p, " board="), stage::board);
            p = fmtInt(fmtStr(p, " key="), stage::key);
            p = fmtInt(fmtStr(p, " score="), game::st.score);
            p = fmtInt(fmtStr(p, " streak="), game::st.streak);
            p = fmtInt(fmtStr(p, " locked="), puz::lockedWords());
            p = fmtInt(fmtStr(p, " words="), puz::nWords);
            p = fmtInt(fmtStr(p, " solved="), game::st.solved);
            p = fmtInt(fmtStr(p, " sec="), game::seconds());
            p = fmtInt(fmtStr(p, " n="), puz::n);
            p = fmtInt(fmtStr(p, " busy="), stage::busy());
            p = fmtInt(fmtStr(p, " jp="), game::st.jackpot);
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
        case 'W': {
            for (uint8_t w = 0; w < puz::nWords; w++) {
                if (puz::wordFull(w) && puz::wordRight(w)) continue;
                p = fmtInt(fmtStr(buf, "WORD "), w);
                p = fmtInt(fmtStr(p, " "), puz::wStart[w]);
                p = fmtInt(fmtStr(p, " "), puz::isDown(w));
                *p++ = ' ';
                for (uint8_t k = 0; k < puz::wLen[w]; k++) *p++ = (char)('A' + puz::sol[puz::cellOf(w, k)] - 1);
                *p++ = ' ';
                for (uint8_t k = 0; k < puz::wLen[w]; k++) {
                    uint8_t v = puz::cell[puz::cellOf(w, k)] & puz::LETTER;
                    *p++ = v ? (char)('A' + v - 1) : '.';
                }
                fmtStr(p, "\n");
                dbg::print(buf);
                return true;
            }
            dbg::print("WORD none\n");
            return true;
        }
        case 'C': {
            uint8_t c = (uint8_t)dbg::parseNum(args, 10);
            stage::setCursor(c, dbg::parseNum(args, 10) != 0);
            return true;
        }
        case 'Z': {
            uint8_t keep = (uint8_t)dbg::parseNum(args, 10);
            for (uint8_t w = 0; w < puz::nWords; w++)
                for (uint8_t k = 0; k < puz::wLen[w]; k++) {
                    uint8_t c = puz::cellOf(w, k);
                    puz::cell[c] = w + keep < puz::nWords ? puz::sol[c] : 0;
                }
            game::Record r;
            game::save(r);
            game::load(r);
            return true;
        }
        case 'U':
            game::st.ticks += dbg::parseNum(args, 10) * 60;
            return true;
        case 'J':
            if (*args == 'T') go(Scr::Title);
            else if (*args == 'S') go(Scr::Select);
            else if (*args == 'O') go(Scr::Options);
            else return false;
            return true;
#ifdef CHSIM
        case 'X':
            sim_cardEject(*args == '1');
            return true;
        case 'Q': {
            // Calibration for chdrive's `cal`: host ns for the primitives the
            // CHGfx benchmark measured on the board (benchmark-results.txt).
            static uint8_t spr[8 * 16];
            memset(spr, 0x3F, sizeof spr);
            uint64_t t0, r[5];
            t0 = sim_hostNanos(); for (int i = 0; i < 200; i++) gfx_clear((uint8_t)i); r[0] = (sim_hostNanos() - t0) / 200;
            t0 = sim_hostNanos(); for (int i = 0; i < 20000; i++) gfx_hline(0, i & 127, 128, (uint8_t)i); r[1] = (sim_hostNanos() - t0) / 20000;
            t0 = sim_hostNanos(); for (int i = 0; i < 2000; i++) gfx_blit(spr, i & 63, i & 63, 16, 16, 15); r[2] = (sim_hostNanos() - t0) / 2000;
            t0 = sim_hostNanos(); for (int i = 0; i < 1000; i++) gfx_text(0, i & 63, "ABCDEFGHIJKLMNOPQRSTUVWX", 1); r[3] = (sim_hostNanos() - t0) / 1000;
            t0 = sim_hostNanos(); for (int i = 0; i < 1000; i++) gfx_fillCircle(64, 64, 30, (uint8_t)i); r[4] = (sim_hostNanos() - t0) / 1000;
            p = fmtStr(buf, "CAL");
            for (int k = 0; k < 5; k++) { *p++ = ' '; p = fmtInt(p, (int32_t)r[k]); }
            fmtStr(p, "\n");
            dbg::print(buf);
            return true;
        }
#endif
    }
    return false;
}
#endif

// ---------------------------------------------------------------------------
void begin() {
    opt.sound = 1;
    save::load(opt, prog, hasGame);
    applyOptions();
    pack::select(0);
    lastPack = pack::id();
#if CHCW_DEBUG
    dbg::hook = debugHook;
#endif
    enter(Scr::Title);
}

void update() {
    t++;
    if (fadeOut) {
        pal::setFade((uint8_t)((fadeOut - 1) * 2));
        if (--fadeOut == 0) enter(pending);
        fx::update();
        return;
    }
    if (fadeIn) { fadeIn--; pal::setFade((uint8_t)(16 - fadeIn * 2)); }
    switch (cur) {
        case Scr::Title:   titleUpdate(); break;
        case Scr::Select:  selectUpdate(); break;
        case Scr::Play:    playUpdate(); break;
#if !CHCW_LEAN
        case Scr::Options: optionsUpdate(); break;
#else
        default: break;
#endif
    }
    fx::update();
}

void render(uint32_t frame) {
    switch (cur) {
        case Scr::Title:   titleRender(frame); break;
        case Scr::Select:  selectRender(frame); break;
        case Scr::Play:    playRender(frame); break;
#if !CHCW_LEAN
        case Scr::Options: optionsRender(frame); break;
#else
        default: break;
#endif
    }
}

}  // namespace screens
