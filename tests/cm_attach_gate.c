/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include "../recovery/cm_rom_probe/attach_gate.h"
int main(void)
{
    attach_gate_t g={0};
    /* An already-low or partly-connected input cannot arm. */
    assert(!attach_arm(&g,0,0,1)); assert(!attach_arm(&g,1,0,1));
    assert(attach_arm(&g,1,1,1000)); assert(!attach_arm(&g,1,1,1001));
    assert(g.deadline==1000+600000000);
    assert(attach_poll(&g,0,0,1100)==ATTACH_WAIT);
    assert(attach_poll(&g,0,0,1199)==ATTACH_WAIT);
    /* Contact bounce cancels the continuous-low interval. */
    assert(attach_poll(&g,1,0,1200)==ATTACH_WAIT);
    assert(attach_poll(&g,0,0,1300)==ATTACH_WAIT);
    assert(attach_poll(&g,0,0,1399)==ATTACH_WAIT);
    assert(attach_poll(&g,0,0,1400)==ATTACH_FIRE);
    assert(g.used && !g.armed);
    assert(attach_poll(&g,0,0,99999)==ATTACH_WAIT);
    assert(!attach_arm(&g,1,1,100000));
    /* Deadline wins over the last sample of an otherwise valid low interval. */
    g=(attach_gate_t){0}; assert(attach_arm(&g,1,1,2000));
    assert(attach_poll(&g,0,0,g.deadline-100)==ATTACH_WAIT);
    assert(attach_poll(&g,0,0,g.deadline)==ATTACH_EXPIRED);
    assert(!g.used && !g.armed);
    return 0;
}
