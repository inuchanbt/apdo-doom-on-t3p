/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include "../recovery/cm_power_probe/stable_power_gate.h"
static power_gate_t ready(uint32_t delay)
{
    power_gate_t g={0}; power_poll(&g,0,0);
    assert(!power_arm(&g,0,POWER_OFF_US-1,delay));
    assert(power_arm(&g,0,POWER_OFF_US,delay)); return g;
}
int main(void)
{
    power_gate_t g={0};
    assert(!power_arm(&g,POWER_ON_MIN,0,100));
    power_poll(&g,0,1);
    assert(!power_arm(&g,POWER_ON_MIN,POWER_OFF_US+1,100));
    assert(!power_arm(&g,0,POWER_OFF_US+2,100));
    assert(!power_arm(&g,0,2*POWER_OFF_US+2,POWER_MAX_DELAY_US+1));
    uint64_t t=POWER_OFF_US+100;
    /* Replay actual observed44us ON /191us OFF. First candidate sends nothing. */
    g=ready(100);
    assert(power_poll(&g,POWER_ON_MIN,t)==POWER_CANDIDATE);
    assert(!g.used && g.armed);
    assert(power_poll(&g,POWER_HOLD_MIN,t+43)==POWER_WAIT);
    assert(power_poll(&g,0,t+44)==POWER_BOUNCE);
    assert(!g.used && g.armed && g.bounces==1);
    assert(power_poll(&g,0,t+234)==POWER_WAIT);
    assert(power_poll(&g,POWER_ON_MIN,t+235)==POWER_CANDIDATE);
    assert(g.edge_at==t+235);
    assert(power_poll(&g,POWER_HOLD_MIN,t+334)==POWER_WAIT);
    assert(power_poll(&g,POWER_HOLD_MIN,t+335)==POWER_FIRE);
    assert(g.used && !g.armed);
    assert(power_poll(&g,0,t+400)==POWER_WAIT);
    assert(power_poll(&g,POWER_ON_MIN,t+500)==POWER_WAIT);
    assert(!power_arm(&g,0,t+POWER_OFF_US+600,100));
    /* Bounces are bounded by50ms from the first candidate; timeout emits none. */
    g=ready(100); assert(power_poll(&g,POWER_ON_MIN,t)==POWER_CANDIDATE);
    assert(power_poll(&g,POWER_HOLD_MIN-1,t+1)==POWER_BOUNCE);
    assert(power_poll(&g,POWER_ON_MIN,t+POWER_SETTLE_MAX_US-1)==POWER_CANDIDATE);
    assert(power_poll(&g,POWER_ON_MIN,t+POWER_SETTLE_MAX_US)==POWER_UNSTABLE);
    assert(g.used && !g.armed);
    /* Hysteresis: a candidate needs4V; once seen,3.5V is enough to hold. */
    g=ready(0);
    assert(power_poll(&g,POWER_ON_MIN-1,t)==POWER_WAIT);
    assert(power_poll(&g,POWER_ON_MIN,t+1)==POWER_CANDIDATE);
    assert(power_poll(&g,POWER_HOLD_MIN,t+20)==POWER_WAIT);
    assert(power_poll(&g,POWER_HOLD_MIN,t+21)==POWER_FIRE);
    /* Native CDC loss uses disarm; no input transition emits after that. */
    g=ready(100); assert(power_poll(&g,POWER_ON_MIN,t)==POWER_CANDIDATE);
    power_disarm(&g); assert(power_poll(&g,POWER_ON_MIN,t+200)==POWER_WAIT);
    assert(!g.used);
    /* Arming deadline always wins, even over a fresh candidate. */
    g=ready(100); assert(power_poll(&g,POWER_ON_MIN,g.deadline)==POWER_EXPIRED);
    assert(!g.used && !g.armed);
    /* Selectable delays hold off through the exact boundary, then latch once. */
    const uint32_t delays[]={100,1000,5000};
    for (unsigned i=0;i<sizeof(delays)/sizeof(delays[0]);i++) {
        g=ready(delays[i]);
        assert(power_poll(&g,POWER_ON_MIN,t)==POWER_CANDIDATE);
        assert(power_poll(&g,POWER_HOLD_MIN,t+delays[i]-1)==POWER_WAIT);
        assert(!g.used);
        assert(power_poll(&g,POWER_HOLD_MIN,t+delays[i])==POWER_FIRE);
        assert(g.used && !g.armed);
        assert(power_poll(&g,POWER_ON_MIN,t+delays[i]+100)==POWER_WAIT);
        assert(!power_arm(&g,0,t+POWER_OFF_US+delays[i]+200,delays[i]));
    }
    return 0;
}
