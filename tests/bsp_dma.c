/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_bsp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct mock {
    uint32_t dbi[64],pa[32],pc[32],clock,pg,tick;
    unsigned writes,tasks,remaining,stuck;
};
static struct mock m;
static uint32_t *reg(uint32_t address)
{
    if(address>=0xfe2200 && address<0xfe2300)return &m.dbi[(address-0xfe2200)/4];
    if(address>=0xfd3000 && address<0xfd3080)return &m.pa[(address-0xfd3000)/4];
    if(address>=0xfd3200 && address<0xfd3280)return &m.pc[(address-0xfd3200)/4];
    if(address==0xfe1908)return &m.clock;
    if(address==0xfd3e60)return &m.pg;
    assert(!"Unexpected MMIO");return 0;
}
static uint32_t read32(void *context,uint32_t address)
{
    (void)context;
    if(address==0xfd0604) { uint32_t tick=m.tick;m.tick=(tick+6)&65535;return tick; }
    if(address==0xfe2204 && m.remaining && !m.stuck && !--m.remaining)m.dbi[1]|=2;
    return *reg(address);
}
static void write32(void *context,uint32_t address,uint32_t value)
{
    (void)context;m.writes++;
    if(address>=0xfd0600 && address<=0xfd060c) { if(address==0xfd0604)m.tick=value;return; }
    if(address==0xfe2208) { m.dbi[1]&=~value;return; }
    if(address==0xfe22f4) { assert(!m.remaining);m.dbi[1]|=1; }
    if(address==0xfe2220 && value==2) {
        assert(!(m.dbi[1]&2)); /* Stale task completion was cleared. */
        assert(m.dbi[0x44/4]==239 && m.dbi[0x48/4]==31 && m.dbi[0x4c/4]==31);
        assert(m.dbi[0x50/4]==0x120000 && m.dbi[0xcc/4]==3839);
        assert(m.dbi[0xe8/4]==0x1a0b && m.dbi[0xec/4]==0x2c);
        m.tasks++;m.remaining=4;
    }
    *reg(address)=value;
}
static void write8(void *context,uint32_t address,uint8_t value)
{ (void)context;(void)value;assert(address==0xf2c818 || address==0xf2c418); }
int main(void)
{
    struct t3p_bsp b;
    const struct t3p_bsp_io io={0,read32,write32,write8};
    assert(!t3p_bsp_init(&b,&io));assert(!t3p_bsp_lcd_init(&b));
    assert(t3p_bsp_dma_poll(&b)==1);
    /* DMA only records the numeric SRAM source here; no fake address is
     * dereferenced. Pixel ownership is exercised by the separate presenter test. */
    const uint16_t *source=(const uint16_t *)(uintptr_t)0x120000;
    unsigned writes=m.writes;
    assert(t3p_bsp_rectangle(&b,0,16,0,239,source,17*240)==-1 && m.writes==writes);
    m.dbi[1]|=2;
    assert(!t3p_bsp_rectangle(&b,0,15,0,239,source,16*240));
    assert(m.tasks==1 && t3p_bsp_dma_poll(&b)==0);
    assert(t3p_bsp_dma_poll(&b)==0 && t3p_bsp_dma_poll(&b)==0);
    assert(t3p_bsp_dma_poll(&b)==1 && !t3p_bsp_dma_wait(&b));
    m.remaining=0;
    assert(!t3p_bsp_rectangle(&b,16,31,0,239,source,16*240));
    assert(!t3p_bsp_dma_wait(&b));m.remaining=0;
    assert(!t3p_bsp_rectangle(&b,32,47,0,239,source,16*240));m.stuck=1;
    int status=0;
    for(unsigned i=0;i<100 && !status;i++)status=t3p_bsp_dma_poll(&b);
    assert(status==-2 && b.failed && !m.dbi[0x28/4] && !m.dbi[0x24/4]);
    writes=m.writes;
    assert(t3p_bsp_dma_poll(&b)==-2 && t3p_bsp_dma_wait(&b)==-2 && m.writes==writes);
    puts("PASS: real MMIO backend, 16-column DMA dimensions/source/active-line task, stale completion, nonblocking busy/idle, blocking drain and timeout output shutdown");
}
