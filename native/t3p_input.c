/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Offline-testable input mixer. No GPIO, USB or sensor initialization. */
#include "t3p_input.h"
#include <string.h>

void t3p_input_reset(struct t3p_input *s) { memset(s,0,sizeof(*s)); }

static int clamp_tilt(int value)
{
    return value < -1024 ? -1024 : value > 1024 ? 1024 : value;
}
static int axis(int value, int previous)
{
    if (value >= 180) return 1;
    if (value <= -180) return -1;
    if (previous > 0 && value > 110) return 1;
    if (previous < 0 && value < -110) return -1;
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
    } else if ((uint32_t)(p->now_ms-s->button_since) >= 20) {
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
        if (!s->chord_sent && (uint32_t)(p->now_ms-s->chord_since) >= 700) {
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
