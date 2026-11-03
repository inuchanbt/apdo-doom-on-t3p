/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Offline link probe. These callbacks are placeholders, not device drivers. */
#include <stdint.h>
#include <string.h>
#include "t3p_input.h"
#include "t3p_sensors.h"
#include "t3p_game.h"
#include "t3p_display.h"

extern void km_doom_init(void);
extern void km_doom_step(void);
void t3p_linkcheck_start(void);
volatile uint32_t t3p_linkcheck_failure;
extern const uint8_t t3p_game_image[],t3p_game_image_end[];
static struct t3p_game game_reader;
static struct t3p_display display;
static int no_panel_command(void *context, uint8_t command, const uint8_t *data, uint32_t size)
{
    (void)context; (void)command; (void)data; (void)size;
    return 0; /* Link-only sink: no hardware or communication. */
}
#ifdef T3P_RUNTIME_LAYOUT
static int no_panel_rectangle(void *context,uint16_t xs,uint16_t xe,
        uint16_t ys,uint16_t ye,const uint16_t *pixels,uint32_t count)
{
    (void)context;(void)xs;(void)xe;(void)ys;(void)ye;(void)pixels;(void)count;
    return 0; /* Offline fixture only: deliberately no hardware submission. */
}
#endif
_Static_assert(sizeof(uint32_t) == 4, "32-bit integer ABI required");
_Static_assert(sizeof(void *) == 4, "32-bit pointer ABI required");

uint32_t km_millis(void) { return 0; }
void km_sleep(uint32_t ms) { (void)ms; }
void km_fail(const char *message) __attribute__((noreturn));
void km_fail(const char *message)
{
    (void)message;
    t3p_linkcheck_failure = 1;
    for (;;) {}
}
void km_exit(void) __attribute__((noreturn));
void km_exit(void) { km_fail("link probe"); }
void km_flash_read(uint32_t offset, void *dst, uint32_t count)
{
    if (t3p_game_read(&game_reader,offset,dst,count)) km_fail("game read");
}
void km_column(int x, int y, int height, const uint16_t *pixels)
{
    if (t3p_display_column(&display,x,y,height,pixels)) km_fail("display column");
}
void km_present(void) { if (t3p_display_flush(&display)) km_fail("display flush"); }
void km_index_column(int x, int y, int height, const uint8_t *pixels, unsigned ui)
{
    (void)x; (void)y; (void)height; (void)pixels; (void)ui;
}
void km_set_palette(const uint16_t *colors) { (void)colors; }
static struct t3p_input input_state;
static struct t3p_input_sample input_sample;
static uint32_t input_keys;
uint32_t km_keys(void) { return input_keys; }
void km_layout(int square)
{
#ifdef T3P_RUNTIME_LAYOUT
    struct t3p_display_bus bus={.rectangle=no_panel_rectangle};
#else
    struct t3p_display_bus bus={.command=no_panel_command};
#endif
    struct t3p_display_config cfg={240,240,0,0,240,240};
    if (!square) km_fail("physical panel requires square layout");
    if (t3p_display_init(&display,&bus,&cfg)) km_fail("display config");
}

#ifdef T3P_RUNTIME_LAYOUT
/* Resolves the board entry for the layout fixture, never a device build. */
void t3p_board_start(void) { t3p_linkcheck_start(); }
#endif
void km_validate_reads(uint32_t size) { if (size!=game_reader.raw_size) km_fail("game size"); }
uint32_t km_cycles(void) { return 0; }

void t3p_linkcheck_start(void)
{
    struct t3p_sensor_bus no_bus = {0};
    struct t3p_accel_raw accel;
    struct t3p_touch_raw touch;
    struct t3p_pressure_raw pressure;
    /* Resolve every new driver helper, using an intentionally missing bus. */
    t3p_linkcheck_failure = (t3p_accel_read(&no_bus,&accel) != -1)
        | (t3p_touch_read(&no_bus,&touch) != -1)
        | (t3p_pressure_read(&no_bus,&pressure) != -1);
    t3p_input_reset(&input_state);
    if (t3p_game_open(&game_reader,t3p_game_image,(uint32_t)(t3p_game_image_end-t3p_game_image)))
        km_fail("game pack");
    km_doom_init();
    for (;;) {
        input_sample.now_ms=km_millis();
        input_keys=t3p_input_update(&input_state,&input_sample);
        km_doom_step();
    }
}
