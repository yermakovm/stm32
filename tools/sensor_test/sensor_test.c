/**
 * @file    tools/sensor_test/sensor_test.c
 * @brief   Host test for Core/Src/bme280.c against an emulated BME280.
 *
 * What it proves:
 *   1. init(): probe, chip id, soft reset (0xB6), NVM-copy wait, and the
 *      oversampling/filter settings actually reach the right registers
 *      in the right order (ctrl_hum before ctrl_meas).
 *   2. The register blocks 0x88..0xA1 / 0xE1..0xE7 are parsed the way the
 *      datasheet lays them out (little endian, dig_H1 at 0xA1, the packed
 *      dig_H4/dig_H5 nibbles).
 *   3. The fixed-point compensation reproduces Bosch's double-precision
 *      formulas over a sweep of raw ADC codes.  The comparison is done
 *      against the float formulas fed with the *integer* t_fine, because
 *      pressure and humidity take t_fine from the temperature step: that
 *      isolates "wrong formula" from "the integer temperature step
 *      rounds differently".
 *   4. The forced-mode handshake (trigger -> measuring -> data) works.
 *   5. A missing chip, or one with the wrong chip id, is handled without
 *      pretending to have data (including a BMP280, which is the part
 *      usually sold as a "BME280").
 *   6. The NVM-copy wait after a soft reset is timed, not counted: it
 *      survives a slow copy and still gives up in bounded time.
 *   7. bme280_diag() tells the same truth for a good chip and a missing
 *      one.
 *
 * Build & run: tools/sensor_test/run.sh
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "app_config.h"
#include "bme280.h"
#include "fake_spi.h"

static int s_fail;
static int s_checks;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        s_checks++;                                         \
        if (!(cond)) {                                      \
            s_fail++;                                       \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);    \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

/* ------------------------------------------------------------------ */
/* calibration fixture, exactly as the chip would answer on I2C        */
/* ------------------------------------------------------------------ */

/* 0x88..0x9F: T and P trim, 0xA0 reserved, 0xA1 dig_H1. */
static const uint8_t CALIB_TP[26] = {
    0xCD, 0x01,   /* 0x88 dig_T1 =  461    */
    0x5C, 0x11,   /* 0x8A dig_T2 = 4444    */
    0xC8, 0x2A,   /* 0x8C dig_T3 = 10952   */
    0x90, 0x8B,   /* 0x8E dig_P1 = 35728   */
    0xB3, 0x8A,   /* 0x90 dig_P2 = -30029  */
    0xA4, 0x6F,   /* 0x92 dig_P3 = 28548   */
    0x41, 0x67,   /* 0x94 dig_P4 = 26433   */
    0xB8, 0xFB,   /* 0x96 dig_P5 = -1064   */
    0xC3, 0xD2,   /* 0x98 dig_P6 = -11581  */
    0xA1, 0x17,   /* 0x9A dig_P7 =  6049   */
    0x96, 0x00,   /* 0x9C dig_P8 =   150   */
    0xE4, 0x24,   /* 0x9E dig_P9 =  9444   */
    0x00,         /* 0xA0 reserved         */
    0x01,         /* 0xA1 dig_H1 = 1       */
};

/* 0xE1..0xE7: dig_H2, dig_H3, packed dig_H4/dig_H5, dig_H6. */
static const uint8_t CALIB_H[7] = {
    0x2D, 0x01,   /* 0xE1 dig_H2 = 301     */
    0x00,         /* 0xE3 dig_H3 = 0       */
    0xFB,         /* 0xE4 dig_H4 MSB = -5  */
    0x47,         /* 0xE5 dig_H4 low = 7, dig_H5 low = 4 */
    0x01,         /* 0xE6 dig_H5 MSB = 1   */
    0x00,         /* 0xE7 dig_H6 = 0       */
};

