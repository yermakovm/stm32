/**
 * @file    tools/face_preview/fake_display.c
 * @brief   Host-side stand-in for Core/Src/ssd1306.c so the face and
 *          the font can be rendered without a panel (see render.c).
 *
 * Same API, same framebuffer layout (fb[page][x], bit0 = top row), no
 * I2C: the pixels are dumped as ASCII by render.c.
 */

#include <stdio.h>
#include <string.h>

#include "ssd1306.h"

static uint8_t s_fb[SSD1306_FB_SIZE];
static bool s_dirty[SSD1306_PAGES];

bool ssd1306_init(void)
{
    ssd1306_clear();
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
    for (unsigned p = 0U; p < SSD1306_PAGES; p++) {
        s_dirty[p] = true;
    }
}

void ssd1306_clear(void)
{
    ssd1306_fill(0x00U);
}

bool ssd1306_flush(void)
{
    return true;
}

bool ssd1306_set_invert(bool inverted)
{
    (void)inverted;
    return true;
}

bool ssd1306_set_power(bool on)
{
    (void)on;
    return true;
}

bool ssd1306_set_contrast(uint8_t contrast)
{
    (void)contrast;
    return true;
}

bool ssd1306_is_alive(void)
{
    return true;
}