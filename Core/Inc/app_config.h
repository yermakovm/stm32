/**
 * @file    app_config.h
 * @brief   Single place to tune hardware + UI choices for the
 *          DS1307/AT24C32 + SSD1306 "retro Casio" clock.
 *
 * Nothing here depends on CubeMX: the peripherals are configured by
 * hand in board.c / i2c_bus.c / ssd1306.c / ilog.c.
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ------------------------------------------------------------------ */
/* Debug log (USB -> TTL -> USART1)                                    */
/* ------------------------------------------------------------------ */
/* Log goes to USART1 (PA9 TX / PA10 RX) @ 115200 8N1.              */
#define LOG_BAUDRATE              115200U
/* 0 = errors only, 1 = +info, 2 = +trace (I2C/RTC chatter)         */
#define LOG_LEVEL                 2
/* Blocking TX timeout for one log line (lines are short).           */
#define LOG_TX_TIMEOUT_MS         100U

/* ------------------------------------------------------------------ */
/* I2C bus: SSD1306 + DS1307 + AT24C32 share I2C1 (PB8/PB9)         */
/* ------------------------------------------------------------------ */
#define I2C_BUS_CLK_SPEED         400000U  /* Fast mode              */
/* Blocking transfer timeout; a full 128x32 blit is ~13 ms.          */
#define I2C_XFER_TIMEOUT_MS       100U
/* After this many failed transfers in a row we try to unlock the bus */
#define I2C_RECOVER_AFTER         3U

/* 7-bit slave addresses (HAL uses 8-bit, so << 1 in i2c_bus.c)     */
#define SSD1306_I2C_ADDR7         0x3CU    /* 0x3D if SA0 tied high  */
#define RTC_I2C_ADDR7             0x68U    /* DS1307                 */
#define EEPROM_I2C_ADDR7          0x50U    /* AT24C32 (A0..A2 = 0)   */
/* BME280 is driven over SPI2 here, but the boot scan still probes the
 * matching I2C addresses: if a module ACKs here while the SPI probe
 * fails, the wiring and the driver disagree, and the log says so. */
#define BME280_I2C_ADDR_LOW       0x76U    /* SDO tied low           */
#define BME280_I2C_ADDR_HIGH      0x77U    /* SDO tied high          */

/* ------------------------------------------------------------------ */
/* BME280 temperature / pressure / humidity sensor                    */
/* ------------------------------------------------------------------ */
/* SPI2, not I2C: SPI1 (PA4..PA7) is routed to this board's onboard     */
/* SPI flash footprint.  PB13=SCK, PB14=MISO, PB15=MOSI, PB12=CS.      */
/* Sensor side (datasheet 5.2.4): CSB tied to GND = SPI mode (tied     */
/* high = I2C mode), SDO -> PB14 (MISO), SDI -> PB15 (MOSI),           */
/* SCK -> PB13.  A 4-pin module (VCC/GND/SCL/SDA only) has no CSB and  */
/* cannot be driven from SPI at all.                                   */
/* SCK = APB1 (42 MHz) / 2^prescaler: 4 -> 2.625 MHz, 5 -> 1.3125 MHz. */
/* The chip allows 10 MHz; 1.3 MHz is chosen for jumper-wire margin.   */
#define SPI_BUS_PRESCALER         5U
#define SPI_TIMEOUT_MS            100U
/* Oversampling codes: 0=skip, 1=x1, 2=x2, 3=x4, 4=x8, 5=x16.        */
/* x2/x16/x1 with the coefficient-4 IIR filter is the datasheet's     */
/* "weather monitoring" compromise: ~46 ms per cycle, low noise.      */
#define BME280_OSRS_T             0x02U
#define BME280_OSRS_P             0x05U
#define BME280_OSRS_H             0x01U
/* IIR filter on the pressure chain: 0=off, 1=2, 2=4, 3=8, 4=16.     */
#define BME280_FILTER             0x02U
/* Standby time (only used in normal mode; we run forced mode).       */
#define BME280_STANDBY_MS         0x00U    /* 0.5 ms                  */
/* One forced measurement every 500 ms -> two display/log updates per */
/* second. Must stay above the chip cycle time (~46 ms here).        */
#define SENSOR_PERIOD_MS          500U
/* Log cadence: same as the sampling by default. Set to 0 to keep the */
/* sensor line on screen but silent on USART1.                        */
#define SENSOR_LOG_PERIOD_MS      500U
/* Give up a measurement that never completes instead of hanging.     */
#define SENSOR_TIMEOUT_MS         500U
/* Re-probe a chip that is not on the bus this often (10 s is the     */
/* same rhythm the dead SSD1306 uses, so a hot plug-in works).        */
#define SENSOR_RETRY_MS           10000U

