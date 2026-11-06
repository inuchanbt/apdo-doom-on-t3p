// SPDX-License-Identifier: GPL-2.0-or-later
// Windows simulator: exercise the T3P bridge against compressed game pages.
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
#include "t3p_game.h"
#include "t3p_input.h"
#include "t3p_display.h"
#ifdef T3P_FRAME_PRESENTER
#include "t3p_present.h"
static struct t3p_present presenter;
static void frame_service(void);
#endif
__attribute__((noreturn)) void km_fail(const char *message);
static struct t3p_game game_reader;
static struct t3p_input input_state;
static const uint8_t *image;
static uint32_t image_size,clock_ms,buttons;
#ifdef T3P_BENCH_FIXED_TICS
volatile uint32_t t3p_native_frames;
#endif
static uint16_t screen[320*240];
#ifdef T3P_INDEXED_FRAME
static uint8_t frame_dirty;
#ifdef T3P_FRAME_PRESENTER
#define indexed_frame (presenter.frame[presenter.draw])
#else
static uint8_t indexed_frame[120][120];
#endif
extern uint16_t pump_lut[256];
__declspec(dllexport) const uint8_t *km_host_indexed_frame(void)
{
#ifdef T3P_FRAME_PRESENTER
    return &presenter.frame[presenter.front][0][0];
#else
    return &indexed_frame[0][0];
#endif
}
static uint8_t palette_index(uint16_t color)
{
    unsigned best=0,error=UINT32_MAX;
    for(unsigned i=0;i<256;i++) {
        uint16_t candidate=pump_lut[i];
        if(candidate==color)return (uint8_t)i;
        int r=(color>>11)-(candidate>>11);
        int g=((color>>5)&63)-((candidate>>5)&63);
        int b=(color&31)-(candidate&31);
        unsigned delta=4*r*r+g*g+4*b*b;
        if(delta<error){best=i;error=delta;}
    }
    return (uint8_t)best;
}
void km_index_column(int x,int y,int h,const uint8_t *pixels,unsigned ui)
{
#ifdef T3P_FRAME_PRESENTER
    frame_service();
#endif
    if(ui) { if(x%4==3)return;x-=x/4; }
    if(x<0 || x>=240 || y<0 || y>=240 || h<=0 || h>240-y)km_fail("Host indexed bounds");
    if(x&1)return;
#ifdef T3P_SEAMLESS_VIEW
    frame_dirty |= t3p_present_index_column(&presenter, x / 2, y, h, pixels, ui);
#else
    for(int row=y+(y&1);row<y+h;row+=2) {
        indexed_frame[x/2][row/2]=pixels[row-y];frame_dirty=1;
    }
#endif
}
#endif
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
#ifdef T3P_FRAME_PRESENTER
static const uint16_t *dma_pixels;
static uint16_t dma_snapshot[T3P_PRESENT_WIDTH*240],completed_panel[320*240];
static unsigned dma_x,dma_ticks,retention_checks,dma_submissions;
static int frame_submit(void *context,uint16_t xs,uint16_t xe,uint16_t ys,uint16_t ye,
                        const uint16_t *pixels,uint32_t n)
{
    (void)context;
    if(dma_pixels || xe!=xs+15u || ys || ye!=239 || n!=16u*240u || (uintptr_t)pixels%4)
        return -1;
    dma_pixels=pixels;dma_x=xs;
    dma_ticks=2u+(dma_submissions++%5u);
    memcpy(dma_snapshot,pixels,sizeof(dma_snapshot));
    return 0;
}
static int frame_poll(void *context)
{
    (void)context;
    if(!dma_pixels)return 1;
    /* Inspect the retained source on every delayed poll, not at submission. */
    if(memcmp(dma_pixels,dma_snapshot,sizeof(dma_snapshot)))km_fail("DMA source overwritten");
    retention_checks++;
    if(--dma_ticks)return 0;
    if(panel_rectangle(0,dma_x,dma_x+15u,0,239,dma_pixels,16u*240u))return -1;
    dma_pixels=0;
    if(dma_x==224u)memcpy(completed_panel,panel_gram,sizeof(completed_panel));
    return 1;
}
static int frame_wait(void *context)
{
    int result;
    do { result=frame_poll(context); } while(result==0);
    return result<0 ? -1 : 0;
}
static void frame_service(void)
{ if(t3p_present_service(&presenter))km_fail("Host frame service"); }
__declspec(dllexport) int km_host_finish(void)
{
    if(setjmp(bail_out))return -1;
    return t3p_present_drain(&presenter);
}
__declspec(dllexport) const uint16_t *km_host_completed_panel(void) { return completed_panel; }
__declspec(dllexport) void km_host_present_stats(uint32_t *out)
{
    out[0]=presenter.submitted;out[1]=presenter.completed;out[2]=retention_checks;
    out[3]=presenter.active;out[4]=presenter.service_calls;out[5]=sizeof(presenter);
}
#endif
__declspec(dllexport) int km_host_display_transport(unsigned native) {
    if(native>1)return -1;native_rgb=native;return 0;
}
void km_layout(int square) {
    square_layout=square;
#ifdef T3P_FRAME_PRESENTER
    const struct t3p_present_bus bus={0,frame_submit,frame_poll,frame_wait};
    phase=0;
    if(!square || t3p_present_init(&presenter,&bus))km_fail("Host frame layout");
#else
    struct t3p_display_bus bus={.command=native_rgb ? 0 : panel_command,
                              .rectangle=native_rgb ? panel_rectangle : 0};
    unsigned width=square ? 240 : 320;
    struct t3p_display_config cfg={(uint16_t)width,240,
        (uint16_t)((panel_size>=width) ? (panel_size-width)/2 : 0),
        (uint16_t)((panel_size-240)/2),(uint16_t)panel_size,(uint16_t)panel_size};
    phase=0;
    if(t3p_display_init(&display,&bus,&cfg)) km_fail("Host display config");
#endif
}
void km_present(void) {
#ifdef T3P_FRAME_PRESENTER
    if(frame_dirty) {
#ifdef T3P_PRESENT_OVERLAP
        const int overlap=1;
#else
        const int overlap=0;
#endif
        if(t3p_present_commit(&presenter,pump_lut,overlap))km_fail("Host frame commit");
        for(unsigned y=0;y<240;y++)for(unsigned x=0;x<240;x++)
            screen[y*320+x]=presenter.palette[presenter.frame[presenter.front][x/2][y/2]];
        frame_dirty=0;
    } else frame_service();
#else
#ifdef T3P_INDEXED_FRAME
    if(frame_dirty) {
        uint16_t column[240];
        for(unsigned x=0;x<120;x++) {
            for(unsigned y=0;y<120;y++)
                column[2*y]=column[2*y+1]=pump_lut[indexed_frame[x][y]];
            for(unsigned r=0;r<2;r++) {
                if(t3p_display_column(&display,2*x+r,0,240,column))km_fail("Host indexed display");
                for(unsigned y=0;y<240;y++)screen[y*320+2*x+r]=column[y];
            }
        }
        frame_dirty=0;
    }
#endif
    if(t3p_display_flush(&display)) km_fail("Host display protocol");
#endif
}
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
#ifdef T3P_FRAME_PRESENTER
    frame_service();
