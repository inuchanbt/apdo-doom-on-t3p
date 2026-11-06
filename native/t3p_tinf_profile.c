/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_tinf_profile.h"
uint32_t t3p_tinf_profile[TINF_P_COUNT];
#ifndef T3P_TINF_PROFILE_HOST
uint32_t t3p_tinf_clock(void)
{ return *(volatile uint32_t *)(uintptr_t)0xfd0604u & 0xffffu; }
#endif
void t3p_tinf_profile_reset(void)
{
 for(unsigned i=0;i<TINF_P_COUNT;i++)t3p_tinf_profile[i]=0;
#ifdef T3P_TINF_PROFILE_SAMPLES
 t3p_tinf_profile[TINF_P_SHIFT]=6;
#endif
}
void t3p_tinf_charge(unsigned bucket,uint32_t start)
{ t3p_tinf_profile[bucket]+=(uint16_t)(t3p_tinf_clock()-start); }
#ifdef T3P_TINF_PROFILE_SAMPLES
uint32_t t3p_tinf_sample_clock(void)
{
    /* Empty pair estimates timer-read/return overhead; nested, quantized
     * intervals must not be corrected automatically with this value. */
    uint32_t before=t3p_tinf_clock();
    t3p_tinf_charge(TINF_P_PROBE_TICKS,before);
    t3p_tinf_profile[TINF_P_PROBES]++;
    /* A flag distinguishes a sample at counter zero from no sample. */
    return 0x10000u|t3p_tinf_clock();
}
#endif
