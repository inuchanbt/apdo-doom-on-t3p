/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native board integration. This is untested hardware code until the separate
 * bring-up image demonstrates cold boot, panel initialization and timebase.
 */
#include <stdint.h>
#ifdef T3P_ASSET_4_START
#include <string.h>
#endif
#include "t3p_bsp.h"
#include "t3p_boot.h"
#include "t3p_signal.h"
#include "t3p_display.h"
#ifdef T3P_FRAME_PRESENTER
#include "t3p_present.h"
#endif
#ifdef T3P_TINF_PROFILE
#include "t3p_tinf_profile.h"
#endif
#ifndef T3P_BOARD_BRINGUP
#include "t3p_game.h"
#include "t3p_input.h"
#ifdef T3P_TOUCH_MOVE
#include "t3p_touch.h"
static struct t3p_touch touch;
#endif
#ifdef T3P_SENSOR_CONTROLS
#include "t3p_mic.h"
#include "t3p_tilt.h"
static struct t3p_sensor_gpio accel_gpio;
static struct t3p_sensor_bus sensor_bus;
static struct t3p_mic mic;
static struct t3p_tilt tilt;
#endif
#ifdef T3P_UAC_MONITOR
#include "t3p_baro.h"
static struct t3p_baro barometer;

const struct t3p_uac_reading *km_uac_reading(void)
{
    return &barometer.reading;
}
#endif
static struct t3p_game game_reader;
extern const uint8_t t3p_game_image[],t3p_game_image_end[];
extern void km_doom_init(void),km_doom_step(void);
#ifdef T3P_STARTUP_BANNER
#define T3P_BANNER_MS 3000u
extern const uint8_t t3p_freedoom_banner[];
extern void D_ResetTics(void);
#endif
#endif
static struct t3p_bsp board;
#ifdef T3P_FRAME_PRESENTER
static struct t3p_present presenter;
#else
static struct t3p_display display;
#endif
void km_fail(const char *message) __attribute__((noreturn));
#ifdef T3P_FRAME_PRESENTER
static void frame_service(void)
{ if(t3p_present_service(&presenter))km_fail("frame service"); }
static int frame_poll(void *context)
{ return t3p_bsp_dma_poll(context); }
static int frame_wait(void *context)
{ return t3p_bsp_dma_wait(context); }
static void frame_drain(void)
{ if(t3p_present_drain(&presenter))km_fail("frame drain"); }
#endif
#ifdef T3P_PROFILE
static uint32_t lcd_ms;
#ifdef T3P_PROFILE_PRESENT
static uint32_t present_ms, present_start;
static uint8_t present_first_band;
#endif
#ifdef T3P_PROFILE_ASSETS
static uint32_t asset_ms;
#endif

static int timed_rectangle(void *ctx,uint16_t x0,uint16_t x1,
                           uint16_t y0,uint16_t y1,const uint16_t *pixels,
                           uint32_t count)
{
    uint32_t start=t3p_bsp_millis(&board);
#ifdef T3P_PROFILE_PRESENT
    if(present_first_band) {
        present_start=start;
        present_first_band=0;
    }
#endif
    int result=t3p_bsp_rectangle(ctx,x0,x1,y0,y1,pixels,count);
    lcd_ms+=t3p_bsp_millis(&board)-start;
    return result;
}
#endif

#if defined(T3P_PROFILE) || defined(T3P_SENSOR_CONTROLS)
static void profile_row(unsigned row,uint32_t value)
{
    static const uint32_t glyphs[16]={
        0x75557,0x26227,0x71747,0x71717,0x55711,0x74717,0x74757,0x71111,
        0x75757,0x75717,0x25755,0x65656,0x74447,0x65556,0x74647,0x74644
    };
#ifdef T3P_SENSOR_CONTROLS
    enum { SCALE=2 };
#else
    enum { SCALE=1 };
#endif
    enum { WIDTH=32*SCALE, HEIGHT=5*SCALE, STEP=6*SCALE };
    uint16_t pixels[WIDTH*HEIGHT];
    for(unsigned y=0;y<HEIGHT;y++) {
        for(unsigned i=0;i<8;i++) {
            uint32_t glyph=glyphs[(value>>(28-4*i))&15u];
            unsigned bits=(glyph>>(16-4*(y/SCALE)))&7u;
            for(unsigned x=0;x<4*SCALE;x++)
                pixels[y*WIDTH+i*4*SCALE+x]=(bits&(4u>>(x/SCALE))) ? 0xffff : 0;
        }
    }
    if(t3p_bsp_rectangle(&board,0,WIDTH-1,row*STEP,row*STEP+HEIGHT-1,pixels,WIDTH*HEIGHT))
        km_fail("profile display");
}
#endif
#ifdef T3P_PROFILE
#ifdef T3P_PROFILE_BENCH
struct bench_metric { uint32_t total, minimum, maximum; };
static struct bench_metric bench_frame={0,UINT32_MAX,0};
static struct bench_metric bench_decode={0,UINT32_MAX,0};
static struct bench_metric bench_present={0,UINT32_MAX,0};
static uint32_t bench_count;
#ifdef T3P_PROFILE_BREAKDOWN
extern uint32_t t3p_sim_ms,t3p_render_ms,t3p_present_call_ms;
/* Means for eleven time buckets, then five totals; first five overlap phase times. */
static uint32_t bench_breakdown[16];
#endif
#ifdef T3P_BENCH_FIXED_TICS
static uint32_t bench_frame_hash=2166136261u;
extern int gametic;
#endif

