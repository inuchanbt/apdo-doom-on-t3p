/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t3p_game.h"
#include "tinf.h"
#include <string.h>
#ifdef T3P_PROFILE_STAGES
extern uint32_t km_millis(void);
#endif
_Static_assert(T3P_GAME_SLOTS>=1 && T3P_GAME_SLOTS<=64,"1..64 cache pages required");

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
    if (!image || size<64 || memcmp(image,"T3PGAME1",8) || u32(image+8)!=1 ||
        u32(image+16)!=T3P_GAME_PAGE || u32(image+24)!=size) return -1;
    uint32_t raw=u32(image+12),pages=u32(image+20);
    if (!raw || raw>0x400000u || pages!=(raw+T3P_GAME_PAGE-1)/T3P_GAME_PAGE ||
        pages>(size-64)/16) return -1;
    if (checksum(image+64,pages*16)!=u32(image+28)) return -1;
    uint32_t next=64+pages*16;
    for (uint32_t page=0;page<pages;page++) {
        const uint8_t *entry=image+64+page*16;
        uint32_t at=u32(entry),flags=u32(entry+4),stored=flags&0x7fffffffu;
        uint32_t length=u32(entry+8),expected=raw-page*T3P_GAME_PAGE;
        if (expected>T3P_GAME_PAGE) expected=T3P_GAME_PAGE;
        if (at!=next || at>size || !stored || stored>size-at || length!=expected ||
            ((flags&0x80000000u) ? stored!=length : stored>=length)) return -1;
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
    uint32_t flags=u32(entry+4),length=u32(entry+8),stored=flags&0x7fffffffu;
    const uint8_t *source=g->image+u32(entry);
    uint8_t *dest=g->slot[index].data;
    if (flags&0x80000000u) memcpy(dest,source,length);
    else {
#ifdef T3P_PROFILE_STAGES
        uint32_t start=km_millis();
        memcpy(g->compressed,source,stored);
        source=g->compressed;
        uint32_t fetched=km_millis();
        g->fetch_ms+=fetched-start;
#endif
        unsigned actual=length;
        if (tinf_uncompress(dest,&actual,source,stored)!=TINF_OK || actual!=length) return -1;
#ifdef T3P_PROFILE_STAGES
        uint32_t elapsed=km_millis()-fetched;
        g->decode_ms+=elapsed;
#ifdef T3P_PROFILE_RAW_RANK
        g->repeat_decodes+=(g->verified[page/32]>>(page%32))&1u;
        uint32_t total=g->page_decodes[page]+elapsed;
        g->page_decodes[page]=total>UINT16_MAX ? UINT16_MAX : total;
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
int t3p_game_read(struct t3p_game *g,uint32_t at,void *out,uint32_t count)
{
    if (!g || !g->image || at>g->raw_size || count>g->raw_size-at || (!out && count)) return -1;
    uint8_t *dest=out;
    while (count) {
        uint32_t page=at/T3P_GAME_PAGE,within=at%T3P_GAME_PAGE;
        uint32_t take=T3P_GAME_PAGE-within;
        if (take>count) take=count;
        const uint8_t *data;
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
        memcpy(dest,data+within,take);
        dest+=take; at+=take; count-=take;
    }
    return 0;
}
