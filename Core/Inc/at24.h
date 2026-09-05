/**
 * @file    at24.h
 * @brief   AT24C32 EEPROM driver (I2C 0x50, 32 KiB, 32-byte pages).
 *          Used to keep the last known time across battery swaps.
 */

#ifndef AT24_H
#define AT24_H

#include <stdbool.h>
#include <stdint.h>

#define AT24C32_SIZE      32768U
#define AT24C32_PAGE_SIZE 32U

/** True if the EEPROM acknowledges. */
bool at24_present(void);

/** Sequential read starting at a byte address (0 .. AT24C32_SIZE-1),
 *  sent to the device as the 16-bit address it expects. */
bool at24_read(uint16_t addr, uint8_t *data, uint16_t len);

/** Write across page boundaries transparently, waiting for each
 *  ~5 ms write cycle. @return false if the chip never came back. */
bool at24_write(uint16_t addr, const uint8_t *data, uint16_t len);

#endif /* AT24_H */
