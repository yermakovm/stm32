/**
 * @file    face_casio.c
 * @see     face_casio.h
 *
 * Screen plan (128x32, UI_SHOW_SENSOR=1; the 128x64 variant uses the
 * same bands, just lower and with taller digits):
 *
 *   +------------------------------+   <- bezel (row 0)
 *   | MON 04.09.2026              |   <- weekday + date, 5x7  (1..7)
 *   |  T:23.4°C RH:45% P:1013hPa  |   <- BME280 line, 4x6     (9..14)
 *   |                             |   <- gutter              (15)
 *   |   [0][9]:[4][1]:[2][3]     |   <- hours:minutes 15px, sec fine
 *   +------------------------------+                              (31)
 *
 * The sensor line needs the 4x6 font: 25 characters x 6 px (5x7) would
 * be 150 px on a 128 px panel, at 5 px/char it is 125 px.  In exchange
 * the big digits lose 5 rows (20 -> 15).  With UI_SHOW_SENSOR=0 the
 * original 20 px digits come back and the row disappears.
 *
 * Only the regions drawn here are pushed to the panel, so a frame
 * costs a couple of I2C page writes instead of the whole framebuffer.
 */

#include "face_casio.h"

#include <string.h>

#include "app_config.h"
#include "gfx.h"
#include "seg7.h"
#include "ssd1306.h"

typedef enum { CELL_DIGIT, CELL_COLON } cell_kind_t;

typedef struct {
    cell_kind_t kind;
    int16_t x;          /* left edge inside the clock row     */
    uint16_t w;         /* cell width                           */
    uint16_t h;         /* cell height (7 for the seconds)    */
    int16_t y_off;      /* offset from clock row top          */
    bool fine;          /* true -> draw with the normal font    */
    bool primed;        /* false until the first real glyph arrives */
    char cur;
    char prev;
    uint32_t changed_at;
} cell_t;

typedef struct {
    int16_t date_y;
    int16_t sensor_y;   /* 0 -> no sensor row at all            */
    int16_t clock_y;
    uint16_t clock_h;
    seg7_geom_t digit;
    uint16_t colon_w;
} layout_t;

static layout_t s_layout;
static cell_t s_cells[8];

static const char DAY_NAMES[7][4] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
};

/* ------------------------------------------------------------------ */
/* string helpers (no snprintf needed for these fixed shapes)         */
/* ------------------------------------------------------------------ */

static void put2(char *dst, uint8_t v)
{
    dst[0] = (char)('0' + (v / 10U));
    dst[1] = (char)('0' + (v % 10U));
}

static void build_time(char dst[9], const ds1307_time_t *t)
{
    put2(&dst[0], t->hour);
    dst[2] = ':';
    put2(&dst[3], t->minute);
    dst[5] = ':';
    put2(&dst[6], t->second);
    dst[8] = '\0';
}

static void build_date(char dst[15], const ds1307_time_t *t)
{
    uint8_t dow = (t->day >= 1U && t->day <= 7U) ? (uint8_t)(t->day - 1U)
                                                   : 0U;

    memcpy(dst, DAY_NAMES[dow], 3U);
    dst[3] = ' ';
    put2(&dst[4], t->date);
    dst[6] = '.';
    put2(&dst[7], t->month);
    dst[9] = '.';
    dst[10] = '2';
    dst[11] = '0';
    put2(&dst[12], t->year);
    dst[14] = '\0';
}