static void bench_add(struct bench_metric *metric,uint32_t value)
{
    metric->total+=value;
    if(value<metric->minimum)metric->minimum=value;
    if(value>metric->maximum)metric->maximum=value;
}

static void bench_report_metric(unsigned row,const struct bench_metric *metric)
{
    profile_row(row,bench_count ? metric->total/bench_count : 0);
    profile_row(row+1,bench_count ? metric->minimum : 0);
    profile_row(row+2,metric->maximum);
}

static void bench_report(void)
{
    profile_row(0,T3P_BENCH_REVISION);
    profile_row(1,bench_count);
    profile_row(2,bench_count==30 ? 0xd00e : 0);
    bench_report_metric(3,&bench_frame);
    bench_report_metric(6,&bench_decode);
    bench_report_metric(9,&bench_present);
#ifdef T3P_BENCH_FIXED_TICS
    profile_row(12,(uint32_t)gametic);
    profile_row(13,bench_frame_hash);
#ifdef T3P_PROFILE_PAGE_TOTALS
    if(bench_count==30) {
        uint16_t pages[8];
        unsigned found=t3p_game_profile_top(&game_reader,pages);
        for(unsigned rank=0;rank<8;rank++) {
            uint32_t page_time=0,codec_count=0;
            if(rank<found) {
                unsigned page=pages[rank];
                unsigned codec=(game_reader.image[64+page*16+7]&0x40u) ? 2u : 1u;
                page_time=(page<<16)|game_reader.page_decodes[page];
                codec_count=(codec<<16)|game_reader.page_decode_count[page];
            }
            profile_row(14+2*rank,page_time);
            profile_row(15+2*rank,codec_count);
        }
    }
#endif
#ifdef T3P_TINF_PROFILE
    for(unsigned i=0;i<TINF_P_COUNT;i++)profile_row(14+i,t3p_tinf_profile[i]);
#endif
#ifdef T3P_PROFILE_BREAKDOWN
    for(unsigned i=0;i<16;i++)
        profile_row(14+i,i<11 ? (bench_count ? bench_breakdown[i]/bench_count : 0) : bench_breakdown[i]);
#endif
#endif
}
#endif
#ifdef T3P_PROFILE_RAW_RANK
static unsigned raw_extra(unsigned page)
{
    const uint8_t *entry=game_reader.image+64+16*page;
    unsigned stored=entry[4]|((unsigned)entry[5]<<8);
    unsigned length=entry[8]|((unsigned)entry[9]<<8);
    return length-stored;
}

static void profile_raw_rank(void)
{
    for(unsigned row=3;row<11;row+=2) {
        unsigned best=0, time=0, extra=1;
        for(unsigned i=0;i<game_reader.pages;i++) {
            unsigned candidate=game_reader.page_decodes[i];
            if(!candidate)continue;
            unsigned cost=raw_extra(i);
            if(candidate*extra>time*cost) {
                best=i;
                time=candidate;
                extra=cost;
            }
        }
        profile_row(row,(best<<16)|time);
        profile_row(row+1,time ? extra : 0);
        game_reader.page_decodes[best]=0;
    }
}
#endif
#ifdef T3P_PROFILE_CACHE
static uint32_t cache_core_base(void)
{
    uint32_t core;
    __asm__ volatile("%0 = cnum" : "=r"(core));
    return 0xffe000u+0x800u*(core&1u);
}

