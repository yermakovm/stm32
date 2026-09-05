/**
 * @file    ssd1306.h
 * @brief   SSD1306 128x32 / 128x64 OLED driver (I2C, page addressing)
 *          with a RAM framebuffer and dirty-page flushing.
 *
 * The framebuffer is laid out exactly like the controller's GDDRAM
 * (page-major: fb[page][x]), so a page can be pushed with one I2C
 * write and unchanged pages cost nothing.
 */

#ifndef SSD1306_H
#define SSD1306_H

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

#define SSD1306_PAGES       (OLED_HEIGHT / 8U)
#define SSD1306_FB_SIZE     (OLED_WIDTH * OLED_HEIGHT / 8U)
#define SSD1306_STRIDE      OLED_WIDTH   /* bytes per page */

/** Power up the panel. @return false if it did not answer on I2C. */
bool ssd1306_init(void);

/** Raw framebuffer (SSD1306_STRIDE bytes per page). */
uint8_t *ssd1306_fb(void);

/** Page index containing row @p y, or -1 when out of range. */
int ssd1306_page_of(uint16_t y);

/** Set/clear one pixel in the framebuffer and mark its page dirty.
 *  Out-of-range coordinates are ignored. */
void ssd1306_pixel(uint16_t x, uint16_t y, bool on);

/** Mark the page that contains @p y as needing a refresh. */
void ssd1306_mark_dirty(uint16_t y);

/** Paint the whole framebuffer with 0x00 / 0xFF / a dither pattern. */
void ssd1306_fill(uint8_t pattern);

/** Clear the framebuffer and mark every page dirty. */
void ssd1306_clear(void);

/** Push only the pages that changed since the last flush.
 *  @return false on I2C failure (next flush retries the same pages). */
bool ssd1306_flush(void);

/** Invert the panel (useful as a "no time yet" alert). */
bool ssd1306_set_invert(bool inverted);

/** Panel on / off (0x00..0x8F contrast via ssd1306_set_contrast). */
bool ssd1306_set_power(bool on);
bool ssd1306_set_contrast(uint8_t contrast);

/** True once the panel answered the init sequence. */
bool ssd1306_is_alive(void);

#endif /* SSD1306_H */
