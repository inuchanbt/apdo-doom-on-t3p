/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_INPUT_H
#define T3P_INPUT_H
#include <stdint.h>

/* First eight bits retain the existing DOOM bridge's key order. */
enum t3p_key {
    T3P_FIRE=1u<<0, T3P_FORWARD=1u<<1, T3P_LEFT=1u<<2,
    T3P_RIGHT=1u<<3, T3P_USE=1u<<4, T3P_ESCAPE=1u<<5,
    T3P_ENTER=1u<<6, T3P_BACK=1u<<7,
    T3P_STRAFE_LEFT=1u<<8, T3P_STRAFE_RIGHT=1u<<9,
    T3P_WEAPON_1=1u<<10
};
/* Board adapter supplies calibrated, display-oriented tilt in -1024..1024.
 * Touch coordinates use 0..1023 independently of the unresolved panel size.
 * Buttons: bit 0 fire/confirm, bit 1 use/back. All inputs are level states.
 * A missing/failed input MUST clear its valid flag; old values cannot be reused.
 * Update once per game input tick and cache the returned mask. Diagnostic
 * queries must read that mask, not call update and consume one-tick gestures.
 */
struct t3p_input_sample {
    uint32_t now_ms;
    int16_t tilt_x, tilt_y;
    uint16_t touch_x, touch_y;
    uint8_t buttons, buttons_valid, tilt_valid, touch_valid, touch_down;
    uint8_t menu;
};
struct t3p_input {
    uint32_t button_since, chord_since;
    int32_t filtered_x, filtered_y;
    uint8_t candidate, stable, chord_active, chord_sent, tilt_active, menu;
    int8_t direction_x, direction_y;
};
void t3p_input_reset(struct t3p_input *state);
uint32_t t3p_input_update(struct t3p_input *state, const struct t3p_input_sample *sample);
#endif
