/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_PRESENT_H
#define T3P_PRESENT_H
#include <stdint.h>
#ifdef T3P_SEAMLESS_VIEW
#include "lcd.h"
#endif

#define T3P_FRAME_SIDE 120u
#define T3P_FRAME_PIXELS (T3P_FRAME_SIDE*T3P_FRAME_SIDE)
#define T3P_PRESENT_WIDTH 16u
#define T3P_PRESENT_HEIGHT 240u

/* One transfer may retain its source until poll returns 1 or wait returns 0.
 * poll: 0 busy, 1 idle, negative failure. All callbacks are non-reentrant.
 * No interrupts or second CPU are required: renderer/read hooks call service.
 */
struct t3p_present_bus {
    void *context;
    int (*submit)(void *,uint16_t,uint16_t,uint16_t,uint16_t,const uint16_t *,uint32_t);
    int (*poll)(void *);
    int (*wait)(void *);
};
struct t3p_present {
    struct t3p_present_bus bus;
    uint8_t frame[2][T3P_FRAME_SIDE][T3P_FRAME_SIDE]; /* column-major */
#ifdef T3P_SEAMLESS_VIEW
    uint8_t message[T3P_FRAME_SIDE][(LCD_BAR_Y - LCD_HUD_Y) / 2];
#endif
    uint16_t palette[256]; /* Palette belongs to the submitted image. */
    _Alignas(4) uint16_t band[2][T3P_PRESENT_WIDTH*T3P_PRESENT_HEIGHT];
    uint32_t submitted,completed,service_calls;
    uint16_t next_x;
    uint8_t draw,front,active,inflight,buffer,prepared,ready,failed;
};
int t3p_present_init(struct t3p_present *,const struct t3p_present_bus *);
#ifdef T3P_STARTUP_BANNER
/* Draw the 159x37 RGB565LE logo pixels on black and wait for the LCD. */
int t3p_present_startup_banner(struct t3p_present *p, const uint8_t *pixels);
#endif
#ifdef T3P_SEAMLESS_VIEW
/* x is a column of the 120x120 buffer. y and h are still in LCD coordinates.
 * PUMP_HUD carries the complete message band. Returns 1 if a row was written. */
int t3p_present_index_column(struct t3p_present *p, unsigned x, unsigned y,
                             unsigned h, const uint8_t *pixels, unsigned flags);
#endif
/* The draw surface is owned by the renderer until commit. Commit drains the
 * previous frame, snapshots the palette, then starts this frame. Copying it
 * into the next draw surface preserves partial HUD/menu updates. */
int t3p_present_commit(struct t3p_present *,const uint16_t palette[256],int overlap);
int t3p_present_service(struct t3p_present *);
int t3p_present_drain(struct t3p_present *);
#endif