static void cache_counters(uint32_t base,uint32_t values[4])
{
    /* BR35 csfr.h: CNT_IHIT, CNT_IMIS, CNT_RHIT, CNT_RMIS. */
    for(unsigned i=0;i<4;i++)
        values[i]=board.io.read32(board.io.context,base+4u*(0x94u+i));
}
#elif defined(T3P_PROFILE_HOT) && !defined(T3P_GAME_RAW_ONLY)
static void profile_hot_pages(void)
{
    /* Report the eight most frequently expanded compressed pages this frame. */
    for(unsigned row=13;row<21;row++) {
        unsigned page=0;
        for(unsigned i=1;i<game_reader.pages;i++)
            if(game_reader.page_decodes[i]>game_reader.page_decodes[page])page=i;
        unsigned count=game_reader.page_decodes[page];
        profile_row(row,count ? (page<<16)|count : 0);
        game_reader.page_decodes[page]=0;
    }
}
#endif
#define DISPLAY_RECTANGLE timed_rectangle
#else
#define DISPLAY_RECTANGLE t3p_bsp_rectangle
#endif
#ifndef T3P_BOARD_BRINGUP
#define LEFT_KEY_PORT 0xfd3000u
#define LEFT_KEY_PIN (1u<<11)

static void init_key(uint32_t port,uint32_t pin)
{
    /* Active-low digital input with the stock 10 kΩ pull-up. */
    static const uint8_t offsets[]={0x0c,0x08,0x14,0x18,0x1c,0x20};
    for(unsigned i=0;i<sizeof(offsets);i++) {
        uint32_t addr=port+offsets[i];
        uint32_t value=board.io.read32(board.io.context,addr);
        board.io.write32(board.io.context,addr,i<3 ? value|pin : value&~pin);
    }
}

static int key_pressed(uint32_t port,uint32_t pin)
{
    return !(board.io.read32(board.io.context,port)&pin);
}

#ifdef T3P_DUAL_KEYS
/* Stock 3.0.3 app at 0x0c0cccf8 registers PB07 and PA11 as low-active keys. */
#define RIGHT_KEY_PORT 0xfd3100u
#define RIGHT_KEY_PIN (1u<<7)
static struct t3p_buttons buttons;
extern int km_doom_menu(void);

static void sample_buttons(void)
{
    unsigned pressed=0;
    if(key_pressed(LEFT_KEY_PORT,LEFT_KEY_PIN))pressed|=T3P_BUTTON_LEFT;
    if(key_pressed(RIGHT_KEY_PORT,RIGHT_KEY_PIN))pressed|=T3P_BUTTON_RIGHT;
    t3p_buttons_sample(&buttons,pressed,t3p_bsp_millis(&board));
}
#ifdef T3P_SENSOR_CONTROLS
static uint32_t sample_sensors(int menu)
{
    uint32_t now=t3p_bsp_millis(&board);
    enum t3p_mic_mode mic_mode=menu || buttons.candidate || buttons.stable
        ? T3P_MIC_PAUSE : T3P_MIC_LISTEN;
#ifdef T3P_HAPTICS
    if(t3p_haptics_mic_muted(&board))mic_mode=T3P_MIC_DISCARD;
#endif
    t3p_mic_poll(&mic,&board,now,mic_mode);
    return t3p_tilt_poll(&tilt,&sensor_bus,now,menu || touch.held ||
                         buttons.candidate==T3P_BUTTON_BOTH);
}
#endif
#else
static uint32_t fire_pending;

static uint32_t fire_level(void)
{
    return key_pressed(LEFT_KEY_PORT,LEFT_KEY_PIN) ? T3P_FIRE : 0;
}
#endif

static void sample_column_buttons(int x)
{
#ifdef T3P_DUAL_KEYS
    if((x&7)==0)sample_buttons();
#else
    (void)x;
    fire_pending|=fire_level();
#endif
}
#endif
volatile uint32_t t3p_native_stage=0x54335033u;
volatile uint32_t t3p_native_frames,t3p_native_time;

