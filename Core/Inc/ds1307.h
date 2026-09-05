/**
 * @file    ds1307.h
 * @brief   DS1307 RTC driver (I2C address 0x68, BCD registers 0x00-0x06
 *          plus the control register at 0x07).
 */

#ifndef DS1307_H
#define DS1307_H

#include <stdbool.h>
#include <stdint.h>

/** Decoded clock/calendar contents (24 h mode assumed). */
typedef struct {
    uint8_t hour;      /* 0..23  */
    uint8_t minute;    /* 0..59  */
    uint8_t second;    /* 0..59  */
    uint8_t day;       /* 1..7, Sunday = 1 */
    uint8_t date;      /* 1..31  */
    uint8_t month;     /* 1..12  */
    uint8_t year;      /* 0..99   */
    bool oscillator_halted;   /* true -> clock stopped, time is junk  */
    bool pm;                  /* set only in 12 h mode                */
} ds1307_time_t;

/** Read the clock. @return false if the RTC did not answer. */
bool ds1307_read(ds1307_time_t *t);

/** Write the clock (always programmed into 24 h mode, oscillator
 *  started). @p t.second is written as-is. */
bool ds1307_write(const ds1307_time_t *t);

/** Square-wave output: true = 1 Hz on the SQW pin, false = off.
 *  Unused in this build (we poll), kept for completeness. */
bool ds1307_set_square_wave(bool enable);

/** True when the clock is running (CH bit clear and 24 h mode). */
bool ds1307_is_running(const ds1307_time_t *t);

/** Seconds since midnight, useful for change detection. */
uint32_t ds1307_seconds_of_day(const ds1307_time_t *t);

#endif /* DS1307_H */
