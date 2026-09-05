/**
 * @file    ilog.c
 * @see     ilog.h - USART1 @115200 8N1 to a USB-TTL adapter.
 *
 * Logging is blocking on purpose: this firmware has nothing else to
 * do between screen frames, and a serial trace that can never be
 * dropped by a half-finished DMA transfer is worth 200 us per line.
 */

#include "ilog.h"

#include <stdarg.h>
#include <string.h>

#include "board.h"

static UART_HandleTypeDef s_uart;

/* ------------------------------------------------------------------ */
/* minimal printf                                                      */
/* ------------------------------------------------------------------ */

/* Digits into a scratch buffer, big-endian, returns length. */
static size_t emit_uint(char *out, uint32_t v, unsigned base,
                        unsigned min_width, bool zero_pad)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[12];
    size_t n = 0;

    if (base < 2U || base > 16U) {
        base = 10U;
    }
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (n < sizeof(tmp));

    size_t len = 0;
    for (unsigned pad = n; pad < min_width; pad++) {
        out[len++] = zero_pad ? '0' : ' ';
    }
    while (n > 0U) {
        out[len++] = tmp[--n];
    }
    return len;
}

/* strlen with a hard cap, so a bad pointer can never run past the
 * buffer. (Avoids strnlen(): not guaranteed in the newlib headers.) */
static size_t strnlen_lite(const char *s, size_t cap)
{
    size_t n = 0;

    while (n < cap && s[n] != '\0') {
        n++;
    }
    return n;
}

static size_t vsnprintf_lite(char *dst, size_t cap, const char *fmt,
                             va_list ap)
{
    size_t out = 0;
    char scratch[16];

    while (*fmt != '\0' && out + 1U < cap) {
        if (*fmt != '%') {
            dst[out++] = *fmt++;
            continue;
        }
        fmt++;

        bool zero_pad = false;
        unsigned width = 0;
        if (*fmt == '0') {
            zero_pad = true;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10U + (unsigned)(*fmt - '0');
            fmt++;
        }
        bool is_long = false;
        if (*fmt == 'l') {
            is_long = true;
            fmt++;
        }

        size_t n = 0;
        const char *s = scratch;
        switch (*fmt) {
        case 's': {
            s = va_arg(ap, const char *);
            if (s == NULL) {
                s = "(null)";
            }
            n = strnlen_lite(s, cap - out - 1U);
            break;
        }
        case 'c':
            scratch[0] = (char)va_arg(ap, int);
            n = 1U;
            break;
        case 'd': {
            int32_t v = is_long ? va_arg(ap, int32_t)
                                : (int32_t)va_arg(ap, int);
            if (v < 0) {
                scratch[0] = '-';
                n = 1U + emit_uint(scratch + 1, (uint32_t)(-v), 10U,
                                   width, zero_pad);
            } else {
                n = emit_uint(scratch, (uint32_t)v, 10U, width, zero_pad);
            }
            break;
        }
        case 'u':
            n = emit_uint(scratch,
                          is_long ? va_arg(ap, uint32_t)
                                  : (uint32_t)va_arg(ap, unsigned),
                          10U, width, zero_pad);
            break;
        case 'x':
        case 'X':
            n = emit_uint(scratch,
                          is_long ? va_arg(ap, uint32_t)
                                  : (uint32_t)va_arg(ap, unsigned),
                          16U, width, zero_pad);
            break;
        case '%':
            scratch[0] = '%';
            n = 1U;
            break;
        default:
            scratch[0] = '?';
            n = 1U;
            break;
        }

        if (n > cap - out - 1U) {
            n = cap - out - 1U;
        }
        memcpy(dst + out, s, n);
        out += n;
        if (*fmt != '\0') {
            fmt++;
        }
    }

    dst[out] = '\0';
    return out;
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

void log_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA9 = TX, PA10 = RX, AF7. Pull-up on RX keeps the line quiet
     * when the USB-TTL adapter is unplugged. */
    gpio.Pin = BOARD_LOG_UART_PINS;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = BOARD_LOG_UART_AF;
    HAL_GPIO_Init(BOARD_LOG_UART_GPIO, &gpio);

    s_uart.Instance = BOARD_LOG_UART;
    s_uart.Init.BaudRate = LOG_BAUDRATE;
    s_uart.Init.WordLength = UART_WORDLENGTH_8B;
    s_uart.Init.StopBits = UART_STOPBITS_1;
    s_uart.Init.Parity = UART_PARITY_NONE;
    s_uart.Init.Mode = UART_MODE_TX_RX;
    s_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&s_uart) != HAL_OK) {
        board_fatal("HAL_UART_Init(USART1)");
    }

    /* Drop garbage left in the shift register by a hot plug-in. */
    (void)HAL_UART_AbortReceive(&s_uart);
}

void log_str(const char *s)
{
    size_t len = strnlen_lite(s, 128U);

    if (HAL_UART_Transmit(&s_uart, (uint8_t *)s, (uint16_t)len,
                         LOG_TX_TIMEOUT_MS) != HAL_OK) {
        /* Log is best effort: never block the clock over a dead
         * serial port. */
        (void)HAL_UART_AbortTransmit(&s_uart);
    }
}

void log_printf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;

    va_start(ap, fmt);
    size_t n = vsnprintf_lite(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0U) {
        (void)HAL_UART_Transmit(&s_uart, (uint8_t *)buf, (uint16_t)n,
                                LOG_TX_TIMEOUT_MS);
    }
}
