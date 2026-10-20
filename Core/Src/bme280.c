/**
 * @file    bme280.c
 * @see     bme280.h
 *
 * Register map, calibration layout and the compensation formulas follow
 * the BME280 datasheet (BST-BME280-DS002) and Bosch's own reference
 * driver (boschsensortec/BME280_SensorAPI, integer variant):
 *
 *   0xD0 chip id (0x60)      0xE0 soft reset (0xB6)   0xF3 status
 *   0xF2 ctrl_hum            0xF4 ctrl_meas           0xF5 config
 *   0x88..0xA1 T+P trim (+ dig_H1 at 0xA1)
 *   0xE1..0xE7 H trim
 *   0xF7..0xFE P[23:4], T[19:0], H[15:0]
 *
 * The chip hangs off SPI2 (PB13 SCK, PB14 MISO, PB15 MOSI, PB12 CS), so
 * every transfer goes through spi_bus, which reports failures instead of
 * hanging the clock.  On the SPI side a "read" is a command byte with bit
 * 7 set followed by the data bytes (datasheet Table 23).
 */

#include "bme280.h"

#include <stddef.h>

#include "app_config.h"
#include "spi_bus.h"
#include "ilog.h"

/* ------------------------------------------------------------------ */
/* registers                                                          */
/* ------------------------------------------------------------------ */

#define SPI_CMD_READ      0x80U   /* SPI command bit 7: 1 = read */

#define REG_ID            0xD0U
#define CHIP_ID_VALUE     0x60U
#define REG_RESET         0xE0U
#define RESET_CMD         0xB6U
#define REG_CALIB_TP      0x88U   /* 26 bytes, dig_H1 is the last one  */
#define CALIB_TP_LEN      26U
#define REG_CALIB_H       0xE1U   /* 7 bytes                           */
#define CALIB_H_LEN       7U
#define REG_DATA          0xF7U   /* 8 bytes: P msb/lsb/xlsb, T, H     */
#define DATA_LEN          8U
#define REG_CTRL_HUM      0xF2U
#define REG_STATUS        0xF3U
#define ST_IM_UPDATE      0x01U   /* NVM copy / config not applied     */
#define ST_MEASURING      0x08U   /* a conversion is running           */
#define REG_CTRL_MEAS     0xF4U
#define REG_CONFIG        0xF5U

#define MODE_SLEEP        0x00U
#define MODE_FORCED       0x01U

/* ctrl_meas: [7:5] osrs_t, [4:2] osrs_p, [1:0] mode. */
#define CTRL_MEAS(t, p, mode) \
    (uint8_t)((((t) & 0x07U) << 5) | (((p) & 0x07U) << 2) | ((mode) & 0x03U))

/* config: [7:5] t_sb, [4:2] filter, [0] iir (forced mode only). */
#define CONFIG(standby, filter) \
    (uint8_t)((((standby) & 0x07U) << 5) | (((filter) & 0x07U) << 2))

/* OSR code -> actual oversampling factor, for the cycle-time estimate.
 * 0b110/0b111 are reserved in the datasheet; treat them as x16. */
static const uint16_t OSR_FACTOR[6] = { 0U, 1U, 2U, 4U, 8U, 16U };

static uint16_t osr_factor(uint8_t code)
{
    return (code <= 5U) ? OSR_FACTOR[code] : 16U;
}

/* Per-oversampling measurement durations, microseconds (datasheet
 * Table 33 / Bosch bme280_cal_meas_delay()). */
#define MEAS_OFFSET_US        1250U
#define MEAS_DUR_US           2300U
#define PRES_HUM_OFFSET_US    575U

/* A soft reset starts the NVM copy of the calibration trim, which the
 * datasheet gives as up to 1.5 ms.  One status read over SPI2 at
 * 1.3 MHz takes ~25 us, so polling back-to-back covers a fraction of
 * the copy time and reports "never finished" on a healthy chip: the
 * wait has to be timed, not counted.  Bounded, because a broken chip
 * must not stall the 33 ms screen loop. */
#define NVM_POLL_MS             1U
#define NVM_WAIT_MS            20U

/* 0xD0 is the only presence test SPI has, so it is read a few times:
 * a floating line or a half-made contact flickers, a real chip does
 * not. */
#define ID_PROBE_READS          3U

