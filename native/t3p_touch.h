/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_TOUCH_H
#define T3P_TOUCH_H
#include <stdint.h>
#include "t3p_sensors.h"

struct t3p_bsp;
struct t3p_bsp_io;

struct t3p_sensor_gpio {
    const struct t3p_bsp_io *io;
    uint32_t port, sda, scl;
};

struct t3p_touch {
    struct t3p_sensor_gpio gpio;
    uint32_t last_poll_ms, keys;
    uint16_t origin_x, origin_y;
    uint8_t ready, held;
};

void t3p_touch_init(struct t3p_touch *, struct t3p_bsp *);
/* Shares the timer started by t3p_touch_init, with its own GPIO pins. */
struct t3p_sensor_bus t3p_accel_bus_init(struct t3p_sensor_gpio *, struct t3p_bsp *);
uint32_t t3p_touch_poll(struct t3p_touch *, struct t3p_bsp *);
#endif