typedef struct {
    uint16_t t1; int16_t t2; int16_t t3;
    uint16_t p1; int16_t p2; int16_t p3; int16_t p4; int16_t p5;
    int16_t p6; int16_t p7; int16_t p8; int16_t p9;
    uint8_t h1; int16_t h2; uint8_t h3; int16_t h4; int16_t h5; int8_t h6;
} ref_calib_t;

/* Independent parse of the same bytes - a mistake on either side shows up
 * as a compensation mismatch. */
static void ref_parse(ref_calib_t *c)
{
    const uint8_t *tp = CALIB_TP;
    const uint8_t *h = CALIB_H;

    c->t1 = (uint16_t)((tp[1] << 8) | tp[0]);
    c->t2 = (int16_t)((tp[3] << 8) | tp[2]);
    c->t3 = (int16_t)((tp[5] << 8) | tp[4]);
    c->p1 = (uint16_t)((tp[7] << 8) | tp[6]);
    c->p2 = (int16_t)((tp[9] << 8) | tp[8]);
    c->p3 = (int16_t)((tp[11] << 8) | tp[10]);
    c->p4 = (int16_t)((tp[13] << 8) | tp[12]);
    c->p5 = (int16_t)((tp[15] << 8) | tp[14]);
    c->p6 = (int16_t)((tp[17] << 8) | tp[16]);
    c->p7 = (int16_t)((tp[19] << 8) | tp[18]);
    c->p8 = (int16_t)((tp[21] << 8) | tp[20]);
    c->p9 = (int16_t)((tp[23] << 8) | tp[22]);
    c->h1 = tp[25];

    c->h2 = (int16_t)((h[1] << 8) | h[0]);
    c->h3 = h[2];
    c->h4 = (int16_t)(((int8_t)h[3] * 16) | (h[4] & 0x0FU));
    c->h5 = (int16_t)(((int8_t)h[5] * 16) | (h[4] >> 4));
    c->h6 = (int8_t)h[6];
}

/* ------------------------------------------------------------------ */
/* reference: Bosch's double-precision compensation formulas           */
/* ------------------------------------------------------------------ */

static double ref_temp(uint32_t adc_t, const ref_calib_t *c, int32_t *t_fine)
{
    double v1 = ((double)adc_t / 16384.0 - (double)c->t1 / 1024.0)
                * (double)c->t2;
    double v2 = ((double)adc_t / 131072.0 - (double)c->t1 / 8192.0);
    double t;

    v2 = (v2 * v2) * (double)c->t3;
    *t_fine = (int32_t)(v1 + v2);
    t = (v1 + v2) / 5120.0;
    return (t < -40.0) ? -40.0 : ((t > 85.0) ? 85.0 : t);
}

static double ref_press(uint32_t adc_p, const ref_calib_t *c, int32_t t_fine)
{
    double v1 = (double)t_fine / 2.0 - 64000.0;
    double v2 = v1 * v1 * (double)c->p6 / 32768.0;
    double v3;
    double p;

    v2 = v2 + v1 * (double)c->p5 * 2.0;
    v2 = (v2 / 4.0) + ((double)c->p4 * 65536.0);
    v3 = (double)c->p3 * v1 * v1 / 524288.0;
    v1 = (v3 + (double)c->p2 * v1) / 524288.0;
    v1 = (1.0 + v1 / 32768.0) * (double)c->p1;
    if (v1 <= 0.0) {
        return 30000.0;
    }
    p = 1048576.0 - (double)adc_p;
    p = (p - (v2 / 4096.0)) * 6250.0 / v1;
    v1 = (double)c->p9 * p * p / 2147483648.0;
    v2 = p * (double)c->p8 / 32768.0;
    p = p + (v1 + v2 + (double)c->p7) / 16.0;
    return (p < 30000.0) ? 30000.0 : ((p > 110000.0) ? 110000.0 : p);
}