/* Append a decimal number, no leading zeros. @return the new end. */
static char *append_u(char *dst, uint32_t v)
{
    char tmp[10];
    uint8_t n = 0;

    do {
        tmp[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v > 0U);
    while (n > 0U) {
        *dst++ = tmp[--n];
    }
    return dst;
}

/* The BME280 line, fixed point -> text, no snprintf and no floats:
 *
 *   valid:   "T:23.4°C RH:45% P:1013hPa"
 *   missing: "T:--.-°C RH:--% P:----hPa"
 *
 * 25 characters x 5 px = 125 px fits the 128 px panel.  The widest
 * reading the sensor can legally produce (-40.0°C, 100 %RH, four-digit
 * pressure) is 27 characters = 135 px, so the caller falls back to
 * @p compact = true, which drops the two field separators. */
void face_build_sensor(char *dst, const bme280_data_t *env, bool valid,
                       bool compact)
{
    char *p = dst;

    *p++ = 'T';
    *p++ = ':';
    if (valid && env != NULL) {
        const int32_t t = env->temp_c_x100;
        const bool neg = t < 0;
        const uint32_t u = neg ? (uint32_t)(-t) : (uint32_t)t;
        const uint32_t t10 = (u + 5U) / 10U;    /* 0.1 degC units */

        if (neg) {
            *p++ = '-';
        }
        p = append_u(p, t10 / 10U);
        *p++ = '.';
        *p++ = (char)('0' + (t10 % 10U));
    } else {
        memcpy(p, "--.-", 4U);
        p += 4U;
    }
    *p++ = CH_DEGREE;
    *p++ = 'C';

    if (!compact) {
        *p++ = ' ';
    }
    memcpy(p, "RH:", 3U);
    p += 3U;
    if (valid && env != NULL) {
        p = append_u(p, (env->rh_x1024 + 512U) / 1024U);    /* whole %RH */
    } else {
        *p++ = '-';
        *p++ = '-';
    }
    *p++ = '%';

    if (!compact) {
        *p++ = ' ';
    }
    memcpy(p, "P:", 2U);
    p += 2U;
    if (valid && env != NULL) {
        /* Pa*100 -> whole hPa, rounded. */
        p = append_u(p, (env->press_pa_x100 + 5000U) / 10000U);
    } else {
        memcpy(p, "----", 4U);
        p += 4U;
    }
    memcpy(p, "hPa", 3U);
    p += 3U;
    *p = '\0';
}

static void draw_sensor(const bme280_data_t *env, bool env_valid)
{
    char line[32];
    uint16_t w;

    if (s_layout.sensor_y <= 0) {
        return;
    }

    face_build_sensor(line, env, env_valid, false);
    if (gfx_text_width(line, FONT_MINI) + 2U > OLED_WIDTH) {
        face_build_sensor(line, env, env_valid, true);
    }

    w = gfx_text_width(line, FONT_MINI);
    gfx_fill_rect(1, s_layout.sensor_y, (uint16_t)(OLED_WIDTH - 2U),
                  gfx_font_height(FONT_MINI), false);
    gfx_text((int16_t)((OLED_WIDTH - w) / 2U), s_layout.sensor_y, line,
             FONT_MINI);
}

/* ------------------------------------------------------------------ */
/* layout + cells                                                      */
/* ------------------------------------------------------------------ */

static void setup_layout(void)
{
    s_layout.sensor_y = 0;

    if (OLED_HEIGHT == 32U) {
#if UI_SHOW_SENSOR
        /* Rows 1..7 date, 9..14 sensor, 15 gutter, 16..30 digits,
         * row 0 and 31 are the bezel. The digits shrink 20 -> 15 px to
         * make room for the 6-row sensor line. */
        s_layout.date_y = 1;
        s_layout.sensor_y = 9;
        s_layout.clock_y = 16;
        s_layout.clock_h = 15;
        s_layout.digit = (seg7_geom_t){ 10, 15, 2 };
        s_layout.colon_w = 6;
#else
        /* Rows 2..7 date, rows 8..9 gutter, rows 10..29 digits,
         * row 30 blank, row 31 bezel. */
        s_layout.date_y = 2;
        s_layout.clock_y = 10;
        s_layout.clock_h = 20;
        s_layout.digit = (seg7_geom_t){ 14, 20, 3 };
        s_layout.colon_w = 6;
#endif
    } else {
        s_layout.date_y = 3;
        s_layout.clock_y = 22;
        s_layout.clock_h = 30;
        s_layout.digit = (seg7_geom_t){ 20, 30, 4 };
        s_layout.colon_w = 12;
#if UI_SHOW_SENSOR
        s_layout.sensor_y = 54;   /* 54..59, bezel stays at 63 */
#endif
    }
}

/* Build the "HH:MM:SS" cells centred on the screen. */
static void setup_cells(void)
{
    static const char pattern[9] = "HH:MM:SS";
    const uint16_t gap = 2U;  /* blank columns between cells */
    uint16_t total = 0U;

    for (uint8_t i = 0U; i < 8U; i++) {
        bool digit = (pattern[i] == 'H' || pattern[i] == 'M'
                      || pattern[i] == 'S');

        s_cells[i].kind = digit ? CELL_DIGIT : CELL_COLON;
        s_cells[i].fine = (pattern[i] == 'S');
        s_cells[i].w = digit ? (s_cells[i].fine ? (uint16_t)7U
                                                  : (uint16_t)s_layout.digit.w)
                             : (uint16_t)s_layout.colon_w;
        s_cells[i].h = s_cells[i].fine ? 7U : s_layout.clock_h;
        s_cells[i].y_off = s_cells[i].fine
                               ? (int16_t)(s_layout.clock_h - 7U)
                               : 0;
        s_cells[i].cur = pattern[i];
        s_cells[i].prev = pattern[i];
        s_cells[i].primed = false;
        s_cells[i].changed_at = 0U;
        total = (uint16_t)(total + s_cells[i].w);
    }
    /* 7 gaps between the 8 cells. */
    total = (uint16_t)(total + 7U * gap);

    int16_t x = (int16_t)((OLED_WIDTH - total) / 2U);
    for (uint8_t i = 0U; i < 8U; i++) {
        s_cells[i].x = x;
        x = (int16_t)(x + s_cells[i].w + gap);
    }
}

void face_init(void)
{
    setup_layout();
    setup_cells();
    ssd1306_clear();
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */

/* Which animation ideas are switched on, chosen by ANIM_RECIPE in
 * app_config.h (0 = static face, 2 = roll + colon pulse). */
#if (ANIM_RECIPE >= 1)
#define ANIM_ROLL 1
#else
#define ANIM_ROLL 0
#endif
#if (ANIM_RECIPE >= 2)
#define ANIM_COLON 1
#else
#define ANIM_COLON 0
#endif

/* Draw one clock cell, rolling from @c prev to @c cur while the
 * animation is still running. */
static void draw_cell(const cell_t *cell, uint32_t now_ms)
{
    const int16_t y = (int16_t)(s_layout.clock_y + cell->y_off);
    uint32_t elapsed = now_ms - cell->changed_at;
    gfx_clip_t clip = { cell->x, y, cell->w, cell->h };

    /* Blank the cell first, otherwise ghosts of the old glyph stay. */
    gfx_fill_rect(cell->x, y, cell->w, cell->h, false);

    if (cell->kind == CELL_COLON) {
        /* Dots blink off in the second half of the animation window. */
        if (!ANIM_COLON || elapsed < ANIM_DURATION_MS / 2U) {
            seg7_draw_colon(&clip, cell->x, y, (uint8_t)cell->w,
                            (uint8_t)cell->h, s_layout.digit.thick, true);
        }
        return;
    }

    if (ANIM_ROLL && elapsed < ANIM_DURATION_MS) {
        /* Slot-machine roll: outgoing glyph leaves upwards, incoming
         * glyph comes in from below, both clipped to the cell. */
        uint16_t off = (uint16_t)((uint32_t)elapsed * cell->h /
                                  ANIM_DURATION_MS);

        if (cell->fine) {
            gfx_char_clip(&clip, cell->x, (int16_t)(y - off), cell->prev,
                          FONT_NORMAL, true);
            gfx_char_clip(&clip, cell->x, (int16_t)(y + cell->h - off),
                          cell->cur, FONT_NORMAL, true);
        } else {
            seg7_draw_digit(&clip, cell->x, (int16_t)(y - off),
                            seg7_mask_for(cell->prev), &s_layout.digit,
                            true);
            seg7_draw_digit(&clip, cell->x, (int16_t)(y + cell->h - off),
                            seg7_mask_for(cell->cur), &s_layout.digit,
                            true);
        }
        return;
    }

    if (cell->fine) {
        gfx_char_clip(&clip, cell->x, y, cell->cur, FONT_NORMAL, true);
    } else {
        seg7_draw_digit(&clip, cell->x, y, seg7_mask_for(cell->cur),
                        &s_layout.digit, true);
    }
}

static void update_cells(const char time_str[9], uint32_t now_ms)
{
    bool changed = false;

    for (uint8_t i = 0U; i < 8U; i++) {
        cell_t *cell = &s_cells[i];

        if (!cell->primed) {
            /* First frame after face_init(): the cell still holds a
             * pattern letter ('H', 'M', 'S'), not a real glyph, so
             * there is nothing to roll in - snap to the time instead. */
            cell->primed = true;
            cell->prev = time_str[i];
            cell->cur = time_str[i];
            cell->changed_at = now_ms - ANIM_DURATION_MS;
            continue;
        }

        if (cell->cur != time_str[i]) {
            cell->prev = cell->cur;
            cell->cur = time_str[i];
            cell->changed_at = now_ms;
            changed = true;
        }
    }

    if (changed) {
        /* The colon glyph never changes, so it cannot time its own
         * blink: it rides along with the digits that flank it. */
        s_cells[2].changed_at = now_ms;
        s_cells[5].changed_at = now_ms;
    }
}

void face_draw(const ds1307_time_t *t, bool time_valid, uint32_t now_ms,
               const bme280_data_t *env, bool env_valid)
{
    char time_str[9];
    char date_str[15];

    if (time_valid) {
        build_time(time_str, t);
        build_date(date_str, t);
    } else {
        memcpy(time_str, "--:--:--", 9U);
        memcpy(date_str, "SET RTC", 8U);
    }

    update_cells(time_str, now_ms);

    /* Header row: weekday + date. */
    gfx_fill_rect(2, s_layout.date_y, (uint16_t)(OLED_WIDTH - 4), 7, false);
    gfx_text(3, s_layout.date_y, date_str, FONT_NORMAL);

    /* Sensor row: temperature, relative humidity, pressure. */
    draw_sensor(env, env_valid);

    /* Clock row. */
    for (uint8_t i = 0U; i < 8U; i++) {
        draw_cell(&s_cells[i], now_ms);
    }

#if UI_DRAW_BEZEL
    gfx_rect(0, 0, OLED_WIDTH, OLED_HEIGHT, true);
#endif
}
