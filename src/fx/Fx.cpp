#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed (hot pixel loops live in Draw/Mask)
#include <CHGfx.h>
#include <string.h>
#include "Fx.h"
#include "../gfx/Draw.h"
#include "../gfx/Fmt.h"
#include "../gfx/Mask.h"
#include "../gfx/Palette.h"
#include "../RamFunc.h"

namespace fx {

// ---------------------------------------------------------------------------
// Curves: 17-point tables in half-Q8 units (a byte each), linearly
// interpolated.
// ---------------------------------------------------------------------------
static const uint8_t CURVES[4][17] = {
    {0, 23, 42, 60, 74, 87, 97, 105, 112, 118, 121, 124, 126, 127, 128, 128, 128},
    {0, 35, 63, 87, 105, 119, 129, 136, 139, 141, 141, 139, 136, 134, 131, 129, 128},
    {0, 2, 6, 12, 20, 30, 41, 52, 64, 76, 88, 99, 108, 116, 123, 127, 128},
    {0, 4, 15, 34, 61, 95, 124, 108, 98, 97, 102, 116, 125, 120, 123, 127, 128},
};

int ease(Ease e, int t, int n) {
    if (n <= 0 || t >= n) return 256;
    if (t <= 0) return 0;
    int p = (t << 8) / n;                    // 0..255
    int i = p >> 4, f = p & 15;
    const uint8_t *c = CURVES[e];
    return 2 * c[i] + (((c[i + 1] - c[i]) * f) >> 3);
}

static const uint8_t SIN[65] = {
    0, 6, 13, 19, 25, 31, 38, 44, 50, 56, 62, 68, 74, 80, 86, 92, 98, 104, 109, 115, 121, 126,
    132, 137, 142, 147, 152, 157, 162, 167, 172, 177, 181, 185, 190, 194, 198, 202, 206, 209,
    213, 216, 220, 223, 226, 229, 231, 234, 237, 239, 241, 243, 245, 247, 248, 250, 251, 252,
    253, 254, 255, 255, 255, 255, 255};

int isin(int a) {
    a &= 255;
    int q = a >> 6, i = a & 63;
    int v;
    switch (q) {
        case 0: v = SIN[i]; break;
        case 1: v = SIN[64 - i]; break;
        case 2: v = -SIN[i]; break;
        default: v = -SIN[64 - i]; break;
    }
    return v;
}

static uint32_t seed = 0x1234567u;
uint32_t rnd() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
void reseed() { seed = 0x1234567u; }
int rndRange(int lo, int hi) { return hi > lo ? lo + (int)(rnd() % (uint32_t)(hi - lo)) : lo; }

// ---------------------------------------------------------------------------
// Particles
// ---------------------------------------------------------------------------
struct Particle { int16_t x, y; int8_t vx, vy; uint8_t life, colour, kind, age; };
static Particle parts[48];
const uint8_t RAIN[5] = {RED, GOLD, FELT_LT, CYAN, BLUE};

bool particles() {
    for (auto &p : parts) if (p.life) return true;
    return false;
}

void spawn(Kind k, int x, int y, int vx, int vy, uint8_t life, uint8_t colour) {
    Particle *slot = nullptr;
    for (auto &p : parts) if (!p.life) { slot = &p; break; }
    if (!slot) slot = &parts[rnd() % 48];                 // steal one
    slot->x = (int16_t)(x << 4); slot->y = (int16_t)(y << 4);
    slot->vx = (int8_t)(vx < -127 ? -127 : vx > 127 ? 127 : vx);
    slot->vy = (int8_t)(vy < -127 ? -127 : vy > 127 ? 127 : vy);
    slot->life = life; slot->colour = colour; slot->kind = k; slot->age = 0;
}

void burst(Kind k, int x, int y, uint8_t n, int speed, uint8_t colour) {
    for (uint8_t i = 0; i < n; i++) {
        int a = (int)(i * 256 / n) + rndRange(0, 12);
        int sp = speed / 2 + rndRange(0, speed / 2 + 1);
        int vy = (isin(a) * sp) >> 8;
        if (k == DUST) vy /= 2;                           // puffs spread along the floor
        spawn(k, x, y, (isin(a + 64) * sp) >> 8, vy, (uint8_t)rndRange(20, 40), colour);
    }
}

void fountain(int x, int y, uint8_t n) {
    static const uint8_t CONF[6] = {RED, GOLD, FELT_LT, CYAN, BLUE, WHITE};
    for (uint8_t i = 0; i < n; i++)
        spawn(CONFETTI, x + rndRange(-4, 5), y, rndRange(-28, 29), rndRange(-60, -30), (uint8_t)rndRange(40, 70),
              CONF[rnd() % 6]);
}

static void updateParticles() {
    for (auto &p : parts) {
        if (!p.life) continue;
        p.life--; p.age++;
        p.x += p.vx; p.y += p.vy;
        switch (p.kind) {
            case CONFETTI: if (p.age & 1) p.vy += 2; if (p.vy > 24) p.vy = 24;
                           p.vx = (int8_t)(p.vx * 15 / 16); break;
            case DUST:     p.vx = (int8_t)(p.vx * 7 / 8); p.vy = (int8_t)(p.vy * 7 / 8); break;
            default:       p.vy += (p.age & 3) == 0; break;
        }
    }
}

void drawParticles(uint8_t dust) {
    for (auto &p : parts) {
        if (!p.life) continue;
        int x = p.x >> 4, y = p.y >> 4;
        uint8_t c = p.colour;
        switch (p.kind) {
            case SPARK:
                gfx_pixel(x, y, c);
                if (p.age < 8) { gfx_pixel(x - 1, y, c); gfx_pixel(x + 1, y, c);
                                 gfx_pixel(x, y - 1, c); gfx_pixel(x, y + 1, c); }
                break;
            case CONFETTI:
                if ((p.age >> 2) & 1) gfx_hline(x, y, 2, c);
                else gfx_vline(x, y, 2, c);
                break;
            case STAR:
                gfx_hline(x - 1, y, 3, c); gfx_vline(x, y - 1, 3, c);
                break;
            case DUST: {                                  // a puff, down to a speck, centred
                int s = p.life > 10 ? dust : (p.life > 4 || (p.life & 1)) ? (dust + 1) / 2 : 0;
                gfx_fillRect(x - s / 2, y - s / 2, s, s, c);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Banner
// ---------------------------------------------------------------------------
static char bannerText[14];                          // the game's longest is 11
static uint8_t bannerLen, bannerStyle, bannerT, bannerFrames;
static int bannerCy;
static bool bannerHeld;

void banner(const char *text, BannerStyle s, int cy, uint8_t frames) {
    bannerLen = (uint8_t)(fmtStr(bannerText, text) - bannerText);
    bannerStyle = s; bannerCy = cy; bannerT = 0; bannerFrames = frames;
    bannerHeld = false;
}

void holdBanner(bool on) { bannerHeld = on; }

bool bannerActive() { return bannerFrames != 0; }

// The letters drop in one after another from DROP px up, bounce as they
// land, then dance on the spot.
static const int DROP = 18, STAGGER = 2, FALL = 12;

static bool dropping() { return bannerT < STAGGER * bannerLen + FALL; }

void drawBanner() {
    if (!bannerFrames) return;
    int t = bannerT;
    uint8_t gap = 1;
    int w = fontWidth(bannerText, gap);
    if (w > 124) w = fontWidth(bannerText, gap = 0);
    int n = bannerLen, off = (dropping() ? DROP : 0) + 2;
    int8_t dy[14];
    for (int k = 0; k < n && k < 14; k++) {
        int tk = t - STAGGER * k, d = 0;
        if (tk < 0) d = -60;                                            // not yet: out of the mask
        else if (tk < FALL) d = -(((256 - ease(OUT_BOUNCE, tk, FALL)) * DROP) >> 8);
        dy[k] = (int8_t)(d + off + ((isin(t * 10 + k * 36) * 2) >> 8));
    }
    int h = FONT_H + off + 2;
    Mask m = maskBegin(w + 1, h);
    maskFont(m, 0, 0, bannerText, dy, gap);
    // Last few frames: blink out.
    if (bannerFrames < 10 && (bannerFrames & 2)) return;
    uint8_t ramp[40];
    for (int r = 0; r < h && r < 40; r++) {
        int g = r - off;                                                // row of the letters
        switch (bannerStyle) {
            case B_RAINBOW: ramp[r] = RAIN[(((r + 8) / 2) + t / 3) % 5]; break;
            case B_GOLD:    ramp[r] = g < 3 ? FX_B : (g < 8 ? GOLD : WOOD); break;
            case B_RED:     ramp[r] = g < 3 ? WHITE : (g < 9 ? RED : WINE); break;
            case B_CYAN:    ramp[r] = g < 3 ? WHITE : CYAN; break;
            default:        ramp[r] = g < 6 ? WHITE : SILVER; break;
        }
    }
    uint8_t outline = bannerStyle == B_RAINBOW ? FX_A : INK;
    maskDraw(m, 64 - w / 2, bannerCy - FONT_H / 2 - off, outline, INK, ramp);
}

// ---------------------------------------------------------------------------
// Shake
// ---------------------------------------------------------------------------
static uint8_t shakeT, shakeAmp;

void shake(uint8_t frames, uint8_t amp) { shakeT = frames; shakeAmp = amp; }

// Rows y0..y1 moved dy rows and one byte (2 px) sideways, in one pass of
// word copies from SRAM (newlib's memmove is a byte loop in flash: ~10 ms a
// shaken frame). Walks away from the direction of travel so every source row
// is read before it is overwritten; rows the move uncovers shift in place.
RAMFUNC(shake) static void shiftRows(int y0, int y1, int dy, bool right) {
    const int W = GFX_FB_STRIDE / 4;
    for (int k = 0; k <= y1 - y0; k++) {
        int y = dy > 0 ? y1 - k : y0 + k, sy = y - dy;
        if (sy < y0 || sy > y1) sy = y;
        uint32_t *d = (uint32_t *)(gfx_fb + y * GFX_FB_STRIDE);
        const uint32_t *s = (const uint32_t *)(gfx_fb + sy * GFX_FB_STRIDE);
        if (right) {        // d[i] = s[i - 1], the first byte kept
            for (int j = W - 1; j > 0; j--) d[j] = (s[j] << 8) | (s[j - 1] >> 24);
            d[0] = (s[0] << 8) | (s[0] & 0xFF);
        } else {            // d[i] = s[i + 1], the last byte kept
            for (int j = 0; j < W - 1; j++) d[j] = (s[j] >> 8) | (s[j + 1] << 24);
            d[W - 1] = (s[W - 1] >> 8) | (s[W - 1] & 0xFF000000u);
        }
    }
}

void applyShake(int y0, int y1) {
    if (!shakeT) return;
    int a = (shakeAmp * shakeT + 9) / 10;
    if (a < 1) a = 1;
    shiftRows(y0, y1, (shakeT & 1) ? a : -a, (shakeT & 2) != 0);    // 2 px sideways
}

bool activeRows(int &lo, int &hi) {
    lo = 999; hi = -1;
    if (shakeT) { lo = 0; hi = 127; return true; }
    for (auto &p : parts) if (p.life) { int y = p.y >> 4; if (y - 2 < lo) lo = y - 2; if (y + 3 > hi) hi = y + 3; }
    if (bannerFrames) { if (bannerCy - 30 < lo) lo = bannerCy - 30; if (bannerCy + 12 > hi) hi = bannerCy + 12; }
    return hi >= lo;
}

void clear() {
    memset(parts, 0, sizeof parts);
    bannerFrames = 0;
    bannerHeld = false;
    shakeT = 0;
}

void update() {
    updateParticles();
    if (bannerFrames) {
        if (!bannerHeld || bannerFrames > 10) bannerFrames--;    // held: up, until let go to blink out
        if (!++bannerT) bannerT = 128;                           // (the same phase of the dance)
    }
    if (shakeT) shakeT--;
}

}  // namespace fx
