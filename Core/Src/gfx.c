/**
 * @file    gfx.c
 * @see     gfx.h
 *
 * Every primitive funnels through pixel_clip(), which clamps to the
 * optional clip rect and to the panel, and marks the touched page
 * dirty - so a partial redraw only costs the pages that changed.
 */

#include "gfx.h"

#include <string.h>

#include "font5x7.h"
#include "ssd1306.h"

static bool in_clip(const gfx_clip_t *clip, int16_t x, int16_t y)
{
    if (clip == NULL) {
        return true;
    }
    if (x < clip->x || y < clip->y) {
        return false;
    }
    return (x < clip->x + (int16_t)clip->w) &&
           (y < clip->y + (int16_t)clip->h);
}

static void put(const gfx_clip_t *clip, int16_t x, int16_t y, bool on)
{
    if (!in_clip(clip, x, y)) {
        return;
    }
    if (x < 0 || y < 0) {
        return;
    }
    ssd1306_pixel((uint16_t)x, (uint16_t)y, on);
}

void gfx_pixel(int16_t x, int16_t y, bool on)
{
    put(NULL, x, y, on);
}

void gfx_hline(int16_t x, int16_t y, uint16_t len, bool on)
{
    for (uint16_t i = 0U; i < len; i++) {
        put(NULL, x + (int16_t)i, y, on);
    }
}

void gfx_vline(int16_t x, int16_t y, uint16_t len, bool on)
{
    for (uint16_t i = 0U; i < len; i++) {
        put(NULL, x, y + (int16_t)i, on);
    }
}

void gfx_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, bool on)
{
    gfx_fill_rect_clip(NULL, x, y, w, h, on);
}

void gfx_fill_rect_clip(const gfx_clip_t *clip, int16_t x, int16_t y,
                        uint16_t w, uint16_t h, bool on)
{
    for (uint16_t row = 0U; row < h; row++) {
        for (uint16_t col = 0U; col < w; col++) {
            put(clip, x + (int16_t)col, y + (int16_t)row, on);
        }
    }
}

void gfx_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, bool on)
{
    if (w == 0U || h == 0U) {
        return;
    }
    gfx_hline(x, y, w, on);
    gfx_hline(x, (int16_t)(y + h - 1U), w, on);
    gfx_vline(x, y, h, on);
    gfx_vline((int16_t)(x + w - 1U), y, h, on);
}

void gfx_bitmap(int16_t x, int16_t y, const uint8_t *cols, uint16_t w,
                uint8_t h, bool on)
{
    gfx_bitmap_clip(NULL, x, y, cols, w, h, on);
}

void gfx_bitmap_clip(const gfx_clip_t *clip, int16_t x, int16_t y,
                    const uint8_t *cols, uint16_t w, uint8_t h, bool on)
{
    for (uint16_t c = 0U; c < w; c++) {
        for (uint8_t r = 0U; r < h; r++) {
            if ((cols[c] & (uint8_t)(1U << r)) != 0U) {
                put(clip, x + (int16_t)c, y + (int16_t)r, on);
            }
        }
    }
}

uint16_t gfx_char_width(gfx_font_t font)
{
    /* Bold is the same 5x7 bitmap with every stroke widened by one
     * pixel (right and down), hence one pixel more per character. */
    return (font == FONT_BOLD) ? (uint16_t)(FONT5X7_WIDTH + 2U)
                               : (uint16_t)(FONT5X7_WIDTH + 1U);
}

void gfx_char_clip(const gfx_clip_t *clip, int16_t x, int16_t y,
                   char ch, gfx_font_t font, bool on)
{
    const uint8_t *cols = font5x7_glyph(ch);

    if (cols == NULL) {
        return;
    }

    for (uint8_t c = 0U; c < FONT5X7_WIDTH; c++) {
        for (uint8_t r = 0U; r < FONT5X7_HEIGHT; r++) {
            if ((cols[c] & (uint8_t)(1U << r)) != 0U) {
                /* Draw the glyph plus a shifted copy: both weights end
                 * up the same advance width, but bold has 2 px thick
                 * strokes and stays readable at a glance. */
                put(clip, x + (int16_t)c, y + (int16_t)r, on);
                if (font == FONT_BOLD) {
                    put(clip, (int16_t)(x + c + 1), (int16_t)(y + r), on);
                    put(clip, (int16_t)(x + c), (int16_t)(y + r + 1), on);
                }
            }
        }
    }
}

void gfx_char(int16_t x, int16_t y, char ch, gfx_font_t font, bool on)
{
    gfx_char_clip(NULL, x, y, ch, font, on);
}

uint16_t gfx_text_width(const char *s, gfx_font_t font)
{
    return (uint16_t)((uint16_t)strlen(s) * gfx_char_width(font));
}

uint16_t gfx_text(int16_t x, int16_t y, const char *s, gfx_font_t font)
{
    uint16_t adv = gfx_char_width(font);

    while (*s != '\0') {
        gfx_char(x, y, *s, font, true);
        x = (int16_t)(x + adv);
        s++;
    }
    return x;
}
