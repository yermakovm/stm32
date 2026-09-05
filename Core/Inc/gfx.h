/**
 * @file    gfx.h
 * @brief   Drawing helpers on top of the SSD1306 framebuffer,
 *          including "bold" text (strokes widened by one pixel).
 */

#ifndef GFX_H
#define GFX_H

#include <stdbool.h>
#include <stdint.h>

/** Text weight: FONT_BOLD widens every stroke by one pixel, so the
 *  hours/minutes stay legible while seconds stay fine. */
typedef enum {
    FONT_NORMAL,
    FONT_BOLD,
} gfx_font_t;

/** Clip rectangle used by the *_clip variants (digit roll animation). */
typedef struct {
    int16_t x;
    int16_t y;
    uint16_t w;
    uint16_t h;
} gfx_clip_t;

/** Set (on = true) or clear one pixel; out-of-range is ignored. */
void gfx_pixel(int16_t x, int16_t y, bool on);

/** Horizontal / vertical line of len pixels starting at (x, y). */
void gfx_hline(int16_t x, int16_t y, uint16_t len, bool on);
void gfx_vline(int16_t x, int16_t y, uint16_t len, bool on);

/** Filled rectangle of size w x h at (x, y). */
void gfx_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, bool on);

/** Hollow 1 px rectangle (the Casio-style bezel). */
void gfx_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, bool on);

/** Clipped variants: nothing is drawn outside the clip rect. Passing
 *  clip = NULL means "no clipping". */
void gfx_fill_rect_clip(const gfx_clip_t *clip, int16_t x, int16_t y,
                        uint16_t w, uint16_t h, bool on);
void gfx_bitmap_clip(const gfx_clip_t *clip, int16_t x, int16_t y,
                     const uint8_t *cols, uint16_t w, uint8_t h, bool on);

/** Width of one glyph / of a whole string in pixels, including the
 *  inter-character gap (6 px normal, 7 px bold). */
uint16_t gfx_char_width(gfx_font_t font);
uint16_t gfx_text_width(const char *s, gfx_font_t font);

/** Draw text with its top-left corner at (x, y), 7 px tall.
 *  @return the x just past the string. */
uint16_t gfx_text(int16_t x, int16_t y, const char *s, gfx_font_t font);

/** Draw a single glyph. */
void gfx_char(int16_t x, int16_t y, char ch, gfx_font_t font, bool on);

/** Clipped glyph: the digit-roll animation scrolls glyphs across cell
 *  borders, so they need the same clipping the rect blits get. */
void gfx_char_clip(const gfx_clip_t *clip, int16_t x, int16_t y,
                   char ch, gfx_font_t font, bool on);

/** Blit a column-major bitmap (h bits per column, bit0 = top row). */
void gfx_bitmap(int16_t x, int16_t y, const uint8_t *cols, uint16_t w,
                uint8_t h, bool on);

#endif /* GFX_H */