/**
 * @file    ssd1306.c
 * @see     ssd1306.h
 *
 * Page addressing mode is used because it needs no "column/page start"
 * bookkeeping across frames: for each dirty page we re-set the address
 * counters and blast one 128-byte run of GDDRAM.
 */

#include "ssd1306.h"

#include <string.h>

#include "board.h"
#include "i2c_bus.h"
#include "ilog.h"

/* Control byte + payload, big enough for one page. */
#define SSD1306_CTL_CMD  0x00U
#define SSD1306_CTL_DATA 0x40U
#define SSD1306_SCRATCH  (1U + OLED_WIDTH)

static uint8_t s_fb[SSD1306_FB_SIZE];
static bool s_dirty[SSD1306_PAGES];
static bool s_alive;
static uint8_t s_scratch[SSD1306_SCRATCH];

/* Power-on sequence. Chunk A enables the charge pump, then we wait for
 * it to stabilise before the panel is switched on. */
static const uint8_t s_init_a[] = {
    0xAE,                        /* display off                            */
    0xD5, 0xF0,                  /* clock divide / oscillator freq        */
    0xA8, OLED_MULTIPLEX,        /* multiplex ratio (32 or 64)            */
    0xD3, 0x00,                  /* display offset 0                      */
    0x40,                        /* start line 0                          */
    0x8D, 0x14,                  /* charge pump on                        */
};

static const uint8_t s_init_b[] = {
    0x20, 0x02,                  /* page addressing mode                  */
    OLED_SEGMENT_REMAP,          /* column remap                          */
    OLED_SCAN_DIR,               /* COM scan direction                    */
    0xA4,                        /* resume from RAM                       */
    0xA6,                        /* normal, not inverted                  */
    0xDA, OLED_COM_PINS,         /* COM pin config                        */
    0x81, OLED_CONTRAST,         /* contrast                              */
    0xAF,                        /* display on                            */
};

/* Everything to the panel is [control byte][payload]: the control byte
 * goes in scratch[0] and the command/data bytes start at scratch[1],
 * otherwise the first command byte would be overwritten. */
static bool send_cmds(const uint8_t *cmds, uint16_t len)
{
    s_scratch[0] = SSD1306_CTL_CMD;
    memcpy(&s_scratch[1], cmds, len);
    return i2c_bus_write(SSD1306_I2C_ADDR7, s_scratch, (uint16_t)(len + 1U));
}

static bool send_cmd(uint8_t c)
{
    return send_cmds(&c, 1U);
}

bool ssd1306_init(void)
{
    if (!i2c_bus_present(SSD1306_I2C_ADDR7)) {
        LOGE("ssd1306: no ACK at 0x%02x", SSD1306_I2C_ADDR7);
        s_alive = false;
        return false;
    }

    if (!send_cmds(s_init_a, sizeof(s_init_a))) {
        s_alive = false;
        return false;
    }
    HAL_Delay(100);                 /* charge pump needs time to settle */

    if (!send_cmds(s_init_b, sizeof(s_init_b))) {
        s_alive = false;
        return false;
    }

    ssd1306_clear();
    s_alive = true;
    LOGI("ssd1306: %ux%u ready", (unsigned)OLED_WIDTH,
         (unsigned)OLED_HEIGHT);
    return true;
}

uint8_t *ssd1306_fb(void)
{
    return s_fb;
}

int ssd1306_page_of(uint16_t y)
{
    if (y >= OLED_HEIGHT) {
        return -1;
    }
    return (int)(y / 8U);
}

void ssd1306_mark_dirty(uint16_t y)
{
    int page = ssd1306_page_of(y);

    if (page >= 0) {
        s_dirty[page] = true;
    }
}

void ssd1306_pixel(uint16_t x, uint16_t y, bool on)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) {
        return;
    }

    uint8_t *cell = &s_fb[(y / 8U) * SSD1306_STRIDE + x];
    uint8_t bit = (uint8_t)(1U << (y % 8U));

    if (on) {
        *cell |= bit;
    } else {
        *cell &= (uint8_t)~bit;
    }
    s_dirty[y / 8U] = true;
}

void ssd1306_fill(uint8_t pattern)
{
    memset(s_fb, pattern, sizeof(s_fb));
    for (uint32_t p = 0U; p < SSD1306_PAGES; p++) {
        s_dirty[p] = true;
    }
}

void ssd1306_clear(void)
{
    ssd1306_fill(0x00);
}

bool ssd1306_flush(void)
{
    if (!s_alive) {
        return false;   /* nothing to push until init() succeeds again */
    }

    bool ok = true;

    for (uint32_t page = 0U; page < SSD1306_PAGES; page++) {
        if (!s_dirty[page]) {
            continue;
        }

        const uint8_t addr[3] = {
            (uint8_t)(0xB0U + page),   /* page start                    */
            0x00U,                     /* lower column start = 0        */
            0x10U,                     /* higher column start = 0       */
        };
        if (!send_cmds(addr, sizeof(addr))) {
            ok = false;
            break;
        }

        uint8_t *row = &s_fb[page * SSD1306_STRIDE];
        s_scratch[0] = SSD1306_CTL_DATA;
        memcpy(&s_scratch[1], row, SSD1306_STRIDE);
        if (!i2c_bus_write(SSD1306_I2C_ADDR7, s_scratch,
                           (uint16_t)(SSD1306_STRIDE + 1U))) {
            ok = false;
            break;
        }

        /* Keep the flag set on failure so the next flush retries. */
        s_dirty[page] = false;
    }

    if (!ok) {
        /* Do not re-run the init sequence from here: it would repeat
         * the 100 ms charge-pump wait on every frame. The caller
         * retries ssd1306_init() at a saner cadence. */
        LOGE("ssd1306: flush incomplete, panel assumed dead");
        s_alive = false;
    }
    return ok;
}

bool ssd1306_set_invert(bool inverted)
{
    return send_cmd(inverted ? 0xA7U : 0xA6U);
}

bool ssd1306_set_power(bool on)
{
    return send_cmd(on ? 0xAFU : 0xAEU);
}

bool ssd1306_set_contrast(uint8_t contrast)
{
    const uint8_t seq[2] = {0x81U, contrast};

    return send_cmds(seq, sizeof(seq));
}

bool ssd1306_is_alive(void)
{
    return s_alive;
}
