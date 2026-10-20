/**
 * @file    ilog.h
 * @brief   Tiny blocking log channel on USART1 (no stdio/newlib
 *          printf, so no float/formatting bloat in a 512 KB flash).
 *
 * Select the noise level with LOG_LEVEL in app_config.h:
 *   0 = LOGE only, 1 = + LOGI, 2 = + LOGT.
 */

#ifndef ILOG_H
#define ILOG_H

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

/** Configure USART1 (PA9/PA10) and emit nothing yet. */
void log_init(void);

/** Blocking raw write of a NUL-terminated string: sent exactly as
 *  given, no line terminator added. Does nothing before log_init(). */
void log_str(const char *s);

/** Blocking formatted write. One call = one line: "\r\n" is appended
 *  unless the text already ends with a newline, so no log line can run
 *  into the next one. Lines longer than 126 characters are cut. Does
 *  nothing before log_init().
 *  Supports %s %c %d %u %x %X plus %ld/%lu/%lx and width/zero-pad like
 *  %02u, %08lx. */
void log_printf(const char *fmt, ...);

/** Raw register/byte dump, 16 bytes per line: "label+off: aa bb ...".
 *  For "is anything actually on the bus?" debugging, where the values
 *  matter more than the meaning. */
void log_hexdump(const char *label, const uint8_t *data, uint16_t len);

#if LOG_LEVEL >= 0
#define LOGE(...)            log_printf("[E] " __VA_ARGS__)
#else
#define LOGE(...)            ((void)0)
#endif
#if LOG_LEVEL >= 1
#define LOGI(...)            log_printf("[I] " __VA_ARGS__)
#else
#define LOGI(...)            ((void)0)
#endif
#if LOG_LEVEL >= 2
#define LOGT(...)            log_printf("[T] " __VA_ARGS__)
#else
#define LOGT(...)            ((void)0)
#endif

#endif /* ILOG_H */
