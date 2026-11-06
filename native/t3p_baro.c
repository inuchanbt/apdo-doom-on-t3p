/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_baro.h"
#include "t3p_sensors.h"

enum {
    SAMPLE_MS = 250, CONVERSION_MS = 32, RETRY_MS = 1000,
    UPPER_DECK_DM = 30, UPPER_DECK_SAMPLES = 9,
    BMP280_ID = 0xd0, BMP280_CALIBRATION = 0x88, BMP280_STATUS = 0xf3,
    BMP280_CTRL_MEAS = 0xf4, BMP280_FORCED_T2_P8 = 0x51
};

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | (unsigned)bytes[1] << 8);
}

static int16_t read_signed_le16(const uint8_t *bytes)
{
    unsigned word = read_le16(bytes);
    return (int16_t)(word < 0x8000 ? (int)word : (int)word - 0x10000);
}

static int read_registers(const struct t3p_sensor_bus *bus, uint8_t reg,
                          uint8_t *data, unsigned count)
{
    return bus->read(bus->ctx, T3P_BMP280, reg, data, count) == (int)count;
}

static enum t3p_baro_status configure(struct t3p_baro *baro, const struct t3p_sensor_bus *bus)
{
    uint8_t id, status, trim[24];
    if (!bus || !bus->read || !bus->write || !read_registers(bus, BMP280_ID, &id, 1))
        return T3P_BARO_READ_ERROR;
    if (id != 0x58)
        return T3P_BARO_UNSUPPORTED;
    if (!read_registers(bus, BMP280_STATUS, &status, 1))
        return T3P_BARO_READ_ERROR;
    if (status & 1u) /* Calibration NVM is still being copied. */
        return T3P_BARO_WAITING;
    if (!read_registers(bus, BMP280_CALIBRATION, trim, sizeof(trim)))
        return T3P_BARO_READ_ERROR;

    baro->t1 = read_le16(trim);
    baro->p1 = read_le16(trim + 6);
    if (!baro->t1 || baro->t1 == 0xffff || !baro->p1 || baro->p1 == 0xffff)
        return T3P_BARO_READ_ERROR;
    baro->t2 = read_signed_le16(trim + 2);
    baro->t3 = read_signed_le16(trim + 4);
    baro->p2 = read_signed_le16(trim + 8);
    baro->p3 = read_signed_le16(trim + 10);
    baro->p4 = read_signed_le16(trim + 12);
    baro->p5 = read_signed_le16(trim + 14);
    baro->p6 = read_signed_le16(trim + 16);
    baro->p7 = read_signed_le16(trim + 18);
    baro->p8 = read_signed_le16(trim + 20);
    baro->p9 = read_signed_le16(trim + 22);
    if (bus->write(bus->ctx, T3P_BMP280, BMP280_CTRL_MEAS, 0))
        return T3P_BARO_READ_ERROR;
    baro->configured = 1;
    return T3P_BARO_WAITING;
}

static uint32_t compensated_pa(const struct t3p_baro *baro, struct t3p_pressure_raw raw)
{
    /* Fixed-point compensation from the BMP280 data sheet, section 3.11.3.
     * Range checks reject invalid readings and bound products from bad trim. */
    int64_t temp_delta = (int64_t)(raw.temperature >> 4) - baro->t1;
    int64_t linear_temp = ((2 * temp_delta + ((raw.temperature >> 3) & 1u)) * baro->t2) >> 11;
    int64_t fine_temp = linear_temp + (((temp_delta * temp_delta >> 12) * baro->t3) >> 14);
    int64_t temp_centi = (5 * fine_temp + 128) >> 8;
    if (temp_centi < -4000 || temp_centi > 8500)
        return 0;

    int64_t delta = fine_temp - 128000;
    int64_t squared = delta * delta;
    int64_t offset = squared * baro->p6 + delta * baro->p5 * (1LL << 17)
                   + baro->p4 * (1LL << 35);
    int64_t gain = (1LL << 47) + ((squared * baro->p3) >> 8)
                 + delta * baro->p2 * (1LL << 12);
    /* The shift before multiplying retains 25 bits of fractional precision. */
    int64_t divisor = ((gain >> 8) * baro->p1) >> 25;
    int64_t numerator = ((1048576LL - raw.pressure) << 31) - offset;
    if (divisor <= 0 || numerator <= 0 || numerator > INT64_MAX / 3125)
        return 0;

    int64_t scaled = numerator * 3125 / divisor;
    if (scaled > (1LL << 35))
        return 0;
    int64_t quadratic = scaled >> 13;
    int64_t correction = (baro->p9 * quadratic * quadratic >> 25)
                       + (baro->p8 * scaled >> 19);
    int64_t pressure_pa = (((scaled + correction) >> 8) + baro->p7 * 16 + 128) >> 8;
    return pressure_pa >= 30000 && pressure_pa <= 110000 ? (uint32_t)pressure_pa : 0;
}

