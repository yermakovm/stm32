/**
 * @file    ds1307.c
 * @see     ds1307.h
 *
 * Register map: 0x00 seconds (bit7 = clock halt), 0x01 minutes,
 * 0x02 hours (bit6 = 12/24 h), 0x03 day, 0x04 date, 0x05 month,
 * 0x06 year, 0x07 control. All values are BCD.
 */

#include "ds1307.h"

#include "app_config.h"
#include "i2c_bus.h"

#define RTC_REG_SECONDS  0x00U
#define RTC_REG_CONTROL  0x07U
#define RTC_CH_BIT       0x80U   /* clock halt / oscillator stop      */
#define RTC_12H_BIT      0x40U   /* hours register: 12 h mode         */
#define RTC_PM_BIT       0x20U   /* hours register: PM (12 h mode)    */

static uint8_t bcd_to_dec(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10U) + (bcd & 0x0FU));
}

static uint8_t dec_to_bcd(uint8_t dec)
{
    return (uint8_t)(((dec / 10U) << 4) | (dec % 10U));
}

/* Hours register in 12 h mode: bit5 = PM, bits3:0 = 1..12. Convert to
 * 24 h so every caller can treat hour as 0..23. */
static uint8_t h12_to_h24(uint8_t reg)
{
    uint8_t h12 = bcd_to_dec((uint8_t)(reg & 0x1FU));
    bool pm = (reg & RTC_PM_BIT) != 0U;

    if (h12 == 0U || h12 > 12U) {
        h12 = (uint8_t)(h12 % 12U);   /* junk value, keep it in range */
    }
    if (pm) {
        return (uint8_t)(h12 == 12U ? 12U : h12 + 12U);
    }
    return (uint8_t)(h12 == 12U ? 0U : h12);
}

bool ds1307_read(ds1307_time_t *t)
{
    uint8_t reg[7];

    if (!i2c_bus_read_reg8(RTC_I2C_ADDR7, RTC_REG_SECONDS, reg,
                           sizeof(reg))) {
        return false;
    }

    t->oscillator_halted = (reg[0] & RTC_CH_BIT) != 0U;
    t->second = bcd_to_dec((uint8_t)(reg[0] & 0x7FU));
    t->minute = bcd_to_dec(reg[1]);

    if ((reg[2] & RTC_12H_BIT) != 0U) {
        t->pm = (reg[2] & RTC_PM_BIT) != 0U;
        t->hour = h12_to_h24(reg[2]);
    } else {
        t->pm = false;
        t->hour = bcd_to_dec((uint8_t)(reg[2] & 0x3FU));
    }
    if (t->hour >= 24U) {
        t->hour = (uint8_t)(t->hour % 24U);
    }

    t->day = bcd_to_dec((uint8_t)(reg[3] & 0x07U));
    t->date = bcd_to_dec(reg[4]);
    t->month = bcd_to_dec((uint8_t)(reg[5] & 0x1FU));
    t->year = bcd_to_dec(reg[6]);
    return true;
}

bool ds1307_write(const ds1307_time_t *t)
{
    uint8_t reg[7];

    /* Bit 7 of register 0 is the clock-halt bit, not part of the
     * seconds: writing the BCD value straight through would stop the
     * oscillator every time the seconds are >= 32. Clear it so the
     * oscillator starts. */
    reg[0] = (uint8_t)(dec_to_bcd(t->second) & 0x7FU);
    reg[1] = dec_to_bcd(t->minute);
    reg[2] = dec_to_bcd(t->hour);          /* 24 h mode: bit6 stays 0 */
    reg[3] = (uint8_t)(t->day == 0U ? 1U : t->day);
    reg[4] = dec_to_bcd(t->date);
    reg[5] = dec_to_bcd(t->month);
    reg[6] = dec_to_bcd(t->year);

    return i2c_bus_write_reg8(RTC_I2C_ADDR7, RTC_REG_SECONDS, reg,
                              sizeof(reg));
}

bool ds1307_set_square_wave(bool enable)
{
    /* bit4 = SQWEN, bits1:0 = rate; 0x10 is 1 Hz, 0x00 turns it off. */
    const uint8_t ctrl = enable ? RTC_SQW_CONTROL_1HZ : RTC_SQW_CONTROL_OFF;

    return i2c_bus_write_reg8(RTC_I2C_ADDR7, RTC_REG_CONTROL, &ctrl, 1U);
}

bool ds1307_is_running(const ds1307_time_t *t)
{
    return !t->oscillator_halted;
}

uint32_t ds1307_seconds_of_day(const ds1307_time_t *t)
{
    return (uint32_t)t->hour * 3600U + (uint32_t)t->minute * 60U + t->second;
}