static double ref_hum(uint32_t adc_h, const ref_calib_t *c, int32_t t_fine)
{
    double v1 = (double)t_fine - 76800.0;
    double v2 = ((double)c->h4 * 64.0 + ((double)c->h5 / 16384.0) * v1);
    double v3 = (double)adc_h - v2;
    double v4 = (double)c->h2 / 65536.0;
    double v5 = 1.0 + ((double)c->h3 / 67108864.0) * v1;
    double v6 = 1.0 + ((double)c->h6 / 67108864.0) * v1 * v5;
    double h;

    v6 = v3 * v4 * (v5 * v6);
    h = v6 * (1.0 - (double)c->h1 * v6 / 524288.0);
    return (h > 100.0) ? 100.0 : ((h < 0.0) ? 0.0 : h);
}

/* The datasheet's integer temperature chain, re-implemented here: the
 * driver's t_fine is private, and pressure/humidity depend on it, so the
 * test needs its own copy to compare against. */
static int32_t ref_t_fine_int(uint32_t adc_t, const ref_calib_t *c)
{
    int64_t v1 = (int64_t)(adc_t / 8U) - ((int64_t)c->t1 * 2);
    int64_t v2 = (int64_t)(adc_t / 16U) - (int64_t)c->t1;

    v1 = (v1 * (int64_t)c->t2) / 2048;
    v2 = (((v2 * v2) / 4096) * (int64_t)c->t3) / 16384;
    return (int32_t)(v1 + v2);
}

/* ------------------------------------------------------------------ */
/* driving the emulator                                                */
/* ------------------------------------------------------------------ */

static void load_calib_into_chip(void)
{
    fake_spi_poke(0x88, CALIB_TP, sizeof(CALIB_TP));
    fake_spi_poke(0xE1, CALIB_H, sizeof(CALIB_H));
}

static void set_raw(uint32_t adc_p, uint32_t adc_t, uint32_t adc_h)
{
    uint8_t d[8];

    d[0] = (uint8_t)(adc_p >> 12);
    d[1] = (uint8_t)(adc_p >> 4);
    d[2] = (uint8_t)(adc_p << 4);
    d[3] = (uint8_t)(adc_t >> 12);
    d[4] = (uint8_t)(adc_t >> 4);
    d[5] = (uint8_t)(adc_t << 4);
    d[6] = (uint8_t)(adc_h >> 8);
    d[7] = (uint8_t)adc_h;
    fake_spi_poke(0xF7, d, sizeof(d));
}

/* One forced-mode cycle through the driver's own API. */
static bool sample(bme280_data_t *out)
{
    if (!bme280_measure()) {
        return false;
    }
    for (int guard = 0; guard < 64; guard++) {
        if (!bme280_is_measuring()) {
            return bme280_read(out);
        }
    }
    return false;
}

/* Find the raw ADC code the reference formulas map to a target value.
 * All three are monotonic in their raw code over the useful range. */
