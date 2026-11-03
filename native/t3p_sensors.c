/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Register layout recovered from stock 3.0.3. Physical device untested. */
#include "t3p_sensors.h"

static int read_exact(const struct t3p_sensor_bus *bus, unsigned address,
                      unsigned reg, uint8_t *data, unsigned count)
{
    if (!bus || !bus->read) return -1;
    return bus->read(bus->ctx,(uint8_t)address,(uint8_t)reg,data,count)==(int)count ? 0 : -1;
}
static int16_t signed12(const uint8_t *p)
{
    unsigned value = ((unsigned)p[1]<<8 | p[0]) >> 4;
    return (int16_t)(value & 0x800 ? (int)value-4096 : (int)value);
}
int t3p_accel_read(const struct t3p_sensor_bus *bus, struct t3p_accel_raw *out)
{
    uint8_t data[6];
    if (!out || read_exact(bus,T3P_SC7A20,0xa8,data,6)) return -1;
    out->x=signed12(data); out->y=signed12(data+2); out->z=signed12(data+4);
    return 0;
}
int t3p_touch_read(const struct t3p_sensor_bus *bus, struct t3p_touch_raw *out)
{
    uint8_t data[5];
    if (!out) return -1;
    out->down=0;
    if (read_exact(bus,T3P_CST816D,2,data,5)) return -1;
    /* More than one contact is unsupported; event bits in high nibbles are
     * excluded just as in the stock touch task's 12-bit coordinate decoder.
     */
    if ((data[0]&15)>1) return -1;
    out->down=(data[0]&15)==1;
    out->x=(uint16_t)((data[1]&15)<<8 | data[2]);
    out->y=(uint16_t)((data[3]&15)<<8 | data[4]);
    return 0;
}
int t3p_pressure_read(const struct t3p_sensor_bus *bus, struct t3p_pressure_raw *out)
{
    uint8_t data[6];
    if (!out || read_exact(bus,T3P_BMP280,0xf7,data,6)) return -1;
    uint32_t pressure=(uint32_t)data[0]<<12 | (uint32_t)data[1]<<4 | data[2]>>4;
    uint32_t temperature=(uint32_t)data[3]<<12 | (uint32_t)data[4]<<4 | data[5]>>4;
    if (pressure==0x80000 || temperature==0x80000) return -1; /* skipped measurement */
    out->pressure=pressure; out->temperature=temperature;
    return 0; /* Raw ADC counts, NOT pascals or degrees; calibration is separate. */
}