#endif
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
#ifdef T3P_FRAME_PRESENTER
    frame_service();
#endif
    if(x==0 && y==0 && h==200) clock_ms+=render_delay_ms;
    if(x<0 || x>=320 || y<0 || h<0 || y+h>240)km_fail("Host LCD bounds");
#ifdef T3P_INDEXED_FRAME
    if(x>=240 || h<=0)km_fail("Host indexed column");
    if(x&1)return;
    uint16_t previous=0;
    uint8_t index=0;
    for(int row=y+(y&1),first=row;row<y+h;row+=2) {
        uint16_t color=pixels[row-y];
        if(row==first || color!=previous)index=palette_index(color);
        indexed_frame[x/2][row/2]=index;
        previous=color;frame_dirty=1;
    }
#else
    if(t3p_display_column(&display,x,y,h,pixels))km_fail("Host LCD column");
    for(int i=0;i<h;i++)screen[(y+i)*320+x]=pixels[i];
#endif
}
__declspec(dllexport) int km_host_init(const uint8_t *data,uint32_t n) {
    image=data;clock_ms=0;
#ifdef T3P_BENCH_FIXED_TICS
    t3p_native_frames=0;
#endif
    memset(screen,0,sizeof(screen));memset(panel_gram,0,sizeof(panel_gram));
#ifdef T3P_FRAME_PRESENTER
    dma_pixels=0;retention_checks=dma_submissions=0;
    memset(completed_panel,0,sizeof(completed_panel));
#endif
#ifdef T3P_INDEXED_FRAME
    memset(indexed_frame,0,sizeof(indexed_frame));frame_dirty=0;
#endif
    if(t3p_game_open(&game_reader,data,n)) { memcpy(error,"Bad compressed game pack",25); return -1; }
    image_size=game_reader.raw_size;
    t3p_input_reset(&input_state);
    if(setjmp(bail_out))return -1;km_doom_init();return 0;
}
__declspec(dllexport) uint32_t km_host_clock_ms(void) { return clock_ms; }
__declspec(dllexport) int km_host_step_ms(uint32_t keys,uint32_t elapsed_ms) {
    clock_ms+=elapsed_ms;buttons=keys;
    if(setjmp(bail_out))return -1;km_doom_step();
#ifdef T3P_BENCH_FIXED_TICS
    t3p_native_frames++;
#endif
    return 0;
}
__declspec(dllexport) int km_host_step(uint32_t keys) {
    return km_host_step_ms(keys,29);
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