uint32_t km_millis(void){return t3p_bsp_millis(&board);}
uint32_t km_cycles(void){return km_millis();}
#ifdef T3P_HAPTICS
void km_weapon_fired(void){t3p_haptics_queue(&board,T3P_HAPTIC_SHOT);}
void km_damage_taken(void){t3p_haptics_queue(&board,T3P_HAPTIC_DAMAGE);}
void km_haptics_begin_frame(int gameplay)
{
    t3p_haptics_play(&board,gameplay);
    if(board.haptics.pulse_ms)t3p_mic_poll(&mic,&board,board.milliseconds,T3P_MIC_DISCARD);
}
#endif
void km_fail(const char *message)
{
#ifdef T3P_HAPTICS
    t3p_haptics_stop(&board);
#endif
    (void)message;t3p_native_stage|=0x80000000u;
    /* Stop after a transport error; never retry a partially sent transaction. */
    t3p_signal_failure(t3p_native_stage&0x7fffffffu);
}
void km_exit(void) __attribute__((noreturn));
void km_exit(void){km_fail("exit");}
void km_sleep(uint32_t ms)
{
    while(ms){uint32_t part=ms>1000 ? 1000 : ms;if(t3p_bsp_delay(&board,part))km_fail("timer");ms-=part;}
}
#ifdef T3P_SENSOR_CONTROLS
static void sensor_diagnostic(void)
{
    uint32_t shots=0, last_draw=km_millis()-200;
    t3p_bsp_backlight(&board,1);
    for(;;) {
        uint32_t now=km_millis();
        t3p_tilt_poll(&tilt,&sensor_bus,now,0);
        t3p_mic_poll(&mic,&board,now,T3P_MIC_LISTEN);
        if(t3p_mic_take(&mic))++shots;
        if(key_pressed(LEFT_KEY_PORT,LEFT_KEY_PIN)) {
            mic.peak_level=mic.peak_windows=0;
            shots=0;
        }
        if(now-last_draw>=200) {
            last_draw=now;
            unsigned state=0;
            if(tilt.ready) {
                state=1;
                if(tilt.have_sample)state=tilt.gravity ? 3 : 2;
            }
            profile_row(0,T3P_SENSOR_REVISION);
            profile_row(1,state);
            profile_row(2,(uint32_t)(int32_t)tilt.latest.x);
            profile_row(3,(uint32_t)(int32_t)tilt.latest.y);
            profile_row(4,(uint32_t)(int32_t)tilt.latest.z);
            profile_row(5,mic.peak_level);
            profile_row(6,t3p_mic_threshold(&mic));
            profile_row(7,mic.peak_windows);
            profile_row(8,shots);
#ifdef T3P_HAPTICS
            profile_row(9,board.haptics.ready);
#endif
        }
        km_sleep(5);
    }
}
#endif
void km_layout(int square)
{
#ifdef T3P_FRAME_PRESENTER
    const struct t3p_present_bus bus={&board,t3p_bsp_rectangle,frame_poll,frame_wait};
    if(!square || t3p_present_init(&presenter,&bus))km_fail("frame layout");
#else
    const struct t3p_display_bus bus={.context=&board,.rectangle=DISPLAY_RECTANGLE};
    const struct t3p_display_config cfg={240,240,0,0,240,240};
    if(!square || t3p_display_init(&display,&bus,&cfg))km_fail("layout");
#endif
}
#ifdef T3P_INDEXED_FRAME
#define FRAME_SIDE 120
/* Column-major to match the renderer and the display band's input. */
#ifdef T3P_FRAME_PRESENTER
#define indexed_frame (presenter.frame[presenter.draw])
#else
static uint8_t indexed_frame[FRAME_SIDE][FRAME_SIDE];
#endif
static uint8_t frame_dirty;
extern uint16_t pump_lut[256];