/* ------------------------------------------------------------------ */
/* SSD1306 panel                                                       */
/* ------------------------------------------------------------------ */
/* Set to 32 for the 2.23"/3.12" 128x32 modules, 64 for 0.96"       */
#define OLED_HEIGHT               32U
#define OLED_WIDTH                128U
/* Controller setup differs between the two panel sizes: the 128x32
 * module needs the *alternative* COM-pin config and a 32-row multiplex
 * ratio, the 128x64 one uses the standard 64-row setup.  MUX is the
 * register value, i.e. rows - 1. */
#if (OLED_HEIGHT == 32U)
#define OLED_MULTIPLEX            0x1FU   /* MUX = 32  */
#define OLED_COM_PINS             0x02U   /* alt COM   */
#elif (OLED_HEIGHT == 64U)
#define OLED_MULTIPLEX            0x3FU   /* MUX = 64  */
#define OLED_COM_PINS             0x12U   /* std COM   */
#else
#error "OLED_HEIGHT must be 32 or 64"
#endif
/* Remap and scan direction are a pair: both common modules wire the
 * panel so that segment remap ON (0xA1) needs reversed COM scan (0xC8).
 * Setting only one of the two mirrors every glyph left-right. */
#define OLED_SEGMENT_REMAP        0xA1U   /* column 127 -> segment 0 */
#define OLED_SCAN_DIR             0xC8U   /* COM scan reversed */
#define OLED_CONTRAST             0x8FU

/* ------------------------------------------------------------------ */
/* RTC                                                                 */
/* ------------------------------------------------------------------ */
/* DS1307 register 0x07 (control): bit7 OUT, bit4 SQWEN, bits1:0 RS.
 * RTC_SQW_CONTROL_OFF = square wave off, RTC_SQW_CONTROL_1HZ = 1 Hz on
 * the SQW pin. Unused here (we poll the clock), so keep it off -> less
 * I2C traffic, nothing extra to wire. */
#define RTC_SQW_CONTROL_OFF       0x00U
#define RTC_SQW_CONTROL_1HZ       0x10U
/* Set 1 to force the time from the PC build stamp on first boot /
 * when the RTC lost its battery. Otherwise the time is restored from
 * the AT24C32 backup and, failing that, from the build stamp.       */
#define RTC_SEED_FROM_BUILD_STAMP 1

/* ------------------------------------------------------------------ */
/* EEPROM-backed time backup (AT24C32)                                 */
/* ------------------------------------------------------------------ */
/* We store the last known good time + a "valid" magic so that after a
 * battery swap we can at least show "time since backup" instead of a
 * bogus 2000-01-01. Sector 0 of the EEPROM is used.                 */
#define TIME_BACKUP_EEPROM_ADDR   0x0000U

/* ------------------------------------------------------------------ */
/* UI / animation                                                      */
/* ------------------------------------------------------------------ */
/* Screen refresh period: the requirement is "once per second", we     */
/* redraw at 30 fps so animations look smooth and the clock is always */
/* in sync within one frame. Set to 1000 for a strictly 1 s refresh. */
#define UI_FRAME_MS               33U
/* Sensor row: 0 = hidden, 1 = always shown (128x32 shrinks the big   */
/* digits from 20 to 15 px to make room for it). The line itself is   */
/* "T:23.4°C RH:45% P:1013hPa" in the 4x6 font.                      */
#define UI_SHOW_SENSOR            1
/* How long an "update animation" lasts after the seconds tick over.  */
#define ANIM_DURATION_MS          600U
/* Animation recipe: 0 = static face, 1 = digit roll,
 * 2 = digit roll + colon pulse.                                       */
#define ANIM_RECIPE               2
/* 1 = show a thin separator line under the clock row.                */
#define UI_DRAW_BEZEL             1
/* Retry the SSD1306 init this often after the panel stopped answering,
 * so a flaky connection does not spam the log every 33 ms.           */
#define SCREEN_RETRY_MS           10000U

#endif /* APP_CONFIG_H */