/* ------------------------------------------------------------------ */
/* calibration trim (datasheet 4.1.1)                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    uint16_t t1;
    int16_t  t2;
    int16_t  t3;
    uint16_t p1;
    int16_t  p2;
    int16_t  p3;
    int16_t  p4;
    int16_t  p5;
    int16_t  p6;
    int16_t  p7;
    int16_t  p8;
    int16_t  p9;
    uint8_t  h1;
    int16_t  h2;
    uint8_t  h3;
    int16_t  h4;
    int16_t  h5;
    int8_t   h6;
    int32_t  t_fine;      /* shared by the pressure and humidity formulas */
} calib_t;

static bool s_ready;
static calib_t s_cal;
static uint32_t s_errors;
static uint32_t s_meas_ms;

/* ------------------------------------------------------------------ */
/* small helpers                                                      */
/* ------------------------------------------------------------------ */

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[1] << 8) | p[0]);   /* LSB first */
}

/* SPI framing, datasheet Table 23: bit 7 of the first byte selects the
 * direction (1 = read, 0 = write), bits 6..0 are the register. CS stays
 * asserted for the whole command + data sequence. */
static bool write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t tx[2] = { (uint8_t)(reg & 0x7FU), value };
    bool ok;

    spi_bus_cs(true);
    ok = spi_bus_transfer(tx, NULL, 2U);
    spi_bus_cs(false);
    if (!ok) {
        s_errors++;
    }
    return ok;
}

