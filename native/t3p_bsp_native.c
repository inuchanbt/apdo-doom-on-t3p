/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_bsp.h"
static uint32_t native_read32(void *context,uint32_t address)
{ (void)context;return *(volatile uint32_t *)(uintptr_t)address; }
static void native_write32(void *context,uint32_t address,uint32_t value)
{ (void)context;*(volatile uint32_t *)(uintptr_t)address=value;__asm__ volatile("csync" ::: "memory"); }
static void native_write8(void *context,uint32_t address,uint8_t value)
{ (void)context;*(volatile uint8_t *)(uintptr_t)address=value;__asm__ volatile("csync" ::: "memory"); }
void t3p_bsp_native_io(struct t3p_bsp_io *io)
{
    io->context=0;io->read32=native_read32;io->write32=native_write32;io->write8=native_write8;
}
