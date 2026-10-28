#ifndef NEMO_CCFONT_H
#define NEMO_CCFONT_H

#include <stdint.h>

// Control Center bitmap font (1.2.4, tools/make_cc_font.py): 4-bit alpha, two pixels per byte, rows of
// w pixels from `off` in ccfont_bits; `top` is the first row below the line top, `left` the x offset.
#define CCFONT_HEIGHT 17   // line height, descenders included (checked by the generator)

typedef struct { uint16_t cp; uint8_t w, h, adv; int8_t top, left; uint16_t off; } CcGlyph;

extern const CcGlyph ccfont_glyphs[];
extern const int ccfont_count;
extern const uint8_t ccfont_bits[];

const CcGlyph *ccfont_glyph(uint32_t cp);   // missing -> '?'

#endif
