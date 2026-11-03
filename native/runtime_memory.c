/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>

/* No compiler/ROM memcpy or memset imports before initialized C storage. */
void t3p_runtime_zero_words(volatile uint32_t *to,uint32_t words)
{
    while(words--) *to++=0;
}
void t3p_runtime_copy_words(volatile uint32_t *to,
                          const volatile uint32_t *from,uint32_t words)
{
    while(words--) *to++=*from++;
}

#ifndef T3P_RUNTIME_MEMORY_HOST
uint32_t t3p_boot_parameters[4] __attribute__((section(".boot_state"),aligned(4)));
extern uint32_t __runtime_data_start[],__runtime_data_end[],__runtime_data_load[];
extern uint32_t __runtime_bss_start[],__runtime_bss_end[];
void t3p_runtime_memory_init(void)
{
    t3p_runtime_copy_words(__runtime_data_start,__runtime_data_load,
        ((uintptr_t)__runtime_data_end-(uintptr_t)__runtime_data_start)/4u);
    t3p_runtime_zero_words(__runtime_bss_start,
        ((uintptr_t)__runtime_bss_end-(uintptr_t)__runtime_bss_start)/4u);
}
#endif
