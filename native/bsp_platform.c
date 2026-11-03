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
#ifndef T3P_BOARD_BRINGUP
#include "t3p_game.h"
#include "t3p_input.h"
static struct t3p_game game_reader;
extern const uint8_t t3p_game_image[],t3p_game_image_end[];
extern void km_doom_init(void),km_doom_step(void);
#endif
static struct t3p_bsp board;
static struct t3p_display display;
void km_fail(const char *message) __attribute__((noreturn));
#ifdef T3P_PROFILE
static uint32_t lcd_ms;
#ifdef T3P_PROFILE_ASSETS
static uint32_t asset_ms;
#endif

static int timed_rectangle(void *ctx,uint16_t x0,uint16_t x1,
                           uint16_t y0,uint16_t y1,const uint16_t *pixels,
                           uint32_t count)
{
    uint32_t start=t3p_bsp_millis(&board);
    int result=t3p_bsp_rectangle(ctx,x0,x1,y0,y1,pixels,count);
    lcd_ms+=t3p_bsp_millis(&board)-start;
    return result;
}

static void profile_row(unsigned row,uint32_t value)
{
    static const uint32_t glyphs[16]={
        0x75557,0x26227,0x71747,0x71717,0x55711,0x74717,0x74757,0x71111,
        0x75757,0x75717,0x25755,0x65656,0x74447,0x65556,0x74647,0x74644
    };
    uint16_t pixels[32*5];
    for(unsigned y=0;y<5;y++) {
        for(unsigned i=0;i<8;i++) {
            uint32_t glyph=glyphs[(value>>(28-4*i))&15u];
            unsigned bits=(glyph>>(16-4*y))&7u;
            for(unsigned x=0;x<4;x++)
                pixels[y*32+i*4+x]=(bits&(4u>>x)) ? 0xffff : 0;
        }
    }
    if(t3p_bsp_rectangle(&board,0,31,row*6,row*6+4,pixels,32*5))
        km_fail("profile display");
}
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
#elif defined(T3P_PROFILE_HOT)
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
#define KEY_PORT 0xfd3000u
#define KEY_PIN (1u<<11)
static uint32_t fire_pending;

static void init_fire_key(void)
{
    /* Mirror the stock bootloader PA11 setup at 0x103ea4..0x103eb8. */
    static const uint8_t offsets[]={0x0c,0x08,0x14,0x18,0x1c,0x20};
    for(unsigned i=0;i<sizeof(offsets);i++) {
        uint32_t addr=KEY_PORT+offsets[i];
        uint32_t value=board.io.read32(board.io.context,addr);
        board.io.write32(board.io.context,addr,i<3 ? value|KEY_PIN : value&~KEY_PIN);
    }
}

static uint32_t fire_level(void)
{
    return board.io.read32(board.io.context,KEY_PORT)&KEY_PIN ? 0 : T3P_FIRE;
}
#endif
volatile uint32_t t3p_native_stage=0x54335033u;
volatile uint32_t t3p_native_frames,t3p_native_time;

uint32_t km_millis(void){return t3p_bsp_millis(&board);}
uint32_t km_cycles(void){return km_millis();}
void km_fail(const char *message)
{
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
void km_layout(int square)
{
    const struct t3p_display_bus bus={.context=&board,.rectangle=DISPLAY_RECTANGLE};
    const struct t3p_display_config cfg={240,240,0,0,240,240};
    if(!square || t3p_display_init(&display,&bus,&cfg))km_fail("layout");
}
void km_column(int x,int y,int h,const uint16_t *pixels)
{
#ifndef T3P_BOARD_BRINGUP
    fire_pending|=fire_level();
#endif
    if(t3p_display_column(&display,x,y,h,pixels))km_fail("column");
}
void km_present(void)
{
    if(t3p_display_flush(&display))km_fail("flush");
#ifdef T3P_DBI_ASYNC
    uint32_t start=t3p_bsp_millis(&board);
    if(t3p_bsp_dma_wait(&board))km_fail("dma drain");
    lcd_ms+=t3p_bsp_millis(&board)-start;
#endif
}
#ifndef T3P_BOARD_BRINGUP
/* Only the known left/boot key is used here. Other buttons, touch and sensors
 * await board integration; do not invent pin assignments for them. */
uint32_t km_keys(void)
{
    uint32_t keys=fire_pending|fire_level();
    fire_pending=0;
    return keys;
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
{ (void)x;(void)y;(void)h;(void)pixels;(void)ui; }
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
    t3p_native_stage=3;
    if(t3p_bsp_lcd_init(&board))km_fail("lcd init");
    t3p_native_stage=4;km_layout(1);
#ifdef T3P_BOARD_BRINGUP
    diagnostic();
#else
    init_fire_key();
    if(t3p_game_open(&game_reader,t3p_game_image,
        (uint32_t)(t3p_game_image_end-t3p_game_image)))km_fail("pack");
    t3p_native_stage=5;
#ifdef T3P_PROFILE
    uint32_t init_start=km_millis();
#endif
    km_doom_init();
#ifdef T3P_PROFILE
    uint32_t init_ms=km_millis()-init_start;
    static const uint32_t clock_registers[]={
        0xfe0400u,0xfe0404u,0xfe040cu,0xfe0410u,0xfd4100u,0xfd4104u,0xfd4108u,0xfe1908u
    };
    uint32_t clock_snapshot[sizeof(clock_registers)/sizeof(*clock_registers)];
    for(unsigned i=0;i<sizeof(clock_registers)/sizeof(*clock_registers);i++)
        clock_snapshot[i]=board.io.read32(board.io.context,clock_registers[i]);
#endif
#ifdef T3P_PROFILE_CACHE
    uint32_t core_base=cache_core_base();
#endif
    t3p_bsp_backlight(&board,1);t3p_native_stage=6;
    for(;;) {
#ifdef T3P_PROFILE
        uint32_t frame_start=km_millis();
        lcd_ms=0;
#ifdef T3P_PROFILE_ASSETS
        asset_ms=0;
#ifdef T3P_ASSET_4_START
        asset_hits=0;
#endif
#ifdef T3P_PROFILE_STAGES
        game_reader.fetch_ms=game_reader.decode_ms=game_reader.crc_ms=game_reader.repeat_decodes=0;
#endif
        uint32_t misses_before=game_reader.misses;
#endif
#if defined(T3P_PROFILE_RAW_RANK) || (defined(T3P_PROFILE_HOT) && !defined(T3P_PROFILE_CACHE))
        for(unsigned i=0;i<game_reader.pages;i++)game_reader.page_decodes[i]=0;
#endif
#endif
#ifdef T3P_PROFILE_CACHE
        uint32_t before[4],after[4];
        cache_counters(core_base,before);
#endif
        km_doom_step();
#ifdef T3P_PROFILE_CACHE
        cache_counters(core_base,after);
#endif
        t3p_native_frames++;
        t3p_native_time=km_millis();
#ifdef T3P_PROFILE
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
        profile_row(16,board.io.read32(board.io.context,core_base+0x240u));
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
    }
#endif
}
