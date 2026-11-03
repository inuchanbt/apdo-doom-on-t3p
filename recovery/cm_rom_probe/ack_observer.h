/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Digital observations under a separately verified external 5k bias.
 * This records a low pulse candidate; it cannot establish ROM identity.
 */
typedef struct { uint64_t start,low_start,high_start,low_us; bool saw_low,done; } ack_observer_t;
static inline void ack_begin(ack_observer_t *a,uint64_t now)
{ *a=(ack_observer_t){.start=now}; }
static inline void ack_sample(ack_observer_t *a,bool high,uint64_t now)
{
    if (a->done) return;
    if (now-a->start>=8000) { a->done=true; return; }
    /* Allow the known resistor and cable capacitance to settle after bias on. */
    if (now-a->start<5) return;
    if (!high) {
        if (!a->saw_low) { a->saw_low=true; a->low_start=now; }
        a->high_start=0;
    } else if (a->saw_low) {
        if (!a->high_start) a->high_start=now;
        if (now-a->high_start>=20) {
            uint64_t width=a->high_start-a->low_start;
            if (width>=500) { a->low_us=width; a->done=true; }
            else a->saw_low=false;
        }
    }
    if (!a->saw_low && now-a->start>=2000) a->done=true;
}
static inline bool ack_low_candidate(const ack_observer_t *a)
{ return a->done && a->low_us>=500; }
