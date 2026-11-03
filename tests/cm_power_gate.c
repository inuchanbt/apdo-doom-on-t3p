/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include "../recovery/cm_rom_probe/power_gate.h"

static power_gate_t ready(uint32_t delay)
{
    power_gate_t g={0};
    assert(power_poll(&g,0,0)==POWER_WAIT);
    assert(!power_arm(&g,0,POWER_OFF_US-1,delay));
    assert(power_arm(&g,0,POWER_OFF_US,delay));
    return g;
}
int main(void)
{
    power_gate_t g={0};
    /* Already-powered or floating/stale-off input cannot arm. */
    assert(!power_arm(&g,POWER_ON_MIN,0,100));
    power_poll(&g,0,1);
    assert(!power_arm(&g,POWER_ON_MIN,POWER_OFF_US+1,100));
    assert(!power_arm(&g,0,POWER_OFF_US+2,100));
    assert(!power_arm(&g,0,2*POWER_OFF_US+2,POWER_MAX_DELAY_US+1));
    g=ready(100);
    assert(!power_arm(&g,0,POWER_OFF_US+1,100));
    uint64_t t=POWER_OFF_US+100;
    assert(power_poll(&g,POWER_ON_MIN-1,t-1)==POWER_WAIT);
    assert(power_poll(&g,POWER_ON_MIN,t)==POWER_EDGE);
    assert(g.used && g.edge_at==t);
    assert(power_poll(&g,POWER_HOLD_MIN,t+99)==POWER_WAIT);
    assert(power_poll(&g,POWER_HOLD_MIN,t+100)==POWER_FIRE);
    assert(power_poll(&g,POWER_ON_MIN,t+200)==POWER_WAIT);
    assert(!power_arm(&g,0,t+POWER_OFF_US,100));
    /* A short glitch is consumed and cancelled, never retriggered. */
    g=ready(0);
    assert(power_poll(&g,POWER_ON_MIN,t)==POWER_EDGE);
    assert(power_poll(&g,POWER_ON_MIN,t+19)==POWER_WAIT);
    assert(power_poll(&g,POWER_HOLD_MIN-1,t+20)==POWER_DROPPED);
    assert(!g.armed && g.used);
    assert(power_poll(&g,POWER_ON_MIN,t+21)==POWER_WAIT);
    assert(!power_arm(&g,0,t+POWER_OFF_US,0));
    /* Even delay=0 requires at least 20us above the hold threshold. */
    g=ready(0);
    assert(power_poll(&g,POWER_ON_MIN,t)==POWER_EDGE);
    assert(power_poll(&g,POWER_ON_MIN,t+20)==POWER_FIRE);
    /* Deadline takes precedence over an edge or firing on that sample. */
    g=ready(100);
    assert(power_poll(&g,POWER_ON_MIN,g.deadline)==POWER_EXPIRED);
    assert(!g.used);
    g=ready(100);
    assert(power_poll(&g,POWER_ON_MIN,g.deadline-50)==POWER_EDGE);
    assert(power_poll(&g,POWER_ON_MIN,g.deadline)==POWER_EXPIRED);
    assert(g.used);
    /* Explicit cancellation before an edge never emits. After an edge it
     * retains the consumed attempt; CDC loss will use the same cancellation.
     */
    g=ready(100); power_disarm(&g);
    assert(power_poll(&g,POWER_ON_MIN,t)==POWER_WAIT);
    g=ready(100); assert(power_poll(&g,POWER_ON_MIN,t)==POWER_EDGE);
    power_disarm(&g);
    assert(power_poll(&g,POWER_ON_MIN,t+100)==POWER_WAIT);
    assert(!power_arm(&g,0,t+POWER_OFF_US,100));
    return 0;
}
