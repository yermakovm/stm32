/**
 * @file    face_casio.h
 * @brief   "Retro Casio" watch face: bold hours:minutes, fine seconds,
 *          a weekday + date line, a BME280 line (T / RH / P) and a
 *          slot-machine roll on the digits that change.
 */

#ifndef FACE_CASIO_H
#define FACE_CASIO_H

#include <stdbool.h>
#include <stdint.h>

#include "bme280.h"
#include "ds1307.h"

/** Boot once, then call face_draw() for every frame. */
void face_init(void);

/**
 * Draw one frame.
 *
 * @param t     current time (may be a seed value if @p time_valid is false)
 * @param time_valid false -> the RTC never answered / never started,
 *                   the face shows "--:--:--" and a hint instead.
 * @param now_ms free-running millisecond clock (board_ms()).
 * @param env    latest BME280 sample, or NULL when there is none yet.
 * @param env_valid false -> the sensor row shows "T:--.-°C RH:--% ...".
 */
void face_draw(const ds1307_time_t *t, bool time_valid, uint32_t now_ms,
               const bme280_data_t *env, bool env_valid);

/** Build the sensor line ("T:23.4°C RH:45% P:1013hPa") without drawing
 *  it. @p compact drops the two field separators, which is what keeps
 *  the widest case (-40.0°C, 100 %RH, 4-digit pressure) inside 128 px.
 *  Exposed for the host preview harness; dst must be at least 32 bytes. */
void face_build_sensor(char *dst, const bme280_data_t *env, bool valid,
                       bool compact);

#endif /* FACE_CASIO_H */
