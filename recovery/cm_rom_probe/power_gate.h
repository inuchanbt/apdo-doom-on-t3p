/* SPDX-License-Identifier: MIT */
/* Offline preparation: not integrated into the installed v3 firmware.
 * ADC0 measures target VBUS through a separately verified 1:1 divider.
 * Counts assume a nominal 3.3V ADC reference; these are detection thresholds,
 * not calibrated voltage measurements. The scope remains the timing evidence.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    POWER_WAIT, POWER_EDGE, POWER_FIRE, POWER_DROPPED, POWER_EXPIRED
};
enum {
    POWER_OFF_MAX=310,       /* nominal target VBUS <=0.5V */
    POWER_ON_MIN=2483,       /* nominal target VBUS >=4.0V */
    POWER_HOLD_MIN=2172,     /* nominal target VBUS >=3.5V after detection */
    POWER_OFF_US=100000,
    POWER_HIGH_MIN_US=20,
    POWER_TIMEOUT_S=600,
    POWER_MAX_DELAY_US=20000
};
typedef struct {
    bool armed,used,off_tracking,edge_seen;
    uint64_t off_since,deadline,edge_at;
    uint32_t delay_us;
} power_gate_t;

/* Call continuously before arming too: a single low sample is insufficient. */
static inline unsigned power_poll(power_gate_t *g,uint16_t adc,uint64_t now)
{
    if (adc<=POWER_OFF_MAX) {
        if (!g->off_tracking) { g->off_tracking=true; g->off_since=now; }
    } else g->off_tracking=false;
    if (!g->armed) return POWER_WAIT;
    if (now>=g->deadline) { g->armed=false; return POWER_EXPIRED; }
    if (!g->edge_seen) {
        if (adc<POWER_ON_MIN) return POWER_WAIT;
        /* Consume the attempt at its first edge, including a failed edge.
         * No contact bounce can start another attempt without a helper reboot.
         */
        g->edge_seen=true; g->used=true; g->edge_at=now;
        return POWER_EDGE;
    }
    if (adc<POWER_HOLD_MIN) { g->armed=false; return POWER_DROPPED; }
    uint32_t wait=g->delay_us<POWER_HIGH_MIN_US?POWER_HIGH_MIN_US:g->delay_us;
    if (now-g->edge_at<wait) return POWER_WAIT;
    g->armed=false; return POWER_FIRE;
}
static inline bool power_arm(power_gate_t *g,uint16_t adc,uint64_t now,uint32_t delay_us)
{
    /* Refresh off tracking so a stale low interval cannot authorize arming. */
    if (g->armed || g->used || delay_us>POWER_MAX_DELAY_US) return false;
    power_poll(g,adc,now);
    if (!g->off_tracking || now-g->off_since<POWER_OFF_US) return false;
    g->armed=true; g->edge_seen=false; g->delay_us=delay_us;
    g->deadline=now+(uint64_t)POWER_TIMEOUT_S*1000000;
    return true;
}
static inline void power_disarm(power_gate_t *g) { g->armed=false; }
