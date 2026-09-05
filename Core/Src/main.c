/**
 * Casio-style desk clock.
 *
 *   STM32F401CEU6
 *     USART1 PA9/PA10 @115200 -> USB-TTL for the log
 *     I2C1 PB8 (SCL) / PB9 (SDA) -> SSD1306 0x3C, DS1307 0x68,
 *                                    AT24C32 0x50
 *
 * Nothing else is attached: no button, no ESP32 link, no status LED.
 * The screen is refreshed once per second (plus one animated frame
 * every UI_FRAME_MS so the roll/spinner look smooth).
 */

#include <string.h>

#include "at24.h"
#include "board.h"
#include "ds1307.h"
#include "face_casio.h"
#include "gfx.h"
#include "i2c_bus.h"
#include "ilog.h"
#include "ssd1306.h"

#define BACKUP_MAGIC 0xC5U

/* Time shown on screen, plus how it was obtained. */
static ds1307_time_t s_time;
static bool s_time_valid;
static bool s_time_is_backup;
static uint32_t s_last_rtc_read_ms;
static uint32_t s_last_backup_min = 0xFFFFFFFFU;

/* Last time we actually believed, and when: if the RTC stops answering
 * the face keeps counting from here instead of freezing. */
static ds1307_time_t s_seed;
static uint32_t s_seed_ms;

/* ------------------------------------------------------------------ */
/* time seeding                                                        */
/* ------------------------------------------------------------------ */