static uint32_t bisect(const ref_calib_t *c, int which, double target,
                      int32_t t_fine)
{
    double lo = 0.0;
    double hi = (which == 2) ? 65535.0 : 1048575.0;

    for (int i = 0; i < 80; i++) {
        const double mid = (lo + hi) / 2.0;
        int32_t tf;
        double v;

        if (which == 0) {
            v = ref_temp((uint32_t)mid, c, &tf);
        } else if (which == 1) {
            v = ref_press((uint32_t)mid, c, t_fine);
        } else {
            v = ref_hum((uint32_t)mid, c, t_fine);
        }

        /* Pressure falls as its raw code grows, the other two rise. */
        if ((which == 1) ? (v > target) : (v < target)) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return (uint32_t)((lo + hi) / 2.0);
}

/* ------------------------------------------------------------------ */
/* tests                                                              */
/* ------------------------------------------------------------------ */

static void test_init(void)
{
    ref_calib_t ref;

    ref_parse(&ref);

    CHECK(ref.t1 == 461 && ref.t2 == 4444 && ref.t3 == 10952,
          "fixture parse: t1=%u t2=%d t3=%d", (unsigned)ref.t1,
          (int)ref.t2, (int)ref.t3);
    CHECK(ref.p1 == 35728 && ref.p2 == -30029 && ref.p9 == 9444,
          "fixture parse: p1=%u p2=%d p9=%d", (unsigned)ref.p1,
          (int)ref.p2, (int)ref.p9);
    CHECK(ref.h1 == 1 && ref.h2 == 301 && ref.h4 == -73 && ref.h5 == 20,
          "fixture parse: h1=%u h2=%d h4=%d h5=%d", (unsigned)ref.h1,
          (int)ref.h2, (int)ref.h4, (int)ref.h5);

    fake_spi_reset();
    load_calib_into_chip();

    CHECK(bme280_init(), "bme280_init() failed with a chip present");
    CHECK(bme280_is_present(), "is_present() false after a good init");
    CHECK(fake_reset_cmd == 0xB6U, "soft reset wrote 0x%02x",
          (unsigned)fake_reset_cmd);
    CHECK((fake_ctrl_hum & 0x07U) == (BME280_OSRS_H & 0x07U),
          "ctrl_hum = 0x%02x", (unsigned)fake_ctrl_hum);
    CHECK(((fake_config >> 2) & 0x07U) == (BME280_FILTER & 0x07U),
          "config filter = %u", (unsigned)((fake_config >> 2) & 0x07U));
    CHECK((fake_ctrl_meas & 0x03U) == 0x00U,
          "ctrl_meas mode after init = %u (expected sleep)",
          (unsigned)(fake_ctrl_meas & 0x03U));
    CHECK(((fake_ctrl_meas >> 5) & 0x07U) == BME280_OSRS_T
          && ((fake_ctrl_meas >> 2) & 0x07U) == BME280_OSRS_P,
          "ctrl_meas osrs = t%u p%u", (unsigned)((fake_ctrl_meas >> 5) & 7U),
          (unsigned)((fake_ctrl_meas >> 2) & 7U));
    CHECK(bme280_meas_time_ms() >= 1U && bme280_meas_time_ms() < 500U,
          "meas_time = %lu ms", (unsigned long)bme280_meas_time_ms());

    /* Forced-mode handshake: trigger, watch "measuring", then read. */
    set_raw(59682U, 277539U, 5180U);
    CHECK(bme280_measure(), "measure() failed");
    {
        /* The emulator holds "measuring" for 3 status reads. */
        int polls = 0;

        CHECK(bme280_is_measuring(), "measuring bit clear right after trigger");
        polls = 1;
        while (bme280_is_measuring() && polls < 8) {
            polls++;
        }
        CHECK(polls == 3, "measuring handshake took %d polls, emulator says 3",
              polls);
    }
    CHECK(fake_spi_bad_cmds == 0, "%d SPI commands used the wrong read/write "
          "bit or ignored CS", fake_spi_bad_cmds);
}

/* The driver must reproduce the reference formulas, and the reference
 * formulas must be able to describe a real room. */
static void test_values(void)
{
    ref_calib_t ref;
    bme280_data_t d;
    const double t_target = 23.4;      /* degC   */
    const double p_target = 101325.0;  /* Pa     */
    const double h_target = 45.0;      /* %RH    */
    uint32_t adc_t;
    uint32_t adc_p;
    uint32_t adc_h;
    int32_t tf_float;
    int32_t tf_int;
    double t_ref;
    double p_ref;
    double h_ref;

    ref_parse(&ref);

    /* Pick raw codes that describe a room, then use the t_fine those
     * codes produce for the pressure and humidity references: the driver
     * feeds its own temperature step into both. */
    adc_t = bisect(&ref, 0, t_target, 0);
    tf_int = ref_t_fine_int(adc_t, &ref);
    adc_p = bisect(&ref, 1, p_target, tf_int);
    adc_h = bisect(&ref, 2, h_target, tf_int);

    t_ref = (double)tf_int * 5.0 / 256.0 / 100.0;   /* the integer chain's
                                                     * own answer */
    p_ref = ref_press(adc_p, &ref, tf_int);
    h_ref = ref_hum(adc_h, &ref, tf_int);

    printf("fixture: raw t=%lu p=%lu h=%u -> T=%.2f C P=%.1f hPa RH=%.2f %%\n",
           (unsigned long)adc_t, (unsigned long)adc_p, (unsigned)adc_h,
           t_ref, p_ref / 100.0, h_ref);

    CHECK(fabs(t_ref - t_target) < 0.02, "bisection temp off: %.3f", t_ref);
    CHECK(fabs(p_ref - p_target) < 2.0, "bisection pressure off: %.1f",
          p_ref);
    CHECK(fabs(h_ref - h_target) < 0.02, "bisection humidity off: %.3f",
          h_ref);

    set_raw(adc_p, adc_t, adc_h);
    CHECK(sample(&d), "sample() failed");

    printf("driver : T=%d.%02u C P=%lu.%02u hPa RH=%u.%03u %%\n",
           (int)(d.temp_c_x100 / 100),
           (unsigned)(d.temp_c_x100 < 0 ? -(d.temp_c_x100 % 100)
                                        : (d.temp_c_x100 % 100)),
           (unsigned long)(d.press_pa_x100 / 10000),
           (unsigned)((d.press_pa_x100 / 100) % 100),
           (unsigned)(d.rh_x1024 / 1024), (unsigned)(d.rh_x1024 % 1024));

    CHECK(fabs((double)d.temp_c_x100 / 100.0 - t_ref) < 0.05,
          "temp: driver %d vs ref %.3f", (int)d.temp_c_x100, t_ref);
    CHECK(fabs((double)d.press_pa_x100 / 100.0 - p_ref) < 5.0,
          "pressure: driver %lu vs ref %.1f", (unsigned long)d.press_pa_x100,
          p_ref);
    CHECK(fabs((double)d.rh_x1024 / 1024.0 - h_ref) < 0.05,
          "humidity: driver %u vs ref %.3f", (unsigned)d.rh_x1024, h_ref);

    /* What the float version would say with its own t_fine.  The
     * deviation is inherent to the integer chain (humidity is steep in
     * t_fine) and is well inside the chip's own +/-3 %RH spec. */
    {
        double t2 = ref_temp(adc_t, &ref, &tf_float);
        double p2 = ref_press(adc_p, &ref, tf_float);
        double h2 = ref_hum(adc_h, &ref, tf_float);

        printf("float  : T=%.3f C P=%.2f hPa RH=%.3f %% (t_fine int=%d "
               "float=%d)\n",
               t2, p2 / 100.0, h2, (int)tf_int, (int)tf_float);
        printf("gap    : dT=%.3f C dP=%.2f Pa dRH=%.3f %%\n",
               (double)d.temp_c_x100 / 100.0 - t2,
               (double)d.press_pa_x100 / 100.0 - p2,
               (double)d.rh_x1024 / 1024.0 - h2);
        CHECK(fabs((double)d.rh_x1024 / 1024.0 - h2) < 2.0,
              "humidity vs pure float: %.3f %% off",
              (double)d.rh_x1024 / 1024.0 - h2);
        CHECK(fabs((double)d.press_pa_x100 / 100.0 - p2) < 50.0,
              "pressure vs pure float: %.1f Pa off",
              (double)d.press_pa_x100 / 100.0 - p2);
    }
}

/* Sweep the raw codes over the whole span the chip can emit, so a
 * transcription error cannot hide behind one lucky input. */
static void test_sweep(void)
{
    ref_calib_t ref;
    bme280_data_t d;
    double worst_t = 0.0;
    double worst_p = 0.0;
    double worst_h = 0.0;
    int used = 0;
    int railed = 0;

    ref_parse(&ref);

    for (uint32_t i = 0U; i < 512U; i++) {
        const uint32_t adc_t = 8000U + i * 2000U;      /* up to ~1.1e6 */
        const uint32_t adc_p = 80000U + i * 1800U;
        const uint32_t adc_h = 2000U + i * 120U;
        int32_t tf;
        double t_ref;
        double p_ref;
        double h_ref;

        set_raw(adc_p, adc_t, adc_h);
        if (!sample(&d)) {
            CHECK(false, "sweep sample %u failed", (unsigned)i);
            return;
        }

        tf = ref_t_fine_int(adc_t, &ref);
        t_ref = (double)tf * 5.0 / 256.0 / 100.0;   /* what the integer
                                                     * temperature chain
                                                     * itself means */
        p_ref = ref_press(adc_p, &ref, tf);
        h_ref = ref_hum(adc_h, &ref, tf);

        /* Skip points outside the chip's operating range: there the
         * datasheet clamps the answer and the comparison proves
         * nothing about the formulas. */
        if (t_ref < -40.0 || t_ref > 85.0) {
            railed++;
            continue;
        }
        if (p_ref <= 30050.0 || p_ref >= 109950.0) {
            railed++;
            continue;
        }
        if (h_ref <= 0.2 || h_ref >= 99.8) {
            railed++;
            continue;
        }
        used++;

        {
            const double e_t = fabs((double)d.temp_c_x100 / 100.0 - t_ref);
            const double e_p = fabs((double)d.press_pa_x100 / 100.0 - p_ref);
            const double e_h = fabs((double)d.rh_x1024 / 1024.0 - h_ref);

            if (e_t > worst_t) {
                worst_t = e_t;
            }
            if (e_p > worst_p) {
                worst_p = e_p;
            }
            if (e_h > worst_h) {
                worst_h = e_h;
            }
        }
    }

    printf("sweep  : %d usable points, %d out of range; worst "
           "|driver-reference| T=%.3f C  P=%.2f Pa  RH=%.4f %%\n",
           used, railed, worst_t, worst_p, worst_h);

    CHECK(used > 100, "sweep produced too few usable points (%d)", used);
    CHECK(worst_t < 0.05, "temperature drifts from the reference: %.3f C",
          worst_t);
    CHECK(worst_p < 20.0, "pressure drifts from the reference: %.1f Pa",
          worst_p);
    CHECK(worst_h < 0.1, "humidity drifts from the reference: %.4f %%",
          worst_h);
}

/* A hot sensor must not wrap into a nonsense reading.  The datasheet's
 * 32-bit temperature chain overflows at adc_t ~460000 for this trim;
 * bme280.c does those two products in 64 bits, so the answer has to stay
 * monotonic and inside the datasheet range. */
static void test_hot_sensor(void)
{
    bme280_data_t d;
    int32_t prev = -4000;
    int wrapped = 0;

    for (uint32_t adc_t = 300000U; adc_t <= 700000U; adc_t += 10000U) {
        set_raw(59682U, adc_t, 5180U);
        if (!sample(&d)) {
            CHECK(false, "hot sample at %lu failed", (unsigned long)adc_t);
            return;
        }
        if (d.temp_c_x100 < prev) {
            wrapped++;
        }
        prev = d.temp_c_x100;
        CHECK(d.temp_c_x100 >= -4000 && d.temp_c_x100 <= 8500,
              "temperature out of the datasheet range at adc_t=%lu: %d",
              (unsigned long)adc_t, (int)d.temp_c_x100);
    }
    printf("hot    : 30..70 C sweep, %d non-monotonic samples\n", wrapped);
    CHECK(wrapped == 0, "temperature wrapped backwards %d times", wrapped);
}

static void test_no_chip(void)
{
    bme280_data_t d;

    fake_spi_reset();
    load_calib_into_chip();
    fake_spi_set_present(false);

    CHECK(!bme280_init(), "init() claimed a chip that is not there");
    CHECK(!bme280_is_present(), "is_present() true after a failed init");
    CHECK(!bme280_measure(), "measure() true without a chip");
    CHECK(!bme280_read(&d), "read() true without a chip");

    /* Present, but it is not a BME280. */
    fake_spi_reset();
    load_calib_into_chip();
    fake_spi_set_chip_id(0x61U);
    CHECK(!bme280_init(), "init() accepted chip id 0x61");
    CHECK(!bme280_is_present(), "is_present() true for a foreign chip");

    /* BMP280 (0x58) is the part most often sold as "BME280": it must be
     * rejected, not silently read as if it had a humidity channel. */
    fake_spi_reset();
    load_calib_into_chip();
    fake_spi_set_chip_id(0x58U);
    CHECK(!bme280_init(), "init() accepted a BMP280 (id 0x58)");
    CHECK(!bme280_is_present(), "is_present() true for a BMP280");
}

/* A soft reset starts the calibration-trim copy inside the chip, which
 * runs while the bus is idle (datasheet: up to 1.5 ms).  The driver has
 * to wait real time for it: the old counted loop of back-to-back status
 * reads covered a fraction of a millisecond and reported "never
 * finished its NVM copy" on a perfectly healthy part. */
static void test_slow_nvm_copy(void)
{
    fake_spi_reset();
    load_calib_into_chip();
    fake_spi_set_im_update_reads(5);   /* 5 status reads still see im_update */

    CHECK(bme280_init(),
          "init() gave up while the NVM copy was still running");
    CHECK(fake_spi_waits >= 5,
          "init() waited only %d times for a copy that needs 5 polls",
          fake_spi_waits);
    CHECK(bme280_is_present(), "is_present() false after a slow but good init");
    CHECK(fake_reset_cmd == 0xB6U, "slow-copy path skipped the soft reset: 0x%02x",
          (unsigned)fake_reset_cmd);
    CHECK(fake_spi_bad_cmds == 0, "%d SPI commands mis-framed during the NVM wait",
          fake_spi_bad_cmds);

    /* A chip whose copy never completes must fail in bounded time: the
     * 10 s retry loop must never be able to stall the screen loop. */
    fake_spi_reset();
    load_calib_into_chip();
    fake_spi_set_im_update_reads(10000);
    CHECK(!bme280_init(),
          "init() claimed success on a chip that never cleared im_update");
    CHECK(fake_spi_waits >= 10 && fake_spi_waits <= 30,
          "the NVM wait is not bounded: %d waits", fake_spi_waits);
    CHECK(!bme280_is_present(), "is_present() true after a timed-out NVM copy");
}

/* The boot diagnostic has to be honest for a good chip and for a
 * missing one: it is only useful if it never pretends and never hangs. */
static void test_diag(void)
{
    fake_spi_reset();
    load_calib_into_chip();
    CHECK(bme280_init(), "diag fixture: init() failed");
    bme280_diag();
    CHECK(bme280_is_present(), "diag() changed the presence state");
    CHECK(fake_spi_bad_cmds == 0, "%d diag reads were mis-framed",
          fake_spi_bad_cmds);

    fake_spi_reset();
    load_calib_into_chip();
    fake_spi_set_present(false);
    CHECK(!bme280_init(), "diag fixture: init() claimed a missing chip");
    bme280_diag();
    CHECK(!bme280_is_present(), "diag() invented a chip that is not there");
}

int main(void)
{
    test_init();
    test_values();
    test_sweep();
    test_hot_sensor();
    test_no_chip();
    test_slow_nvm_copy();
    test_diag();

    printf("\n%d checks, %d failed\n", s_checks, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
