/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include "../recovery/cm_rom_probe/ack_observer.h"
int main(void)
{
    ack_observer_t a;
    ack_begin(&a,1000);
    ack_sample(&a,0,1001); assert(!a.saw_low); /* settling */
    ack_sample(&a,1,1005); ack_sample(&a,1,3000);
    assert(a.done && !ack_low_candidate(&a)); /* no low pulse */
    ack_begin(&a,1000);
    ack_sample(&a,0,1010); ack_sample(&a,1,1200); ack_sample(&a,1,1220);
    assert(!a.saw_low && !a.done); /* short low rejected */
    ack_sample(&a,0,1300); ack_sample(&a,1,2300);
    ack_sample(&a,1,2319); assert(!a.done);
    ack_sample(&a,1,2320); assert(ack_low_candidate(&a) && a.low_us==1000);
    ack_begin(&a,1000); ack_sample(&a,0,1010);
    ack_sample(&a,1,1700); ack_sample(&a,0,1701); assert(!a.done); /* high glitch */
    ack_sample(&a,1,2000); ack_sample(&a,1,2020);
    assert(ack_low_candidate(&a) && a.low_us==990);
    ack_begin(&a,1000); ack_sample(&a,0,1010); ack_sample(&a,0,9000);
    assert(a.done && a.saw_low && !ack_low_candidate(&a)); /* held-low timeout */
    return 0;
}
