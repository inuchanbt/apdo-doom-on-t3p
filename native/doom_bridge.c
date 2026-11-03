// SPDX-License-Identifier: GPL-2.0-or-later
// T3P port of KM003C glue for Artur Bieniek's low-memory DOOM engine.
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "doomdef.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "d_event.h"
#include "d_main.h"
#include "d_loop.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "i_sound.h"
#include "i_system.h"
#include "i_video.h"
#include "m_menu.h"
#include "p_setup.h"
#include "r_local.h"
#include "s_sound.h"
#include "st_stuff.h"
#include "v_video.h"
#include "z_zone.h"
#include "cache.h"
#include "dmc_fw.h"
#include "crc32.h"
#include "fe.h"
#include "fetch.h"
#include "img.h"
#include "i_input.h"
#include "lcd.h"
#include "nor.h"
#include "ovl.h"
#include "platform.h"
#include "r_strip.h"

extern uint32_t km_millis(void);
extern void km_sleep(uint32_t ms);
extern void km_fail(const char *message) __attribute__((noreturn));
extern void km_flash_read(uint32_t offset, void *dst, uint32_t count);
extern void km_column(int x, int y, int h, const uint16_t *pixels);
extern void km_present(void);
extern void km_layout(int square);
extern void km_validate_reads(uint32_t size);
extern uint32_t km_cycles(void);
static int ui_window;
extern uint32_t km_keys(void);
extern void km_exit(void) __attribute__((noreturn));

#ifdef KM_FAST_ARENA
uint8_t km_engine_ram[32768] __attribute__((section(".doom_arena"), aligned(16)));
#else
uint8_t km_engine_ram[32768] __attribute__((aligned(16)));
#endif
__asm__(".global __frame_start\n.set __frame_start, km_engine_ram\n"
        ".global __ram_end\n.set __ram_end, km_engine_ram + 32768\n"
        ".global __frame_max\n.set __frame_max, 16000\n"
        ".global __frame_min\n.set __frame_min, 8368\n"
        ".global dmc_tic\n.set dmc_tic, km_engine_ram + 6912\n");
byte *I_VideoBuffer;
int dmc_render = 1;
dmc_set_t dmc_set;
uint16_t pump_lut[256];
volatile struct pump_stats pump_stats;
volatile struct nor_stats nor_stats;
FILE *stderr, *stdout;

// Read-only game data: a small four-way cache retains short record reads
// between frames. It never participates in service-mode NOR writes.
#ifndef KM_CACHE_KIB
#define KM_CACHE_KIB 8
#endif
#if KM_CACHE_KIB
#define READ_LINE 16u
#define READ_WAYS 4u
#define READ_SETS (KM_CACHE_KIB * 1024u / (READ_LINE * READ_WAYS))
static uint8_t read_data[READ_SETS][READ_WAYS][READ_LINE];
static uint32_t read_tag[READ_SETS][READ_WAYS];
static uint8_t read_mru[READ_SETS];
static void read_cache_reset(void) {
    memset(read_tag,0xff,sizeof(read_tag));memset(read_mru,0,sizeof(read_mru));
}
void nor_read(uint32_t at, void *dst, uint32_t n) {
    if (at>0x400000u || n>0x400000u-at) km_fail("NOR read out of range");
    // Large columns/flats stream directly; do not overfetch or evict records.
    if (n > READ_LINE) { km_flash_read(at,dst,n); return; }
    uint8_t *out = dst;
    while (n) {
        uint32_t base = at & ~(READ_LINE-1u), line = base / READ_LINE;
        unsigned set = line % READ_SETS, way;
        for (way=0; way<READ_WAYS; way++) if (read_tag[set][way]==base) break;
        if (way==READ_WAYS) {
            // Round-robin replacement with a recent-hit hint; bounded lookup.
            way = (read_mru[set]+1) % READ_WAYS;
            uint32_t end = img.size ? img.size : 0x400000u;
            uint32_t count = end-base < READ_LINE ? end-base : READ_LINE;
            km_flash_read(base,read_data[set][way],count);
            read_tag[set][way]=base;
        }
        read_mru[set]=way;
        unsigned offset=at-base, count=READ_LINE-offset;
        if (count>n) count=n;
        memcpy(out,read_data[set][way]+offset,count);
        at+=count;out+=count;n-=count;
    }
}
#else
static void read_cache_reset(void) {}
void nor_read(uint32_t at, void *dst, uint32_t n) { km_flash_read(at,dst,n); }
#endif
int nor_read_async(uint32_t at, void *dst, uint32_t n, int prio) {
    (void)prio; nor_read(at, dst, n); return 1;
}
bool nor_done(int ticket) { (void)ticket; return true; }
void nor_wait(int ticket) { (void)ticket; }
void nor_wait_all(void) {}
int nor_col_start(uint32_t at, void *dst, uint32_t n) { nor_read(at,dst,n); return 0; }
void nor_col_wait(void) {}
int nor_erase(uint32_t at, unsigned command) { (void)at; (void)command; return -1; }
int nor_program(uint32_t at, const void *src, uint32_t n) { (void)at; (void)src; (void)n; return -1; }

