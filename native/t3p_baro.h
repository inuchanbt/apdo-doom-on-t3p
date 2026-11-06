/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_BARO_H
#define T3P_BARO_H
#include <stdint.h>

struct t3p_sensor_bus;

enum t3p_baro_status {
    T3P_BARO_WAITING,
    T3P_BARO_READY,
    T3P_BARO_READ_ERROR,
    T3P_BARO_UNSUPPORTED
};

struct t3p_uac_reading {
    enum t3p_baro_status status;
    int32_t pressure_pa;
    int32_t altitude_dm;
    uint8_t upper_deck;
};

enum { T3P_BARO_FILTER_SAMPLES = 8 };

struct t3p_baro {
    struct t3p_uac_reading reading;
    uint32_t last_start_ms, last_valid_ms, baseline_pa, sum_pa;
    uint32_t samples_pa[T3P_BARO_FILTER_SAMPLES];
    uint16_t t1, p1;
    int16_t t2, t3, p2, p3, p4, p5, p6, p7, p8, p9;
    uint8_t configured, measuring, sample_count, sample_index, high_samples;
};

void t3p_baro_init(struct t3p_baro *baro, uint32_t now);
void t3p_baro_poll(struct t3p_baro *baro, const struct t3p_sensor_bus *bus, uint32_t now);
const struct t3p_uac_reading *km_uac_reading(void);
#endif
