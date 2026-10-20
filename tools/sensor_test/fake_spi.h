/**
 * @file    fake_spi.h
 * @brief   Host stand-in for spi_bus.h: an emulated BME280 register file
 *          behind an SPI slave that follows the datasheet framing.
 */

#ifndef FAKE_SPI_H
#define FAKE_SPI_H

#include <stdbool.h>
#include <stdint.h>

void fake_spi_init(void);
void fake_spi_reset(void);

/* Preload registers the way the real chip's NVM would. */
void fake_spi_poke(uint8_t reg, const uint8_t *data, uint16_t len);

/* Host-side knobs for the tests. */
void fake_spi_set_present(bool present);
void fake_spi_set_chip_id(uint8_t id);
void fake_spi_set_measuring_reads(int n);   /* how long the busy bit stays */
void fake_spi_set_im_update_reads(int n);   /* how long the NVM copy takes */

/* What the driver wrote, for assertions. */
extern uint8_t fake_reset_cmd;
extern uint8_t fake_ctrl_hum;
extern uint8_t fake_ctrl_meas;
extern uint8_t fake_config;
extern int fake_spi_bad_cmds;   /* commands with the wrong read/write bit */
extern int fake_spi_waits;      /* spi_bus_wait_ms() calls by the driver */

/* spi_bus.h API (see tools/sensor_test/spi_bus.h, which mirrors
 * Core/Inc/spi_bus.h). */

#endif /* FAKE_SPI_H */
