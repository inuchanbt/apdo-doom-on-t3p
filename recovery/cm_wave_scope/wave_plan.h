/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { WAVE_SETUP=15,WAVE_HIGH=1160,WAVE_LOW=1155,WAVE_OVERHEAD=6,WAVE_WORDS=48 };
/* OUT PINS starts at GPIO12: bit0=DM data, bit1=DP clock.
 * BR35 ROM samples PORTUSB.IN bit1; SDK maps that bit to DM.
 * Versions 1..3 used the opposite candidate mapping (bench measurements only).
 */
enum { WAVE_CLOCK_MASK=2,WAVE_DATA_MASK=1 };
static inline uint32_t wave_command(unsigned pins,unsigned ticks,bool finish)
{
    return pins | (finish?4u:((ticks-WAVE_OVERHEAD)<<3));
}
static inline void wave_plan(uint32_t words[WAVE_WORDS])
{
    for (unsigned i=0;i<16;i++) {
        unsigned data=(0x16efu>>(15-i))&1u;
        words[i*3]=wave_command(data,i?WAVE_SETUP:WAVE_LOW,false);
        words[i*3+1]=wave_command(data|WAVE_CLOCK_MASK,WAVE_HIGH,false);
        words[i*3+2]=wave_command(data,WAVE_LOW-WAVE_SETUP,i==15);
    }
}