static const char MONTH_ABBR[12][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

/* Sakamoto's algorithm: 0 = Sunday ... 6 = Saturday. */
static uint8_t day_of_week(uint16_t y, uint8_t m, uint8_t d)
{
    static const uint8_t T[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    uint16_t year = y;

    if (m < 3U) {
        year--;
    }
    return (uint8_t)((year + year / 4U - year / 100U + year / 400U +
                      T[m - 1U] + d) % 7U);
}

/* Fill t from the build stamp. The caller must pass a zeroed struct:
 * an unknown month abbreviation leaves month == 0 and the function
 * returns false. */
static bool parse_build_stamp(ds1307_time_t *t)
{
    /* __DATE__ = "Sep  4 2026", __TIME__ = "21:23:45". */
    const char *d = __DATE__;
    const char *tm = __TIME__;

    for (uint8_t i = 0U; i < 12U; i++) {
        if (memcmp(d, MONTH_ABBR[i], 3U) == 0) {
            t->month = (uint8_t)(i + 1U);
            break;
        }
    }
    if (t->month == 0U) {
        return false;
    }

    t->date = (uint8_t)((d[4] == ' ' ? 0U : (d[4] - '0') * 10U) +
                        (d[5] - '0'));
    /* __DATE__ is e.g. "Sep  5 2026": day is right-aligned at d[4..5],
     * the year is the LAST two chars, at d[9..10]. */
    t->year = (uint8_t)(((d[9] - '0') * 10U) + (d[10] - '0'));
    t->hour = (uint8_t)((tm[0] - '0') * 10U + (tm[1] - '0'));
    t->minute = (uint8_t)((tm[3] - '0') * 10U + (tm[4] - '0'));
    t->second = (uint8_t)((tm[6] - '0') * 10U + (tm[7] - '0'));
    t->day = (uint8_t)(day_of_week(2000U + t->year, t->month, t->date) + 1U);
    t->pm = false;
    t->oscillator_halted = false;
    return true;
}

/* Days in month (1..12) of the given 2-digit year. Only exact for
 * 1970..2099, which is all a build stamp can ever mean here. */
static uint8_t days_in_month(uint8_t month, uint8_t year)
{
    static const uint8_t dm[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };

    if (month < 1U || month > 12U) {
        return 30U;
    }
    if (month == 2U && (year % 4U) == 0U) {
        return 29U;
    }
    return dm[month - 1U];
}

/* Rough seconds since 2000-01-01 from a 2-digit-year time (only
 * meaningful within 2000..2099, which is all we can represent).
 * Used only to compare which of two times is newer, so a possible
 * one-day skew around 29 Feb does not matter. */
static uint64_t seconds_since_2000(const ds1307_time_t *t)
{
    static const uint16_t CUM_DAYS[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
    };
    const uint64_t y = t->year;
    uint64_t days = (y * 365ULL) + ((y == 0U) ? 0U : ((y + 3U) / 4U));

    days += CUM_DAYS[(t->month >= 1U && t->month <= 12U) ? t->month - 1U
                                                           : 0U];
    days += (t->date >= 1U) ? (t->date - 1U) : 0U;

    return (days * 86400ULL) +
           (uint64_t)ds1307_seconds_of_day(t);
}

/* Add seconds to a time, rolling over into the next day/month/year.
 * Used to derive the displayed time from the seed while the RTC is
 * unreachable, so a few days of drift are tolerated rather than the
 * clock standing still. */
static void advance_time(ds1307_time_t *t, uint32_t seconds)
{
    uint32_t sod = ds1307_seconds_of_day(t) + seconds;

    while (sod >= 86400U) {
        uint8_t dim;

        sod -= 86400U;
        dim = days_in_month(t->month, t->year);
        if (t->date >= dim) {
            t->date = 1U;
            if (t->month >= 12U) {
                t->month = 1U;
                t->year = (uint8_t)(t->year + 1U);
            } else {
                t->month = (uint8_t)(t->month + 1U);
            }
        } else {
            t->date = (uint8_t)(t->date + 1U);
        }
        t->day = (uint8_t)((t->day == 0U ? 1U : t->day) % 7U) + 1U;
    }

    t->hour = (uint8_t)(sod / 3600U);
    t->minute = (uint8_t)((sod % 3600U) / 60U);
    t->second = (uint8_t)(sod % 60U);
}

static void seed_time(const ds1307_time_t *t, uint32_t at_ms)
{
    s_seed = *t;
    s_seed_ms = at_ms;
}

static void derive_from_seed(uint32_t now_ms)
{
    s_time = s_seed;
    advance_time(&s_time, (uint32_t)(now_ms - s_seed_ms) / 1000U);
}

/* EEPROM layout at TIME_BACKUP_EEPROM_ADDR:
 * [0]=magic, [1]=hour, [2]=minute, [3]=second, [4]=day,
 * [5]=date, [6]=month, [7]=year. */
static bool backup_load(ds1307_time_t *t)
{
    uint8_t blob[8];

    if (!at24_read(TIME_BACKUP_EEPROM_ADDR, blob, sizeof(blob))) {
        return false;
    }
    if (blob[0] != BACKUP_MAGIC) {
        return false;
    }

    t->hour = blob[1];
    t->minute = blob[2];
    t->second = blob[3];
    t->day = blob[4];
    t->date = blob[5];
    t->month = blob[6];
    t->year = blob[7];
    t->day = (t->day >= 1U && t->day <= 7U) ? t->day : 1U;
    t->hour %= 24U;
    t->minute %= 60U;
    t->second %= 60U;
    t->pm = false;
    t->oscillator_halted = false;
    return true;
}

static void backup_save(const ds1307_time_t *t)
{
    uint8_t blob[8] = {
        BACKUP_MAGIC, t->hour, t->minute, t->second,
        t->day, t->date, t->month, t->year,
    };

    if (!at24_write(TIME_BACKUP_EEPROM_ADDR, blob, sizeof(blob))) {
        LOGE("eeprom: could not save time backup");
    }
}

/* One-line summary of the time we actually show, plus where it came
 * from, so the boot log makes the setup obvious. */
static void log_clock_state(const char *phase)
{
    static const char DAY_ABBR[7][4] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
    };
    const uint8_t dow = (s_time.day >= 1U && s_time.day <= 7U)
                            ? s_time.day : 1U;

    LOGI("clock: %s -> %u-%02u-%02u (%s) %02u:%02u:%02u [%s]",
         phase,
         2000U + (unsigned int)s_time.year, (unsigned int)s_time.month,
         (unsigned int)s_time.date, DAY_ABBR[dow - 1U],
         (unsigned int)s_time.hour, (unsigned int)s_time.minute,
         (unsigned int)s_time.second,
         !s_time_valid ? "INVALID, no source"
                       : (s_time_is_backup
                              ? "seeded, RTC not trusted"
                              : "RTC"));
}

/* Get a usable time: prefer the RTC, then the build stamp (freshest),
 * then the EEPROM backup (may be stale). Whatever we end up with
 * becomes the seed, so the clock keeps counting even when nothing
 * answers on the bus. */
static void acquire_time(void)
{
    ds1307_time_t t = {0};
    const uint32_t now = board_ms();

    if (!ds1307_read(&t)) {
        if (!s_time_valid) {
            /* Build stamp first: it is the compile-time clock, which is
             * newer by definition than a backup saved during an earlier
             * run (the backup just replays whatever we last believed,
             * including a stale seed). */
            if (!parse_build_stamp(&t) && !backup_load(&t)) {
                LOGE("clock: no time source available");
                return;
            }
            seed_time(&t, now);
            LOGI("clock: RTC never answered, running from the "
                 "backup/build stamp");
            s_time_is_backup = true;
        } else if (!s_time_is_backup) {
            LOGE("rtc: DS1307 stopped answering, counting from "
                 "the last known time");
            s_time_is_backup = true;
        }
        derive_from_seed(now);
        s_time_valid = true;
        return;
    }

    if (t.oscillator_halted) {
        LOGI("rtc: oscillator stopped, seeding from build stamp/backup");
        if (parse_build_stamp(&t)) {
            LOGI("clock: seeding from build stamp %s %s", __DATE__, __TIME__);
        } else if (backup_load(&t)) {
            LOGI("clock: no build stamp usable, restored last saved time");
        } else {
            LOGE("clock: no time source available");
            s_time_valid = false;
            return;
        }
        if (ds1307_write(&t)) {
            LOGI("clock: wrote seed time to DS1307");
        } else {
            LOGE("rtc: seed write failed, showing seed anyway");
            /* The seed is still the best guess, but it is not ticking,
             * so derive it from board_ms() instead of the RTC. */
            seed_time(&t, now);
            s_time_is_backup = true;
            derive_from_seed(now);
            s_time_valid = true;
            return;
        }
    } else if (RTC_SEED_FROM_BUILD_STAMP) {
        /* The RTC may be running but stuck on an old seed (e.g. a
         * previous flash wrote a stale build stamp or backup). The
         * build stamp is the compile time of THIS image, so if it
         * is newer than what the RTC believes, re-seed the RTC. */
        ds1307_time_t stamp = {0};

        if (parse_build_stamp(&stamp) &&
            seconds_since_2000(&stamp) > seconds_since_2000(&t)) {
            LOGI("rtc: time older than build stamp, re-seeding");
            if (ds1307_write(&stamp)) {
                t = stamp;
            } else {
                LOGE("rtc: re-seed write failed, keeping RTC time");
            }
        }
        s_time_is_backup = false;
    }

    s_time = t;
    s_time_valid = true;
    seed_time(&t, now);

    /* One EEPROM write per minute at most: keeps the 1 M cycle budget
     * comfortable and still survives a battery swap. */
    if (t.second == 0U && s_last_backup_min != t.minute) {
        s_last_backup_min = t.minute;
        backup_save(&t);
    }
}

/* ------------------------------------------------------------------ */
/* boot + main loop                                                    */
/* ------------------------------------------------------------------ */

static void boot(void)
{
    if (!i2c_bus_init()) {
        board_fatal("i2c_bus_init");
    }
    LOGI("i2c1 ready: PB8=SCL, PB9=SDA @%lu Hz",
         (unsigned long)I2C_BUS_CLK_SPEED);
    i2c_bus_scan();

    acquire_time();
    log_clock_state("boot");

    if (!ssd1306_init()) {
        LOGE("display: no SSD1306 at 0x%02x - showing nothing",
             SSD1306_I2C_ADDR7);
    }
    face_init();

    /* Push the very first frame even if the time came from the seed. */
    face_draw(&s_time, s_time_valid, board_ms());
    if (!ssd1306_flush()) {
        LOGE("display: first frame never made it to the panel");
    }
}

int main(void)
{
    uint32_t next_frame;
    uint32_t next_screen_try;

    board_init();
    log_init();
    log_str("\r\n===== STM32F401 + DS1307 + SSD1306 =====\r\n");
    boot();

    next_frame = board_ms() + UI_FRAME_MS;
    next_screen_try = board_ms() + SCREEN_RETRY_MS;
    for (;;) {
        uint32_t now = board_ms();

        /* Poll the clock once per second: the seconds digit changes
         * right after that read, which is what triggers the animation. */
        if (now - s_last_rtc_read_ms >= 1000U) {
            s_last_rtc_read_ms = now;
            acquire_time();
        }

        if (ssd1306_is_alive()) {
            face_draw(&s_time, s_time_valid, now);
            if (!ssd1306_flush()) {
                LOGE("display: refresh failed, retrying in %u ms",
                     (unsigned)SCREEN_RETRY_MS);
            }
        } else if (now >= next_screen_try) {
            next_screen_try = now + SCREEN_RETRY_MS;
            if (ssd1306_init()) {
                LOGI("display: panel answered again");
                face_init();
            } else {
                LOGE("display: still no SSD1306 at 0x%02x",
                     SSD1306_I2C_ADDR7);
            }
        }

        /* Keep the frame rate steady instead of spinning on the bus. */
        while (board_ms() < next_frame) {
            HAL_Delay(2);
        }
        next_frame += UI_FRAME_MS;
        if ((int32_t)(next_frame - board_ms()) <= 0) {
            next_frame = board_ms() + UI_FRAME_MS;  /* missed a frame */
        }
    }
}
