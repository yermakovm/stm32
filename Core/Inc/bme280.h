/**
 * @file    bme280.h
 * @brief   Bosch BME280 (temperature / pressure / relative humidity)
 *          driver on SPI2 (PB13 SCK, PB14 MISO, PB15 MOSI, PB12 CS).
 *
 * Not on the I2C bus: SPI1 (PA4..PA7) is taken by this board's onboard
 * SPI flash footprint, so the sensor uses SPI2.  If a module is wired
 * to I2C1 instead, this driver will not see it - see README
 * "Troubleshooting".
 *
 * The chip is polled like the rest of this board: no interrupts, no DMA.
 * A measurement is started in FORCED mode and finished later, so a
 * 2 Hz sampling rate never stalls the 30 fps screen loop:
 *
 *   bme280_measure()      start one conversion (returns immediately)
 *   bme280_meas_time_ms() how long the chip needs at the configured
 *                         oversampling - do not poll before that
 *   bme280_is_measuring() true while the conversion is still running
 *   bme280_read()         fetch 0xF7..0xFE and compensate
 *
 * Everything is 32/64-bit fixed point (Bosch's own compensation
 * formulas), so no float printf is pulled into the image.  Values:
 *   temperature  0.01 degC   (2344 = 23.44 C)
 *   pressure     0.01 Pa     (10132500 = 1013.25 hPa)
 *   humidity     1/1024 %RH  (46080 = 45.0 %RH, Q10 fixed point)
 *
 * A missing or dead chip is not fatal: init() returns false, every
 * other call fails cheaply, and the clock keeps running.
 */

#ifndef BME280_H
#define BME280_H

#include <stdbool.h>
#include <stdint.h>

/** One compensated sample, all fields fixed point. */
typedef struct {
    int32_t temp_c_x100;   /* degC * 100,  -4000..8500            */
    uint32_t press_pa_x100;/* Pa * 100,     3000000..11000000     */
    uint32_t rh_x1024;     /* %RH * 1024 (Q10), 0..102400        */
} bme280_data_t;

/** Probe the chip id (0xD0 = 0x60), soft reset, wait for the NVM trim
 *  copy, load the calibration trim, apply the oversampling/filter
 *  settings from app_config.h.  Logs which step failed and what the
 *  bus actually returned.  @return false if no BME280 answered. */
bool bme280_init(void);

/** Raw register dump + wiring verdict, for "is it connected at all?":
 *  0xD0..0xD3, 0xF2..0xF5 and the first 8 calibration bytes. */
void bme280_diag(void);

/** True once bme280_init() has succeeded (and until it fails again). */
bool bme280_is_present(void);

/** Worst-case conversion time of the configured oversampling, in ms. */
uint32_t bme280_meas_time_ms(void);

/** Trigger one forced-mode measurement of T, P and RH. */
bool bme280_measure(void);

/** True while the status register still reports "measuring". */
bool bme280_is_measuring(void);

/** Read the data registers and compensate. @return false on SPI error. */
bool bme280_read(bme280_data_t *out);

/** Failed SPI transfers to this chip, for the log. */
uint32_t bme280_error_count(void);

#endif /* BME280_H */
