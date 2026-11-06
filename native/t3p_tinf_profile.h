/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_TINF_PROFILE_H
#define T3P_TINF_PROFILE_H
#include <stdint.h>
enum {
 TINF_P_TREE, TINF_P_PAYLOAD, TINF_P_STORED, TINF_P_CALLS,
 TINF_P_DYNAMIC, TINF_P_FIXED, TINF_P_STORED_BLOCKS,
 TINF_P_SYMBOL_CALLS, TINF_P_SYMBOL_TICKS, TINF_P_SYMBOL_SAMPLES,
 TINF_P_EXTRA_CALLS, TINF_P_EXTRA_TICKS, TINF_P_EXTRA_SAMPLES,
 TINF_P_PROBE_TICKS, TINF_P_PROBES, TINF_P_SHIFT, TINF_P_COUNT
};
extern uint32_t t3p_tinf_profile[TINF_P_COUNT];
/* Same TIMER0 counter as the existing timebase, without millisecond division.
 * Delta endpoints must be less than one 16-bit wrap (about 11.18 s) apart. */
uint32_t t3p_tinf_clock(void);
void t3p_tinf_profile_reset(void);
void t3p_tinf_charge(unsigned bucket,uint32_t start);
#ifdef T3P_TINF_PROFILE_SAMPLES
uint32_t t3p_tinf_sample_clock(void);
#endif
static inline uint32_t t3p_tinf_sample_begin(unsigned calls_bucket)
{
    uint32_t call=t3p_tinf_profile[calls_bucket]++;
#ifdef T3P_TINF_PROFILE_SAMPLES
    if(!(call&63u))return t3p_tinf_sample_clock();
#else
    (void)call;
#endif
    return 0;
}
static inline void t3p_tinf_sample_end(unsigned calls_bucket,uint32_t start)
{
#ifdef T3P_TINF_PROFILE_SAMPLES
    if(start) {
        t3p_tinf_charge(calls_bucket+1,start);
        t3p_tinf_profile[calls_bucket+2]++;
    }
#else
    (void)calls_bucket;(void)start;
#endif
}
#endif
