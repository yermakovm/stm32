/**
 * @file    font4x6.h
 * @brief   Compact 4x6 glyph table (caps, digits, punctuation), generated
 *          from ASCII art by tools/gen_font4x6.py (do not edit by hand).
 *
 * The 5x7 font advances 6 px per character, so the sensor line
 * "T:23.4°C RH:45% P:1013hPa" (25 chars) would need 150 px of a 128 px
 * panel.  This one advances 5 px, which makes the same line 125 px.
 *
 * Glyphs are column-major with bit0 = top row, like font5x7.
 */

#ifndef FONT4X6_H
#define FONT4X6_H

#include <stdint.h>

#define FONT4X6_WIDTH        4
#define FONT4X6_HEIGHT       6
#define FONT4X6_FIRST        0x20
#define FONT4X6_LAST         0x7F   /* 0x7F = degree, see CH_DEGREE in gfx.h */
#define FONT4X6_GLYPH_COUNT  (FONT4X6_LAST - FONT4X6_FIRST + 1)

/** Column data for @p ch (4 bytes), or NULL if the code is out of range.
 *  Lowercase is folded to uppercase; codes with no art are blank. */
const uint8_t *font4x6_glyph(char ch);

#endif /* FONT4X6_H */
