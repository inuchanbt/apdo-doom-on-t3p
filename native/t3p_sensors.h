/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_SENSORS_H
#define T3P_SENSORS_H
#include <stdint.h>
/* Addresses are seven-bit. Read returns count bytes and write returns zero
 * on success. Both return a negative error on transport failure.
 */
typedef int (*t3p_sensor_read)(void *ctx, uint8_t address, uint8_t reg,
                             uint8_t *data, unsigned count);
typedef int (*t3p_sensor_write)(void *ctx, uint8_t address, uint8_t reg, uint8_t data);
struct t3p_sensor_bus {
    t3p_sensor_read read;
    void *ctx;
    t3p_sensor_write write;
};
enum { T3P_SC7A20=0x19, T3P_BMP280=0x77, T3P_CST816D=0x15 };
struct t3p_accel_raw { int16_t x,y,z; };
struct t3p_touch_raw { uint16_t x,y; uint8_t down; };
struct t3p_pressure_raw { uint32_t pressure,temperature; };
int t3p_accel_configure(const struct t3p_sensor_bus *bus);
int t3p_accel_read(const struct t3p_sensor_bus *bus, struct t3p_accel_raw *out);
/* 0: latest sample, 1: FIFO empty, 2: backlog still draining, -1: I2C error. */
int t3p_accel_read_latest(const struct t3p_sensor_bus *bus, struct t3p_accel_raw *out);
int t3p_touch_read(const struct t3p_sensor_bus *bus, struct t3p_touch_raw *out);
int t3p_pressure_read(const struct t3p_sensor_bus *bus, struct t3p_pressure_raw *out);
#endif
