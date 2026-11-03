/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_display.h"
#include <string.h>

static uint16_t *draw_pixels(struct t3p_display *d)
{
#ifdef T3P_DBI_ASYNC
    return d->pixels+d->buffer*T3P_DISPLAY_COLUMNS*T3P_DISPLAY_HEIGHT;
#else
    return d->pixels;
#endif
}

int t3p_display_init(struct t3p_display *d, const struct t3p_display_bus *bus,
                     const struct t3p_display_config *c)
{
    if (!d) return -1;
    /* Invalidate an old configuration even when a new one is rejected. */
    d->ready=0; d->count=0; d->failed=0;
    if (!bus || (!!bus->command == !!bus->rectangle) || !c || !c->width || !c->height ||
        c->height>T3P_DISPLAY_HEIGHT || !c->address_width || !c->address_height ||
        (uint32_t)c->offset_x+c->width>c->address_width ||
        (uint32_t)c->offset_y+c->height>c->address_height) return -1;
#ifdef T3P_DBI_ASYNC
    d->buffer=0;
#endif
    d->bus=*bus; d->config=*c;
    d->windows=0; d->pixel_bytes=0; d->ready=1;
    return 0;
}

static int send(struct t3p_display *d, uint8_t command,
                const uint8_t *data, uint32_t size)
{
    if (d->bus.command(d->bus.context,command,data,size)) {
        d->failed=1;
        return -2;
    }
    return 0;
}

int t3p_display_flush(struct t3p_display *d)
{
    uint8_t area[4];
    uint32_t start, end, row_pixels, bytes;
    if (!d || !d->ready) return -1;
    if (d->failed) return -2;
    if (!d->count) return 0;
    start=(uint32_t)d->x+d->config.offset_x;
    end=start+d->count-1u;
    area[0]=(uint8_t)(start>>8); area[1]=(uint8_t)start;
    area[2]=(uint8_t)(end>>8); area[3]=(uint8_t)end;
    if (d->bus.command && send(d,0x2a,area,4)) return -2;
    start=(uint32_t)d->y+d->config.offset_y;
    end=start+d->height-1u;
    area[0]=(uint8_t)(start>>8); area[1]=(uint8_t)start;
    area[2]=(uint8_t)(end>>8); area[3]=(uint8_t)end;
    if (d->bus.command && send(d,0x2b,area,4)) return -2;
    /* Compact a partial band forward in its own buffer. */
    uint16_t *pixels=draw_pixels(d);
    row_pixels=d->count;
    for (uint32_t row=1;row<d->height;row++)
        memmove(pixels+row*row_pixels,
                pixels+row*T3P_DISPLAY_COLUMNS,row_pixels*2u);
    bytes=row_pixels*d->height*2u;
    if (d->bus.rectangle) {
        if (d->bus.rectangle(d->bus.context,
                d->x+d->config.offset_x,d->x+d->config.offset_x+d->count-1u,
                d->y+d->config.offset_y,d->y+d->config.offset_y+d->height-1u,
                pixels,bytes/2u)) {
            d->failed=1;
            return -2;
        }
    } else if (send(d,0x2c,(const uint8_t *)pixels,bytes)) return -2;
#ifdef T3P_DBI_ASYNC
    d->buffer^=1;
#endif
    d->windows++; d->pixel_bytes+=bytes; d->count=0;
    return 0;
}

int t3p_display_column(struct t3p_display *d, int x, int y, int height,
                       const uint16_t *rgb565)
{
    if (!d || !d->ready) return -1;
    if (d->failed) return -2;
    if (x<0 || y<0 || height<=0 || !rgb565 ||
        x>=d->config.width || y>=d->config.height ||
        height>(int)d->config.height-y) return -1;
    if (d->count && (x!=d->x+d->count || y!=d->y || height!=d->height))
        if (t3p_display_flush(d)) return -2;
    if (!d->count) {
        d->x=(uint16_t)x; d->y=(uint16_t)y; d->height=(uint16_t)height;
    }
    uint16_t *pixels=draw_pixels(d);
    for (int row=0;row<height;row++) {
        uint32_t at=(uint32_t)row*T3P_DISPLAY_COLUMNS+d->count;
        uint16_t color=rgb565[row];
        if (d->bus.rectangle) pixels[at]=color;
        else {
            uint8_t *bytes=(uint8_t *)&pixels[at];
            bytes[0]=(uint8_t)(color>>8);
            bytes[1]=(uint8_t)color;
        }
    }
    d->count++;
    return d->count==T3P_DISPLAY_COLUMNS ? t3p_display_flush(d) : 0;
}
