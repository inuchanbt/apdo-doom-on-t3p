/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_bsp.h"

/* BR35 mcpwm.h and power_gate.c. Stock 3.0.3 configures IO_MT_PG at
 * 0x0c00eaea, routes channel 1 at 0x0c00e982, and applies 80% at 0x0c0112aa. */
enum {
    MTPG_CON = 0xfd3e64u,
    MTPG_MODE_MASK = 3u,
    MTPG_PWM = 1u,
    MTPG_SOURCE_MASK = 63u << 2,
    MTPG_PWM1H = 0x1eu << 2,
    PWM_TIMER_CON = 0xfd170cu,
    PWM_TIMER_CNT = 0xfd1710u,
    PWM_TIMER_PR = 0xfd1714u,
    PWM_CH_CON0 = 0xfd1774u,
    PWM_CH_CON1 = 0xfd1778u,
    PWM_CH_CMPH = 0xfd177cu,
    PWM_CON = 0xfd17e4u,
    PWM_CH1_ENABLE = 1u << 1,
    PWM_TMR1_ENABLE = 1u << 9,
    PWM_CLOCK_ENABLE = 1u << 16,
    PWM_EDGE_ALIGNED = 1u,
    PWM_DIV32 = 5u << 3,
    SHOT_MS = 35,
    DAMAGE_MS = 65,
    MIC_TAIL_MS = 96,
};

static uint32_t read_reg(struct t3p_bsp *board, uint32_t address)
{
    return board->io.read32(board->io.context, address);
}

static void write_reg(struct t3p_bsp *board, uint32_t address, uint32_t value)
{
    board->io.write32(board->io.context, address, value);
}

void t3p_haptics_init(struct t3p_bsp *board)
{
    uint32_t gate = read_reg(board, MTPG_CON) & ~MTPG_MODE_MASK;
    write_reg(board, MTPG_CON, gate); /* Static open drain, high impedance. */
    uint32_t enable = read_reg(board, PWM_CON);
    if (enable & (PWM_TMR1_ENABLE | PWM_CH1_ENABLE))
        return;

    /* Measure the inherited PWM clock over 4 ms with the motor disconnected
     * from the PWM output. At /32 even the fastest SoC clock fits in 16 bits. */
    write_reg(board, PWM_CON, enable | PWM_CLOCK_ENABLE);
    write_reg(board, PWM_TIMER_CON, PWM_DIV32 | PWM_EDGE_ALIGNED);
    write_reg(board, PWM_TIMER_CNT, 0);
    write_reg(board, PWM_TIMER_PR, 0xffffu);
    write_reg(board, PWM_CON, enable | PWM_CLOCK_ENABLE | PWM_TMR1_ENABLE);
    int failed = t3p_bsp_delay(board, 4);
    uint32_t ticks = read_reg(board, PWM_TIMER_CNT) & 0xffffu;
    write_reg(board, PWM_CON, enable | PWM_CLOCK_ENABLE);
    /* 4 ms contains 40 periods at 10 kHz. Restore the /1 prescaler. */
    uint32_t period = ticks * 32u / 40u;
    if (!failed && period >= 200 && period <= 30000) {
        write_reg(board, PWM_TIMER_CON, PWM_EDGE_ALIGNED);
        write_reg(board, PWM_TIMER_CNT, 0);
        write_reg(board, PWM_TIMER_PR, period - 1);
        write_reg(board, PWM_CH_CON0, 1u << 2); /* H output enabled. */
        write_reg(board, PWM_CH_CON1, 1u << 8); /* Timer 1 selected. */
        write_reg(board, PWM_CH_CMPH, (period - 1) * 4u / 5u);
        write_reg(board, MTPG_CON, (gate & ~MTPG_SOURCE_MASK) | MTPG_PWM1H);
        board->haptics.ready = 1;
    }
    write_reg(board, PWM_CON, enable);
}

void t3p_haptics_queue(struct t3p_bsp *board, enum t3p_haptic_event event)
{
    if (event > board->haptics.pending)
        board->haptics.pending = event;
}

int t3p_haptics_mic_muted(struct t3p_bsp *board)
{
    struct t3p_haptics *haptics = &board->haptics;
    if (!haptics->pulse_ms && board->milliseconds - haptics->stopped_ms >= MIC_TAIL_MS)
        haptics->muted = 0;
    return haptics->muted;
}

void t3p_haptics_play(struct t3p_bsp *board, int gameplay)
{
    struct t3p_haptics *haptics = &board->haptics;
    enum t3p_haptic_event event = haptics->pending;
    haptics->pending = T3P_HAPTIC_NONE;
    if (!gameplay || !haptics->ready || event == T3P_HAPTIC_NONE)
        return;
    uint32_t now = t3p_bsp_millis(board);
    /* Also limits motor duty during automatic fire and repeated damage. */
    if (t3p_haptics_mic_muted(board))
        return;
    haptics->started_ms = now;
    haptics->pulse_ms = event == T3P_HAPTIC_DAMAGE ? DAMAGE_MS : SHOT_MS;
    haptics->muted = 1;
    write_reg(board, PWM_CON, read_reg(board, PWM_CON) |
              PWM_CLOCK_ENABLE | PWM_TMR1_ENABLE | PWM_CH1_ENABLE);
    write_reg(board, MTPG_CON, read_reg(board, MTPG_CON) | MTPG_PWM);
}

void t3p_haptics_stop(struct t3p_bsp *board)
{
    struct t3p_haptics *haptics = &board->haptics;
    if (!haptics->pulse_ms)
        return;
    write_reg(board, MTPG_CON, read_reg(board, MTPG_CON) & ~MTPG_MODE_MASK);
    write_reg(board, PWM_CON, read_reg(board, PWM_CON) & ~(PWM_TMR1_ENABLE | PWM_CH1_ENABLE));
    haptics->pulse_ms = 0;
    haptics->stopped_ms = board->milliseconds;
}
