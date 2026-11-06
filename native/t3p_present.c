/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_present.h"
#include <string.h>

_Static_assert(240u%T3P_PRESENT_WIDTH==0,"Only complete DMA bands are submitted");

int t3p_present_init(struct t3p_present *p,const struct t3p_present_bus *bus)
{
    if(!p || !bus || !bus->submit || !bus->poll || !bus->wait)return -1;
    memset(p,0,sizeof(*p));p->bus=*bus;p->ready=1;
    return 0;
}
#ifdef T3P_SEAMLESS_VIEW
/* Insert one 2x pixel row in each group, evenly across the 3D view. */
#define VIEW_ROWS_PER_REPEAT (LCD_VIEW_H / ((LCD_BAR_Y - LCD_VIEW_H) / 2))
_Static_assert(LCD_HUD_Y == LCD_VIEW_H && LCD_VIEW_H % VIEW_ROWS_PER_REPEAT == 0 &&
               VIEW_ROWS_PER_REPEAT % 2 == 0, "View scaling must preserve complete row pairs");

int t3p_present_index_column(struct t3p_present *p, unsigned x, unsigned y,
                             unsigned h, const uint8_t *pixels, unsigned flags)
{
    if (flags & PUMP_HUD) {
        for (unsigned row = 0; row < sizeof(p->message[x]); row++)
            p->message[x][row] = pixels[2 * row];
        return 1;
    }

    uint8_t *dest = p->frame[p->draw][x];
    if (!(flags & PUMP_UI) && y == 0 && h == LCD_VIEW_H) {
        for (unsigned start = 0; start < LCD_VIEW_H; start += VIEW_ROWS_PER_REPEAT) {
            *dest++ = pixels[start];
            for (unsigned row = start; row < start + VIEW_ROWS_PER_REPEAT; row += 2)
                *dest++ = pixels[row];
        }
        return 1;
    }

    for (unsigned row = y + (y & 1); row < y + h; row += 2)
        dest[row / 2] = pixels[row - y];
    return !(y & 1) || h > 1;
}
#endif

static int failed(struct t3p_present *p)
{ p->failed=1;return -2; }

#ifdef T3P_STARTUP_BANNER
int t3p_present_startup_banner(struct t3p_present *p, const uint8_t *pixels)
{
    enum {
        SOURCE_WIDTH = 159, SOURCE_HEIGHT = 37,
        DRAW_WIDTH = 216, DRAW_HEIGHT = DRAW_WIDTH * SOURCE_HEIGHT / SOURCE_WIDTH,
        PANEL_WIDTH = 2 * T3P_FRAME_SIDE,
        LEFT = (PANEL_WIDTH - DRAW_WIDTH) / 2,
        TOP = (T3P_PRESENT_HEIGHT - DRAW_HEIGHT) / 2
    };

    if (t3p_present_drain(p))
        return -2;

    uint16_t *band = p->band[0];
    for (unsigned x = 0; x < PANEL_WIDTH; x += T3P_PRESENT_WIDTH) {
        memset(band, 0, sizeof(p->band[0]));
        unsigned first = x < LEFT ? LEFT : x;
        unsigned end = x + T3P_PRESENT_WIDTH;
        unsigned last = end < LEFT + DRAW_WIDTH ? end : LEFT + DRAW_WIDTH;
        for (unsigned y = 0; y < DRAW_HEIGHT; y++) {
            unsigned source_y = y * SOURCE_HEIGHT / DRAW_HEIGHT;
            const uint8_t *row = pixels + 2 * source_y * SOURCE_WIDTH;
            uint16_t *dest = band + (TOP + y) * T3P_PRESENT_WIDTH;
            for (unsigned col = first; col < last; col++) {
                unsigned at = 2 * ((col - LEFT) * SOURCE_WIDTH / DRAW_WIDTH);
                dest[col - x] = (uint16_t)(row[at] | ((uint16_t)row[at + 1] << 8));
            }
        }
        if (p->bus.submit(p->bus.context, x, end - 1, 0, T3P_PRESENT_HEIGHT - 1,
                          band, T3P_PRESENT_WIDTH * T3P_PRESENT_HEIGHT) ||
            p->bus.wait(p->bus.context))
            return failed(p);
    }
    return 0;
}
#endif

static void prepare(struct t3p_present *p)
{
    uint16_t *out=p->band[p->buffer];
    for(unsigned y=0;y<T3P_FRAME_SIDE;y++) {
        for(unsigned x=0;x<T3P_PRESENT_WIDTH/2u;x++) {
            uint16_t color=p->palette[p->frame[p->front][p->next_x/2u+x][y]];
            unsigned at=(2u*y)*T3P_PRESENT_WIDTH+2u*x;
            out[at]=out[at+1]=out[at+T3P_PRESENT_WIDTH]=out[at+T3P_PRESENT_WIDTH+1]=color;
        }
    }
    p->prepared=1;
}
int t3p_present_service(struct t3p_present *p)
{
    if(!p || !p->ready)return -1;
    if(p->failed)return -2;
    p->service_calls++;
    if(!p->active)return 0;
    if(p->inflight) {
        int ready=p->bus.poll(p->bus.context);
        if(ready<0)return failed(p);
        if(!ready) {
            /* The other band is independent of the retained DMA buffer. */
            if(!p->prepared && p->next_x<240u)prepare(p);
            return 0;
        }
        p->inflight=0;
    }
    if(p->next_x==240u) {
        p->active=0;p->completed++;
        return 0;
    }
    if(!p->prepared)prepare(p);
    if(p->bus.submit(p->bus.context,p->next_x,p->next_x+T3P_PRESENT_WIDTH-1u,
                     0,239,p->band[p->buffer],T3P_PRESENT_WIDTH*T3P_PRESENT_HEIGHT))
        return failed(p);
    p->inflight=1;p->prepared=0;p->buffer^=1u;
    p->next_x+=T3P_PRESENT_WIDTH;
    return 0;
}
int t3p_present_drain(struct t3p_present *p)
{
    if(!p || !p->ready)return -1;
    if(p->failed)return -2;
    while(p->active) {
        if(t3p_present_service(p))return -2;
        if(p->inflight && p->bus.wait(p->bus.context))return failed(p);
    }
    return 0;
}
int t3p_present_commit(struct t3p_present *p,const uint16_t palette[256],int overlap)
{
    if(!p || !p->ready || !palette)return -1;
    if(t3p_present_drain(p))return -2;
    p->front=p->draw;p->draw^=1u;
    memcpy(p->frame[p->draw],p->frame[p->front],T3P_FRAME_PIXELS);
#ifdef T3P_SEAMLESS_VIEW
    /* Keep the draw surface clean so clearing a message reveals the view
     * even if the next frame only draws UI. The submitted surface is final. */
    for (unsigned x = 0; x < T3P_FRAME_SIDE; x++) {
        for (unsigned y = 0; y < sizeof(p->message[x]); y++) {
            uint8_t pixel = p->message[x][y];
            if (pixel)
                p->frame[p->front][x][LCD_HUD_Y / 2 + y] = pixel;
        }
    }
#endif
    memcpy(p->palette,palette,sizeof(p->palette));
    p->next_x=0;p->active=1;p->inflight=p->prepared=p->buffer=0;
    p->submitted++;
    if(t3p_present_service(p))return -2;
    return overlap ? 0 : t3p_present_drain(p);
}
