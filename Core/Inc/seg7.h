/**
 * @file    seg7.h
 * @brief   Big "calculator-style" 7-segment digits used for the bold
 *          hours:minutes, drawn procedurally so they scale to any size.
 */

#ifndef SEG7_H
#define SEG7_H

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

/* Segment bitmask:   AAA      *  = segment lit                */
/*                  FF   BB     *  digit 0..9 -> SEG7_DIGITS[]  */
/*                   GGG                                         */
/*                  EE   CC     *  SEG7_MINUS / SEG7_BLANK      */
/*                   DDD                                         */
#define SEG7_A  0x01U
#define SEG7_B  0x02U
#define SEG7_C  0x04U
#define SEG7_D  0x08U
#define SEG7_E  0x10U
#define SEG7_F  0x20U
#define SEG7_G  0x40U
#define SEG7_BLANK 0x00U
#define SEG7_MINUS 0x40U

/** Digit shapes, index = digit value ('0'..'9' - '0'). */
extern const uint8_t SEG7_DIGITS[10];

/** Geometry of one digit cell (thickness = "boldness"). */
typedef struct {
    uint8_t w;      /* cell width including strokes          */
    uint8_t h;      /* cell height                            */
    uint8_t thick;  /* stroke thickness, 2 for thin, 3-4 bold */
} seg7_geom_t;

/** Segment mask for a character ('0'..'9', '-' else SEG7_BLANK). */
uint8_t seg7_mask_for(char ch);

/** Draw one digit at (x, y). clip = NULL for no clipping; pass a
 *  per-cell clip rect when animating a roll so digits slide in and
 *  out of their cell without bleeding into neighbours. */
void seg7_draw_digit(const gfx_clip_t *clip, int16_t x, int16_t y,
                     uint8_t segs, const seg7_geom_t *geom, bool on);

/** Draw a separator colon (two dots) in a cell of width w. */
void seg7_draw_colon(const gfx_clip_t *clip, int16_t x, int16_t y,
                     uint8_t w, uint8_t h, uint8_t thick, bool on);

#endif /* SEG7_H */
