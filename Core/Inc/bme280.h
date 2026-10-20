/**
 * @file    bme280.h
 * @brief   Bosch BME280 (temperature / pressure / relative humidity)
 *          driver for the shared I2C1 bus, I2C address 0x76 or 0x77.
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
 *   humidity     0.001 %RH   (45123 = 45.123 %)
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

/** Probe 0x76 then 0x77, check the chip id, soft reset, load the
 *  calibration trim, apply the oversampling/filter settings from
 *  app_config.h. @return false if no BME280 answered. */
bool bme280_init(void);

/** 7-bit address the chip answered on, 0 when it is not there. */
uint8_t bme280_addr(void);

/** True once bme280_init() has succeeded (and until it fails again). */
bool bme280_is_present(void);

/** Worst-case conversion time of the configured oversampling, in ms. */
uint32_t bme280_meas_time_ms(void);

/** Trigger one forced-mode measurement of T, P and RH. */
bool bme280_measure(void);

/** True while the status register still reports "measuring". */
bool bme280_is_measuring(void);

/** Read the data registers and compensate. @return false on I2C error. */
bool bme280_read(bme280_data_t *out);

/** Failed I2C transfers to this chip, for the log. */
uint32_t bme280_error_count(void);

#endif /* BME280_H */
