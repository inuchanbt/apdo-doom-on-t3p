/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_bsp.h"
#include <assert.h>
#include <stdio.h>

enum { PWM = 0xfd1700u, PWM_ENABLE = 0xe4u / 4u, PWM1 = 0x202u };

struct mock {
    uint32_t pwm[PWM_ENABLE + 1];
    uint32_t gate, count_at_4ms;
    uint16_t tick;
};

static uint32_t read32(void *context, uint32_t addr)
{
    struct mock *mock = context;
    if (addr == 0xfd0604u) {
        mock->tick += 6;
        return mock->tick;
    }
    if (addr == 0xfd3e60u)
        return 0;
    if (addr == 0xfd3e64u)
        return mock->gate;
    assert(addr >= PWM && addr <= PWM + 0xe4u);
    if (addr == PWM + 0x10u && (mock->pwm[PWM_ENABLE] & 0x10200u) == 0x10200u)
        return mock->count_at_4ms;
    return mock->pwm[(addr - PWM) / 4u];
}

static void write32(void *context, uint32_t addr, uint32_t value)
{
    struct mock *mock = context;
    if ((addr >= 0xfd0600u && addr <= 0xfd060cu) || addr == 0xfd3e60u)
        return;
    if (addr == 0xfd3e64u) {
        assert(!(value & 2u));
        if (value & 1u)
            assert((mock->pwm[PWM_ENABLE] & PWM1) == PWM1);
        mock->gate = value;
        return;
    }
    assert(addr >= PWM && addr <= PWM + 0xe4u);
    if (addr == PWM + 0xe4u && !(value & PWM1))
        assert(!(mock->gate & 3u));
    mock->pwm[(addr - PWM) / 4u] = value;
}

static void write8(void *context, uint32_t addr, uint8_t value)
{
    (void)context;
    (void)value;
    assert(addr == 0xf2c818u || addr == 0xf2c418u);
}

static void check_pulse(struct t3p_bsp *board, struct mock *mock, unsigned length)
{
    assert(board->haptics.pulse_ms == length && (mock->gate & 3u) == 1);
    uint32_t start = board->haptics.started_ms;
    for (unsigned i = 0; i < length + 4 && (mock->gate & 1u); ++i)
        t3p_bsp_millis(board);
    assert(!(mock->gate & 3u) && !(mock->pwm[PWM_ENABLE] & PWM1));
    assert(board->milliseconds - start >= length && board->milliseconds - start <= length + 1);
    assert(t3p_haptics_mic_muted(board));
    t3p_haptics_queue(board, T3P_HAPTIC_SHOT);
    t3p_haptics_play(board, 1);
    assert(!(mock->gate & 3u));
    assert(!t3p_bsp_delay(board, 96) && !t3p_haptics_mic_muted(board));
    t3p_haptics_play(board, 1);
    assert(!(mock->gate & 3u)); /* A suppressed request cannot play later. */
}

int main(void)
{
    const uint32_t pwm0 = 0x101u;
    struct mock mock = {.gate = 0x100u, .count_at_4ms = 3000u};
    struct t3p_bsp board;
    const struct t3p_bsp_io io = {&mock, read32, write32, write8};
    assert(!t3p_bsp_init(&board, &io));
    mock.pwm[PWM_ENABLE] = pwm0;
    t3p_haptics_init(&board);
    assert(board.haptics.ready && !t3p_haptics_mic_muted(&board));
    assert(mock.gate == (0x100u | (0x1eu << 2)) && mock.pwm[PWM_ENABLE] == pwm0);
    assert(mock.pwm[0x14u / 4u] == 2399 && mock.pwm[0x7cu / 4u] == 1919);

    board.milliseconds = UINT32_MAX - 20;
    t3p_haptics_queue(&board, T3P_HAPTIC_SHOT);
    t3p_haptics_play(&board, 1);
    check_pulse(&board, &mock, 35);
    t3p_haptics_queue(&board, T3P_HAPTIC_DAMAGE);
    t3p_haptics_queue(&board, T3P_HAPTIC_SHOT);
    t3p_haptics_play(&board, 1);
    check_pulse(&board, &mock, 65);
    assert((mock.pwm[PWM_ENABLE] & pwm0) == pwm0);

    t3p_haptics_queue(&board, T3P_HAPTIC_SHOT);
    t3p_haptics_play(&board, 0);
    t3p_haptics_play(&board, 1);
    assert(!(mock.gate & 3u));
    t3p_haptics_queue(&board, T3P_HAPTIC_DAMAGE);
    t3p_haptics_play(&board, 1);
    t3p_haptics_stop(&board);
    assert(!(mock.gate & 3u) && t3p_haptics_mic_muted(&board));

    assert(!t3p_bsp_init(&board, &io));
    mock.count_at_4ms = 0;
    t3p_haptics_init(&board);
    t3p_haptics_queue(&board, T3P_HAPTIC_SHOT);
    t3p_haptics_play(&board, 1);
    assert(!board.haptics.ready && !(mock.gate & 3u));
    puts("PASS: motor PWM setup, pulse deadlines, priority, cooldown, wraparound and idle output");
}
