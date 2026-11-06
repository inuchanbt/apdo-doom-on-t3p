/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "t3p_bsp.h"
#include "t3p_mic.h"
#include "t3p_tilt.h"

struct fake_accel {
    uint8_t id, fail_read;
    unsigned writes, fail_write, fifo;
    struct t3p_accel_raw sample;
};

static int accel_read(void *ctx, uint8_t addr, uint8_t reg, uint8_t *data, unsigned count)
{
    struct fake_accel *accel = ctx;
    assert(addr == T3P_SC7A20);
    if (accel->fail_read)
        return -1;
    if (reg == 0x0f && count == 1) {
        *data = accel->id;
        return 1;
    }
    if (reg == 0x2f && count == 1) {
        *data = accel->fifo ? (uint8_t)(0x80 | accel->fifo) : 0x20;
        return 1;
    }
    assert(reg == 0xa8 && count == 6 && accel->fifo);
    --accel->fifo;
    const int axes[] = {accel->sample.x, accel->sample.y, accel->sample.z};
    for (unsigned i = 0; i < 3; ++i) {
        unsigned value = (unsigned)(axes[i] * 16);
        data[i * 2] = (uint8_t)value;
        data[i * 2 + 1] = (uint8_t)(value >> 8);
    }
    return 6;
}

static int accel_write(void *ctx, uint8_t addr, uint8_t reg, uint8_t data)
{
    struct fake_accel *accel = ctx;
    static const uint8_t expected[][2] = {
        {0x57, 0}, {0x1f, 0}, {0x21, 0x70}, {0x23, 0x10},
        {0x24, 0x40}, {0x2e, 0x8f}, {0x22, 0}, {0x20, 0x37},
    };
    unsigned i = accel->writes++;
    assert(addr == T3P_SC7A20 && i < sizeof(expected) / sizeof(expected[0]));
    assert(reg == expected[i][0] && data == expected[i][1]);
    return accel->writes == accel->fail_write ? -1 : 0;
}

static uint32_t sample_tilt(struct t3p_tilt *tilt, const struct t3p_sensor_bus *bus,
                            uint32_t *now, struct t3p_accel_raw sample)
{
    struct fake_accel *accel = bus->ctx;
    accel->fifo = 1;
    accel->sample = sample;
    return t3p_tilt_poll(tilt, bus, *now += 40, 0);
}

