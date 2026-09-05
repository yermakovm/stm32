/**
 * @file    face_casio.h
 * @brief   "Retro Casio" watch face: bold hours:minutes, fine seconds
 *          and a weekday + date line, with a slot-machine roll on the
 *          digits that change.
 */

#ifndef FACE_CASIO_H
#define FACE_CASIO_H

#include <stdbool.h>
#include <stdint.h>

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
 */
void face_draw(const ds1307_time_t *t, bool time_valid, uint32_t now_ms);

#endif /* FACE_CASIO_H */
