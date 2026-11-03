/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Decode emitted controller commands independently into a virtual GRAM. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "t3p_display.h"

struct panel {
    uint16_t screen[320*320];
    unsigned calls, writes, fail_at, phase, x0, x1, y0, y1;
};
static struct panel panel;
static uint16_t expected[320*320];
static struct t3p_display display;

static unsigned be16(const uint8_t *p) { return p[0]*256u+p[1]; }
static int rectangle(void *context,uint16_t xs,uint16_t xe,uint16_t ys,uint16_t ye,
                     const uint16_t *pixels,uint32_t n)
{
    struct panel *p=context;
    p->calls++;
    if(p->fail_at==p->calls)return -1;
    assert(!p->phase && (uintptr_t)pixels%2==0);
    assert(xs<=xe && xe<320 && ys<=ye && ye<320);
    assert(n==(uint32_t)(xe-xs+1)*(ye-ys+1));
    unsigned cursor=0;
    for(unsigned y=ys;y<=ye;y++) for(unsigned x=xs;x<=xe;x++)
        p->screen[y*320+x]=pixels[cursor++];
    p->writes++;
    return 0;
}
static int command(void *context, uint8_t cmd, const uint8_t *data, uint32_t n)
{
    struct panel *p=context;
    p->calls++;
    if(p->fail_at==p->calls) return -1;
    if(cmd==0x2a) {
        assert(p->phase==0 && n==4);
        p->x0=be16(data); p->x1=be16(data+2); p->phase=1;
    } else if(cmd==0x2b) {
        assert(p->phase==1 && n==4);
        p->y0=be16(data); p->y1=be16(data+2); p->phase=2;
    } else {
        assert(cmd==0x2c && p->phase==2);
        assert(p->x0<=p->x1 && p->x1<320 && p->y0<=p->y1 && p->y1<320);
        assert(n==2u*(p->x1-p->x0+1)*(p->y1-p->y0+1));
        unsigned cursor=0;
        for(unsigned y=p->y0;y<=p->y1;y++) for(unsigned x=p->x0;x<=p->x1;x++) {
            p->screen[y*320+x]=(uint16_t)be16(data+cursor); cursor+=2;
        }
        p->writes++; p->phase=0;
    }
    return 0;
}
static uint32_t random_state=17;
static unsigned next(void) {
    random_state=random_state*1664525u+1013904223u; return random_state;
}
static void column(unsigned x,unsigned y,unsigned height,unsigned ox,unsigned oy)
{
    uint16_t colors[240];
    for(unsigned i=0;i<height;i++) {
        colors[i]=(uint16_t)(next()>>9);
        expected[(y+oy+i)*320+x+ox]=colors[i];
    }
    assert(t3p_display_column(&display,(int)x,(int)y,(int)height,colors)==0);
}
static void exercise(unsigned size,unsigned offset,int native)
{
    struct t3p_display_bus bus={.context=&panel,.command=native ? NULL : command,
                              .rectangle=native ? rectangle : NULL};
    struct t3p_display_config cfg={240,240,(uint16_t)offset,(uint16_t)offset,(uint16_t)size,(uint16_t)size};
    memset(&panel,0,sizeof(panel)); memset(expected,0,sizeof(expected));
    assert(t3p_display_init(&display,&bus,&cfg)==0 && panel.calls==0);
    for(unsigned x=0;x<240;x++) column(x,0,240,offset,offset);
    assert(t3p_display_flush(&display)==0);
    assert(panel.writes==30 && display.pixel_bytes==240*240*2);
    /* Mixed heights, gaps, backwards columns, overlaps and partial batches.
     * Compare all GRAM, including every pixel outside the viewport. */
    for(unsigned i=0;i<3000;i++) {
        unsigned y=next()%240, h=1+next()%(240-y), x=next()%240;
        for(unsigned j=0;j<1+i%8 && x+j<240;j++) column(x+j,y,h,offset,offset);
        if(i%17==0) {
            assert(t3p_display_flush(&display)==0);
            assert(memcmp(expected,panel.screen,sizeof(expected))==0);
        }
    }
    assert(t3p_display_flush(&display)==0);
    assert(memcmp(expected,panel.screen,sizeof(expected))==0);
    uint16_t red=0xf800;
    unsigned calls=panel.calls;
    assert(t3p_display_column(&display,-1,0,1,&red)==-1);
    assert(t3p_display_column(&display,240,0,1,&red)==-1);
    assert(t3p_display_column(&display,0,-1,1,&red)==-1);
    assert(t3p_display_column(&display,0,239,2,&red)==-1);
    assert(t3p_display_column(&display,0,0,INT_MAX,&red)==-1);
    assert(t3p_display_column(&display,0,0,0,&red)==-1);
    assert(t3p_display_column(&display,0,0,1,NULL)==-1);
    assert(panel.calls==calls);
}
int main(void)
{
    struct t3p_display_bus bus={.context=&panel,.command=command};
    struct t3p_display_config cfg={240,240,40,40,320,320};
    uint16_t red=0xf800;
    for(int native=0;native<2;native++) {
        exercise(240,0,native); exercise(320,40,native);
    }
    for(unsigned fail=1;fail<=3;fail++) {
        memset(&panel,0,sizeof(panel)); panel.fail_at=fail;
        assert(t3p_display_init(&display,&bus,&cfg)==0);
        assert(t3p_display_column(&display,0,0,1,&red)==0);
        assert(t3p_display_flush(&display)==-2);
        assert(panel.calls==fail);
        assert(t3p_display_flush(&display)==-2);
        assert(t3p_display_column(&display,1,0,1,&red)==-2);
        assert(panel.calls==fail && display.windows==0);
    }
    bus.command=NULL;bus.rectangle=rectangle;
    memset(&panel,0,sizeof(panel));panel.fail_at=1;
    assert(t3p_display_init(&display,&bus,&cfg)==0);
    assert(t3p_display_column(&display,239,239,1,&red)==0);
    assert(t3p_display_flush(&display)==-2 && panel.calls==1);
    assert(t3p_display_column(&display,0,0,1,&red)==-2);
    assert(t3p_display_flush(&display)==-2 && panel.calls==1);
    /* Supplying both transports or neither invalidates a prior instance. */
    bus.command=command;
    assert(t3p_display_init(&display,&bus,&cfg)==-1);
    assert(t3p_display_flush(&display)==-1);
    bus.command=NULL;bus.rectangle=NULL;
    assert(t3p_display_init(&display,&bus,&cfg)==-1);
    bus.command=command;
    cfg.offset_x=81;
    assert(t3p_display_init(&display,&bus,&cfg)==-1);
    assert(t3p_display_column(&display,0,0,1,&red)==-1);
    cfg.offset_x=65535;
    assert(t3p_display_init(&display,&bus,&cfg)==-1);
    assert(t3p_display_init(NULL,&bus,&cfg)==-1);
    assert(t3p_display_flush(NULL)==-1);
    puts("PASS: command/native-word GRAM round trips, 240/320 profiles, partial batching, alignment, bounds, invalid transports and latched failures");
    return 0;
}