// Raw E1M1 pack at NOR offset zero. No FAT and no writes during play.
img_dir_t img;
void (*img_verify_hook)(uint32_t, uint32_t);
void dmc_fatal(const char *s) { km_fail(s); }
void img_read(uint32_t at, void *dst, uint32_t n) {
    if (at > img.size || n > img.size-at) km_fail("Game data out of range");
    nor_read(at,dst,n);
}
int img_read_async(uint32_t at, void *dst, uint32_t n, int prio) {
    (void)prio; img_read(at,dst,n); return 1;
}
uint32_t img_phys_run(uint32_t at, uint32_t *run) {
    if (at > img.size) km_fail("Game data range");
    *run=img.size-at; return at;
}
void img_mapent(int k, dmc_mapent_t *e) {
    if (k<0 || k>=img.nmaps) km_fail("Bad map index");
    img_read(DMC_MAP_TABLE_OFF+32u*k,e,sizeof(*e));
}
int img_init(void) {
    dmc_header_t h;
    nor_read(0,&h,sizeof(h));
    if (memcmp(h.magic,DMC_MAGIC,8) || h.fmt_major!=DMC_FMT_MAJOR ||
        h.image_size!=h.split || h.image_size>DMC_HEADER_SIZE+0x3fd000u ||
        h.image_size<DMC_HEADER_SIZE || h.nmaps!=1 || h.nsect>DMC_MAX_SECTS)
        return IMG_E_HEADER;
    if ((h.wad_sha256[0] | h.wad_sha256[1]<<8 | h.wad_sha256[2]<<16 |
         (uint32_t)h.wad_sha256[3]<<24)!=DMC_WAD_ID) return IMG_E_HEADER;
    uint8_t block[256]; uint32_t reg=CRC32_INIT;
    for (uint32_t at=0;at<DMC_HEADER_SIZE;at+=sizeof(block)) {
        nor_read(at,block,sizeof(block));
        if (!at) memset(block+offsetof(dmc_header_t,header_crc),0,4);
        reg=crc32_update(reg,block,sizeof(block));
    }
    if (crc32_final(reg)!=h.header_crc) return IMG_E_CRC;
    memset(&img,0,sizeof(img));
    img.size=h.image_size; img.split=h.split; img.header_crc=h.header_crc;
    img.nmaps=h.nmaps; img.nsect=h.nsect;
    uint32_t seen=0;
    for (unsigned k=0;k<h.nsect;k++) {
        dmc_sect_t s; img_read(DMC_SECT_TABLE_OFF+16*k,&s,sizeof(s));
        if(s.off>img.size || s.size>img.size-s.off) return IMG_E_HEADER;
        if(s.id && s.id<DMC_SECT_MAX) {
            img.sect[s.id]=(img_sect_t){s.off,s.size,s.flags}; seen|=1u<<s.id;
        }
    }
    return seen==(1u<<DMC_SECT_MAX)-2 ? IMG_OK : IMG_E_HEADER;
}
int dmc_demo_open(const char *name,uint32_t *size) { (void)name;(void)size;return -1; }
void dmc_demo_read(uint32_t at,void *dst,uint32_t n) { (void)at;(void)dst;(void)n;km_fail("No external demo"); }

