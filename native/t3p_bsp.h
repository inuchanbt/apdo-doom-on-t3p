/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_BSP_H
#define T3P_BSP_H
#include <stdint.h>
struct t3p_bsp_io {
    void *context;
    uint32_t (*read32)(void *,uint32_t);
    void (*write32)(void *,uint32_t,uint32_t);
    void (*write8)(void *,uint32_t,uint8_t);
};
struct t3p_bsp {
    struct t3p_bsp_io io;
    uint32_t milliseconds,fraction,operations;
    uint16_t previous_tick;
    uint8_t ready,failed;
};
/* Owns TIMER0 and the DBI command engine with interrupts disabled. CPU/PLL/SFC
 * clock configuration is inherited from the stock bootloader. No Flash
 * programming, measurements, triggers, motor or charger control is performed.
 */
int t3p_bsp_init(struct t3p_bsp *,const struct t3p_bsp_io *);
uint32_t t3p_bsp_millis(struct t3p_bsp *);
int t3p_bsp_delay(struct t3p_bsp *,uint32_t);
int t3p_bsp_lcd_init(struct t3p_bsp *);
int t3p_bsp_command(struct t3p_bsp *,uint8_t,const uint8_t *,uint32_t);
int t3p_bsp_rectangle(void *,uint16_t,uint16_t,uint16_t,uint16_t,const uint16_t *,uint32_t);
#ifdef T3P_DBI_ASYNC
int t3p_bsp_dma_wait(struct t3p_bsp *);
#endif
void t3p_bsp_backlight(struct t3p_bsp *,int);
#ifndef T3P_BSP_HOST
void t3p_bsp_native_io(struct t3p_bsp_io *);
#endif
#endif
