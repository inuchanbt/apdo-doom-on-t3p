/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_SENSORS_H
#define T3P_SENSORS_H
#include <stdint.h>
/* Reads only. Transport must return exactly count bytes, or a negative error.
 * Addresses are SEVEN-bit, unlike the stock wrappers' on-wire addresses.
 * These helpers assume the sensor was already configured. They do not replace
 * board initialization, clock setup or a real I2C implementation.
 */
typedef int (*t3p_sensor_read)(void *ctx, uint8_t address, uint8_t reg,
                             uint8_t *data, unsigned count);
struct t3p_sensor_bus { t3p_sensor_read read; void *ctx; };
enum { T3P_SC7A20=0x19, T3P_BMP280=0x77, T3P_CST816D=0x15 };
struct t3p_accel_raw { int16_t x,y,z; };
struct t3p_touch_raw { uint16_t x,y; uint8_t down; };
struct t3p_pressure_raw { uint32_t pressure,temperature; };
int t3p_accel_read(const struct t3p_sensor_bus *bus, struct t3p_accel_raw *out);
int t3p_touch_read(const struct t3p_sensor_bus *bus, struct t3p_touch_raw *out);
int t3p_pressure_read(const struct t3p_sensor_bus *bus, struct t3p_pressure_raw *out);
#endif
