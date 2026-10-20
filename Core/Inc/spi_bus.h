/**
 * @file    spi_bus.h
 * @brief   Blocking SPI2 master with a manual chip select.
 *
 * SPI2 on the Black Pill:
 *   PB13 = SPI2_SCK
 *   PB14 = SPI2_MISO
 *   PB15 = SPI2_MOSI
 *   PB12 = chip select (plain GPIO, active low)
 *
 * SPI1 (PA4/PA5/PA6/PA7) is deliberately left alone: on this board those
 * four pins are routed to the SPI flash footprint on the underside.
 * PB12..PB15 are 5 V tolerant and unused by the clock.
 *
 * The wrapper stays protocol-agnostic - assert CS, shift bytes, release
 * CS - and each device driver builds its own command framing.
 */

#ifndef SPI_BUS_H
#define SPI_BUS_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"

#define SPI_BUS_INSTANCE SPI2
#define SPI_BUS_CS_PORT  GPIOB
#define SPI_BUS_CS_PIN   GPIO_PIN_12

/* Init SPI2 (mode 0) and the chip-select pin. CS idles high. */
bool spi_bus_init(void);

/* Select the peripheral. Call before a grouped transfer. */
void spi_bus_cs(bool asserted);

/* One full-duplex transfer of len bytes. tx or rx may be NULL. */
bool spi_bus_transfer(const uint8_t *tx, uint8_t *rx, uint16_t len);

/* Bus-level wait (1 ms granularity) for drivers that must respect a
 * chip-internal timing instead of just polling as fast as possible. */
void spi_bus_wait_ms(uint32_t ms);

/* Log the pad levels, SPI2 CR1/SR and the error state: the difference
 * between "sensor not wired", "peripheral broken" and "wiring fine,
 * chip unhappy". */
void spi_bus_diag(const char *why);

bool spi_bus_is_ready(void);
uint32_t spi_bus_error_count(void);

#endif /* SPI_BUS_H */