static void reset_filter(struct t3p_baro *baro)
{
    baro->sum_pa = 0;
    baro->sample_count = baro->sample_index = baro->high_samples = 0;
}

static void update_reading(struct t3p_baro *baro, uint32_t pressure_pa, uint32_t now)
{
    if (now - baro->last_valid_ms > RETRY_MS) {
        reset_filter(baro);
        baro->reading.status = T3P_BARO_WAITING;
    }
    baro->last_valid_ms = now;

    if (baro->sample_count == T3P_BARO_FILTER_SAMPLES)
        baro->sum_pa -= baro->samples_pa[baro->sample_index];
    else
        ++baro->sample_count;
    baro->samples_pa[baro->sample_index] = pressure_pa;
    baro->sum_pa += pressure_pa;
    baro->sample_index = (baro->sample_index + 1) % T3P_BARO_FILTER_SAMPLES;
    if (baro->sample_count < T3P_BARO_FILTER_SAMPLES)
        return;

    uint32_t filtered_pa = (baro->sum_pa + T3P_BARO_FILTER_SAMPLES / 2) / T3P_BARO_FILTER_SAMPLES;
    if (!baro->baseline_pa)
        baro->baseline_pa = filtered_pa;
    /* Local linearization of the barometric formula, with 8430 m scale height. */
    int64_t delta_pa = (int64_t)baro->baseline_pa - filtered_pa;
    int32_t height_dm = (int32_t)(delta_pa * 84300 / baro->baseline_pa);
    baro->reading.pressure_pa = (int32_t)filtered_pa;
    baro->reading.altitude_dm = height_dm;
    baro->reading.status = T3P_BARO_READY;
    if (height_dm < UPPER_DECK_DM)
        baro->high_samples = 0;
    else if (!baro->reading.upper_deck && ++baro->high_samples == UPPER_DECK_SAMPLES)
        baro->reading.upper_deck = 1;
}

void t3p_baro_init(struct t3p_baro *baro, uint32_t now)
{
    *baro = (struct t3p_baro){.last_start_ms = now - RETRY_MS, .last_valid_ms = now};
}

void t3p_baro_poll(struct t3p_baro *baro, const struct t3p_sensor_bus *bus, uint32_t now)
{
    uint32_t elapsed = now - baro->last_start_ms;
    if (!baro->configured) {
        if (baro->reading.status != T3P_BARO_UNSUPPORTED && elapsed >= RETRY_MS) {
            baro->last_start_ms = now;
            baro->reading.status = configure(baro, bus);
        }
        return;
    }

    if (baro->measuring && elapsed >= CONVERSION_MS) {
        struct t3p_pressure_raw raw;
        baro->measuring = 0;
        uint32_t pressure = t3p_pressure_read(bus, &raw) ? 0 : compensated_pa(baro, raw);
        if (!pressure)
            goto read_error;
        update_reading(baro, pressure, now);
    }
    if (elapsed >= SAMPLE_MS) {
        baro->last_start_ms = now;
        if (bus->write(bus->ctx, T3P_BMP280, BMP280_CTRL_MEAS, BMP280_FORCED_T2_P8))
            goto read_error;
        baro->measuring = 1;
    }
    return;

read_error:
    baro->last_start_ms = now;
    baro->configured = 0;
    baro->reading.status = T3P_BARO_READ_ERROR;
    reset_filter(baro);
}
