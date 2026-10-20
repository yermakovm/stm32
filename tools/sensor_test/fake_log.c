/**
 * @file    tools/sensor_test/fake_log.c
 * @brief   Host-side stand-in for Core/Src/ilog.c: the same log_printf /
 *          log_str API, printing to stdout instead of USART1.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ilog.h"

void log_init(void)
{
}

void log_str(const char *s)
{
    (void)printf("  > %s", s);
}

void log_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    size_t n;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf) - 2U, fmt, ap);
    va_end(ap);

    /* Trace lines would bury the test output; the firmware gates them
     * with LOG_LEVEL instead. */
    if (buf[0] == '[' && buf[1] == 'T') {
        return;
    }

    /* Same rule as Core/Src/ilog.c: one call = one line, so the host
     * log looks exactly like the one on USART1. */
    n = strlen(buf);
    if (n > 0U && buf[n - 1U] != '\n') {
        buf[n++] = '\r';
        buf[n++] = '\n';
        buf[n] = '\0';
    }

    (void)printf("  > %s", buf);
}

void log_hexdump(const char *label, const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if (data == NULL || len == 0U) {
        return;
    }
    (void)printf("  > %s+%02x:", label ? label : "", (unsigned)0);
    for (i = 0U; i < len; i++) {
        (void)printf(" %02x", (unsigned)data[i]);
    }
    (void)printf("\n");
}
