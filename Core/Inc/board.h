/**
 * @file    board.h
 * @brief   Pin map + minimal board bring-up for
 *          STM32F401CEU6 + SSD1306 (I2C) + DS1307/AT24C32 (I2C)
 *          + USART1 to a USB-TTL adapter for logging.
 *
 * Wiring (everything on one 3.3 V I2C bus):
 *
 *   USB-TTL adapter              STM32F401CEU6
 *   -----------------            -----------------
 *   TX  ---------------------->  PA10 (USART1_RX, AF7)
 *   RX  <---------------------   PA9  (USART1_TX, AF7)
 *   GND <--------------------->  GND
 *
 *   I2C1 (SSD1306 + DS1307/AT24C32)
 *   -------------------------------
 *   PB8  (I2C1_SCL, AF4) ---->  OLED SCL, RTC SCL
 *   PB9  (I2C1_SDA, AF4) ---->  OLED SDA, RTC SDA
 *   3V3  -------------------->  OLED VCC, RTC VCC   (5 V modules:
 *   GND  -------------------->  OLED GND, RTC GND    see HARDWARE.md)
 *
 *   Slave addresses: SSD1306 0x3C, DS1307 0x68, AT24C32 0x50.
 *   No button, no ESP32 link, no status LED: only the clock module and
 *   the display are driven, as requested.
 *
 * Alternative pin sets (only if you'd rather not share one bus):
 *   I2C1 for the OLED on PB6 (SCL)/PB7 (SDA), I2C2 for the RTC on
 *   PB10 (SCL)/PB3 (SDA) - note PB3 is also JTDO/SWO, so trace output
 *   is lost. PB8/PB9 on a single I2C1 bus is the recommended wiring.
 */

#ifndef BOARD_H
#define BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "stm32f4xx_hal.h"

/* Peripheral instances, kept here so every module refers to the same
 * hardware without hard-coding it. */
#define BOARD_LOG_UART     USART1   /* PA9 TX / PA10 RX, 115200 8N1 */
#define BOARD_I2C          I2C1     /* PB8 SCL / PB9 SDA, 400 kHz   */

#define BOARD_LOG_UART_AF  GPIO_AF7_USART1
#define BOARD_LOG_UART_PINS (GPIO_PIN_9 | GPIO_PIN_10)
#define BOARD_LOG_UART_GPIO GPIOA

#define BOARD_I2C_AF       GPIO_AF4_I2C1
#define BOARD_I2C_SCL_PIN  GPIO_PIN_8
#define BOARD_I2C_SDA_PIN  GPIO_PIN_9
#define BOARD_I2C_GPIO     GPIOB

/** HAL_Init + NVIC + 84 MHz sysclock (HSI -> PLL). */
void board_init(void);

/** Milliseconds since reset (driven by SysTick through HAL). */
uint32_t board_ms(void);

/** Halt forever, after trying to scream over the log UART. */
void board_fatal(const char *where);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
