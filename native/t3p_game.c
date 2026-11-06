/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_game.h"
#ifndef T3P_GAME_RAW_ONLY
#include "tinf.h"
#endif
#ifdef T3P_GAME_LZ4
#include "t3p_lz4.h"
#define STORED_MASK 0x3fffffffu
#else
#define STORED_MASK 0x7fffffffu
#endif
#include <string.h>
#ifdef T3P_PROFILE_STAGES
extern uint32_t km_millis(void);
#endif
#ifdef T3P_GAME_RAW_ONLY
#if !defined(T3P_PROFILE_HOT) || defined(T3P_PROFILE_RAW_RANK) || defined(T3P_PROFILE_PAGE_TOTALS) || defined(T3P_GAME_LZ4)
#error Raw-only reading requires first-use CRC verification and no decoder ranking/LZ4
#endif
_Static_assert(T3P_GAME_SLOTS==0,"Raw-only builds must not reserve decoder cache pages");
#else
_Static_assert(T3P_GAME_SLOTS>=1 && T3P_GAME_SLOTS<=64,"1..64 cache pages required");
#endif

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint32_t checksum(const uint8_t *p,uint32_t count)
{
    static const uint32_t table[16]={
        0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
        0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
        0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
        0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu
    };
    uint32_t crc=0xffffffffu;
    while (count--) {
        crc^=*p++;
        crc=(crc>>4)^table[crc&15u];
        crc=(crc>>4)^table[crc&15u];
    }
    return ~crc;
}
int t3p_game_open(struct t3p_game *g,const void *source,uint32_t size)
{
    const uint8_t *image=source;
    if (!g) return -1;
    memset(g,0,sizeof(*g));
    if (!image || size<64 || memcmp(image,"T3PGAME1",8) ||
#ifdef T3P_GAME_LZ4
        (u32(image+8)!=1 && u32(image+8)!=2) ||
#else
        u32(image+8)!=1 ||
#endif
        u32(image+16)!=T3P_GAME_PAGE || u32(image+24)!=size) return -1;
    uint32_t raw=u32(image+12),pages=u32(image+20);
    if (!raw || raw>0x400000u || pages!=(raw+T3P_GAME_PAGE-1)/T3P_GAME_PAGE ||
        pages>(size-64)/16) return -1;
#ifdef T3P_PROFILE_HOT
    if(pages>T3P_GAME_MAX_PAGES)return -1;
#endif
    if (checksum(image+64,pages*16)!=u32(image+28)) return -1;
    uint32_t next=64+pages*16;
    for (uint32_t page=0;page<pages;page++) {
        const uint8_t *entry=image+64+page*16;
        uint32_t at=u32(entry),flags=u32(entry+4),stored=flags&STORED_MASK;
        uint32_t length=u32(entry+8),expected=raw-page*T3P_GAME_PAGE;
        if (expected>T3P_GAME_PAGE) expected=T3P_GAME_PAGE;
        if (at!=next || at>size || !stored || stored>size-at || length!=expected ||
            ((flags&0x80000000u) ? stored!=length : stored>=length)) return -1;
#ifdef T3P_GAME_RAW_ONLY
        /* Reject compressed input before publishing any readable image. */
        if(!(flags&0x80000000u))return -1;
#endif
#ifdef T3P_GAME_LZ4
        if((flags&0xc0000000u)==0xc0000000u ||
           (u32(image+8)==1 && (flags&0x40000000u)))return -1;
#endif
        next+=stored;
    }
    if (next!=size) return -1;
    g->image=image; g->image_size=size; g->raw_size=raw; g->pages=pages;
    return 0;
}
#ifdef T3P_PROFILE_HOT
static int verify_page(struct t3p_game *g,uint32_t page,const uint8_t *entry,
                       const uint8_t *data,uint32_t length)
{
    uint32_t mask=1u<<(page%32);
    if(g->verified[page/32]&mask)return 0;
#ifdef T3P_PROFILE_STAGES
    uint32_t start=km_millis();
#endif
    if(checksum(data,length)!=u32(entry+12))return -1;
#ifdef T3P_PROFILE_STAGES
    g->crc_ms+=km_millis()-start;
#endif
    g->verified[page/32]|=mask;
    return 0;
}
#endif
#ifndef T3P_GAME_RAW_ONLY
static int page_load(struct t3p_game *g,uint32_t page)
{
    for (unsigned index=0;index<T3P_GAME_SLOTS;index++) {
        if ((g->valid[index/32]&(1u<<(index%32))) && g->slot[index].page==page) {
            g->slot[index].used=++g->clock; g->hits++;
            return (int)index;
        }
    }
    unsigned index=0;
    uint32_t oldest=0;
    for (unsigned candidate=0;candidate<T3P_GAME_SLOTS;candidate++) {
        if (!(g->valid[candidate/32]&(1u<<(candidate%32)))) { index=candidate; break; }
        uint32_t age=g->clock-g->slot[candidate].used;
        if (age>=oldest) { oldest=age; index=candidate; }
    }
    uint32_t mask=1u<<(index%32);
    g->valid[index/32] &= ~mask;
    const uint8_t *entry=g->image+64+page*16;
    uint32_t flags=u32(entry+4),length=u32(entry+8),stored=flags&STORED_MASK;
    const uint8_t *source=g->image+u32(entry);
    uint8_t *dest=g->slot[index].data;
    if (flags&0x80000000u) {
#ifdef T3P_PROFILE_BREAKDOWN
        uint32_t start=km_millis();
#endif
        memcpy(dest,source,length);
#ifdef T3P_PROFILE_BREAKDOWN
        g->raw_copy_ms+=km_millis()-start;
        g->raw_bytes+=length;
#endif
    }
    else {
#ifdef T3P_PROFILE_STAGES
        uint32_t start=km_millis();
        memcpy(g->compressed,source,stored);
        source=g->compressed;
        uint32_t fetched=km_millis();
        g->fetch_ms+=fetched-start;
#ifdef T3P_PROFILE_BREAKDOWN
        g->fetch_bytes+=stored;
#endif
#endif
        unsigned actual=length;
#ifdef T3P_GAME_LZ4
        if(flags&0x40000000u) {
            if(t3p_lz4_decode(dest,length,source,stored))return -1;
        } else
#endif
        if (tinf_uncompress(dest,&actual,source,stored)!=TINF_OK || actual!=length) return -1;
#ifdef T3P_PROFILE_STAGES
        uint32_t elapsed=km_millis()-fetched;
        g->decode_ms+=elapsed;
#ifdef T3P_PROFILE_BREAKDOWN
#ifdef T3P_GAME_LZ4
        if(flags&0x40000000u) { g->lz4_ms+=elapsed; g->lz4_calls++; }
        else
#endif
            g->deflate_calls++;
#endif
#ifdef T3P_PROFILE_RAW_RANK
        g->repeat_decodes+=(g->verified[page/32]>>(page%32))&1u;
        uint32_t total=g->page_decodes[page]+elapsed;
        g->page_decodes[page]=total>UINT16_MAX ? UINT16_MAX : total;
#ifdef T3P_PROFILE_PAGE_TOTALS
        if(g->page_decode_count[page]!=UINT16_MAX)g->page_decode_count[page]++;
#endif
#else
        g->repeat_decodes+=g->page_decodes[page]!=0;
#endif
#endif
#if defined(T3P_PROFILE_HOT) && !defined(T3P_PROFILE_RAW_RANK)
        if(g->page_decodes[page]!=UINT16_MAX)g->page_decodes[page]++;
#endif
    }
#ifdef T3P_PROFILE_HOT
    /* The pack is immutable. Reopening clears all verified bits. */
    if(verify_page(g,page,entry,dest,length))return -1;
#else
    if (checksum(dest,length)!=u32(entry+12)) return -1;
#endif
    g->slot[index].page=page;
    g->valid[index/32]|=mask; g->slot[index].used=++g->clock; g->misses++;
    return (int)index;
}
#endif
int t3p_game_read(struct t3p_game *g,uint32_t at,void *out,uint32_t count)
{
    if (!g || !g->image || at>g->raw_size || count>g->raw_size-at || (!out && count)) return -1;
    uint8_t *dest=out;
    while (count) {
        uint32_t page=at/T3P_GAME_PAGE,within=at%T3P_GAME_PAGE;
        uint32_t take=T3P_GAME_PAGE-within;
        if (take>count) take=count;
        const uint8_t *data;
#ifdef T3P_GAME_RAW_ONLY
        const uint8_t *entry=g->image+64+page*16;
        data=g->image+u32(entry);
        if(verify_page(g,page,entry,data,u32(entry+8)))return -1;
#else
#ifdef T3P_PROFILE_HOT
        const uint8_t *entry=g->image+64+page*16;
        if(u32(entry+4)&0x80000000u) {
            /* Uncompressed pages can be read directly from mapped Flash. */
            data=g->image+u32(entry);
            if(verify_page(g,page,entry,data,u32(entry+8)))return -1;
        } else
#endif
        {
            int index=page_load(g,page);
            if(index<0)return -1;
            data=g->slot[index].data;
        }
#endif
#ifdef T3P_PROFILE_BREAKDOWN
        uint32_t copy_start=km_millis();
#endif
        memcpy(dest,data+within,take);
#ifdef T3P_PROFILE_BREAKDOWN
#ifdef T3P_PROFILE_HOT
        if(u32(entry+4)&0x80000000u) {
            g->raw_copy_ms+=km_millis()-copy_start; g->raw_bytes+=take;
        } else
#endif
        {
            g->cache_copy_ms+=km_millis()-copy_start; g->cache_bytes+=take;
        }
#endif
        dest+=take; at+=take; count-=take;
    }
    return 0;
}

#ifdef T3P_PROFILE_PAGE_TOTALS
void t3p_game_profile_reset(struct t3p_game *g)
{
    memset(g->page_decodes,0,sizeof(g->page_decodes));
    memset(g->page_decode_count,0,sizeof(g->page_decode_count));
}
unsigned t3p_game_profile_top(const struct t3p_game *g,uint16_t out[8])
{
    unsigned found=0;
    for(unsigned rank=0;rank<8;rank++) {
        unsigned best=g->pages;
        for(unsigned page=0;page<g->pages;page++) {
            if(!g->page_decode_count[page])continue;
            unsigned previous=0;
            while(previous<found && out[previous]!=page)previous++;
            if(previous<found)continue;
            if(best==g->pages || g->page_decodes[page]>g->page_decodes[best] ||
               (g->page_decodes[page]==g->page_decodes[best] &&
                g->page_decode_count[page]>g->page_decode_count[best]))best=page;
        }
        if(best==g->pages)break;
        out[found++]=(uint16_t)best;
    }
    return found;
}
#endif
