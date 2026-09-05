/**
 * @file    font5x7.h
 * @brief   Hand-written 5x7 glyph table (ASCII 0x20..0x7E), generated
 *          from ASCII art by tools/gen_font.py (do not edit by hand).
 *
 * Glyphs are stored column-major with bit0 = top row, which is the
 * same layout the SSD1306 uses in GDDRAM, so a glyph can be copied
 * straight into the framebuffer when the baseline is page-aligned.
 */

#ifndef FONT5X7_H
#define FONT5X7_H

#include <stdint.h>

/* Named ..._WIDTH/_HEIGHT so the include guard below (FONT5X7_H) does
 * not collide with a macro of the same name. */
#define FONT5X7_WIDTH        5
#define FONT5X7_HEIGHT       7
#define FONT5X7_FIRST        0x20
#define FONT5X7_LAST         0x7E
#define FONT5X7_GLYPH_COUNT  (FONT5X7_LAST - FONT5X7_FIRST + 1)

/** Column data for @p ch (5 bytes), or NULL if the glyph is missing.
 *  Lowercase is folded to uppercase. */
const uint8_t *font5x7_glyph(char ch);

#endif /* FONT5X7_H */
