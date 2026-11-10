/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "t3p_baro.h"
#include "t3p_sensors.h"

enum { BASE_ADC_PRESSURE = 415148, BASE_ADC_TEMPERATURE = 519888 };

struct fake_baro {
    uint32_t adc_pressure, adc_temperature;
    unsigned conversions, data_reads, sleep_writes;
    uint8_t id, nvm_busy, no_ack, bad_trim, conversion_pending;
};

static void put_adc(uint8_t *bytes, uint32_t count)
{
    bytes[0] = (uint8_t)(count >> 12);
    bytes[1] = (uint8_t)(count >> 4);
    bytes[2] = (uint8_t)(count << 4);
}

static int read_sensor(void *context, uint8_t address, uint8_t reg,
                       uint8_t *data, unsigned count)
{
    struct fake_baro *fake = context;
    if (address == 0x20)
        return -1;
    assert(address == T3P_BMP280);
    if (fake->no_ack)
        return -1;
    switch (reg) {
    case 0xd0:
        assert(count == 1);
        data[0] = fake->id;
        break;
    case 0xf3:
        assert(count == 1);
        data[0] = fake->nvm_busy;
        break;
    case 0x88: {
        static const int coefficients[] = {
            27504, 26435, -1000, 36477, -10685, 3024,
            2855, 140, -7, 15500, -14600, 6000
        };
        assert(count == sizeof(coefficients) / sizeof(coefficients[0]) * 2);
        for (unsigned i = 0; i < count / 2; ++i) {
            unsigned value = fake->bad_trim ? 0xffff : (uint16_t)coefficients[i];
            data[2 * i] = (uint8_t)value;
            data[2 * i + 1] = (uint8_t)(value >> 8);
        }
        break;
    }
    default:
        assert(reg == 0xf7 && count == 6 && fake->conversion_pending);
        fake->conversion_pending = 0;
        ++fake->data_reads;
        put_adc(data, fake->adc_pressure);
        put_adc(data + 3, fake->adc_temperature);
    }
    return (int)count;
}

static int write_sensor(void *context, uint8_t address, uint8_t reg, uint8_t value)
{
    struct fake_baro *fake = context;
    assert(address == T3P_BMP280 && reg == 0xf4);
    if (fake->no_ack)
        return -1;
    if (value == 0) {
        fake->conversion_pending = 0;
        ++fake->sleep_writes;
    } else {
        assert(value == 0x51 && !fake->conversion_pending);
        fake->conversion_pending = 1;
        ++fake->conversions;
    }
    return 0;
}

static void sample(struct t3p_baro *baro, const struct t3p_sensor_bus *bus, uint32_t *start)
{
    *start += 250;
    t3p_baro_poll(baro, bus, *start);
    t3p_baro_poll(baro, bus, *start + 32);
}

struct fake_alternate {
    uint32_t counts;
    unsigned writes;
    uint8_t ready;
};

static int read_alternate(void *context, uint8_t address, uint8_t reg,
                          uint8_t *data, unsigned count)
{
    struct fake_alternate *fake = context;
    if (address == T3P_BMP280)
        return -1;
    assert(address == 0x20 && count == 1);
    if (reg == 0x14) {
        data[0] = fake->ready ? 0x02 : 0;
    } else {
        assert(fake->ready && fake->writes == 6 && reg >= 0x17 && reg <= 0x19);
        data[0] = (uint8_t)(fake->counts >> ((reg - 0x17) * 8));
    }
    return 1;
}

static int write_alternate(void *context, uint8_t address, uint8_t reg, uint8_t value)
{
    static const uint8_t sequence[][2] = {
        {0x06, 0x08}, {0x06, 0x80}, {0x07, 0x08}, {0x08, 0}, {0x09, 0x22}, {0x06, 0x13}
    };
    struct fake_alternate *fake = context;
    assert(address == 0x20 && fake->writes < 6);
    assert(reg == sequence[fake->writes][0] && value == sequence[fake->writes][1]);
    ++fake->writes;
    return 0;
}

static void check_alternate(void)
{
    struct fake_alternate fake = {.counts = 100653u * 64 + 10};
    const struct t3p_sensor_bus bus = {read_alternate, &fake, write_alternate};
    struct t3p_baro baro;
    t3p_baro_init(&baro, 0);
    t3p_baro_poll(&baro, &bus, 0);
    assert(fake.writes == 1);
    t3p_baro_poll(&baro, &bus, 249);
    assert(fake.writes == 1);
    t3p_baro_poll(&baro, &bus, 250);
    assert(fake.writes == 2);
    t3p_baro_poll(&baro, &bus, 500);
    t3p_baro_poll(&baro, &bus, 750);
    assert(fake.writes == 6 && baro.reading.status == T3P_BARO_WAITING);
    fake.ready = 1;
    for (uint32_t now = 1000; now <= 2750; now += 250)
        t3p_baro_poll(&baro, &bus, now);
    assert(baro.reading.status == T3P_BARO_READY && baro.reading.sensor_address == 0x20);
    assert(baro.reading.pressure_pa == 100653 && baro.reading.altitude_dm == 0);
    fake.ready = 0;
    t3p_baro_poll(&baro, &bus, 3000);
    assert(baro.reading.status == T3P_BARO_READY);
    t3p_baro_poll(&baro, &bus, 4000);
    assert(baro.reading.status == T3P_BARO_READ_ERROR && !baro.alternate_stage);
    assert(baro.reading.diagnostic);
    puts("PASS: stock alternate barometer setup, ready bit, Pa conversion and timeout");
}

