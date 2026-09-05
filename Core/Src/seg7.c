/**
 * @file    seg7.c
 * @see     seg7.h
 */

#include "seg7.h"

/* 0..9 as segment masks (bit0 = A ... bit6 = G). */
const uint8_t SEG7_DIGITS[10] = {
    SEG7_A | SEG7_B | SEG7_C | SEG7_D | SEG7_E | SEG7_F,
    SEG7_B | SEG7_C,
    SEG7_A | SEG7_B | SEG7_G | SEG7_E | SEG7_D,
    SEG7_A | SEG7_B | SEG7_G | SEG7_C | SEG7_D,
    SEG7_F | SEG7_G | SEG7_B | SEG7_C,
    SEG7_A | SEG7_F | SEG7_G | SEG7_C | SEG7_D,
    SEG7_A | SEG7_F | SEG7_G | SEG7_E | SEG7_C | SEG7_D,
    SEG7_A | SEG7_B | SEG7_C,
    SEG7_A | SEG7_B | SEG7_C | SEG7_D | SEG7_E | SEG7_F | SEG7_G,
    /* 9: include D (bottom bar) so it doesn't look like a '4'-ish gap. */
    SEG7_A | SEG7_B | SEG7_C | SEG7_D | SEG7_F | SEG7_G,
};

uint8_t seg7_mask_for(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return SEG7_DIGITS[ch - '0'];
    }
    if (ch == '-') {
        return SEG7_MINUS;
    }
    return SEG7_BLANK;
}

void seg7_draw_digit(const gfx_clip_t *clip, int16_t x, int16_t y,
                     uint8_t segs, const seg7_geom_t *geom, bool on)
{
    const int16_t w = geom->w;
    const int16_t h = geom->h;
    const int16_t t = geom->thick;
    const int16_t mid = (h - t) / 2;      /* top of the middle segment */

    /* Horizontal segments. */
    if ((segs & SEG7_A) != 0U) {
        gfx_fill_rect_clip(clip, x + t, y, (uint16_t)(w - 2 * t), t, on);
    }
    if ((segs & SEG7_G) != 0U) {
        gfx_fill_rect_clip(clip, x + t, y + mid, (uint16_t)(w - 2 * t), t,
                           on);
    }
    if ((segs & SEG7_D) != 0U) {
        gfx_fill_rect_clip(clip, x + t, y + h - t, (uint16_t)(w - 2 * t), t,
                           on);
    }
    /* Vertical segments: F/E on the left, B/C on the right. */
    if ((segs & SEG7_F) != 0U) {
        gfx_fill_rect_clip(clip, x, y + t, t, (uint16_t)(mid - t), on);
    }
    if ((segs & SEG7_E) != 0U) {
        gfx_fill_rect_clip(clip, x, y + mid + t, t,
                           (uint16_t)(h - t - (mid + t)), on);
    }
    if ((segs & SEG7_B) != 0U) {
        gfx_fill_rect_clip(clip, x + w - t, y + t, t, (uint16_t)(mid - t),
                           on);
    }
    if ((segs & SEG7_C) != 0U) {
        gfx_fill_rect_clip(clip, x + w - t, y + mid + t, t,
                           (uint16_t)(h - t - (mid + t)), on);
    }
}

void seg7_draw_colon(const gfx_clip_t *clip, int16_t x, int16_t y,
                     uint8_t w, uint8_t h, uint8_t thick, bool on)
{
    const int16_t cx = x + ((int16_t)w - (int16_t)thick) / 2;

    /* Dots at about 1/3 and 2/3 of the cell height, like a Casio. */
    gfx_fill_rect_clip(clip, cx, y + h / 3 - (int16_t)thick / 2, thick,
                       thick, on);
    gfx_fill_rect_clip(clip, cx, y + (2 * h) / 3 - (int16_t)thick / 2,
                       thick, thick, on);
}
