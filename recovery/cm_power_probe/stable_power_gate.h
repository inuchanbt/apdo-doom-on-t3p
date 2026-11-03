/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* No signal is sent while contacts settle. After the first detected rise,
 * allow at most50ms to obtain one sampled stable-high interval. Once FIRE is
 * returned, used is latched and no further power event can request a signal.
 * ADC thresholds assume nominal3.3V and a verified1:1 divider, not calibration.
 */
enum { POWER_WAIT,POWER_CANDIDATE,POWER_BOUNCE,POWER_FIRE,POWER_UNSTABLE,POWER_EXPIRED };
enum {
    POWER_OFF_MAX=310, POWER_ON_MIN=2483, POWER_HOLD_MIN=2172,
    POWER_OFF_US=100000, POWER_HIGH_MIN_US=20,
    POWER_TIMEOUT_S=600, POWER_MAX_DELAY_US=20000, POWER_SETTLE_MAX_US=50000
};
typedef struct {
    bool armed,used,off_tracking,edge_seen,settling;
    uint64_t off_since,deadline,edge_at,settle_begin;
    uint32_t delay_us,bounces;
} power_gate_t;
static inline unsigned power_poll(power_gate_t *g,uint16_t adc,uint64_t now)
{
    if (adc<=POWER_OFF_MAX) {
        if (!g->off_tracking) { g->off_tracking=true; g->off_since=now; }
    } else g->off_tracking=false;
    if (!g->armed) return POWER_WAIT;
    if (now>=g->deadline) { g->armed=false; return POWER_EXPIRED; }
    if (g->settling && now-g->settle_begin>=POWER_SETTLE_MAX_US) {
        g->armed=false; g->used=true; return POWER_UNSTABLE;
    }
    if (!g->edge_seen) {
        if (adc<POWER_ON_MIN) return POWER_WAIT;
        if (!g->settling) { g->settling=true; g->settle_begin=now; }
        g->edge_seen=true; g->edge_at=now; return POWER_CANDIDATE;
    }
    if (adc<POWER_HOLD_MIN) {
        g->edge_seen=false; g->bounces++; return POWER_BOUNCE;
    }
    uint32_t wait=g->delay_us<POWER_HIGH_MIN_US?POWER_HIGH_MIN_US:g->delay_us;
    if (now-g->edge_at<wait) return POWER_WAIT;
    g->armed=false; g->used=true; return POWER_FIRE;
}
static inline bool power_arm(power_gate_t *g,uint16_t adc,uint64_t now,uint32_t delay_us)
{
    if (g->armed || g->used || delay_us>POWER_MAX_DELAY_US) return false;
    power_poll(g,adc,now);
    if (!g->off_tracking || now-g->off_since<POWER_OFF_US) return false;
    g->armed=true; g->edge_seen=false; g->settling=false; g->bounces=0;
    g->delay_us=delay_us; g->deadline=now+(uint64_t)POWER_TIMEOUT_S*1000000;
    return true;
}
static inline void power_disarm(power_gate_t *g) { g->armed=false; }
