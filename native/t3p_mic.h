/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_MIC_H
#define T3P_MIC_H
#include <stdint.h>

struct t3p_bsp;

enum { T3P_MIC_BLOCK_SAMPLES = 512 };

enum t3p_mic_mode {
    T3P_MIC_LISTEN,
    T3P_MIC_PAUSE,
    T3P_MIC_DISCARD,
};

struct t3p_mic {
    volatile int16_t pcm[2 * T3P_MIC_BLOCK_SAMPLES] __attribute__((aligned(4)));
    uint32_t noise, fired_ms;
    uint16_t peak_level;
    uint8_t peak_windows;
    uint8_t warmup, quiet, armed, pending, ready;
};

/* Initialize once from zeroed, DMA-accessible SRAM. */
void t3p_mic_init(struct t3p_mic *, struct t3p_bsp *, uint32_t now);
void t3p_mic_poll(struct t3p_mic *, struct t3p_bsp *, uint32_t now, enum t3p_mic_mode);
unsigned t3p_mic_threshold(const struct t3p_mic *);
uint32_t t3p_mic_take(struct t3p_mic *);
#endif
