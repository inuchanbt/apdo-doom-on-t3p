/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_HAPTICS_H
#define T3P_HAPTICS_H

#include <stdint.h>

enum t3p_haptic_event {
    T3P_HAPTIC_NONE,
    T3P_HAPTIC_SHOT,
    T3P_HAPTIC_DAMAGE,
};

struct t3p_haptics {
    uint32_t started_ms, stopped_ms;
    enum t3p_haptic_event pending;
    uint8_t pulse_ms, muted, ready;
};

struct t3p_bsp;
void t3p_haptics_init(struct t3p_bsp *);
void t3p_haptics_queue(struct t3p_bsp *, enum t3p_haptic_event);
void t3p_haptics_play(struct t3p_bsp *, int gameplay);
void t3p_haptics_stop(struct t3p_bsp *);
int t3p_haptics_mic_muted(struct t3p_bsp *);

#endif
