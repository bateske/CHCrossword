#pragma GCC optimize("Os", "no-ipa-sra")
#include <string.h>
#include <CHGfx.h>
#include "Mask.h"

#include "Draw.h"
#include "../assets/Assets.h"
#include "../RamFunc.h"

Mask maskBegin(int w, int h) {
    Mask m;
    m.w = (uint8_t)w; m.h = (uint8_t)h;
    m.stride = (uint8_t)((w + 2 + 7) >> 3);
    m.bits = gfx_chunkScratch();
    memset(m.bits, 0, (uint32_t)m.stride * (uint32_t)(h + 2));   // newlib's: word stores
    return m;
}

// The glyph after its character byte, or none (a space).
static const uint8_t *glyphOf(char ch) {
    for (const uint8_t *g = FONT; *g; g += 3 + ((g[1] * (g[2] & 15) + 7) >> 3))
        if (*g == (uint8_t)ch) return g + 1;
    return nullptr;
}

int fontWidth(const char *s, uint8_t gap) {
    int w = 0;
    for (; *s; s++) {
        const uint8_t *g = glyphOf(*s);
        w += g ? g[0] + gap : 4;
    }
    return w > 0 ? w - gap : 0;
}

// Each glyph row is one bit pattern (up to 13 bits) ORed into its mask row:
// a few byte ORs a row rather than a call per pixel.
void maskFont(Mask &m, int x, int y, const char *s, const int8_t *dy, uint8_t gap) {
    for (int k = 0; s[k]; k++) {
        const uint8_t *g = glyphOf(s[k]);
        if (!g) { x += 4; continue; }                       // a space
        int w = g[0], rows = g[1] & 15, bx = x + 1;          // + the margin
        int top = y + 1 + (g[1] >> 4) + (dy ? dy[k] : 0);
        const uint8_t *bits = g + 2;
        uint32_t bit = 0;
        for (int r = 0; r < rows; r++) {
            uint32_t pat = 0;
            for (int i = 0; i < w; i++, bit++) pat = (pat << 1) | ((bits[bit >> 3] >> (7 - (bit & 7))) & 1);
            int row = top + r;
            if (!pat || bx < 0 || (unsigned)row >= (unsigned)(m.h + 2)) continue;
            pat <<= 32 - w - (bx & 7);
            uint8_t *p = m.bits + row * m.stride;
            for (int b = 0; b < 4 && (bx >> 3) + b < m.stride; b++) p[(bx >> 3) + b] |= (uint8_t)(pat >> (24 - 8 * b));
        }
        x += w + gap;
    }
}

// Paint the set bits of one mask row (stride bytes, MSB-first) at screen
// row y, bit 0 at screen column x. A mask byte is eight pixels, four
// framebuffer bytes, so the bits go two at a time (at an odd x shifted one
// along first). From SRAM: this loop is most of a banner's cost.
RAMFUNC(maskruns) static void runs(const uint8_t *row, uint8_t stride, int x, int y, uint8_t c) {
    if ((unsigned)y >= GFX_H) return;
    uint8_t *fb = gfx_fb + y * GFX_FB_STRIDE;
    uint8_t cc = (uint8_t)(c | (c << 4));
    int odd = x & 1, px = x - odd;
    uint8_t prev = 0;
    for (int i = 0; i <= stride; i++, px += 8) {
        uint8_t cur = i < stride ? row[i] : 0;
        uint8_t b = odd ? (uint8_t)((prev << 7) | (cur >> 1)) : cur;
        prev = cur;
        for (int xx = px; b; xx += 2, b = (uint8_t)(b << 2)) {
            if ((unsigned)xx >= GFX_W) continue;
            uint8_t &q = fb[xx >> 1];
            switch (b & 0xC0) {
                case 0xC0: q = cc; break;
                case 0x80: q = (uint8_t)((q & 0xF0) | c); break;          // left pixel
                case 0x40: q = (uint8_t)((q & 0x0F) | (c << 4)); break;   // right pixel
            }
        }
    }
}

// Grow row r by one pixel in all 8 directions into out. From SRAM too: it
// runs for every row (the outline and the shadow share it).
RAMFUNC(maskdilate) static void dilateRow(const Mask &m, int r, uint8_t *out) {
    int rows = m.h + 2;
    for (int b = 0; b < m.stride; b++) {
        uint8_t v = m.bits[r * m.stride + b];
        if (r > 0) v |= m.bits[(r - 1) * m.stride + b];
        if (r + 1 < rows) v |= m.bits[(r + 1) * m.stride + b];
        out[b] = v;
    }
    uint8_t carryL = 0;
    uint8_t tmp[32];
    for (int b = 0; b < m.stride; b++) tmp[b] = out[b];
    for (int b = m.stride - 1; b >= 0; b--) {           // shift left (x-1)
        uint8_t nc = (uint8_t)(tmp[b] >> 7);
        out[b] |= (uint8_t)((tmp[b] << 1) | carryL);
        carryL = nc;
    }
    uint8_t carryR = 0;
    for (int b = 0; b < m.stride; b++) {                  // shift right (x+1)
        uint8_t nc = (uint8_t)(tmp[b] << 7);
        out[b] |= (uint8_t)((tmp[b] >> 1) | carryR);
        carryR = nc;
    }
}

int fontText(int x, int y, const char *s, uint8_t c, uint8_t gap) {
    int w = fontWidth(s, gap);
    Mask m = maskBegin(w, FONT_H);
    maskFont(m, 0, 0, s, nullptr, gap);
    for (int r = 1; r <= m.h; r++) runs(m.bits + r * m.stride, m.stride, x, y + r - 1, c);
    return w;
}

void maskDraw(const Mask &m, int x, int y, uint8_t outline, uint8_t shadow, const uint8_t *ramp) {
    int rows = m.h + 2;
    int ox = x - 1, oy = y - 1;                          // undo the margin
    uint8_t d[32];
    // Each grown row is painted twice: as the shadow, a row down and a pixel
    // right, then as the outline. Screen row Y still gets its shadow before
    // its outline, as if all the shadow went down first. A shadow the colour
    // of the outline is none.
    for (int r = 0; r < rows; r++) {
        dilateRow(m, r, d);
        if (shadow != outline) runs(d, m.stride, ox + 1, oy + r + 1, shadow);
        runs(d, m.stride, ox, oy + r, outline);
    }
    for (int r = 1; r < rows - 1; r++) runs(m.bits + r * m.stride, m.stride, ox, oy + r, ramp[r - 1]);
}
