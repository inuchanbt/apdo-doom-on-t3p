/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_TILT_H
#define T3P_TILT_H
#include "t3p_input.h"
#include "t3p_sensors.h"

struct t3p_tilt {
    struct t3p_input filter;
    struct t3p_accel_raw reference, latest;
    int16_t sum_x, sum_y, sum_z;
    int gravity;
    uint32_t last_poll_ms, last_sample_ms, keys;
    uint8_t samples, ready, have_sample;
};

void t3p_tilt_init(struct t3p_tilt *, const struct t3p_sensor_bus *);
uint32_t t3p_tilt_poll(struct t3p_tilt *, const struct t3p_sensor_bus *, uint32_t now,
                      int paused);
#endif
