/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Early, timer-independent diagnostics. No initialized RAM, ROM calls or
 * peripheral callbacks: usable before boot-argument capture and BSS init.
 * Delays are uncalibrated; blink counts, not durations, carry information. */
#include <stdint.h>
#include "t3p_signal.h"
static void settle(unsigned units)
{
    while(units--)
        for(volatile unsigned count=0;count<2000000u;count++)
            __asm__ volatile("" ::: "memory");
}
static void light(unsigned enabled)
{
    volatile uint32_t *pg=(volatile uint32_t *)(uintptr_t)0xfd3e60u;
    *pg=(*pg&~3u)|(enabled ? 2u : 0u);
    __asm__ volatile("csync" ::: "memory");
}
void t3p_runtime_signal_entry(void)
{
    /* Stop the watchdog before even the first boot-context dereference. */
    *(volatile uint8_t *)(uintptr_t)0xf2c818u=0x40;
    __asm__ volatile("csync" ::: "memory");
    *(volatile uint8_t *)(uintptr_t)0xf2c418u=0xef;
    __asm__ volatile("csync" ::: "memory");
    light(0);settle(4);light(1);settle(1);
    light(0);settle(1);light(1);settle(1);
}
void t3p_signal_failure(unsigned stage)
{
    if(stage<1 || stage>7)stage=7;
    for(;;) {
        light(0);settle(10);
        for(unsigned i=0;i<stage;i++) {
            light(1);settle(2);light(0);settle(2);
        }
    }
}
