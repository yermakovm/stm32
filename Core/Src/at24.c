/**
 * @file    at24.c
 * @see     at24.h
 *
 * The AT24C32 is addressed with a 16-bit big-endian word address, which
 * is what i2c_bus_read_reg16()/i2c_bus_write_reg16() send, so a plain
 * byte address can be passed straight through.
 *
 * Writes must not cross a 32-byte page: the device wraps inside the
 * page and the remainder of the transfer is lost, so every transfer is
 * cut at the page boundary and followed by a poll for the end of the
 * ~5 ms write cycle.
 */

#include "at24.h"

#include "app_config.h"
#include "board.h"
#include "i2c_bus.h"
#include "ilog.h"

/* 5 ms typical/max write cycle; polling every millisecond with 10
 * attempts leaves headroom for a slow part without hanging forever. */
#define AT24_WRITE_CYCLE_POLLS 10U

bool at24_present(void)
{
    return i2c_bus_present(EEPROM_I2C_ADDR7);
}

bool at24_read(uint16_t addr, uint8_t *data, uint16_t len)
{
    if (len == 0U) {
        return true;
    }
    if ((uint32_t)addr + (uint32_t)len > (uint32_t)AT24C32_SIZE) {
        LOGE("eeprom: read 0x%04x+%u past the end of the device",
             addr, (unsigned)len);
        return false;
    }

    return i2c_bus_read_reg16(EEPROM_I2C_ADDR7, addr, data, len);
}

/* Wait until the chip ACKs again, i.e. the internal write cycle is
 * over. The device does not answer while it is programming. */
static bool wait_write_cycle(void)
{
    for (uint32_t polls = 0U; polls < AT24_WRITE_CYCLE_POLLS; polls++) {
        if (i2c_bus_present(EEPROM_I2C_ADDR7)) {
            return true;
        }
        HAL_Delay(1U);
    }
    return false;
}

bool at24_write(uint16_t addr, const uint8_t *data, uint16_t len)
{
    uint16_t done = 0U;

    if (len == 0U) {
        return true;
    }
    if ((uint32_t)addr + (uint32_t)len > (uint32_t)AT24C32_SIZE) {
        LOGE("eeprom: write 0x%04x+%u past the end of the device",
             addr, (unsigned)len);
        return false;
    }

    while (done < len) {
        uint16_t page_off = (uint16_t)((uint32_t)(addr + done) %
                                        (uint32_t)AT24C32_PAGE_SIZE);
        uint16_t chunk = (uint16_t)(AT24C32_PAGE_SIZE - page_off);
        uint16_t left = (uint16_t)(len - done);

        if (chunk > left) {
            chunk = left;
        }

        if (!i2c_bus_write_reg16(EEPROM_I2C_ADDR7,
                                 (uint16_t)(addr + done),
                                 &data[done], chunk)) {
            LOGE("eeprom: write to 0x%04x failed", (unsigned)(addr + done));
            return false;
        }
        if (!wait_write_cycle()) {
            LOGE("eeprom: write cycle at 0x%04x never finished",
                 (unsigned)(addr + done));
            return false;
        }

        done = (uint16_t)(done + chunk);
    }

    return true;
}