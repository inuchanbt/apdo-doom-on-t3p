/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "t3p_bsp.h"
#include "t3p_input.h"
#include "t3p_touch.h"

struct fake_touch {
    uint32_t now, port;
    uint16_t timer;
    unsigned scl, sda, address, reg, read_count;
    unsigned released, clock_count, transactions;
    unsigned phase, nack, stuck_clock, stopped_timer, write_only, writes;
    uint8_t bits[56], report[5];
};

static unsigned byte_at(const struct fake_touch *f, unsigned start)
{
    unsigned value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value = value * 2 + f->bits[start + i];
    return value;
}

static uint32_t read_reg(void *context, uint32_t address)
{
    struct fake_touch *f = context;
    if (address == 0xfd0618) {
        if (!f->stopped_timer) f->timer += 24;
        return f->timer;
    }
    assert(address == f->port);
    unsigned level = f->released & (f->stuck_clock ? f->sda : f->scl | f->sda);
    unsigned bit = f->clock_count;
    if (!(level & f->scl) || f->nack) return level;
    if (f->write_only && f->phase == 1 && bit == 27)
        return level & ~f->sda;
    if ((f->phase == 1 && (bit == 9 || bit == 18)) || (f->phase == 2 && bit == 9))
        return level & ~f->sda;
    if (f->phase == 2 && bit >= 10 && bit < 10 + 9 * f->read_count) {
        bit -= 10;
        if (bit % 9 < 8 && !(f->report[bit / 9] & (0x80u >> (bit % 9))))
            return level & ~f->sda;
    }
    return level;
}

static void write_reg(void *context, uint32_t address, uint32_t value)
{
    struct fake_touch *f = context;
    unsigned lines = f->scl | f->sda;
    if (address == f->port + 0x34) {
        assert(!(value & lines));  /* SDA and SCL are never driven high. */
        return;
    }
    if (address != f->port + 0x38) return;
    unsigned before = f->released;
    f->released = (before | (value & lines)) & ~(value >> 16);
    unsigned after = f->released;
    if (f->stuck_clock) return;
    if ((before & after & f->scl) && ((before ^ after) & f->sda)) {
        if (!(after & f->sda)) {
            if (f->phase == 1) {
                assert(f->clock_count == 19);
                assert(byte_at(f, 0) == 2 * f->address && byte_at(f, 9) == f->reg);
            }
            ++f->phase;
            f->clock_count = 0;
        } else {
            if (f->write_only && !f->nack) {
                assert(f->phase == 1 && f->clock_count == 28);
                assert(byte_at(f, 0) == 0x32 && byte_at(f, 9) == 0x20);
                assert(byte_at(f, 18) == 0x37);
                ++f->writes;
            }
            if (f->phase == 2) {
                assert(f->clock_count == 10 + 9 * f->read_count);
                assert(byte_at(f, 0) == 2 * f->address + 1);
                for (unsigned i = 0; i < f->read_count; ++i)
                    assert(f->bits[17 + 9 * i] == (i + 1 == f->read_count));
                ++f->transactions;
            }
            f->phase = 0;
        }
    }
    if (f->phase && !(before & f->scl) && (after & f->scl)) {
        assert(f->clock_count < sizeof(f->bits));
        f->bits[f->clock_count++] = !!(after & f->sda);
    }
}

uint32_t t3p_bsp_millis(struct t3p_bsp *board)
{
    return ((struct fake_touch *)board->io.context)->now;
}

int t3p_bsp_delay(struct t3p_bsp *board, uint32_t ms)
{
    ((struct fake_touch *)board->io.context)->now += ms;
    return 0;
}

static uint32_t sample(struct t3p_touch *touch, struct t3p_bsp *board,
                       unsigned down, unsigned x, unsigned y)
{
    struct fake_touch *f = board->io.context;
    f->report[0] = (uint8_t)down;
    f->report[1] = (uint8_t)(x >> 8);
    f->report[2] = (uint8_t)x;
    f->report[3] = (uint8_t)(y >> 8);
    f->report[4] = (uint8_t)y;
    f->now += 20;
    return t3p_touch_poll(touch, board);
}

int main(void)
{
    enum { TOUCH_LINES = 0x60, ACCEL_LINES = 6 };
    struct fake_touch f = {.timer = 0xfff0, .port = 0xfd3000, .sda = 32, .scl = 64,
                          .address = T3P_CST816D, .reg = 2, .read_count = 5};
    struct t3p_bsp board = {.io = {&f, read_reg, write_reg, 0}};
    struct t3p_touch touch;
    t3p_touch_init(&touch, &board);
    assert(touch.ready && f.now == 120 && f.released == TOUCH_LINES);
    assert(sample(&touch, &board, 1, 350, 260) == 0);
    assert(sample(&touch, &board, 1, 374, 236) == (T3P_RIGHT | T3P_FORWARD));
    assert(t3p_touch_poll(&touch, &board) == (T3P_RIGHT | T3P_FORWARD));
    assert(f.transactions == 2);
    assert(sample(&touch, &board, 0, 374, 236) == 0);
    assert(sample(&touch, &board, 1, 350, 260) == 0);
    assert(sample(&touch, &board, 1, 326, 284) == (T3P_LEFT | T3P_BACK));
    f.nack = 1;
    assert(sample(&touch, &board, 1, 326, 284) == 0 && !touch.held);
    assert(f.released == TOUCH_LINES);
    f.nack = 0;
    assert(sample(&touch, &board, 1, 326, 284) == 0);
    f.stuck_clock = 1;
    assert(sample(&touch, &board, 1, 326, 284) == 0 && !touch.held);
    assert(f.released == TOUCH_LINES);
    f.stuck_clock = 0;
    assert(sample(&touch, &board, 1, 326, 284) == 0);
    assert(sample(&touch, &board, 1, 326, 260) == T3P_FORWARD);
    f.stopped_timer = 1;
    assert(sample(&touch, &board, 1, 326, 260) == 0 && !touch.held);
    assert(f.released == TOUCH_LINES && f.transactions == 8);

    struct fake_touch accel = {.port = 0xfd3100, .sda = 2, .scl = 4,
                              .address = T3P_SC7A20, .reg = 0x0f, .read_count = 1,
                              .report = {0x11}};
    struct t3p_bsp accel_board = {.io = {&accel, read_reg, write_reg, 0}};
    struct t3p_sensor_gpio gpio;
    const struct t3p_sensor_bus bus = t3p_accel_bus_init(&gpio, &accel_board);
    uint8_t who;
    assert(bus.read(bus.ctx, T3P_SC7A20, 0x0f, &who, 1) == 1 && who == 0x11);
    assert(accel.transactions == 1 && accel.released == ACCEL_LINES);
    accel.write_only = 1;
    assert(bus.write(bus.ctx, T3P_SC7A20, 0x20, 0x37) == 0);
    assert(accel.writes == 1 && accel.released == ACCEL_LINES);
    accel.nack = 1;
    assert(bus.write(bus.ctx, T3P_SC7A20, 0x20, 0x37) < 0);
    assert(accel.writes == 1 && accel.released == ACCEL_LINES);
    puts("PASS: independent PA touch and PB accelerometer buses, touch drag and bus error recovery");
    return 0;
}