static bool read_reg(uint8_t reg, uint8_t *data, uint16_t len)
{
    const uint8_t cmd = (uint8_t)(reg | SPI_CMD_READ);
    bool ok;

    spi_bus_cs(true);
    ok = spi_bus_transfer(&cmd, NULL, 1U)
         && spi_bus_transfer(NULL, data, len);
    spi_bus_cs(false);
    if (!ok) {
        s_errors++;
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* compensation (Bosch reference formulas, integer)                   */
/* ------------------------------------------------------------------ */

/* Temperature: returns degC * 100 and refreshes cal->t_fine.
 *
 * The datasheet writes this chain in 32 bits.  With a trim like
 * t3 = 10952 the (adc_t/16 - t1)^2 * t3 product already needs 2.2e9 at
 * adc_t = 462700, i.e. it wraps to a negative t_fine and the reported
 * temperature jumps from +50 C to -1 C.  Cortex-M4 does 64-bit multiplies
 * cheaply and this runs twice a second, so the two intermediate products
 * are done in int64: identical results everywhere the 32-bit version is
 * valid, no garbage outside it. */
static int32_t comp_temp(uint32_t adc_t, calib_t *cal)
{
    int64_t var1;
    int64_t var2;
    int32_t temp;

    var1 = (int64_t)(adc_t / 8U) - ((int64_t)cal->t1 * 2);
    var1 = (var1 * (int64_t)cal->t2) / 2048;
    var2 = (int64_t)(adc_t / 16U) - (int64_t)cal->t1;
    var2 = (((var2 * var2) / 4096) * (int64_t)cal->t3) / 16384;
    cal->t_fine = (int32_t)(var1 + var2);
    temp = (cal->t_fine * 5 + 128) / 256;

    return (temp < -4000) ? -4000 : ((temp > 8500) ? 8500 : temp);
}

/* Pressure: returns Pa * 100. Needs the t_fine of the same cycle.
 * 64-bit variant - the Cortex-M4 does the multiplies in one cycle. */
static uint32_t comp_press(uint32_t adc_p, const calib_t *cal)
{
    int64_t var1;
    int64_t var2;
    int64_t var3;
    int64_t var4;
    uint64_t press;

    var1 = ((int64_t)cal->t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)cal->p6;
    var2 = var2 + ((var1 * (int64_t)cal->p5) * 131072);
    var2 = var2 + ((int64_t)cal->p4 * 34359738368LL);
    var1 = ((var1 * var1 * (int64_t)cal->p3) / 256)
           + ((var1 * ((int64_t)cal->p2) * 4096));
    var3 = 140737488355328LL;                       /* 2^47 */
    var1 = ((var3 + var1) * ((int64_t)cal->p1)) / 8589934592LL;

    if (var1 == 0) {
        return 3000000U;                            /* 300 hPa floor */
    }

    var4 = 1048576 - (int64_t)adc_p;
    var4 = (((var4 * 2147483648LL) - var2) * 3125) / var1;
    var1 = (((int64_t)cal->p9) * (var4 / 8192) * (var4 / 8192)) / 33554432;
    var2 = (((int64_t)cal->p8) * var4) / 524288;
    var4 = ((var4 + var1 + var2) / 256) + (((int64_t)cal->p7) * 16);

    press = (uint64_t)(((var4 / 2) * 100) / 128);   /* Pa * 100 */
    if (press < 3000000ULL) {
        return 3000000U;
    }
    if (press > 11000000ULL) {
        return 11000000U;
    }
    return (uint32_t)press;
}

/* Humidity: returns %RH * 1000. */
/* Humidity: returns %RH * 1024.
 *
 * The datasheet comment claims a 1/1000 resolution, but the arithmetic
 * (and its own clamps) say otherwise: var5 ends up as 2^22 * %RH, the
 * 419430400 clamp is 100 %RH * 2^22, and the 102400 clamp is 100 %RH *
 * 1024.  So the value after /4096 is Q10 fixed point, i.e. 1024 units
 * per %RH - TI's port documents it the same way ("42313 = 41.321 rH").
 * Feeding the same raw codes through the datasheet's double-precision
 * formula agrees with this to better than 0.01 %RH once the 1024 scale
 * is used; treating it as 1/1000 reads 2.4 % too high. */
static uint32_t comp_hum(uint32_t adc_h, const calib_t *cal)
{
    int32_t var1;
    int32_t var2;
    int32_t var3;
    int32_t var4;
    int32_t var5;
    uint32_t hum;

    var1 = cal->t_fine - 76800;
    var2 = (int32_t)(adc_h * 16384U);
    var3 = (int32_t)cal->h4 * 1048576;
    var4 = (int32_t)cal->h5 * var1;
    var5 = (((var2 - var3) - var4) + 16384) / 32768;
    var2 = (var1 * (int32_t)cal->h6) / 1024;
    var3 = (var1 * (int32_t)cal->h3) / 2048;
    var4 = ((var2 * (var3 + 32768)) / 1024) + 2097152;
    var2 = ((var4 * (int32_t)cal->h2) + 8192) / 16384;
    var3 = var5 * var2;
    var4 = ((var3 / 32768) * (var3 / 32768)) / 128;
    var5 = var3 - ((var4 * (int32_t)cal->h1) / 16);
    var5 = (var5 < 0) ? 0 : var5;
    var5 = (var5 > 419430400) ? 419430400 : var5;
    hum = (uint32_t)(var5 / 4096);

    return (hum > 102400U) ? 102400U : hum;
}

/* ------------------------------------------------------------------ */
/* init                                                               */
/* ------------------------------------------------------------------ */

static bool load_calib(void)
{
    uint8_t tp[CALIB_TP_LEN];
    uint8_t h[CALIB_H_LEN];
    calib_t c;

    if (!read_reg(REG_CALIB_TP, tp, CALIB_TP_LEN)) {
        return false;
    }
    c.t1 = le16(&tp[0]);
    c.t2 = (int16_t)le16(&tp[2]);
    c.t3 = (int16_t)le16(&tp[4]);
    c.p1 = le16(&tp[6]);
    c.p2 = (int16_t)le16(&tp[8]);
    c.p3 = (int16_t)le16(&tp[10]);
    c.p4 = (int16_t)le16(&tp[12]);
    c.p5 = (int16_t)le16(&tp[14]);
    c.p6 = (int16_t)le16(&tp[16]);
    c.p7 = (int16_t)le16(&tp[18]);
    c.p8 = (int16_t)le16(&tp[20]);
    c.p9 = (int16_t)le16(&tp[22]);
    c.h1 = tp[25];                       /* dig_H1 lives at 0xA1 */

    if (!read_reg(REG_CALIB_H, h, CALIB_H_LEN)) {
        return false;
    }
    c.h2 = (int16_t)le16(&h[0]);
    c.h3 = h[2];
    c.h4 = (int16_t)((int8_t)h[3] * 16) | (int16_t)(h[4] & 0x0FU);
    c.h5 = (int16_t)((int8_t)h[5] * 16) | (int16_t)(h[4] >> 4);
    c.h6 = (int8_t)h[6];

    c.t_fine = 0;
    if (c.t1 == 0U || c.p1 == 0U) {
        /* A zero here means the NVM copy failed: every formula would
         * divide by zero or return a constant. */
        LOGE("sensor: BME280 calibration is empty (t1=%u p1=%u)",
             (unsigned int)c.t1, (unsigned int)c.p1);
        return false;
    }
    s_cal = c;
    LOGT("sensor: trim t1=%u t2=%d t3=%d p1=%u p2=%d h1=%u h2=%d h3=%u",
         (unsigned int)c.t1, (int)c.t2, (int)c.t3, (unsigned int)c.p1,
         (int)c.p2, (unsigned int)c.h1, (int)c.h2, (unsigned int)c.h3);
    return true;
}

/* Wait for the NVM trim copy after a soft reset: timed, because the
 * copy is a chip-internal process that runs while the bus is idle.
 * Polling every NVM_POLL_MS for up to NVM_WAIT_MS covers the datasheet
 * 1.5 ms with room for a slow part, and still cannot stall the boot -
 * the old counted loop (24 back-to-back SPI reads, ~25 us each) did
 * not even reach 1 ms and reported "never finished" on a healthy chip. */
static bool wait_nvm_copy(void)
{
    uint8_t st = 0U;
    uint32_t waited = 0U;
    uint32_t polls = 0U;

    while (waited <= NVM_WAIT_MS) {
        if (!read_reg(REG_STATUS, &st, 1U)) {
            LOGE("sensor: status read failed while waiting for the NVM copy (after %lu ms)",
                 (unsigned long)waited);
            return false;
        }
        polls++;
        if ((st & ST_IM_UPDATE) == 0U) {
            LOGT("sensor: nvm copy done after %lu ms (%u polls, status=0x%02x)",
                 (unsigned long)waited, (unsigned)polls, (unsigned)st);
            return true;
        }
        spi_bus_wait_ms(NVM_POLL_MS);
        waited += NVM_POLL_MS;
    }

    /* im_update still 1 after 20 ms: the reset command must have
     * arrived - it is what sets that bit - so CS/MOSI/MISO are alive.
     * What is left is power, a chip held in reset, or an SPI clock the
     * part cannot keep up with. */
    LOGE("sensor: BME280 never finished its NVM copy: status=0x%02x (im_update=1) after %lu ms / %u polls",
         (unsigned int)st, (unsigned long)waited, (unsigned)polls);
    LOGE("sensor:   hints: 3V3+GND at the sensor, CSB tied to GND, SPI at or below 3 MHz (prescaler %u = %lu Hz)",
         (unsigned)SPI_BUS_PRESCALER,
         (unsigned long)(42000000U / (1U << SPI_BUS_PRESCALER)));
    return false;
}

static bool apply_settings(void)
{
    /* ctrl_hum only takes effect once ctrl_meas is written afterwards,
     * so the order is fixed: ctrl_hum -> config -> ctrl_meas (sleep). */
    if (!write_reg(REG_CTRL_HUM, (uint8_t)(BME280_OSRS_H & 0x07U))) {
        return false;
    }
    if (!write_reg(REG_CONFIG, CONFIG(BME280_STANDBY_MS, BME280_FILTER))) {
        return false;
    }
    if (!write_reg(REG_CTRL_MEAS,
                   CTRL_MEAS(BME280_OSRS_T, BME280_OSRS_P, MODE_SLEEP))) {
        return false;
    }

    {
        const uint16_t us = MEAS_OFFSET_US
                          + MEAS_DUR_US * osr_factor(BME280_OSRS_T)
                          + (MEAS_DUR_US * osr_factor(BME280_OSRS_P)
                             + PRES_HUM_OFFSET_US)
                          + (MEAS_DUR_US * osr_factor(BME280_OSRS_H)
                             + PRES_HUM_OFFSET_US);
        s_meas_ms = (uint32_t)((us + 999U) / 1000U);
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* presence diagnostics                                               */
/* ------------------------------------------------------------------ */

/* On SPI there is no address to probe: the chip id is the whole
 * presence test, and the value that comes back is itself the
 * diagnosis. */
static const char *id_verdict(uint8_t id)
{
    switch (id) {
    case CHIP_ID_VALUE:
        return "this is a BME280";
    case 0x58U:
        return "this is a BMP280: no humidity, different driver";
    case 0x56U:
        return "this is a BME680: different driver";
    case 0x00U:
        return "MISO reads low: sensor unpowered, PB14 not on sensor SDO, or CSB not tied to GND";
    case 0xFFU:
        return "MISO floats high: nothing drives it - CS (PB12) not reaching the sensor, CSB tied HIGH (that selects I2C mode), or MISO shorted to MOSI";
    default:
        return "not a BME280: wrong device, or the bits are shifted (swap MOSI/MISO, check CPOL/CPHA)";
    }
}

/* Read 0xD0 ID_PROBE_READS times.  A real chip answers the same value
 * every time; a floating line or a half-made contact does not.
 * @return false only if the transfers themselves failed. */
static bool probe_chip_id(uint8_t *out_id, uint8_t *out_agree)
{
    uint8_t v[ID_PROBE_READS];
    uint8_t agree = 0U;
    uint8_t i;

    for (i = 0U; i < ID_PROBE_READS; i++) {
        if (!read_reg(REG_ID, &v[i], 1U)) {
            LOGE("sensor: chip id read %u of %u failed on SPI2 (spi errors %u)",
                 (unsigned)(i + 1U), (unsigned)ID_PROBE_READS,
                 (unsigned)s_errors);
            *out_id = v[i];
            *out_agree = 0U;
            return false;
        }
        LOGT("sensor: chip id read %u -> 0x%02x", (unsigned)(i + 1U),
             (unsigned)v[i]);
    }

    for (i = 0U; i < ID_PROBE_READS; i++) {
        if (v[i] == v[ID_PROBE_READS - 1U]) {
            agree++;
        }
    }
    *out_id = v[ID_PROBE_READS - 1U];
    *out_agree = agree;
    return true;
}

/* Classify a block of raw register bytes: the difference between "no
 * chip", "chip not selected" and "chip answering, contents wrong". */
static const char *block_pattern(const uint8_t *p, uint16_t len)
{
    bool all_zero = true;
    bool all_ff = true;
    uint16_t i;

    for (i = 0U; i < len; i++) {
        if (p[i] != 0U) {
            all_zero = false;
        }
        if (p[i] != 0xFFU) {
            all_ff = false;
        }
    }
    if (all_zero) {
        return "all 0x00: MISO is held low - no power on the sensor, or SDO not wired to PB14";
    }
    if (all_ff) {
        return "all 0xFF: MISO floats high - the chip is not being selected (CS), or it is in I2C mode";
    }
    return "mixed values: the bus is moving and the chip is answering";
}

void bme280_diag(void)
{
    uint8_t id4[4];
    uint8_t ctl4[4];
    uint8_t cal8[8];

    LOGI("sensor: diag: raw reads over SPI2 (cs=PB12 sck=PB13 miso=PB14 mosi=PB15)");

    if (read_reg(REG_ID, id4, 4U)) {
        log_hexdump("id d0..d3", id4, 4U);
        LOGI("sensor:   id block: %s", block_pattern(id4, 4U));
    } else {
        LOGE("sensor:   0xD0 read failed - no transfer at all");
    }
    /* 0xF2..0xF5 = ctrl_hum, status, ctrl_meas, config.  status bit 0
     * (im_update) still set means the chip never applied its settings;
     * status bit 3 stuck means it is measuring forever. */
    if (read_reg(REG_CTRL_HUM, ctl4, 4U)) {
        log_hexdump("ctl f2..f5", ctl4, 4U);
        LOGI("sensor:   status 0x%02x: im_update=%u measuring=%u",
             (unsigned)ctl4[1],
             (unsigned)((ctl4[1] & ST_IM_UPDATE) != 0U),
             (unsigned)((ctl4[1] & ST_MEASURING) != 0U));
    } else {
        LOGE("sensor:   0xF2 read failed - no transfer at all");
    }
    /* A genuine BME280 trim is never all zero: dig_T1 is a few hundred
     * and dig_P1 is around 36000. */
    if (read_reg(REG_CALIB_TP, cal8, 8U)) {
        log_hexdump("trim 88..8f", cal8, 8U);
        LOGI("sensor:   trim: %s", block_pattern(cal8, 8U));
    } else {
        LOGE("sensor:   0x88 read failed - no transfer at all");
    }
}

bool bme280_init(void)
{
    uint8_t id = 0U;
    uint8_t agree = 0U;

    s_ready = false;

    if (!probe_chip_id(&id, &agree)) {
        LOGE("sensor: SPI2 got no answer at all - check 3V3/GND and PB12..PB15 (cs/sck/miso/mosi)");
        return false;
    }
    if (agree != ID_PROBE_READS) {
        LOGE("sensor: chip id is not stable (%u of %u reads agreed on 0x%02x) - suspect a flaky contact or a floating line",
             (unsigned)agree, (unsigned)ID_PROBE_READS, (unsigned)id);
    }
    if (id != CHIP_ID_VALUE) {
        LOGE("sensor: no BME280 on SPI2: chip id 0x%02x, expected 0x60 - %s",
             (unsigned int)id, id_verdict(id));
        return false;
    }

    if (!write_reg(REG_RESET, RESET_CMD)) {
        LOGE("sensor: init stopped at soft reset (0xE0 = 0xB6)");
        return false;
    }
    if (!wait_nvm_copy()) {
        LOGE("sensor: init stopped at the NVM copy wait");
        return false;
    }
    if (!load_calib()) {
        LOGE("sensor: init stopped at the calibration read (0x88..0xA1, 0xE1..0xE7)");
        return false;
    }
    if (!apply_settings()) {
        LOGE("sensor: init stopped at the settings write (0xF2, 0xF5, 0xF4)");
        return false;
    }

    s_ready = true;
    LOGI("sensor: BME280 on SPI2 (CS PB12), osrs t/p/h=%u/%u/%u "
         "filter=%u, cycle<=%lu ms",
         (unsigned int)BME280_OSRS_T, (unsigned int)BME280_OSRS_P,
         (unsigned int)BME280_OSRS_H, (unsigned int)BME280_FILTER,
         (unsigned long)s_meas_ms);
    return true;
}

/* ------------------------------------------------------------------ */
/* sampling                                                           */
/* ------------------------------------------------------------------ */

bool bme280_is_present(void)
{
    return s_ready;
}

uint32_t bme280_meas_time_ms(void)
{
    return s_meas_ms;
}

bool bme280_measure(void)
{
    if (!s_ready) {
        return false;
    }
    if (!write_reg(REG_CTRL_MEAS,
                   CTRL_MEAS(BME280_OSRS_T, BME280_OSRS_P, MODE_FORCED))) {
        s_errors++;
        return false;
    }
    return true;
}

bool bme280_is_measuring(void)
{
    uint8_t st;

    if (!s_ready) {
        return false;
    }
    if (!read_reg(REG_STATUS, &st, 1U)) {
        s_errors++;
        return false;
    }
    return (st & ST_MEASURING) != 0U;
}

bool bme280_read(bme280_data_t *out)
{
    uint8_t d[DATA_LEN];
    uint32_t adc_p;
    uint32_t adc_t;
    uint32_t adc_h;

    if (!s_ready || out == NULL) {
        return false;
    }
    if (!read_reg(REG_DATA, d, DATA_LEN)) {
        s_errors++;
        return false;
    }

    adc_p = ((uint32_t)d[0] << 12) | ((uint32_t)d[1] << 4)
          | ((uint32_t)d[2] >> 4);
    adc_t = ((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4)
          | ((uint32_t)d[5] >> 4);
    adc_h = ((uint32_t)d[6] << 8) | (uint32_t)d[7];

    /* Order matters: comp_temp() produces the t_fine that the pressure
     * and humidity formulas of the SAME cycle need. */
    out->temp_c_x100 = comp_temp(adc_t, &s_cal);
    out->press_pa_x100 = comp_press(adc_p, &s_cal);
    out->rh_x1024 = comp_hum(adc_h, &s_cal);

    {
        const uint32_t rh100 = (out->rh_x1024 * 100U + 512U) / 1024U;

        LOGT("sensor: raw p=%lu t=%lu h=%u -> T=%d.%02u C P=%lu.%02u hPa "
             "RH=%u.%02u %%",
             (unsigned long)adc_p, (unsigned long)adc_t, (unsigned int)adc_h,
             (int)(out->temp_c_x100 / 100),
             (unsigned)(out->temp_c_x100 < 0 ? -(out->temp_c_x100 % 100)
                                             : (out->temp_c_x100 % 100)),
             (unsigned long)(out->press_pa_x100 / 10000),
             (unsigned)((out->press_pa_x100 / 100) % 100),
             (unsigned)(rh100 / 100U), (unsigned)(rh100 % 100U));
    }
    return true;
}

uint32_t bme280_error_count(void)
{
    return s_errors;
}
