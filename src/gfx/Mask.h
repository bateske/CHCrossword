// 1 bpp scratch masks for text and logo effects.
//
// Outlined, shadowed or gradient text drawn the naive way (the glyphs nine
// times over, pixel by pixel from flash) cost ~5 ms a line on this chip.
// Instead: render the shape once into a bit mask, grow it by one pixel with
// byte-wide ORs for the outline, and paint every row straight into the
// framebuffer, two pixels a byte.
//
// CHGfx 1.3's gfx_textFx() works the same way, and draws the same pixels
// with CHGfx_Tiny3x5, but it brings CHGfx's whole GFXfont text path: about
// 2 KB more flash than this and Draw's 3x5 font (docs/CHGfx-notes.md).
//
// The mask lives in CHGfx's chunk buffers (gfx_chunkScratch, 1 KB), which
// are idle between gfx_wait() and the next flush - i.e. during render.
// Never keep a Mask across frames or use one outside render.
#pragma once
#include <stdint.h>

struct Mask {
    uint8_t *bits;       // MSB-first rows, 1 px margin on every side
    uint8_t stride;      // bytes per row
    uint8_t w, h;        // usable size (excluding the margin)
};

Mask maskBegin(int w, int h);                   // cleared; w*h <= ~7000 px

// The display font (tools/art/font.txt: bold slab serifs, capitals 11 px,
// lowercase descending to 13), with its top row at y. dy, if given, moves
// each character up or down (dancing banners); gap is the space between
// letters (their outlines touch at 1, as in CHBlackjack's lettering).
void maskFont(Mask &m, int x, int y, const char *s, const int8_t *dy = nullptr, uint8_t gap = 1);
int  fontWidth(const char *s, uint8_t gap = 1);
constexpr int FONT_H = 13;
// Plain lettering in it, one colour, its top row at y (menus, the cube's
// value close up); returns the width. Render time only (the mask).
int fontText(int x, int y, const char *s, uint8_t c, uint8_t gap = 1);

// Paint the mask with its top-left at (x, y): the shadow (the outline moved
// (1, 1); none when shadow == outline), the outline, then the fill, ramp[r]
// for mask row r (gradient lettering).
void maskDraw(const Mask &m, int x, int y, uint8_t outline, uint8_t shadow, const uint8_t *ramp);