// Synchronous column pump; square mode maps the 320-wide UI separately.
static int px,py,ph,reps;
static void emit_column(int x, int y, int h, const uint16_t *pixels) {
#ifdef KM_SQUARE_VIEW
    if (ui_window) {
        if (x % 4 == 3) return;
        x -= x / 4;
    }
#endif
    km_column(x,y,h,pixels);
}
static uint16_t column[240];
void lcd_begin(int x0,int y0,int x1,int y1) { (void)x1;px=x0;py=y0;ph=y1-y0+1;ui_window=1; }
static int cy;
void lcd_push(uint16_t c) {
    if(ph<=0 || ph>240) km_fail("Bad LCD window");
    column[cy++]=c;
    if(cy==ph) { emit_column(px++,py,ph,column);cy=0; }
}
void lcd_push_n(const uint16_t *p,uint32_t n) { while(n--) lcd_push(*p++); }
void lcd_end(void) { if(cy) km_fail("Partial LCD column"); }
void lcd_fill(int x0,int y0,int x1,int y1,uint16_t c) {
    lcd_begin(x0,y0,x1,y1); for(int x=x0;x<=x1;x++) for(int y=y0;y<=y1;y++) lcd_push(c); lcd_end();
}
void pump_begin(int x0,int x1,int y0,int y1,int flags) {
    lcd_begin(x0,y0,x1,y1); reps=(flags&PUMP_DOUBLE)?2:1;ui_window=!!(flags&PUMP_UI);
}
void pump_submit(const uint8_t *p,int ncols) {
#ifdef T3P_INDEXED_FRAME
    extern void km_index_column(int,int,int,const uint8_t *,unsigned);
    for(int x=0;x<ncols;x++)
        for(int r=0;r<reps;r++)km_index_column(px++,py,ph,p+x*ph,ui_window);
#else
    for(int x=0;x<ncols;x++) {
        for(int y=0;y<ph;y++) column[y]=pump_lut[p[x*ph+y]];
        for(int r=0;r<reps;r++) emit_column(px++,py,ph,column);
    }
#endif
}
void pump_sync(int max) { (void)max; }
void pump_end(void) {}
int pump_pending(void) { return 0; }
void lcd_flip(int v) { (void)v; }
int lcd_flipped(void) { return 0; }
void lcd_backlight(int v) { (void)v; }
int lcd_profile(void) { return 0; }

void I_InitGraphics(void) {}
void I_StartFrame(void) {}
void I_UpdateNoBlit(void) {}
void I_FinishUpdate(void) {}
void I_SetPalette(int p) { R_PaletteChanged();pal_read(p,pump_lut); }
void I_StartTic(void) {
    static uint32_t previous;
    uint32_t next=km_keys();
    const int keys[]={KEY_FIRE,KEY_UPARROW,KEY_LEFTARROW,KEY_RIGHTARROW,KEY_USE,KEY_ESCAPE,KEY_ENTER,KEY_DOWNARROW,
                      KEY_STRAFE_L,KEY_STRAFE_R,'1','2','3','4','5','6','7'};
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);i++) if((next^previous)&(1u<<i)) {
        event_t e={.type=(next&(1u<<i))?ev_keydown:ev_keyup,.data1=keys[i]};
        D_PostEvent(&e);D_ProcessEvents();
    }
    previous=next;
}
int I_GetTimeMS(void) { return km_millis(); }
int I_GetTime(void) { uint32_t ms=km_millis();return ms/1000*35+ms%1000*35/1000; }
void I_Sleep(int ms) { km_sleep(ms>0?(uint32_t)ms:1); }
void I_InitTimer(void) {}
void I_WaitVBL(int count) { km_sleep((uint32_t)count*1000/70); }
void I_Error(char *fmt,...) {
    char msg[128];va_list ap;va_start(ap,fmt);plat_vfmt(msg,sizeof(msg),fmt,ap);va_end(ap);km_fail(msg);
}
void I_Quit(void) { km_exit(); }
void I_Tactile(int a,int b,int c) { (void)a;(void)b;(void)c; }
void I_InitSound(boolean v) { (void)v; }
void I_UpdateSound(void) {}
int I_GetSfxLumpNum(sfxinfo_t *s) { (void)s;return 0; }
int I_StartSound(sfxinfo_t *s,int c,int v,int p) { (void)s;(void)v;(void)p;return c; }
void I_StopSound(int v) { (void)v; }
boolean I_SoundIsPlaying(int v) { (void)v;return false; }
void I_UpdateSoundParams(int c,int v,int p) { (void)c;(void)v;(void)p; }
static uint8_t pool[256];static unsigned pool_used;
void Z_Init(void) {}
void *Z_Malloc(int n,int tag,void *user) {
    unsigned count=((unsigned)n+3)&~3u;(void)tag;(void)user;
    if(n<0 || count>sizeof(pool)-pool_used) km_fail("DOOM static pool exhausted");
    void *p=pool+pool_used;pool_used+=count;return p;
}
void Z_Free(void *p) { (void)p; }
void Z_FreeTags(int a,int b) { (void)a;(void)b; }
void Z_CheckHeap(void) {}
void dmc_place_caches(void) {
    uint8_t *p=R_FrameSetup();
    if(p+RC_BYTES+M.plan.k*DMC_FLAT_SIZE>__ram_end-M.plan.arena_bytes) km_fail("DOOM RAM plan overlap");
    rc_place(p);flat_place(M.plan.k?p+RC_BYTES:NULL);
}

