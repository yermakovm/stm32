/**
 * @file    tools/sensor_test/fake_log.c
 * @brief   Host-side stand-in for Core/Src/ilog.c: the same log_printf /
 *          log_str API, printing to stdout instead of USART1.
 */

#include <stdarg.h>
#include <stdio.h>

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

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* Trace lines would bury the test output; the firmware gates them
     * with LOG_LEVEL instead. */
    if (buf[0] == '[' && buf[1] == 'T') {
        return;
    }
    (void)printf("  > %s\n", buf);
}
