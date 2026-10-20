/**
 * @file    fake_spi.c
 * @brief   Emulated BME280 SPI slave (see fake_spi.h).
 *
 * Models the parts the driver depends on:
 *   - command framing: bit 7 = 1 read / 0 write, bits 6..0 = register
 *   - 0xE0 = 0xB6 soft reset, which sets "im_update" in 0xF3 while the
 *     NVM copy is running
 *   - 0xF4 with mode = forced: 0xF3.measuring stays set for a fixed
 *     number of status reads
 *   - a 256-byte register file for the calibration and data blocks
 */

#include "fake_spi.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "app_config.h"

#define REG_FILE_SIZE 256U
#define REG_STATUS    0xF3U
#define ST_IM_UPDATE  0x01U
#define ST_MEASURING  0x08U

typedef enum {
    PH_CMD,       /* expecting a command byte */
    PH_WRITE_DATA,/* expecting the value byte of a write */
    PH_READ       /* streaming the requested register block out */
} phase_t;

static uint8_t s_regs[REG_FILE_SIZE];
static uint8_t s_chip_id = 0x60U;
static bool s_present = true;
static bool s_cs;
static phase_t s_phase;
static uint8_t s_reg;
static int s_meas_reads;      /* status reads left with the busy bit set */
static int s_meas_target = 3;
static int s_im_reads;        /* status reads left with im_update set */
static int s_im_target = 1;   /* how long a soft reset's NVM copy takes */
static uint32_t s_errors;

uint8_t fake_reset_cmd;
uint8_t fake_ctrl_hum;
uint8_t fake_ctrl_meas;
uint8_t fake_config;
int fake_spi_bad_cmds;
int fake_spi_waits;

void fake_spi_reset(void)
{
    /* Back to the defaults, not just to the registers: a test that
     * modelled a missing chip or a BMP280 must not leak into the next
     * one. */
    s_present = true;
    s_chip_id = 0x60U;
    s_meas_target = 3;
    s_meas_reads = 0;
    s_im_reads = 0;
    s_im_target = 1;

    memset(s_regs, 0, sizeof(s_regs));
    s_regs[0xD0U] = s_chip_id;
    s_regs[0xF3U] = 0x00U;
    s_phase = PH_CMD;
    s_cs = false;
    s_errors = 0;
    fake_reset_cmd = 0;
    fake_ctrl_hum = 0;
    fake_ctrl_meas = 0;
    fake_config = 0;
    fake_spi_bad_cmds = 0;
    fake_spi_waits = 0;
    s_im_target = 1;
}

void fake_spi_init(void)
{
    fake_spi_reset();
}

void fake_spi_poke(uint8_t reg, const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0U; i < len; i++) {
        if ((uint32_t)reg + i < REG_FILE_SIZE) {
            s_regs[reg + i] = data[i];
        }
    }
}

void fake_spi_set_present(bool present)
{
    s_present = present;
}

void fake_spi_set_chip_id(uint8_t id)
{
    s_chip_id = id;
    s_regs[0xD0U] = id;
}

void fake_spi_set_measuring_reads(int n)
{
    s_meas_target = n;
    s_meas_reads = n;
}

/* Model a slow NVM copy: the first n status reads after a soft reset
 * still see im_update = 1. */
void fake_spi_set_im_update_reads(int n)
{
    s_im_target = n;
    s_im_reads = n;
}

static void apply_write(uint8_t reg, uint8_t value)
{
    if (!s_present) {
        return;
    }
    s_regs[reg] = value;

    switch (reg) {
    case 0xE0U:                       /* soft reset */
        fake_reset_cmd = value;
        s_regs[0xF4U] = 0x00U;       /* datasheet: ctrl_meas resets too */
        s_regs[REG_STATUS] |= ST_IM_UPDATE;
        s_im_reads = s_im_target;   /* the NVM copy takes a moment */
        break;
    case REG_STATUS:
        break;
    case 0xF2U:
        fake_ctrl_hum = value;
        break;
    case 0xF4U:                       /* ctrl_meas */
        fake_ctrl_meas = value;
        if ((value & 0x03U) == 0x01U) {   /* forced mode */
            s_regs[REG_STATUS] |= ST_MEASURING;
            s_regs[REG_STATUS] &= (uint8_t)~ST_IM_UPDATE;
            s_meas_reads = s_meas_target;
        } else {
            s_regs[REG_STATUS] &= (uint8_t)~ST_MEASURING;
        }
        break;
    case 0xF5U:
        fake_config = value;
        break;
    default:
        break;
    }
}

static uint8_t read_byte(uint8_t reg)
{
    if (!s_present) {
        return 0x00U;   /* MISO with nothing driving it */
    }
    if (reg == 0xD0U) {
        return s_chip_id;
    }
    if (reg == REG_STATUS) {
        /* Both flags clear "after a moment": the first status read still
         * sees them, the next one does not. */
        if (s_im_reads > 0) {
            s_im_reads--;
        } else {
            s_regs[REG_STATUS] &= (uint8_t)~ST_IM_UPDATE;
        }
        if (s_meas_reads > 0) {
            s_meas_reads--;
        } else {
            s_regs[REG_STATUS] &= (uint8_t)~ST_MEASURING;
        }
    }
    return s_regs[reg];   /* reg is a uint8_t, the file covers 0..255 */
}

/* ---- spi_bus.h ------------------------------------------------------------ */

bool spi_bus_init(void)
{
    s_cs = false;
    s_phase = PH_CMD;
    return true;
}

void spi_bus_cs(bool asserted)
{
    s_cs = asserted;
    if (asserted) {
        s_phase = PH_CMD;   /* every transaction starts with a command */
    }
}

bool spi_bus_transfer(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    if (!s_cs) {
        s_errors++;
        fake_spi_bad_cmds++;
        return false;
    }

    for (uint16_t i = 0U; i < len; i++) {
        const uint8_t out = tx ? tx[i] : 0x00U;

        switch (s_phase) {
        case PH_CMD:
            if ((out & 0x80U) != 0U) {
                /* Bit 7 is the direction flag, not part of the address.
                 * Every BME280 register lives in 0x80..0xFE, so the low
                 * seven bits map back one-to-one. */
                s_reg = (uint8_t)((out & 0x7FU) | 0x80U);
                s_phase = PH_READ;
                if (rx) {
                    rx[i] = read_byte(s_reg++);
                }
            } else {
                s_reg = (uint8_t)(out | 0x80U);   /* same 0x80..0xFE space */
                s_phase = PH_WRITE_DATA;
            }
            break;

        case PH_WRITE_DATA:
            apply_write(s_reg, out);
            s_phase = PH_CMD;
            break;

        case PH_READ:
            if (rx) {
                rx[i] = read_byte(s_reg++);
            }
            break;

        default:
            fake_spi_bad_cmds++;
            break;
        }
    }
    return true;
}

bool spi_bus_is_ready(void)
{
    return true;
}

void spi_bus_wait_ms(uint32_t ms)
{
    fake_spi_waits++;
    (void)usleep((unsigned)ms * 1000U);
}

void spi_bus_diag(const char *why)
{
    (void)printf("  > [I] spi: diag (%s): host stand-in, no registers\n", why);
}

uint32_t spi_bus_error_count(void)
{
    return s_errors;
}