static uint8_t palette_index(uint16_t color)
{
    unsigned best=0, error=UINT32_MAX;
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
#endif
void km_column(int x,int y,int h,const uint16_t *pixels)
{
#ifdef T3P_FRAME_PRESENTER
    frame_service();
#endif
#ifndef T3P_BOARD_BRINGUP
    sample_column_buttons(x);
#endif
#ifdef T3P_INDEXED_FRAME
    if(x<0 || x>=240 || y<0 || y>=240 || h<=0 || h>240-y)km_fail("column");
    if(x&1)return;
    uint8_t *dest=indexed_frame[x/2];
    uint16_t previous=0;
    uint8_t index=0;
    for(int row=y+(y&1),first=row;row<y+h;row+=2) {
        uint16_t color=pixels[row-y];
        if(row==first || color!=previous)index=palette_index(color);
        dest[row/2]=index;
        previous=color;
        frame_dirty=1;
    }
#else
    if(t3p_display_column(&display,x,y,h,pixels))km_fail("column");
#endif
}
void km_present(void)
{
#ifdef T3P_FRAME_PRESENTER
#ifdef T3P_PROFILE_PRESENT
    uint32_t begin=km_millis();
#endif
    if(frame_dirty) {
#ifdef T3P_PRESENT_OVERLAP
        const int overlap=1;
#else
        const int overlap=0;
#endif
        if(t3p_present_commit(&presenter,pump_lut,overlap))km_fail("frame commit");
        frame_dirty=0;
    } else frame_service(); /* Menus may skip drawing while a frame is pending. */
#ifdef T3P_PROFILE_PRESENT
    /* This call's wait/setup time, not concurrent DMA elapsed time. */
    present_ms=km_millis()-begin;
#endif
#else
#ifdef T3P_INDEXED_FRAME
#ifdef T3P_PROFILE_PRESENT
    int presenting=frame_dirty;
    present_first_band=presenting;
#endif
    if(frame_dirty) {
        uint16_t column[240];
        for(int x=0;x<FRAME_SIDE;x++) {
            for(int y=0;y<FRAME_SIDE;y++)
                column[2*y]=column[2*y+1]=pump_lut[indexed_frame[x][y]];
            for(int r=0;r<2;r++)
                if(t3p_display_column(&display,2*x+r,0,240,column))km_fail("frame column");
        }
        frame_dirty=0;
    }
#endif
    if(t3p_display_flush(&display))km_fail("flush");
#ifdef T3P_DBI_ASYNC
    uint32_t start=t3p_bsp_millis(&board);
    if(t3p_bsp_dma_wait(&board))km_fail("dma drain");
    uint32_t end=t3p_bsp_millis(&board);
    lcd_ms+=end-start;
#ifdef T3P_PROFILE_PRESENT
    if(presenting)present_ms=end-present_start;
#endif
#endif
#endif
#ifdef T3P_HAPTICS
    /* End the pulse before entering game logic again, even on a short frame. */
    if(board.haptics.pulse_ms) {
        t3p_bsp_millis(&board);
        t3p_haptics_stop(&board);
    }
#endif
}
#ifndef T3P_BOARD_BRINGUP
uint32_t km_keys(void)
{
#ifdef T3P_PROFILE_BENCH
    return 0;
#else
#ifdef T3P_DUAL_KEYS
    sample_buttons();
    int menu=km_doom_menu();
    uint32_t keys=t3p_buttons_take(&buttons,menu);
#else
    uint32_t keys=fire_pending|fire_level();
    fire_pending=0;
#endif
#ifdef T3P_TOUCH_MOVE
    uint32_t touch_keys=t3p_touch_poll(&touch,&board);
#ifdef T3P_SENSOR_CONTROLS
    keys|=sample_sensors(menu || (keys&T3P_ESCAPE));
    keys|=t3p_mic_take(&mic);
#endif
#ifdef T3P_DUAL_KEYS
    /* Dispatch Escape by itself so a drag cannot select an item on entry. */
    if(keys&T3P_ESCAPE)return T3P_ESCAPE;
#endif
    keys|=touch_keys;
#endif
    return keys;
#endif
}
#ifdef T3P_ASSET_4_START
/* The converted pack already stores flats and drawable columns as records. */
struct asset_slot { uint32_t key, age; };
static struct asset_slot flats[8], columns[32];
static uint8_t flat_data[8][1024], column_data[32][256];
static uint32_t asset_age, asset_hits;

static int read_asset(struct asset_slot *slots,uint8_t *data,unsigned capacity,
                      unsigned stride,uint32_t offset,void *out,uint32_t count)
{
    uint32_t key=(offset<<11)|count;
    uint32_t now=++asset_age;
    unsigned oldest=0;
    for(unsigned i=0;i<capacity;i++) {
        if(slots[i].key==key) {
            slots[i].age=now;
            memcpy(out,data+i*stride,count);
            asset_hits++;
            return 0;
        }
        if(now-slots[i].age>now-slots[oldest].age)oldest=i;
    }
    uint8_t *dest=data+oldest*stride;
    if(t3p_game_read(&game_reader,offset,dest,count))return -1;
    slots[oldest]=(struct asset_slot){key,now};
    memcpy(out,dest,count);
    return 0;
}

static int read_game(uint32_t offset,void *out,uint32_t count)
{
    if(count==1024 && offset>=T3P_ASSET_4_START && offset<T3P_ASSET_4_END &&
       count<=T3P_ASSET_4_END-offset && (offset-T3P_ASSET_4_START)%1024==0)
        return read_asset(flats,(uint8_t *)&flat_data,8,1024,offset,out,count);
    if(count>16 && count<=256 &&
       ((offset>=T3P_ASSET_3_START && offset<T3P_ASSET_3_END && count<=T3P_ASSET_3_END-offset) ||
        (offset>=T3P_ASSET_6_START && offset<T3P_ASSET_6_END && count<=T3P_ASSET_6_END-offset)))
        return read_asset(columns,(uint8_t *)&column_data,32,256,offset,out,count);
    return t3p_game_read(&game_reader,offset,out,count);
}
#endif
void km_flash_read(uint32_t offset,void *out,uint32_t count)
{
#ifdef T3P_FRAME_PRESENTER
    frame_service();
#endif
#ifdef T3P_PROFILE_ASSETS
    uint32_t start=km_millis();
#endif
#ifdef T3P_ASSET_4_START
    if(read_game(offset,out,count))km_fail("game read");
#else
    if(t3p_game_read(&game_reader,offset,out,count))km_fail("game read");
#endif
#ifdef T3P_PROFILE_ASSETS
    asset_ms+=km_millis()-start;
#endif
}
void km_validate_reads(uint32_t size)
{ if(size!=game_reader.raw_size)km_fail("game size"); }
void km_set_palette(const uint16_t *colors){(void)colors;}
void km_index_column(int x,int y,int h,const uint8_t *pixels,unsigned ui)
{
#ifdef T3P_INDEXED_FRAME
#ifdef T3P_FRAME_PRESENTER
    frame_service();
#endif
    sample_column_buttons(x);
#ifdef T3P_TOUCH_MOVE
    /* A held drag can begin or end during a long frame. */
    if((x&15)==0) {
        t3p_touch_poll(&touch,&board);
#ifdef T3P_SENSOR_CONTROLS
        sample_sensors(km_doom_menu());
#endif
    }
#endif
    if(ui) {
        if(x%4==3)return;
        x-=x/4;
    }
    if(x<0 || x>=240 || y<0 || y>=240 || h<=0 || h>240-y)km_fail("indexed column");
    if(x&1)return;
#ifdef T3P_SEAMLESS_VIEW
    frame_dirty |= t3p_present_index_column(&presenter, x / 2, y, h, pixels, ui);
#else
    uint8_t *dest=indexed_frame[x/2];
    for(int row=y+(y&1);row<y+h;row+=2) {
        dest[row/2]=pixels[row-y];
        frame_dirty=1;
    }
#endif
#else
    (void)x;(void)y;(void)h;(void)pixels;(void)ui;
#endif
}
#else
static void diagnostic(void)
{
    uint16_t column[240];
    for(int x=0;x<240;x++) {
        for(int y=0;y<240;y++) {
            uint16_t color=x<80 ? 0xf800 : x<160 ? 0x07e0 : 0x001f;
            if(x<2 || x>=238 || y<12 || y>=238)color=0xffff;
            if(y>=112 && y<128)color=0;
            if(y>=224 && y<238)color=(x<8 ? 0xffff : 0);
            column[y]=color;
        }
        km_column(x,0,240,column);
    }
    km_present();
    t3p_bsp_backlight(&board,1);
    for(;;) {
        km_sleep(250);
        unsigned position=(++t3p_native_frames%30u)*8u;
        for(int x=0;x<240;x++) {
            uint16_t color=(unsigned)x>=position && (unsigned)x<position+8 ? 0xffff : 0;
            for(unsigned y=0;y<14;y++)column[y]=color;
            km_column(x,224,14,column);
        }
        km_present();t3p_native_time=km_millis();
    }
}
#endif
void t3p_board_start(void)
{
    t3p_native_stage=1;
    if(t3p_boot_snapshot.valid!=T3P_BOOT_MAGIC)km_fail("boot handoff");
    struct t3p_bsp_io io;t3p_bsp_native_io(&io);
    t3p_native_stage=2;
    if(t3p_bsp_init(&board,&io))km_fail("clock");
#ifdef T3P_HAPTICS
    t3p_haptics_init(&board);
#endif
    t3p_native_stage=3;
    if(t3p_bsp_lcd_init(&board))km_fail("lcd init");
    t3p_native_stage=4;km_layout(1);
#ifdef T3P_BOARD_BRINGUP
    diagnostic();
#else
    init_key(LEFT_KEY_PORT,LEFT_KEY_PIN);
#ifdef T3P_DUAL_KEYS
    init_key(RIGHT_KEY_PORT,RIGHT_KEY_PIN);
#endif
#ifdef T3P_SENSOR_CONTROLS
    int show_sensors=key_pressed(RIGHT_KEY_PORT,RIGHT_KEY_PIN);
#endif
#ifdef T3P_STARTUP_BANNER
    uint32_t banner_started = 0;
    if (!show_sensors) {
        /* The asset starts with its little-endian width and height. */
        if (t3p_present_startup_banner(&presenter, t3p_freedoom_banner + 4))
            km_fail("startup banner");
        t3p_bsp_backlight(&board, 1);
        banner_started = km_millis();
    }
#endif
#ifdef T3P_TOUCH_MOVE
    t3p_touch_init(&touch,&board);
#endif
    if(t3p_game_open(&game_reader,t3p_game_image,
        (uint32_t)(t3p_game_image_end-t3p_game_image)))km_fail("pack");
    t3p_native_stage=5;
#ifdef T3P_PROFILE
    uint32_t init_start=km_millis();
#endif
    km_doom_init();
#ifdef T3P_STARTUP_BANNER
    if (!show_sensors) {
        uint32_t elapsed = km_millis() - banner_started;
        if (elapsed < T3P_BANNER_MS) {
            km_sleep(T3P_BANNER_MS - elapsed);
            D_ResetTics();
        }
    }
#endif
#ifdef T3P_SENSOR_CONTROLS
    sensor_bus=t3p_accel_bus_init(&accel_gpio,&board);
    t3p_tilt_init(&tilt,&sensor_bus);
    t3p_mic_init(&mic,&board,km_millis());
    if(show_sensors)sensor_diagnostic();
#endif
#ifdef T3P_UAC_MONITOR
    /* Stock 3.0.3 reaches the same PB02/PB01 bus for both sensors. */
    t3p_baro_init(&barometer, km_millis());
#endif
#ifdef T3P_PROFILE
    uint32_t init_ms=km_millis()-init_start;
#if !defined(T3P_TINF_PROFILE) && !defined(T3P_BENCH_DECODE_CONTROL)
    static const uint32_t clock_registers[]={
        0xfe0400u,0xfe0404u,0xfe040cu,0xfe0410u,0xfd4100u,0xfd4104u,0xfd4108u,0xfe1908u
    };
    uint32_t clock_snapshot[sizeof(clock_registers)/sizeof(*clock_registers)];
    for(unsigned i=0;i<sizeof(clock_registers)/sizeof(*clock_registers);i++)
        clock_snapshot[i]=board.io.read32(board.io.context,clock_registers[i]);
#endif
#endif
#ifdef T3P_PROFILE_CACHE
    uint32_t core_base=cache_core_base();
#endif
    t3p_bsp_backlight(&board,1);t3p_native_stage=6;
    for(;;) {
#if defined(T3P_FRAME_PRESENTER) && defined(T3P_PROFILE_BENCH)
        /* Start the measured batch with no warmup frame still outstanding. */
        if(t3p_native_frames==10)frame_drain();
#endif
#ifdef T3P_TINF_PROFILE
        /* Exclude initialization/warmup without changing cache residency. */
        if(t3p_native_frames==10)t3p_tinf_profile_reset();
#endif
#ifdef T3P_PROFILE
        uint32_t frame_start=km_millis();
        lcd_ms=0;
#ifdef T3P_PROFILE_PRESENT
        present_ms=0;
#endif
#ifdef T3P_PROFILE_ASSETS
        asset_ms=0;
#ifdef T3P_ASSET_4_START
        asset_hits=0;
#endif
#ifdef T3P_PROFILE_STAGES
        game_reader.fetch_ms=game_reader.decode_ms=game_reader.crc_ms=game_reader.repeat_decodes=0;
#ifdef T3P_PROFILE_BREAKDOWN
        game_reader.raw_copy_ms=game_reader.cache_copy_ms=game_reader.lz4_ms=0;
        game_reader.raw_bytes=game_reader.fetch_bytes=game_reader.cache_bytes=0;
        game_reader.deflate_calls=game_reader.lz4_calls=0;
#endif
#endif
        uint32_t misses_before=game_reader.misses;
#endif
#ifdef T3P_PROFILE_PAGE_TOTALS
        /* Before frame 11, exclude initialization/warmup but retain warm cache. */
        if(t3p_native_frames==10)t3p_game_profile_reset(&game_reader);
#elif !defined(T3P_TINF_PROFILE) && !defined(T3P_BENCH_DECODE_CONTROL) && (defined(T3P_PROFILE_RAW_RANK) || (defined(T3P_PROFILE_HOT) && !defined(T3P_PROFILE_CACHE)))
        for(unsigned i=0;i<game_reader.pages;i++)game_reader.page_decodes[i]=0;
#endif
#endif
#ifdef T3P_PROFILE_CACHE
        uint32_t before[4],after[4];
        cache_counters(core_base,before);
#endif
#ifdef T3P_UAC_MONITOR
        t3p_baro_poll(&barometer, &sensor_bus, km_millis());
#endif
        km_doom_step();
#ifdef T3P_PROFILE_CACHE
        cache_counters(core_base,after);
#endif
        t3p_native_frames++;
#if defined(T3P_FRAME_PRESENTER) && defined(T3P_PROFILE_BENCH)
        if(t3p_native_frames>10) {
            /* Count checksum work in both new variants: DMA must not get
             * untimed progress while the CPU hashes an indexed frame. */
            for(unsigned i=0;i<sizeof(indexed_frame);i++)
                bench_frame_hash=(bench_frame_hash^((uint8_t *)indexed_frame)[i])*16777619u;
        }
        if(t3p_native_frames==40) {
            uint32_t start=km_millis();
            frame_drain(); /* Include all 30 presentations in the timed batch. */
            present_ms+=km_millis()-start;
        }
#endif
        t3p_native_time=km_millis();
#ifdef T3P_PROFILE
#ifdef T3P_PROFILE_BENCH
        if(t3p_native_frames>10) {
            bench_add(&bench_frame,t3p_native_time-frame_start);
            bench_add(&bench_decode,game_reader.decode_ms);
            bench_add(&bench_present,present_ms);
#ifdef T3P_PROFILE_BREAKDOWN
            uint32_t frame_ms=t3p_native_time-frame_start;
            uint32_t phases=t3p_sim_ms+t3p_render_ms+t3p_present_call_ms;
            const uint32_t values[16]={game_reader.fetch_ms,game_reader.raw_copy_ms,
                game_reader.cache_copy_ms,game_reader.crc_ms,asset_ms,t3p_sim_ms,
                t3p_render_ms,t3p_present_call_ms,frame_ms>=phases ? frame_ms-phases : 0,
                game_reader.decode_ms-game_reader.lz4_ms,game_reader.lz4_ms,
                game_reader.raw_bytes,game_reader.fetch_bytes,game_reader.cache_bytes,
                game_reader.deflate_calls,game_reader.lz4_calls};
            for(unsigned i=0;i<16;i++)bench_breakdown[i]+=values[i];
#endif
#if defined(T3P_BENCH_FIXED_TICS) && !defined(T3P_FRAME_PRESENTER)
            /* Hash after timing; it does not affect the simulated game clock. */
            for(unsigned i=0;i<sizeof(indexed_frame);i++)
                bench_frame_hash=(bench_frame_hash^((uint8_t *)indexed_frame)[i])*16777619u;
#endif
            bench_count++;
        }
#ifdef T3P_FRAME_PRESENTER
        /* Diagnostic rectangles must never borrow the DMA engine mid-frame. */
        if(bench_count==30)bench_report();
#else
        bench_report();
#endif
        if(bench_count==30)for(;;)km_sleep(20);
#else
        profile_row(0,init_ms);
        profile_row(1,t3p_native_time-frame_start);
        profile_row(2,lcd_ms);
#ifdef T3P_PROFILE_RAW_RANK
        profile_raw_rank();
#else
        for(unsigned i=0;i<sizeof(clock_registers)/sizeof(*clock_registers);i++)
            profile_row(i+3,clock_snapshot[i]);
#endif
#ifdef T3P_PROFILE_ASSETS
        profile_row(11,asset_ms);
        profile_row(12,game_reader.misses-misses_before);
#endif
#ifdef T3P_PROFILE_CACHE
        profile_row(13,board.io.read32(board.io.context,core_base+0x400u));
        profile_row(14,board.io.read32(board.io.context,0xff0500u));
        profile_row(15,board.io.read32(board.io.context,0xfe0200u));
#ifdef T3P_PROFILE_PRESENT
        profile_row(16,present_ms);
#else
        profile_row(16,board.io.read32(board.io.context,core_base+0x240u));
#endif
#ifdef T3P_PROFILE_STAGES
        profile_row(17,game_reader.fetch_ms);
        profile_row(18,game_reader.decode_ms);
#ifdef T3P_ASSET_4_START
        profile_row(19,asset_hits);
#else
        profile_row(19,game_reader.crc_ms);
#endif
        profile_row(20,game_reader.repeat_decodes);
#else
        for(unsigned i=0;i<4;i++)profile_row(17+i,after[i]-before[i]);
#endif
#elif defined(T3P_PROFILE_HOT)
        profile_hot_pages();
#endif
#endif
#endif
    }
#endif
}
