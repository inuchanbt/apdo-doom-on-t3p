/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_tilt.h"

enum { SAMPLE_MS = 40, STALE_MS = 160, NEUTRAL_SAMPLES = 8 };

void t3p_tilt_init(struct t3p_tilt *tilt, const struct t3p_sensor_bus *bus)
{
    *tilt = (struct t3p_tilt){0};
    tilt->ready = t3p_accel_configure(bus) == 0;
}

static int close_to(int a, int b, int tolerance)
{
    int delta = a - b;
    return delta >= -tolerance && delta <= tolerance;
}

static int magnitude_squared(const struct t3p_accel_raw *sample)
{
    int x = sample->x, y = sample->y, z = sample->z;
    return x*x + y*y + z*z;
}

static int integer_sqrt(int squared)
{
    int root = 0;
    for (int bit = 1024; bit; bit >>= 1) {
        int trial = root + bit;
        if (trial * trial <= squared) root = trial;
    }
    return root;
}

static void calibrate(struct t3p_tilt *tilt, const struct t3p_accel_raw *sample)
{
    int squared = magnitude_squared(sample);
    if (squared < 64*64 || squared > 1536*1536) {
        tilt->samples = 0;
        return;
    }

    int tolerance = integer_sqrt(squared) / 12;
    if (!tilt->samples || !close_to(sample->x, tilt->reference.x, tolerance) ||
        !close_to(sample->y, tilt->reference.y, tolerance) ||
        !close_to(sample->z, tilt->reference.z, tolerance)) {
        tilt->reference = *sample;
        tilt->sum_x = tilt->sum_y = tilt->sum_z = 0;
        tilt->samples = 0;
    }
    tilt->sum_x += sample->x;
    tilt->sum_y += sample->y;
    tilt->sum_z += sample->z;
    if (++tilt->samples == NEUTRAL_SAMPLES) {
        tilt->reference.x = tilt->sum_x / NEUTRAL_SAMPLES;
        tilt->reference.y = tilt->sum_y / NEUTRAL_SAMPLES;
        tilt->reference.z = tilt->sum_z / NEUTRAL_SAMPLES;
        tilt->gravity = integer_sqrt(magnitude_squared(&tilt->reference));
    }
}

static int scaled_delta(int delta, int gravity)
{
    if (delta > gravity) delta = gravity;
    if (delta < -gravity) delta = -gravity;
    return delta * 1024 / gravity;
}

uint32_t t3p_tilt_poll(struct t3p_tilt *tilt, const struct t3p_sensor_bus *bus,
                       uint32_t now, int paused)
{
    if (!tilt->ready)
        return 0;
    if (paused) {
        if (!tilt->gravity)
            tilt->samples = 0;
        if (tilt->filter.tilt_active)
            t3p_input_reset(&tilt->filter);
        return tilt->keys = 0;
    }
    if (now - tilt->last_poll_ms < SAMPLE_MS)
        return tilt->keys;
    tilt->last_poll_ms = now;

    struct t3p_accel_raw sample;
    int result = t3p_accel_read_latest(bus, &sample);
    if (result == 1 && tilt->have_sample && now - tilt->last_sample_ms < STALE_MS)
        return tilt->keys;
    if (result) {
        tilt->have_sample = 0;
        if (!tilt->gravity)
            tilt->samples = 0;
        t3p_input_reset(&tilt->filter);
        return tilt->keys = 0;
    }
    tilt->have_sample = 1;
    tilt->latest = sample;
    tilt->last_sample_ms = now;
    if (!tilt->gravity) {
        calibrate(tilt, &sample);
        return 0;
    }

    /* Project pitch perpendicular to the neutral gravity in the Y/Z plane.
     * This lets both a face-up and an upright starting pose respond to pitch. */
    int pitch = (sample.y * tilt->reference.z - sample.z * tilt->reference.y) / tilt->gravity;
    int roll = sample.x - tilt->reference.x;
#ifdef T3P_TILT_TUNING
    /* The T3P's sensor X axis increases on a physical left tilt. */
    roll = -roll;
#endif
    const struct t3p_input_sample input = {
        .tilt_x = scaled_delta(roll, tilt->gravity),
        .tilt_y = scaled_delta(pitch, tilt->gravity),
        .tilt_valid = 1,
    };
    return tilt->keys = t3p_input_update(&tilt->filter, &input);
}
