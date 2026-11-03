/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_boot.h"
static int ram_range(uint32_t address,uint32_t count)
{ return address>=0x100000u && address<=0x137000u && count<=0x137000u-address; }
int t3p_boot_capture(struct t3p_boot_snapshot *s,uint32_t address,t3p_boot_read read,void *context)
{
    if(!s)return -1;
    s->valid=0;
    if(!read || address%4 || !ram_range(address,24) ||
       read(context,address,s->context,24))return -1;
    /* The actual bootloader handoff must not be interpreted as the SDK's
     * BOOT_DEVICE_INFO: a word may be Flash capacity rather than a pointer.
     * Preserve the root words; optionally snapshot the RAM object referenced
     * by word2, as seen at the stock bootloader's app call. */
    volatile uint8_t *aux=s->auxiliary;
    for(uint32_t i=0;i<32;i++)aux[i]=0;
    uint32_t secondary=s->context[2];
    if(!(secondary%4) && ram_range(secondary,32))
        (void)read(context,secondary,s->auxiliary,32);
    /* Stock3.0.3 copies these exact fields from r0. Keep the words opaque:
     * the SDK boot_info ABI has not been established by device readback.
     * The initial backend preserves SFC registers, rather than reconfiguring
     * them from an assumed interpretation of these fields. */
    s->valid=T3P_BOOT_MAGIC;
    return 0;
}
#ifndef T3P_BOOT_HOST
struct t3p_boot_snapshot t3p_boot_snapshot __attribute__((section(".boot_snapshot"),aligned(4)));
static int read_ram(void *unused,uint32_t address,void *out,uint32_t count)
{
    (void)unused;
    volatile const uint8_t *src=(volatile const uint8_t *)(uintptr_t)address;
    uint8_t *dst=out;
    while(count--)*dst++=*src++;
    return 0;
}
void t3p_runtime_capture_boot(void)
{ (void)t3p_boot_capture(&t3p_boot_snapshot,t3p_boot_parameters[0],read_ram,0); }
#endif
