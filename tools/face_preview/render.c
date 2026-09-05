/**
 * @file    tools/face_preview/render.c
 * @brief   Host harness: renders the Casio face with the real
 *          face_casio.c / gfx.c / seg7.c / font5x7.c and a fake
 *          display, then dumps ASCII frames to stdout.
 *
 * Build & run:  tools/face_preview/run.sh
 */

#include <stdio.h>
#include <string.h>

#include "face_casio.h"
#include "ssd1306.h"

/* Print rows y0..y1 (inclusive) of the fake panel. */
static void dump_band(const char *label, uint16_t y0, uint16_t y1)
{
    const uint8_t *fb = ssd1306_fb();

    printf("-- %s\n", label);
    for (unsigned y = y0; y <= y1; y++) {
        for (unsigned x = 0U; x < OLED_WIDTH; x++) {
            unsigned page = y / 8U;
            unsigned bit = y % 8U;

            putchar((fb[page * SSD1306_STRIDE + x] & (1U << bit)) ? '#'
                                                                    : '.');
        }
        putchar('\n');
    }
    putchar('\n');
}

int main(void)
{
    ds1307_time_t t = {0};
    char label[64];

    t.minute = 41U;
    t.hour = 9U;
    t.date = 4U;
    t.month = 9U;
    t.year = 26U;
    t.pm = false;
    t.oscillator_halted = false;

    face_init();

    /* Frame right after boot: cells snap to the real glyphs (nothing to
     * roll in yet), so the whole time should already be readable. */
    t.second = 23U;
    t.day = 5U; /* Friday */
    face_draw(&t, true, 0U);
    dump_band("boot frame 09:41:23 (clock band, rows 9..27)", 9U, 27U);
    dump_band("boot frame header (rows 0..8)", 0U, 8U);

    /* Same time, animation finished. */
    face_draw(&t, true, 500U);
    dump_band("settled 09:41:23", 9U, 27U);

    /* Seconds tick 23 -> 24 at t=1000ms: watch the units cell roll. */
    t.second = 24U;
    for (uint32_t ms = 1000U; ms <= 1700U; ms += 100U) {
        snprintf(label, sizeof(label), "roll %04ums (sec 3 -> 4)",
                 (unsigned)ms);
        face_draw(&t, true, ms);
        dump_band(label, 9U, 27U);
    }

    /* Big jump 09:41:24 -> 10:00:00: five cells change at once. */
    t.second = 0U;
    t.minute = 0U;
    t.hour = 10U;
    t.day = 5U;
    for (uint32_t ms = 2000U; ms <= 2400U; ms += 200U) {
        snprintf(label, sizeof(label), "roll %04ums (09:41:24 -> 10:00:00)",
                 (unsigned)ms);
        face_draw(&t, true, ms);
        dump_band(label, 9U, 27U);
    }

    /* RTC not talking: dashes and SET RTC. */
    face_draw(&t, false, 3000U);
    dump_band("no time source", 0U, 27U);

    return 0;
}