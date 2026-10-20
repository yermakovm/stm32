/**
 * @file    tools/face_preview/render.c
 * @brief   Host harness: renders the Casio face with the real
 *          face_casio.c / gfx.c / seg7.c / font5x7.c / font4x6.c and a
 *          fake display, then dumps ASCII frames to stdout.
 *
 * Build & run:  tools/face_preview/run.sh
 */

#include <stdio.h>
#include <string.h>

#include "face_casio.h"
#include "gfx.h"
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

/* Call sites use plain engineering units x1000; the driver's humidity is
 * Q10 (see bme280.c), so convert here. */
static bme280_data_t env_sample(int32_t temp_c_x100, uint32_t press_pa_x100,
                                uint32_t rh_pct_x1000)
{
    bme280_data_t e;

    e.temp_c_x100 = temp_c_x100;
    e.press_pa_x100 = press_pa_x100;
    e.rh_x1024 = rh_pct_x1000 * 1024U / 1000U;
    return e;
}

/* 0x7F is invisible in a terminal: print it as "o" so the line is
 * readable the way it will look on the panel. */
static void show_line(const char *label, const char *s)
{
    char shown[64];
    size_t i = 0;

    while (s[i] != '\0' && i < sizeof(shown) - 1U) {
        shown[i] = (s[i] == CH_DEGREE) ? 'o' : s[i];
        i++;
    }
    shown[i] = '\0';
    printf("  %-8s \"%s\"  (%u px)\n", label, shown,
           (unsigned)gfx_text_width(s, FONT_MINI));
}

int main(void)
{
    ds1307_time_t t = {0};
    bme280_data_t env = env_sample(2344U, 10132500U, 45123U);
    char label[64];
    char line[32];

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
    face_draw(&t, true, 0U, &env, true);
    dump_band("boot frame 09:41:23 + 23.4C/45%/1013hPa (clock band, rows 9..31)",
              9U, 31U);
    dump_band("boot frame header + sensor row (rows 0..8)", 0U, 8U);

    /* Same time, animation finished. */
    face_draw(&t, true, 500U, &env, true);
    dump_band("settled 09:41:23", 9U, 31U);

    /* Seconds tick 23 -> 24 at t=1000ms: watch the units cell roll. */
    t.second = 24U;
    for (uint32_t ms = 1000U; ms <= 1700U; ms += 100U) {
        snprintf(label, sizeof(label), "roll %04ums (sec 3 -> 4)",
                 (unsigned)ms);
        face_draw(&t, true, ms, &env, true);
        dump_band(label, 15U, 31U);
    }

    /* Big jump 09:41:24 -> 10:00:00: five cells change at once. */
    t.second = 0U;
    t.minute = 0U;
    t.hour = 10U;
    t.day = 5U;
    for (uint32_t ms = 2000U; ms <= 2400U; ms += 200U) {
        snprintf(label, sizeof(label), "roll %04ums (09:41:24 -> 10:00:00)",
                 (unsigned)ms);
        face_draw(&t, true, ms, &env, true);
        dump_band(label, 15U, 31U);
    }

    /* Sensor row on its own: normal, worst case, and no chip. */
    printf("-- sensor strings\n");
    face_build_sensor(line, &env, true, false);
    show_line("normal", line);
    env = env_sample(-4000, 11000000U, 100000U);   /* -40.0C 100% 1100hPa */
    face_build_sensor(line, &env, true, false);
    show_line("widest", line);
    face_build_sensor(line, &env, true, true);
    show_line("compact", line);
    face_build_sensor(line, NULL, false, false);
    show_line("missing", line);

    env = env_sample(2344U, 10132500U, 45123U);
    face_draw(&t, true, 3000U, &env, true);
    dump_band("sensor row in place (rows 8..15)", 8U, 15U);

    face_draw(&t, true, 3100U, NULL, false);
    dump_band("no sensor (rows 8..15)", 8U, 15U);

    /* RTC not talking: dashes and SET RTC. */
    face_draw(&t, false, 3000U, &env, true);
    dump_band("no time source", 0U, 31U);

    return 0;
}
