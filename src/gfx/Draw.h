// The game's own drawing primitives: rounded rects, remapped 4 bpp row-span
// sprites (plain, scaled, rotated), dithering, coloured column glyphs and
// PPOT's 3x5 font. CHGfx 1.3 has its own versions of all but the column
// glyphs (the font as CHGfx_Tiny3x5, with the same glyphs), but they cost
// more flash here, the text about 2 KB more (docs/CHGfx-notes.md).
// Framebuffer only.
//
// The rule on this chip: code runs from flash with 3 wait states, so a
// function call per pixel costs ~2-3 us. Everything here is built from
// gfx_hline spans (word stores, from SRAM) or tight byte and word loops.
#pragma once
#include <stdint.h>
#include <CHGfx.h>

void fillRound(int x, int y, int w, int h, uint8_t r, uint8_t c);   // r <= 4
void roundRect(int x, int y, int w, int h, uint8_t r, uint8_t c);

// span4 art (tools/assets.py pack_span4): w, h, then per row a count and
// (len-1)<<4|colour bytes, colour 15 = skip. Drawn through a remap and
// scaled (Q8, 256 = 1:1).
void sprite4(const uint8_t *data, int x, int y, const uint8_t *remap = nullptr, int scale = 256);
// span4 art turned by `angle` (256 = one turn) and scaled (256 = 1:1) about
// its pixel (ax, ay), which lands on screen (px, py). Decodes into the CHGfx
// chunk scratch: render time only, art up to 32x60.
void spriteRot(const uint8_t *data, int ax, int ay, int px, int py, uint8_t angle, int scale,
               const uint8_t *remap);

void dither(int x, int y, int w, int h, uint8_t c, uint8_t phase);      // 50% checker

// Column-major 1 bpp glyph (bit 0 = top row, <= 8 rows), from SRAM.
void glyph(int x, int y, const uint8_t *cols, uint8_t ncols, uint8_t c);

// Row-major 1 bpp glyph up to 16 wide (the leftmost pixel in bit 15 of each
// row): the close-up's letters. From SRAM.
void glyph16(int x, int y, const uint16_t *rows, uint8_t nrows, uint8_t c);

// PPOT's 3x5 font: 4 px advance, '~' = 2 px space, newline = 7 px down.
int  text35(int x, int y, const char *str, uint8_t c);
int  text35Width(const char *str);
// The 3x5 font at twice the size (menus), each pixel a 2x2 block.
void text35x2(int x, int y, const char *str, uint8_t c);
inline int text35x2Width(const char *str) { return text35Width(str) * 2; }
enum : char { FONT35_FIRST = '!', FONT35_LAST = '_' };
extern const uint8_t FONT35[FONT35_LAST - FONT35_FIRST + 1][3];   // column bytes per glyph
