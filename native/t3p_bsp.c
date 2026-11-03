/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Register operations are transcribed from stock3.0.3 and cross-checked with
 * the pinned official BR35 SDK register definitions. This first backend uses
 * synchronous manual DBI transactions, not the OS/DMA driver entry points.
 */
#include "t3p_bsp.h"
#define DBI 0xfe2200u
#define TIMER 0xfd0600u
#define PA 0xfd3000u
#define PC 0xfd3200u
#define LCDPG 0xfd3e60u
#define DISPLAY_CLOCK 0xfe1908u
#define PND (DBI+4)
#define PCLR (DBI+8)
#define HDLCON (DBI+0xf4)
#define HDLBUF (DBI+0xf8)
#define POLL_LIMIT 2000000u
int t3p_bsp_lcd_sequence(struct t3p_bsp *);
static uint32_t rd(struct t3p_bsp *b,uint32_t a){return b->io.read32(b->io.context,a);}
static void wr(struct t3p_bsp *b,uint32_t a,uint32_t v){b->io.write32(b->io.context,a,v);}
static void change(struct t3p_bsp *b,uint32_t a,uint32_t clear,uint32_t set)
{ wr(b,a,(rd(b,a)&~clear)|set); }
uint32_t t3p_bsp_millis(struct t3p_bsp *b)
{
    /* BR35 standard24MHz timer source6, predivider selector10 => 2^12.
     * 24MHz/4096 = 5859.375Hz; 64/375 milliseconds per counter increment.
     * Poll at least once per 11.18 seconds, including during LCD transfers.
     */
    uint16_t now=(uint16_t)rd(b,TIMER+4);
    uint32_t elapsed=(uint16_t)(now-b->previous_tick);
    b->previous_tick=now;
    b->fraction+=elapsed*64u;
    b->milliseconds+=b->fraction/375u;
    b->fraction%=375u;
    return b->milliseconds;
}
int t3p_bsp_delay(struct t3p_bsp *b,uint32_t ms)
{
    if(!b || b->failed || !b->ready || ms>1000)return -1;
    uint32_t start=t3p_bsp_millis(b),last=start,stalled=0;
    while((uint32_t)(t3p_bsp_millis(b)-start)<ms) {
        if(last!=b->milliseconds){last=b->milliseconds;stalled=0;}
        else if(++stalled==POLL_LIMIT){b->failed=1;return -2;}
    }
    return 0;
}
static int operation(struct t3p_bsp *b,uint32_t op)
{
    if(b->failed)return -2;
    /* cf_hdl_done is W1C. Clear stale completion before each new operation.
     * Poll until hardware completion so the caller may reuse its buffer.
     */
    wr(b,PCLR,1);
    wr(b,HDLCON,op);
    uint32_t begin=t3p_bsp_millis(b);
    for(uint32_t polls=0;polls<POLL_LIMIT;polls++) {
        if(rd(b,PND)&1u){wr(b,PCLR,1);b->operations++;return 0;}
        if((uint32_t)(t3p_bsp_millis(b)-begin)>20u)break;
    }
    b->failed=1;
    /* Stop output rather than continue with an uncertain chip-select state. */
    wr(b,DBI+0x28,0);
    return -2;
}
static int byte(struct t3p_bsp *b,uint8_t value,int command)
{
    wr(b,HDLBUF,value);
    /* Stock c02e188 uses line2 for 4-wire command, line1 for payload bytes. */
    return operation(b,8u|(command ? 0x10u : 0));
}
int t3p_bsp_command(struct t3p_bsp *b,uint8_t cmd,const uint8_t *data,uint32_t size)
{
    if(!b || !b->ready || b->failed)return -2;
    if(size>3840u || (size && !data))return -1;
    if(operation(b,0) || operation(b,2) || byte(b,cmd,1) || operation(b,3))return -2;
    for(uint32_t i=0;i<size;i++)if(byte(b,data[i],0))return -2;
    return operation(b,1);
}
#ifdef T3P_DBI_DMA
/* Stock 3.0.3 c02e754 configures image dimensions and c02ebec
 * starts a configuration task. Poll cf_task_done with its IRQ disabled. */