// All cold code fits internal flash on the KM003C, so native entry tables
// replace DMC-100's RAM code overlays. Saving and settings writes are disabled.
const uint16_t ovl_win[OVL_IDS]={0};
uint16_t ovl_loads;
uint8_t dmc_build_id[20];
extern const ovl_fn ovl_tab_MENU[],ovl_tab_WI[],ovl_tab_FINALE[];
static int no_save(int slot,int flags) { (void)slot;(void)flags;return SAVE_ENOFILE; }
static int no_header(int slot,dmc_save_hdr_t *h) { (void)slot;(void)h;return SAVE_ENOFILE; }
static const ovl_fn save_table[]={(ovl_fn)no_save,(ovl_fn)no_header,(ovl_fn)no_save,(ovl_fn)no_header};
ovl_fn ovl_entry(int id,int idx) {
    if(idx<0) km_fail("Bad code entry");
    if(id==OVL_MENU && idx<3) return ovl_tab_MENU[idx];
    if(id==OVL_WI && idx<4) return ovl_tab_WI[idx];
    if(id==OVL_FINALE && idx<3) return ovl_tab_FINALE[idx];
    if(id==OVL_SAVEG && idx<4) return save_table[idx];
    km_fail("Unsupported code entry");
}
void ovl_invalidate(void) {}
int ovl_resident(void) { return 0; }
int G_SaveUsable(void) { return SAVE_ENOFILE; }
int G_SaveHeader(int slot,dmc_save_hdr_t *h) { return no_header(slot,h); }
int G_SaveCheck(int slot,dmc_save_hdr_t *h) { return no_header(slot,h); }
int ext_read(int file,uint32_t off,void *dst,uint32_t n) {
    (void)file;(void)off;memset(dst,0,n);return -1;
}
int settings_save(const void *p,uint32_t n,uint16_t v) { (void)p;(void)n;(void)v;return -1; }
void snd_volume(int v) { (void)v; }
uint32_t plat_buttons(void) { return 0; }
int plat_event(void) { return -1; }

void km_doom_init(void) {
    // The SRAMH arena is NOLOAD, separate from cortex-m-rt's ordinary BSS.
    memset(km_engine_ram,0,sizeof(km_engine_ram));
    read_cache_reset();
#ifdef KM_SQUARE_VIEW
    km_layout(1);
#else
    km_layout(0);
#endif
    if(img_init()) km_fail("Upload matching E1M1 game data");
    km_validate_reads(img.size);
    read_cache_reset();
    rc_place(km_engine_ram+16000);flat_place(NULL);flat_set_slots(0);
    dmc_set.always_run=1;
    V_Init();M_Init();R_Init();P_Init();S_Init(0);D_CheckNetGame();HU_Init();ST_Init();
    detailLevel=1;
    II_Configure(ii_presets[0],0,1);
    G_InitNew(sk_medium,1,1);
    V_RestoreBuffer();R_ExecuteSetViewSize();D_StartGameLoop();D_ResetTics();
    km_present();
}
void D_Display(void);
static uint32_t simulation_cycles;
void km_doom_step(void) {
    uint32_t start=km_cycles();
    I_StartFrame();TryRunTics();
    simulation_cycles=km_cycles()-start;
    S_UpdateSounds(players[consoleplayer].mo);D_Display();km_present();
}
void km_doom_state(uint32_t *state) {
    player_t *p=&players[consoleplayer];
    state[0]=gametic;state[1]=p->mo ? p->mo->x : 0;state[2]=p->mo ? p->mo->y : 0;
    state[3]=p->mo ? p->mo->angle : 0;state[4]=km_keys();
    state[5]=(int32_t)p->cmd.forwardmove;state[6]=(int32_t)p->cmd.angleturn;
    state[7]=(menuactive ? 1u : 0u)|(paused ? 2u : 0u);state[8]=simulation_cycles;
}
int km_doom_menu(void) { return menuactive || messageToPrint; }
int km_doom_width(void) { return scaledviewwidth; }
// This port has no wipe animation; invalidating this state forces a full HUD redraw.
void km_doom_redraw(void) { wipegamestate = (gamestate_t)-1; }
