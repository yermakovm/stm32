/**
 * @file    tools/sensor_test/spi_bus.h
 * @brief   Host stand-in for Core/Inc/spi_bus.h: same API, no HAL types.
 *          Picked up by putting -Itools/sensor_test before -ICore/Inc.
 *          If Core/Inc/spi_bus.h changes, this file and fake_spi.c change
 *          with it - the test would not link otherwise.
 */

#ifndef SPI_BUS_H
#define SPI_BUS_H

#include <stdbool.h>
#include <stdint.h>

bool spi_bus_init(void);
void spi_bus_cs(bool asserted);
bool spi_bus_transfer(const uint8_t *tx, uint8_t *rx, uint16_t len);
bool spi_bus_is_ready(void);
uint32_t spi_bus_error_count(void);

#endif /* SPI_BUS_H */