static void check_tilt(void)
{
    struct fake_accel accel = {0};
    const struct t3p_sensor_bus bus = {accel_read, &accel, accel_write};
    struct t3p_tilt tilt;
    t3p_tilt_init(&tilt, &bus);
    assert(!tilt.ready && accel.writes == 0);
    accel.id = 0x11;
    accel.fail_write = 3;
    t3p_tilt_init(&tilt, &bus);
    assert(!tilt.ready && accel.writes == 3);
    accel.fail_write = accel.writes = 0;
    t3p_tilt_init(&tilt, &bus);
    assert(tilt.ready && accel.writes == 8);

    uint32_t now = 0;
    assert(t3p_tilt_poll(&tilt, &bus, now += 40, 0) == 0 && tilt.samples == 0);
    assert(sample_tilt(&tilt, &bus, &now, (struct t3p_accel_raw){0}) == 0);
    assert(tilt.samples == 0);
    for (int i = 0; i < 12; ++i) {
        struct t3p_accel_raw moving = {i % 2 ? 150 : -150, -60, 470};
        assert(sample_tilt(&tilt, &bus, &now, moving) == 0);
    }
    assert(tilt.samples == 1);
    struct t3p_accel_raw neutral = {40, -60, 490};
    for (unsigned i = 0; i < 8; ++i)
        assert(sample_tilt(&tilt, &bus, &now, neutral) == 0);
    assert(tilt.reference.x == 40 && tilt.reference.y == -60 && tilt.gravity == 495);
    struct t3p_accel_raw shallow = {165, -60, 465};
    for (unsigned i = 0; i < 12; ++i)
        assert(sample_tilt(&tilt, &bus, &now, shallow) == 0);
    struct t3p_accel_raw forward_left = {245, -265, 350};
    uint32_t keys = 0;
    for (unsigned i = 0; i < 12; ++i)
        keys = sample_tilt(&tilt, &bus, &now, forward_left);
    assert(keys == (T3P_FORWARD | T3P_LEFT));
    for (unsigned i = 0; i < 12; ++i)
        keys = sample_tilt(&tilt, &bus, &now, shallow);
    assert(keys == T3P_LEFT);  /* Hysteresis keeps a turn already in progress. */
    assert(t3p_tilt_poll(&tilt, &bus, now, 1) == 0);
    assert(t3p_tilt_poll(&tilt, &bus, now, 0) == 0);
    struct t3p_accel_raw back_right = {-160, 155, 438};
    assert(sample_tilt(&tilt, &bus, &now, back_right) == (T3P_BACK | T3P_RIGHT));
    for (unsigned i = 0; i < 12; ++i)
        keys = sample_tilt(&tilt, &bus, &now, neutral);
    assert(keys == 0);

    accel.fifo = 6;
    accel.sample = forward_left;
    assert(t3p_tilt_poll(&tilt, &bus, now += 40, 0) == 0 && accel.fifo == 2);
    keys = T3P_FORWARD | T3P_LEFT;
    assert(t3p_tilt_poll(&tilt, &bus, now += 40, 0) == keys && accel.fifo == 0);
    for (unsigned i = 0; i < 3; ++i)
        assert(t3p_tilt_poll(&tilt, &bus, now += 40, 0) == keys);
    assert(t3p_tilt_poll(&tilt, &bus, now += 40, 0) == 0 && !tilt.have_sample);

    accel.writes = 0;
    t3p_tilt_init(&tilt, &bus);
    struct t3p_accel_raw upright = {10, 248, 12};
    for (unsigned i = 0; i < 8; ++i)
        assert(sample_tilt(&tilt, &bus, &now, upright) == 0);
    assert(tilt.gravity == 248);
    assert(sample_tilt(&tilt, &bus, &now, (struct t3p_accel_raw){95, 218, 100}) == keys);

    accel.fail_read = 1;
    assert(t3p_tilt_poll(&tilt, &bus, now += 40, 0) == 0 && !tilt.filter.tilt_active);
}

enum {
    CON0 = 0xf10300, ADR = 0xf10318, LEN = 0xf1031c,
    PNS = 0xf10320, SRP = 0xf10328, SRN = 0xf1032c, ACK = 64, PENDING = 128,
};

struct fake_adc {
    uint32_t regs[12], now;
    unsigned drained;
};

static uint32_t adc_read(void *ctx, uint32_t reg)
{
    struct fake_adc *adc = ctx;
    if (reg >= CON0 && reg <= SRN)
        return adc->regs[(reg - CON0) / 4];
    return 0;
}

static void adc_write(void *ctx, uint32_t reg, uint32_t value)
{
    struct fake_adc *adc = ctx;
    if (reg < CON0 || reg > SRN)
        return;
    if (reg == CON0)
        value &= ~(ACK | PENDING);
    adc->regs[(reg - CON0) / 4] = value;
    if (reg == SRN) {
        assert(value == T3P_MIC_BLOCK_SAMPLES);
        adc->regs[(SRP - CON0) / 4] ^= T3P_MIC_BLOCK_SAMPLES;
        ++adc->drained;
    }
}

int t3p_bsp_delay(struct t3p_bsp *board, uint32_t ms)
{
    ((struct fake_adc *)board->io.context)->now += ms;
    return 0;
}

static void feed_sound_mode(struct t3p_mic *mic, struct t3p_bsp *board,
                            int level, unsigned active_samples, enum t3p_mic_mode mode)
{
    struct fake_adc *adc = board->io.context;
    unsigned offset = adc_read(adc, SRP);
    for (unsigned i = 0; i < T3P_MIC_BLOCK_SAMPLES; ++i)
        mic->pcm[offset + i] = (int16_t)(7000 + (i < active_samples && i % 2 ? level : 0));
    adc->regs[0] |= PENDING;
    t3p_mic_poll(mic, board, adc->now += 32, mode);
    assert(!(adc->regs[0] & PENDING));
}