#ifdef T3P_DBI_ASYNC
static uint8_t dma_pending;
static uint32_t dma_begin;

int t3p_bsp_dma_wait(struct t3p_bsp *b)
{
    if(b->failed)return -2;
    if(!dma_pending)return 0;
    for(uint32_t polls=0;polls<POLL_LIMIT;polls++) {
        if(rd(b,PND)&2u) {
            wr(b,PCLR,2);
            dma_pending=0;
            b->operations++;
            return 0;
        }
        if(t3p_bsp_millis(b)-dma_begin>40u)break;
    }
    b->failed=1;
    wr(b,DBI+0x28,0);
    wr(b,DBI+0x24,0);
    return -2;
}
#endif
static int dma_rectangle(struct t3p_bsp *b,const uint16_t *pixels,
                         uint32_t width,uint32_t height)
{
    uint32_t row_bytes=width*2u;
    wr(b,DBI+0x40,0); /* RGB565, native little-endian source */
    wr(b,DBI+0x44,height-1u);
    wr(b,DBI+0x48,row_bytes-1u);
    wr(b,DBI+0x4c,row_bytes-1u);
    wr(b,DBI+0x50,(uint32_t)(uintptr_t)pixels);
    wr(b,DBI+0xcc,width*height-1u);
    /* START, CMD, DCX1, HACT, END. RAMWR starts this rectangle. */
#ifdef T3P_DBI_ACTIVE_LINE
    /* Stock c02eb42/c02eba6 writes the mask to HACT+0x1c (ALINE).
     * c02eaee inserts the RAMWR command in ALINE's companion register. */
    wr(b,DBI+0xd8,0);
    wr(b,DBI+0xe0,0);
    wr(b,DBI+0xe8,0x1a0bu);
    wr(b,DBI+0xec,0x2cu);
#else
    wr(b,DBI+0xd8,0x1a0bu);
    wr(b,DBI+0xdc,0x2cu);
    wr(b,DBI+0xe0,0);
    wr(b,DBI+0xe8,0);
#endif
    wr(b,PCLR,2);
    wr(b,DBI+0x20,2);
#ifdef T3P_DBI_ASYNC
    dma_begin=t3p_bsp_millis(b);
    dma_pending=1;
    return 0;
#else
    uint32_t begin=t3p_bsp_millis(b);
    for(uint32_t polls=0;polls<POLL_LIMIT;polls++) {
        if(rd(b,PND)&2u) {
            wr(b,PCLR,2);
            b->operations++;
            return 0;
        }
        if(t3p_bsp_millis(b)-begin>40u)break;
    }
    b->failed=1;
    wr(b,DBI+0x28,0);
    wr(b,DBI+0x24,0);
    return -2;
#endif
}
#endif
int t3p_bsp_rectangle(void *context,uint16_t xs,uint16_t xe,uint16_t ys,uint16_t ye,
                      const uint16_t *pixels,uint32_t count)
{
    struct t3p_bsp *b=context;
    if(!b || !b->ready || b->failed)return -2;
    if(xs>xe || xe>=240 || ys>ye || ye>=240 || !pixels ||
       (uintptr_t)pixels%2 || count!=(uint32_t)(xe-xs+1)*(ye-ys+1) || count>1920)return -1;
#ifdef T3P_DBI_ASYNC
    if(t3p_bsp_dma_wait(b))return -2;
#endif
    uint8_t area[4]={xs>>8,xs,xe>>8,xe};
    if(t3p_bsp_command(b,0x2a,area,4))return -2;
    area[0]=ys>>8;area[1]=ys;area[2]=ye>>8;area[3]=ye;
    if(t3p_bsp_command(b,0x2b,area,4))return -2;
#ifdef T3P_DBI_DMA
    uintptr_t address=(uintptr_t)pixels;
#ifdef T3P_DBI_ASYNC
    const unsigned dma_width=4;
#else
    const unsigned dma_width=8;
#endif
    if(xe-xs+1==dma_width && ye-ys+1>=32 && !(address&3u) &&
       address>=0x101a80u && address<=0x136e00u &&
       count*2u<=0x136e00u-address)
        return dma_rectangle(b,pixels,dma_width,ye-ys+1);
#endif
    if(operation(b,0)||operation(b,2)||byte(b,0x2c,1)||operation(b,3))return -2;
    uint32_t i=0;
    for(;i+1<count;i+=2) {
        uint32_t a=pixels[i], c=pixels[i+1];
        /* Stock DBI packs the earliest byte in bits 7:0, with num=3
         * for four bytes. RGB565 still goes to the panel high byte first. */
        wr(b,HDLBUF,(a>>8)|((a&255u)<<8)|((c>>8)<<16)|((c&255u)<<24));
        if(operation(b,8u|(3u<<7)))return -2;
    }
    for(;i<count;i++) {
        if(byte(b,(uint8_t)(pixels[i]>>8),0)||byte(b,(uint8_t)pixels[i],0))return -2;
    }
    return operation(b,1);
}
void t3p_bsp_backlight(struct t3p_bsp *b,int enabled)
{
    /* Official power_gate_open_drain_output: LCDPG bit1 sinks low (active),
     * bit0 chooses PWM rather than the static open-drain output. */
    change(b,LCDPG,3u,enabled ? 2u : 0u);
}
int t3p_bsp_init(struct t3p_bsp *b,const struct t3p_bsp_io *io)
{
    if(!b || !io || !io->read32 || !io->write32 || !io->write8)return -1;
    b->io=*io;b->milliseconds=b->fraction=b->operations=0;
    b->previous_tick=0;b->ready=b->failed=0;
    /* WDT disable sequence from SDK wdt_disable and stock WDT register use. */
    io->write8(io->context,0xf2c818u,0x40);
    io->write8(io->context,0xf2c418u,0xef);
    wr(b,TIMER,0x4000);wr(b,TIMER+4,0);wr(b,TIMER+8,0xffff);wr(b,TIMER+12,0);
    wr(b,TIMER,(6u<<10)|(10u<<4)|1u);
    uint32_t polls=0;
    while(!(rd(b,TIMER+4)&0xffffu))if(++polls==POLL_LIMIT){b->failed=1;return -2;}
    b->previous_tick=(uint16_t)rd(b,TIMER+4);
    b->ready=1;
    t3p_bsp_backlight(b,0);
    return 0;
}
int t3p_bsp_lcd_init(struct t3p_bsp *b)
{
    if(!b || !b->ready || b->failed)return -2;
    /* Stock DBI clock row9: source3, div0, div3, nominal6MHz. Retain all
     * unrelated bits and preserve the bootloader's CPU/Flash clock settings. */
    change(b,DISPLAY_CLOCK,0x2ffu,3u|(3u<<6));
    change(b,DISPLAY_CLOCK,0,(1u<<9)|(1u<<10));
    wr(b,DBI+0x0c,0);wr(b,DBI+0x28,0);wr(b,PCLR,15);
    /* Hardware owns PA7 CS, PA8 data, PA9 command/data, PA12 clock. */
    uint32_t pins=(1u<<7)|(1u<<8)|(1u<<9)|(1u<<12);
    /* Stock c02dc06 calls gpio_set_die(...,1), then
     * gpio_set_direction(...,0). DIR=1 leaves the LCD bus high impedance. */
    change(b,PA+0x0c,0,pins);
    change(b,PA+0x08,pins,0);
    change(b,PA+0x14,pins,0);change(b,PA+0x18,pins,0);
    change(b,PA+0x1c,pins,0);change(b,PA+0x20,pins,0);
    wr(b,DBI+0xd0,3);wr(b,DBI+0x40,0);
    wr(b,DBI+0xf0,0x1010);
    wr(b,DBI+0x28,3u|(1u<<18)|(1u<<21)|(1u<<25)|(1u<<26));
    wr(b,DBI+0x24,1);
    /* PC0 reset: preload high, enable ordinary output, high/low/high. */
    change(b,PC+4,0,1);change(b,PC+8,1,0);
    change(b,PC+0x0c,0,1);
    if(t3p_bsp_delay(b,10))return -2;
    change(b,PC+4,1,0);if(t3p_bsp_delay(b,10))return -2;
    change(b,PC+4,0,1);if(t3p_bsp_delay(b,100))return -2;
    return t3p_bsp_lcd_sequence(b);
}