int main(void)
{
    struct fake_baro fake = {
        .adc_pressure = BASE_ADC_PRESSURE, .adc_temperature = BASE_ADC_TEMPERATURE, .id = 0x58
    };
    const struct t3p_sensor_bus bus = {read_sensor, &fake, write_sensor};
    struct t3p_baro baro;
    uint32_t start = UINT32_MAX - 100;
    t3p_baro_init(&baro, start);
    t3p_baro_poll(&baro, &bus, start);
    assert(baro.reading.status == T3P_BARO_WAITING && fake.sleep_writes == 1);
    t3p_baro_poll(&baro, &bus, start + 249);
    assert(fake.conversions == 0);
    t3p_baro_poll(&baro, &bus, start + 250);
    t3p_baro_poll(&baro, &bus, start + 281);
    assert(fake.conversions == 1 && fake.data_reads == 0);
    start += 250;
    t3p_baro_poll(&baro, &bus, start + 32);
    assert(fake.data_reads == 1);
    for (unsigned i = 0; i < 6; ++i)
        sample(&baro, &bus, &start);
    assert(baro.reading.status == T3P_BARO_WAITING);
    sample(&baro, &bus, &start);
    assert(baro.reading.status == T3P_BARO_READY);
    assert(baro.reading.pressure_pa == 100653 && baro.reading.altitude_dm == 0);

    fake.adc_pressure = BASE_ADC_PRESSURE + 4000;
    sample(&baro, &bus, &start);
    assert(baro.reading.altitude_dm >= 30);
    fake.adc_pressure = BASE_ADC_PRESSURE;
    for (unsigned i = 0; i < 8; ++i)
        sample(&baro, &bus, &start);
    assert(!baro.reading.upper_deck && baro.reading.altitude_dm == 0);
    fake.adc_pressure = BASE_ADC_PRESSURE + 450;
    for (unsigned i = 0; i < 8; ++i)
        sample(&baro, &bus, &start);
    assert(baro.reading.altitude_dm > 30 && !baro.reading.upper_deck);
    for (unsigned i = 0; i < 9; ++i)
        sample(&baro, &bus, &start);
    assert(baro.reading.upper_deck);
    fake.adc_pressure = BASE_ADC_PRESSURE - 450;
    for (unsigned i = 0; i < 8; ++i)
        sample(&baro, &bus, &start);
    assert(baro.reading.altitude_dm < -30 && baro.reading.upper_deck);

    fake.adc_pressure = 0x80000; /* Measurement skipped or reset. */
    sample(&baro, &bus, &start);
    assert(baro.reading.status == T3P_BARO_READ_ERROR && !baro.configured);
    fake.adc_pressure = BASE_ADC_PRESSURE;
    fake.no_ack = 1;
    t3p_baro_poll(&baro, &bus, start += 1032);
    assert(baro.reading.status == T3P_BARO_READ_ERROR && fake.sleep_writes == 1);
    fake.no_ack = 0;
    fake.nvm_busy = 1;
    t3p_baro_poll(&baro, &bus, start += 1000);
    assert(baro.reading.status == T3P_BARO_WAITING && fake.sleep_writes == 1);
    fake.nvm_busy = 0;
    fake.bad_trim = 1;
    t3p_baro_poll(&baro, &bus, start += 1000);
    assert(baro.reading.status == T3P_BARO_READ_ERROR && fake.sleep_writes == 1);
    fake.bad_trim = 0;
    t3p_baro_poll(&baro, &bus, start += 1000);
    assert(baro.configured && fake.sleep_writes == 2);
    for (unsigned i = 0; i < 8; ++i)
        sample(&baro, &bus, &start);
    assert(baro.reading.status == T3P_BARO_READY && baro.reading.altitude_dm == 0);
    assert(baro.reading.upper_deck);

    t3p_baro_init(&baro, start);
    fake.id = 0x60;
    t3p_baro_poll(&baro, &bus, start);
    assert(baro.reading.status == T3P_BARO_UNSUPPORTED && fake.sleep_writes == 2);
    assert(!baro.reading.upper_deck);
    check_alternate();
    puts("PASS: BMP280 compensation, baseline, sustained rise, sensor failures and timer wrap");
    return 0;
}
