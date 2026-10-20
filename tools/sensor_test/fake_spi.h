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

/* What the driver wrote, for assertions. */
extern uint8_t fake_reset_cmd;
extern uint8_t fake_ctrl_hum;
extern uint8_t fake_ctrl_meas;
extern uint8_t fake_config;
extern int fake_spi_bad_cmds;   /* commands with the wrong read/write bit */

/* spi_bus.h API */
bool spi_bus_init(void);
bool spi_bus_read_reg(uint8_t reg, uint8_t *data, uint16_t len);
bool spi_bus_write_reg(uint8_t reg, uint8_t value);
bool spi_bus_is_ready(void);
uint32_t spi_bus_error_count(void);

#endif /* FAKE_SPI_H */
