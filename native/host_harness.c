// SPDX-License-Identifier: GPL-2.0-or-later
// Windows simulator: exercise the T3P bridge against compressed game pages.
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
#include "t3p_game.h"
#include "t3p_input.h"
#include "t3p_display.h"
static struct t3p_game game_reader;
static struct t3p_input input_state;
static const uint8_t *image;
static uint32_t image_size,clock_ms,buttons;
static uint16_t screen[320*240];
static uint16_t panel_gram[320*320];
static struct t3p_display display;
/* Matches the visible area confirmed by the on-device RGB/geometry probe.
 * This remains a host transport, not the stock SPI callback ABI.
 */
static unsigned panel_size=240,phase,x0,x1,y0,y1,native_rgb;
static char error[192];
static jmp_buf bail_out;
static int square_layout;
static uint32_t render_delay_ms;
__attribute__((noreturn)) void km_fail(const char *message);
__declspec(dllexport) void km_host_render_delay(uint32_t ms) { render_delay_ms=ms; }
static unsigned be16(const uint8_t *p) { return p[0]*256u+p[1]; }
static int panel_command(void *context,uint8_t cmd,const uint8_t *data,uint32_t n)
{
    (void)context;
    if(cmd==0x2a && phase==0 && n==4) {
        x0=be16(data); x1=be16(data+2); phase=1; return 0;
    }
    if(cmd==0x2b && phase==1 && n==4) {
        y0=be16(data); y1=be16(data+2); phase=2; return 0;
    }
    if(cmd!=0x2c || phase!=2 || x0>x1 || y0>y1 || x1>=panel_size || y1>=panel_size ||
       n!=2u*(x1-x0+1)*(y1-y0+1)) return -1;
    unsigned at=0;
    for(unsigned y=y0;y<=y1;y++) for(unsigned x=x0;x<=x1;x++) {
        panel_gram[y*320+x]=(uint16_t)be16(data+at); at+=2;
    }
    phase=0; return 0;
}
static int panel_rectangle(void *context,uint16_t xs,uint16_t xe,
                           uint16_t ys,uint16_t ye,const uint16_t *pixels,uint32_t n)
{
    (void)context;
    if(phase || xs>xe || ys>ye || xe>=panel_size || ye>=panel_size ||
       n!=(uint32_t)(xe-xs+1)*(ye-ys+1) || (uintptr_t)pixels%2) return -1;
    unsigned at=0;
    for(unsigned y=ys;y<=ye;y++) for(unsigned x=xs;x<=xe;x++)
        panel_gram[y*320+x]=pixels[at++];
    return 0;
}
__declspec(dllexport) int km_host_display_transport(unsigned native) {
    if(native>1)return -1;native_rgb=native;return 0;
}
void km_layout(int square) {
    square_layout=square;
    struct t3p_display_bus bus={.command=native_rgb ? 0 : panel_command,
                              .rectangle=native_rgb ? panel_rectangle : 0};
    unsigned width=square ? 240 : 320;
    struct t3p_display_config cfg={(uint16_t)width,240,
        (uint16_t)((panel_size>=width) ? (panel_size-width)/2 : 0),
        (uint16_t)((panel_size-240)/2),(uint16_t)panel_size,(uint16_t)panel_size};
    phase=0;
    if(t3p_display_init(&display,&bus,&cfg)) km_fail("Host display config");
}
void km_present(void) { if(t3p_display_flush(&display)) km_fail("Host display protocol"); }
__declspec(dllexport) int km_host_panel_config(unsigned size) {
    if(size!=240 && size!=320)return -1;panel_size=size;return 0;
}
__declspec(dllexport) const uint16_t *km_host_panel(void) { return panel_gram; }
__declspec(dllexport) void km_host_display_stats(uint32_t *out) {
    out[0]=display.windows;out[1]=display.pixel_bytes;out[2]=(uint32_t)sizeof(display);
}
void km_validate_reads(uint32_t size) { if(size!=image_size)km_fail("Host game size"); }
uint32_t km_cycles(void) { return 0; }
__declspec(dllexport) int km_host_width(void) { return square_layout ? 240 : 320; }
void km_doom_init(void);
void km_doom_step(void);
void km_doom_redraw(void);
void km_doom_state(uint32_t *state);
int km_doom_menu(void);
__attribute__((noreturn)) void km_fail(const char *message);
__declspec(dllexport) void km_host_state(uint32_t *state) { km_doom_state(state); }
void D_Display(void);
uint32_t km_millis(void) { return clock_ms; }
void km_sleep(uint32_t ms) { clock_ms+=ms; }
uint32_t km_keys(void) { return buttons; }
__attribute__((noreturn)) void km_fail(const char *message) {
    size_t n=strlen(message);if(n>=sizeof(error))n=sizeof(error)-1;
    memcpy(error,message,n);error[n]=0;longjmp(bail_out,1);
}
__attribute__((noreturn)) void km_exit(void) { km_fail("Game requested exit"); }
void km_flash_read(uint32_t off,void *dst,uint32_t n) {
    if(off>image_size || n>image_size-off)km_fail("Host pack read out of range");
    if(t3p_game_read(&game_reader,off,dst,n))km_fail("Compressed game read");
}
void nor_read(uint32_t off,void *dst,uint32_t n);
__declspec(dllexport) int km_host_read(uint32_t off,void *dst,uint32_t n) {
    if(setjmp(bail_out))return -1;nor_read(off,dst,n);return 0;
}
__declspec(dllexport) int km_host_check_projection(void) {
#ifdef KM_SQUARE_VIEW
    extern void km_square_projection(int);
    extern int km_square_angle_to_x(unsigned);
    extern uint16_t km_square_xtva[];
    extern int dmc_viewangletox(int,unsigned);
    extern uint32_t dmc_xtoviewangle(int,unsigned);
    extern int viewwidth;
    for (int detail=0; detail<2; detail++) {
        int width=320>>detail;
        km_square_projection(width);
        for(unsigned i=0;i<4096;i++)
            if(km_square_angle_to_x(i)!=dmc_viewangletox(detail,i)) return -1;
        for(unsigned x=0;x<=(unsigned)width;x++)
            if(((uint32_t)km_square_xtva[x]<<19)-0x40000000u != dmc_xtoviewangle(detail,x)) return -2;
    }
    km_square_projection(viewwidth);
#endif
    return 0;
}
void km_column(int x,int y,int h,const uint16_t *pixels) {
    if(x==0 && y==0 && h==200) clock_ms+=render_delay_ms;
    if(x<0 || x>=320 || y<0 || h<0 || y+h>240)km_fail("Host LCD bounds");
    if(t3p_display_column(&display,x,y,h,pixels))km_fail("Host LCD column");
    for(int i=0;i<h;i++)screen[(y+i)*320+x]=pixels[i];
}
__declspec(dllexport) int km_host_init(const uint8_t *data,uint32_t n) {
    image=data;clock_ms=0;
    memset(screen,0,sizeof(screen));memset(panel_gram,0,sizeof(panel_gram));
    if(t3p_game_open(&game_reader,data,n)) { memcpy(error,"Bad compressed game pack",25); return -1; }
    image_size=game_reader.raw_size;
    t3p_input_reset(&input_state);
    if(setjmp(bail_out))return -1;km_doom_init();return 0;
}
__declspec(dllexport) int km_host_step(uint32_t keys) {
    clock_ms+=29;buttons=keys;
    if(setjmp(bail_out))return -1;km_doom_step();return 0;
}
__declspec(dllexport) int km_host_sample(struct t3p_input_sample *source) {
    struct t3p_input_sample sample=*source;
    sample.now_ms=clock_ms+29; sample.menu=(uint8_t)km_doom_menu();
    return km_host_step(t3p_input_update(&input_state,&sample));
}
__declspec(dllexport) void km_host_game_stats(uint32_t *out) {
    out[0]=game_reader.hits;out[1]=game_reader.misses;out[2]=(uint32_t)sizeof(game_reader);
}
__declspec(dllexport) const char *km_host_error(void) { return error; }
__declspec(dllexport) const uint16_t *km_host_screen(void) { return screen; }
__declspec(dllexport) void km_host_redraw(void) { km_doom_redraw();D_Display();km_present(); }
__declspec(dllexport) void km_host_clear_and_redraw(void) {
    memset(screen,0,sizeof(screen));km_doom_redraw();D_Display();km_present();
}
