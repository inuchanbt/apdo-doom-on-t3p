/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Offline-testable input mixer. No GPIO, USB or sensor initialization. */
#include "t3p_input.h"
#include <string.h>

enum {
    BUTTON_DEBOUNCE_MS = 20,
    BUTTON_CHORD_GRACE_MS = 50,
    BUTTON_CHORD_HOLD_MS = 700
};

void t3p_buttons_sample(struct t3p_buttons *s, unsigned pressed, uint32_t now)
{
    pressed &= T3P_BUTTON_BOTH;
    if (s->candidate != pressed) {
        s->candidate = pressed;
        s->candidate_since = now;
    }
    uint32_t elapsed = now - s->candidate_since;
    if (elapsed < BUTTON_DEBOUNCE_MS)
        return;

    if (s->stable != pressed) {
        /* Keep taps that ended during the chord grace period. */
        if (!s->locked && !s->single_ready && pressed != T3P_BUTTON_BOTH)
            s->pending |= s->stable;
        s->stable = pressed;
        s->single_ready = 0;
        s->chord_tracking = 0;
        if (!pressed) {
            s->locked = 0;
        } else if (pressed == T3P_BUTTON_BOTH && !s->locked) {
            s->pending = 0;
            s->locked = 1;
            s->chord_tracking = 1;
            s->chord_since = now;
        }
    }

    if (s->chord_tracking && now - s->chord_since >= BUTTON_CHORD_HOLD_MS) {
        s->escape_pending = 1;
        s->chord_tracking = 0;
    }
    if (pressed && !s->locked && !s->single_ready && elapsed >= BUTTON_CHORD_GRACE_MS) {
        s->pending |= pressed;
        s->single_ready = 1;
    }
}

uint32_t t3p_buttons_take(struct t3p_buttons *s, int menu)
{
    menu = !!menu;
    if (s->menu != menu) {
        s->menu = menu;
        s->pending = 0;
        /* A held button keeps its original meaning until it is released. */
        if (s->stable)
            s->locked = 1;
    }

    /* Leave a release tic between pulses so the bridge sees both edges. */
    int can_pulse = !s->pulse_sent;
    s->pulse_sent = 0;
    if (s->escape_pending) {
        if (!can_pulse)
            return 0;
        s->escape_pending = 0;
        s->pulse_sent = 1;
        return T3P_ESCAPE;
    }
    if (s->locked || s->candidate == T3P_BUTTON_BOTH)
        return 0;

    uint32_t keys = 0;
    if (!menu) {
        if ((s->pending & T3P_BUTTON_LEFT) ||
            (s->stable == T3P_BUTTON_LEFT && s->single_ready))
            keys |= T3P_FIRE;
        s->pending &= ~T3P_BUTTON_LEFT;
    }
    if (can_pulse && s->pending) {
        if (s->pending & T3P_BUTTON_RIGHT) {
            keys |= menu ? T3P_ESCAPE : T3P_USE;
            s->pending &= ~T3P_BUTTON_RIGHT;
        } else {
            keys |= T3P_ENTER;
            s->pending &= ~T3P_BUTTON_LEFT;
        }
        s->pulse_sent = 1;
    }
    return keys;
}

void t3p_input_reset(struct t3p_input *s) { memset(s,0,sizeof(*s)); }

static int clamp_tilt(int value)
{
    return value < -1024 ? -1024 : value > 1024 ? 1024 : value;
}
static int axis(int value, int previous)
{
#ifdef T3P_TILT_TUNING
    enum { ENGAGE = 300, RELEASE = 200 };
#else
    enum { ENGAGE = 180, RELEASE = 110 };
#endif
    if (value >= ENGAGE) return 1;
    if (value <= -ENGAGE) return -1;
    if (previous > 0 && value > RELEASE) return 1;
    if (previous < 0 && value < -RELEASE) return -1;
    return 0;
}
static uint32_t direction(int x, int y)
{
    return (x < 0 ? T3P_LEFT : x > 0 ? T3P_RIGHT : 0)
         | (y < 0 ? T3P_FORWARD : y > 0 ? T3P_BACK : 0);
}

uint32_t t3p_input_update(struct t3p_input *s, const struct t3p_input_sample *p)
{
    uint32_t keys = 0;
    uint8_t raw = p->buttons_valid ? p->buttons & 3 : 0;
    if (!p->buttons_valid) {
        s->candidate = s->stable = 0;
    } else if (s->candidate != raw) {
        s->candidate = raw;
        s->button_since = p->now_ms;
    } else if ((uint32_t)(p->now_ms-s->button_since) >= BUTTON_DEBOUNCE_MS) {
        s->stable = raw;
    }
    /* Two buttons held for 700ms open/close the menu once. Suppress fire/use
     * during the chord. Unsigned elapsed times remain valid across wraparound.
     */
    if (s->stable == 3) {
        if (!s->chord_active) {
            s->chord_active = 1;
            s->chord_sent = 0;
            s->chord_since = p->now_ms;
        }
        if (!s->chord_sent && (uint32_t)(p->now_ms-s->chord_since) >= BUTTON_CHORD_HOLD_MS) {
            keys |= T3P_ESCAPE;
            s->chord_sent = 1;
        }
    } else {
        s->chord_active = s->chord_sent = 0;
        if (s->stable & 1) keys |= p->menu ? T3P_ENTER : T3P_FIRE;
        if (s->stable & 2) keys |= p->menu ? T3P_ESCAPE : T3P_USE;
    }
    /* Tilt is disabled in menus: opening a menu while tilted must not select
     * a different item. Menus retain the touch direction pad and two buttons.
     */
    if (!p->tilt_valid || p->menu) {
        s->tilt_active = 0;
        s->direction_x = s->direction_y = 0;
        s->filtered_x = s->filtered_y = 0;
    } else {
        int x = clamp_tilt(p->tilt_x), y = clamp_tilt(p->tilt_y);
        if (!s->tilt_active) {
            s->filtered_x = x; s->filtered_y = y;
            s->tilt_active = 1;
        } else {
            s->filtered_x += (x-s->filtered_x)/4;
            s->filtered_y += (y-s->filtered_y)/4;
        }
        s->direction_x = (int8_t)axis(s->filtered_x,s->direction_x);
        s->direction_y = (int8_t)axis(s->filtered_y,s->direction_y);
        keys |= direction(s->direction_x,s->direction_y);
    }
    if (p->touch_valid && p->touch_down && p->touch_x <= 1023 && p->touch_y <= 1023) {
        unsigned x = p->touch_x, y = p->touch_y;
        if (y < 160) {
            if (x < 128) keys |= T3P_ESCAPE;
            else if (!p->menu) keys |= T3P_WEAPON_1 << ((x-128)*7/896);
        } else if (x < 512) {
            int dx = x < 160 ? -1 : x > 352 ? 1 : 0;
            int dy = y < 480 ? -1 : y > 704 ? 1 : 0;
            /* A touch direction overrides the tilt on that axis. */
            if (dx) keys &= ~(T3P_LEFT|T3P_RIGHT);
            if (dy) keys &= ~(T3P_FORWARD|T3P_BACK);
            keys |= direction(dx,dy);
        } else {
            keys |= y < 640 ? (p->menu ? T3P_ENTER : T3P_FIRE)
                             : (p->menu ? T3P_ESCAPE : T3P_USE);
        }
    }
    s->menu = p->menu;
    return keys;
}