static void feed_sound(struct t3p_mic *mic, struct t3p_bsp *board,
                       int level, unsigned active_samples)
{
    feed_sound_mode(mic, board, level, active_samples, T3P_MIC_LISTEN);
}

static void feed(struct t3p_mic *mic, struct t3p_bsp *board, int level)
{
    feed_sound(mic, board, level, T3P_MIC_BLOCK_SAMPLES);
}

static void check_mic(void)
{
    struct fake_adc adc = {.now = UINT32_MAX - 400};
    struct t3p_bsp board = {.io = {&adc, adc_read, adc_write, 0}};
    struct t3p_mic mic = {0};
    t3p_mic_init(&mic, &board, adc.now);
    assert(mic.ready && adc_read(&adc, ADR) == (uint32_t)(uintptr_t)mic.pcm);
    assert(adc_read(&adc, LEN) == 2 * T3P_MIC_BLOCK_SAMPLES);
    assert(adc_read(&adc, PNS) == T3P_MIC_BLOCK_SAMPLES);
    t3p_mic_poll(&mic, &board, adc.now, T3P_MIC_LISTEN);
    assert(adc.drained == 0 && t3p_mic_take(&mic) == 0);
    feed_sound_mode(&mic, &board, 10000, T3P_MIC_BLOCK_SAMPLES, T3P_MIC_DISCARD);
    assert(adc.drained == 1 && mic.warmup == 0 && mic.noise == 0);
    for (unsigned i = 0; i < 24; ++i) {
        feed(&mic, &board, i < 16 ? 10000 : 160);
        assert(t3p_mic_take(&mic) == 0);
    }
    for (unsigned i = 0; i < 3; ++i)
        feed(&mic, &board, 240);
    assert(mic.armed && adc.drained == 28);
    assert(t3p_mic_threshold(&mic) == 4096);
    feed(&mic, &board, 2500);
    assert(t3p_mic_take(&mic) == 0);
    feed_sound(&mic, &board, 9000, 2);
    assert(t3p_mic_take(&mic) == 0 && mic.peak_level == 9000 && mic.peak_windows == 1);
    feed_sound(&mic, &board, 7000, 48);
    assert(t3p_mic_take(&mic) == T3P_FIRE && t3p_mic_take(&mic) == 0);
    for (unsigned i = 0; i < 18; ++i) {
        feed(&mic, &board, 9000);
        assert(t3p_mic_take(&mic) == 0);
    }
    for (unsigned i = 0; i < 3; ++i)
        feed(&mic, &board, 240);
    feed(&mic, &board, 7000);
    assert(mic.pending);
    t3p_mic_poll(&mic, &board, adc.now, T3P_MIC_PAUSE);
    assert(!mic.armed && t3p_mic_take(&mic) == 0);
    feed(&mic, &board, 7000);
    assert(t3p_mic_take(&mic) == 0);
    for (unsigned i = 0; i < 10; ++i)
        feed(&mic, &board, 180);
    assert(mic.armed);
    unsigned peak = mic.peak_level, drained = adc.drained;
    feed_sound_mode(&mic, &board, 15000, T3P_MIC_BLOCK_SAMPLES, T3P_MIC_DISCARD);
    assert(adc.drained == drained + 1 && mic.peak_level == peak);
    assert(!mic.armed && t3p_mic_take(&mic) == 0);
    for (unsigned i = 0; i < 3; ++i)
        feed(&mic, &board, 180);
    feed(&mic, &board, 7000);
    assert(mic.pending);
    t3p_mic_poll(&mic, &board, adc.now + 251, T3P_MIC_LISTEN);
    assert(t3p_mic_take(&mic) == 0);
    adc.regs[(SRP - CON0) / 4] = 0xffff;
    adc.regs[0] |= PENDING;
    t3p_mic_poll(&mic, &board, adc.now + 252, T3P_MIC_LISTEN);
    assert(!mic.ready && t3p_mic_take(&mic) == 0);
}

int main(void)
{
    check_tilt();
    check_mic();
    puts("PASS: tilt calibration, priority and release; PCM DMA bounds and single-fire gating");
    return 0;
}
