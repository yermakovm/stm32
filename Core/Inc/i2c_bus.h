/**
 * @file    i2c_bus.h
 * @brief   Thin wrapper over HAL_I2C for the single I2C1 bus that
 *          carries the SSD1306 (0x3C), the DS1307 (0x68) and the
 *          AT24C32 (0x50).
 *
 * All transfers are blocking with a timeout and never panic the
 * caller: they return false and bump an error counter, so the clock
 * keeps ticking even if a module is unplugged.
 */

#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stdbool.h>
#include <stdint.h>

/** Configure I2C1 (PB8 SCL / PB9 SDA) at I2C_BUS_CLK_SPEED.
 *  @return false if the peripheral refused to init. */
bool i2c_bus_init(void);

/** Raw transfer: [addr+W, data...] / [addr+R, data...]. */
bool i2c_bus_write(uint8_t addr7, const uint8_t *data, uint16_t len);
bool i2c_bus_read(uint8_t addr7, uint8_t *data, uint16_t len);

/** Register-style transfer with repeated start:
 *  write: [addr+W, reg, data...]  stop
 *  read:  [addr+W, reg] restart [addr+R, data...] stop
 *  This is exactly what the DS1307 (8-bit register pointer) expects
 *  and also what the AT24C32 expects with a 16-bit word address. */
bool i2c_bus_write_reg8(uint8_t addr7, uint8_t reg, const uint8_t *data,
                        uint16_t len);
bool i2c_bus_read_reg8(uint8_t addr7, uint8_t reg, uint8_t *data,
                       uint16_t len);
bool i2c_bus_write_reg16(uint8_t addr7, uint16_t reg, const uint8_t *data,
                         uint16_t len);
bool i2c_bus_read_reg16(uint8_t addr7, uint16_t reg, uint8_t *data,
                        uint16_t len);

/** Acknowledge poll: true if a chip answers at addr7. */
bool i2c_bus_present(uint8_t addr7);

/** Consecutive/total failed transfers, for the log + diagnostics. */
uint32_t i2c_bus_error_count(void);

/** Dump which addresses answer, called once at boot. */
void i2c_bus_scan(void);

#endif /* I2C_BUS_H */
