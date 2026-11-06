/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_present.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FRAMES 24u
static struct t3p_present p;
static uint8_t index_image[120][120];
static uint16_t expected[FRAMES][240*240],gram[240*240],palette[256];
struct deferred {
    const uint16_t *source;
    uint16_t snapshot[16*240];
    unsigned x,remaining,next_x,completed,submits,checks,waits,delay;
    int submit_error,poll_error,wait_error;
};
static struct deferred mock;
static int submit(void *context,uint16_t xs,uint16_t xe,uint16_t ys,uint16_t ye,
                  const uint16_t *pixels,uint32_t count)
{
    struct deferred *m=context;
    assert(!m->source && xs==m->next_x && xe==xs+15 && ys==0 && ye==239);
    assert(count==16u*240u && !((uintptr_t)pixels&3u));
    m->submits++;
    if(m->submit_error)return -1;
    m->source=pixels;m->x=xs;m->remaining=m->delay;
    memcpy(m->snapshot,pixels,sizeof(m->snapshot));
    return 0;
}
static int poll(void *context)
{
    struct deferred *m=context;
    if(m->poll_error)return -1;
    if(!m->source)return 1;
    assert(!memcmp(m->source,m->snapshot,sizeof(m->snapshot)));m->checks++;
    if(--m->remaining)return 0;
    for(unsigned y=0;y<240;y++)for(unsigned x=0;x<16;x++)
        gram[y*240+m->x+x]=m->source[y*16+x];
    m->source=0;m->next_x+=16;
    if(m->next_x==240) {
        assert(m->completed<FRAMES);
        assert(!memcmp(gram,expected[m->completed],sizeof(gram)));
        m->completed++;m->next_x=0;
    }
    return 1;
}
static int wait_dma(void *context)
{
    struct deferred *m=context;m->waits++;
    if(m->wait_error)return -1;
    int status;
    do { status=poll(m); } while(!status);
    return status<0 ? -1 : 0;
}
static void reset(unsigned delay)
{
    memset(&mock,0,sizeof(mock));mock.delay=delay;
    memset(gram,0,sizeof(gram));memset(index_image,0,sizeof(index_image));
    const struct t3p_present_bus bus={&mock,submit,poll,wait_dma};
    assert(!t3p_present_init(&p,&bus));
}
static void run(int overlap,unsigned delay)
{
    reset(delay);
    for(unsigned frame=0;frame<FRAMES;frame++) {
        for(unsigned i=0;i<256;i++)palette[i]=(uint16_t)(i*163u+frame*751u);
        /* All columns change above the HUD; its retained lower rows change
         * only at initialization. DMA reads the prior frame during this work. */
        for(unsigned x=0;x<120;x++) {
            for(unsigned y=0;y<(frame ? 100u : 120u);y++)
                p.frame[p.draw][x][y]=index_image[x][y]=(uint8_t)(x*7u+y*11u+frame*31u);
            assert(!t3p_present_service(&p));
        }
        for(unsigned y=0;y<240;y++)for(unsigned x=0;x<240;x++)
            expected[frame][y*240+x]=palette[index_image[x/2][y/2]];
        assert(!t3p_present_commit(&p,palette,overlap));
        assert(!memcmp(p.frame[p.draw],index_image,sizeof(index_image)));
        assert(p.submitted==frame+1 && p.completed==mock.completed);
        if(overlap)assert(p.active && mock.completed==frame);
        else assert(!p.active && mock.completed==frame+1);
    }
    assert(!t3p_present_drain(&p));
    assert(!p.active && !mock.source && mock.completed==FRAMES && p.completed==FRAMES);
    assert(mock.submits==FRAMES*15 && mock.checks==mock.submits*delay);
}
#ifdef T3P_STARTUP_BANNER
static uint16_t banner_color(unsigned x, unsigned y)
{
    return (uint16_t)(x * 283u + y * 7919u + 1u);
}

static void check_startup_banner(void)
{
    static uint8_t pixels[159 * 37 * 2];

    for (unsigned y = 0; y < 37; y++) {
        for (unsigned x = 0; x < 159; x++) {
            uint16_t color = banner_color(x, y);
            unsigned offset = 2 * (y * 159 + x);
            pixels[offset] = (uint8_t)color;
            pixels[offset + 1] = (uint8_t)(color >> 8);
        }
    }

    reset(5);
    memset(expected[0], 0, sizeof(expected[0]));
    for (unsigned y = 0; y < 50; y++)
        for (unsigned x = 0; x < 216; x++)
            expected[0][(y + 95) * 240 + x + 12] = banner_color(x * 159 / 216, y * 37 / 50);
    assert(t3p_present_startup_banner(&p, pixels) == 0);
    assert(mock.completed == 1 && !mock.source && mock.checks == 75 && mock.waits == 15);
    assert(p.submitted == 0 && !memcmp(p.frame[p.draw], index_image, sizeof(index_image)));
    puts("PASS: centered 216x50 startup banner, black borders, and complete DMA drain");
}
#endif

int main(void)
{
#ifdef T3P_STARTUP_BANNER
    check_startup_banner();
#endif
    for(unsigned d=1;d<=19;d+=3) { run(0,d);run(1,d); }
    assert(t3p_present_init(0,0)==-1 && t3p_present_service(0)==-1);
    reset(5);mock.submit_error=1;
    assert(t3p_present_commit(&p,palette,1)==-2 && p.failed);
    unsigned submissions=mock.submits;
    assert(t3p_present_commit(&p,palette,1)==-2 && mock.submits==submissions);
    reset(5);assert(!t3p_present_commit(&p,palette,1));mock.poll_error=1;
    assert(t3p_present_service(&p)==-2 && p.failed);
    submissions=mock.submits;
    assert(t3p_present_drain(&p)==-2 && mock.submits==submissions);
    reset(5);assert(!t3p_present_commit(&p,palette,1));mock.wait_error=1;
    assert(t3p_present_drain(&p)==-2 && p.failed);
    printf("PASS: 336 presented frames; delayed source ownership, RGB565/2x geometry, palette snapshots, persistent HUD, complete drain, latched failures; state %zu B\n",sizeof(p));
}
