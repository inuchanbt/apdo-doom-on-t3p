/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host-only verification of all game pages, random ranges and corruption. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t3p_game.h"

static uint8_t *file_read(const char *path,uint32_t *size)
{
    FILE *file=fopen(path,"rb"); assert(file);
    assert(!fseek(file,0,SEEK_END)); long length=ftell(file);
    assert(length>0 && length<0x800000); *size=(uint32_t)length;
    assert(!fseek(file,0,SEEK_SET));
    uint8_t *data=malloc(*size); assert(data);
    assert(fread(data,1,*size,file)==*size); assert(!fclose(file));
    return data;
}
static uint32_t random_next(uint32_t *state)
{
    *state=*state*1664525u+1013904223u;
    return *state;
}
#ifdef T3P_PROFILE_STAGES
uint32_t km_millis(void)
{
    static uint32_t ticks;
    return ticks++;
}
#endif
#ifdef T3P_GAME_LZ4
static void put32(uint8_t *p,uint32_t value)
{
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));
}
static uint32_t crc32_table(const uint8_t *data,uint32_t bytes)
{
    uint32_t crc=0xffffffffu;
    while(bytes--) {
        crc^=*data++;
        for(unsigned bit=0;bit<8;bit++)
            crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}
#endif
int main(int argc,char **argv)
{
    assert(argc==3 || argc==4);
    uint32_t packed_size,raw_size;
    uint8_t *image=file_read(argv[1],&packed_size),*raw=file_read(argv[2],&raw_size);
    struct t3p_game game;
    uint8_t buffer[8192];
    assert(!t3p_game_open(&game,image,packed_size));
    assert(game.raw_size==raw_size);
#ifdef T3P_PROFILE_PAGE_TOTALS
    uint32_t target=0;
    while(target<game.pages && (image[64+target*16+7]&0x80u))target++;
    assert(target<game.pages);
    /* Cache hits do not count as decompression. Measurement reset leaves
     * both the warm page and its verified CRC bit intact. */
    assert(!t3p_game_read(&game,target*T3P_GAME_PAGE,buffer,1));
    assert(game.page_decode_count[target]==1 && game.page_decodes[target]==1);
    assert(!t3p_game_read(&game,target*T3P_GAME_PAGE,buffer,1));
    assert(game.page_decode_count[target]==1);
    uint32_t initial_misses=game.misses,initial_crc=game.crc_ms;
    t3p_game_profile_reset(&game);
    assert(!t3p_game_read(&game,target*T3P_GAME_PAGE,buffer,1));
    assert(game.page_decode_count[target]==0 && game.page_decodes[target]==0);
    assert(game.misses==initial_misses && game.crc_ms==initial_crc);
    /* Re-load after eviction: repeated misses accumulate instead of resetting. */
    for(unsigned pass=0;pass<2;pass++) {
        for(uint32_t i=0;i<game.pages;i++)if(i!=target) {
            uint32_t at=i*T3P_GAME_PAGE;
            assert(!t3p_game_read(&game,at,buffer,1));
        }
        assert(!t3p_game_read(&game,target*T3P_GAME_PAGE,buffer,1));
        assert(game.page_decode_count[target]==pass+1);
        assert(game.page_decodes[target]==pass+1);
    }
    /* Saturation must never wrap; reopening discards cache residency. */
    assert(!t3p_game_open(&game,image,packed_size));
    game.page_decode_count[target]=UINT16_MAX;
    game.page_decodes[target]=UINT16_MAX;
    assert(!t3p_game_read(&game,target*T3P_GAME_PAGE,buffer,1));
    assert(game.page_decode_count[target]==UINT16_MAX && game.page_decodes[target]==UINT16_MAX);
    t3p_game_profile_reset(&game);
    uint16_t ranked[8];
    assert(t3p_game_profile_top(&game,ranked)==0);
    /* More than eight candidates, ties and zero-ms decodes; report is read-only. */
    for(unsigned i=0;i<10;i++) {
        game.page_decodes[i]=(uint16_t)(10-i);
        game.page_decode_count[i]=1;
    }
    game.page_decodes[9]=10;game.page_decode_count[9]=2;
    game.page_decodes[10]=0;game.page_decode_count[10]=1;
    assert(t3p_game_profile_top(&game,ranked)==8);
    assert(ranked[0]==9 && ranked[1]==0);
    for(unsigned i=2;i<8;i++)assert(ranked[i]==i-1);
    assert(game.page_decodes[9]==10 && game.page_decode_count[9]==2);
    t3p_game_profile_reset(&game);
    game.page_decode_count[3]=game.page_decode_count[1]=1;
    assert(t3p_game_profile_top(&game,ranked)==2 && ranked[0]==1 && ranked[1]==3);
    assert(!t3p_game_open(&game,image,packed_size));
    puts("PASS page totals: warm reset, hits/eviction/repeated misses, saturation, top-eight and tie order");
#endif
#ifdef T3P_PROFILE_BREAKDOWN
    /* Count actual reader paths, including direct raw Flash and cache hits.
     * The synthetic clock advances once per query, so each timed operation
     * contributes exactly one tick regardless of host execution speed. */
    uint32_t raw_page=game.pages,deflate_page=game.pages,lz4_page=game.pages;
    for(uint32_t i=0;i<game.pages;i++) {
        uint8_t flags=image[64+i*16+7];
        if(flags&0x80u)raw_page=i;
        else if(flags&0x40u)lz4_page=i;
        else deflate_page=i;
    }
    assert(raw_page<game.pages && deflate_page<game.pages);
    assert(!t3p_game_read(&game,raw_page*T3P_GAME_PAGE,buffer,3));
    assert(!t3p_game_read(&game,raw_page*T3P_GAME_PAGE,buffer,3));
    assert(game.raw_bytes==6 && game.raw_copy_ms==2);
    assert(game.crc_ms==1 && game.cache_bytes==0 && game.decode_ms==0);
    assert(!t3p_game_read(&game,deflate_page*T3P_GAME_PAGE,buffer,3));
    uint32_t fetched=game.fetch_bytes;
    assert(fetched>0 && game.fetch_ms==1 && game.decode_ms==1);
    assert(game.deflate_calls==1 && game.lz4_calls==0 && game.lz4_ms==0);
    assert(game.crc_ms==2 && game.cache_bytes==3 && game.cache_copy_ms==1);
    assert(!t3p_game_read(&game,deflate_page*T3P_GAME_PAGE,buffer,3));
    assert(game.cache_bytes==6 && game.cache_copy_ms==2 && game.fetch_bytes==fetched);
    assert(game.decode_ms==1 && game.crc_ms==2 && game.deflate_calls==1);
#ifdef T3P_GAME_LZ4
    assert(lz4_page<game.pages);
    assert(!t3p_game_read(&game,lz4_page*T3P_GAME_PAGE,buffer,3));
    assert(game.lz4_calls==1 && game.lz4_ms==1 && game.deflate_calls==1);
    assert(game.decode_ms==2 && game.crc_ms==3 && game.fetch_bytes>fetched);
#else
    (void)lz4_page;
#endif
    assert(!t3p_game_open(&game,image,packed_size));
    assert(game.raw_bytes==0 && game.cache_copy_ms==0 && game.decode_ms==0);
    puts("PASS profile accounting: raw/cached copies, first-use CRC, fetch, codecs and reset");
#endif
    for (uint32_t at=0;at<raw_size;at+=T3P_GAME_PAGE) {
        uint32_t count=raw_size-at; if (count>T3P_GAME_PAGE) count=T3P_GAME_PAGE;
        assert(!t3p_game_read(&game,at,buffer,count));
        assert(!memcmp(buffer,raw+at,count));
    }
    uint32_t rng=0x12345678;
    for (unsigned i=0;i<5000;i++) {
        uint32_t at=random_next(&rng)%raw_size;
        uint32_t count=1+random_next(&rng)%sizeof(buffer);
        if (count>raw_size-at) count=raw_size-at;
        assert(!t3p_game_read(&game,at,buffer,count));
        assert(!memcmp(buffer,raw+at,count));
    }
    assert(!t3p_game_read(&game,raw_size,0,0));
    buffer[0]=0xa5;
    assert(t3p_game_read(&game,raw_size-1,buffer,2)==-1 && buffer[0]==0xa5);
    assert(t3p_game_read(&game,0xffffffffu,buffer,2)==-1);
    assert(t3p_game_read(&game,0,0,1)==-1);
    uint32_t pages=game.pages,hits=game.hits,misses=game.misses;
#ifdef T3P_GAME_RAW_ONLY
    assert(sizeof(game)<1024 && hits==0 && misses==0);
    assert(game.decode_ms==0 && game.fetch_ms==0);
    /* A valid compressed pack is rejected during open, even when its header
     * and every table entry pass the normal container checks. */
    if(argc==4) {
        uint32_t compressed_size;
        uint8_t *compressed=file_read(argv[3],&compressed_size);
        assert(t3p_game_open(&game,compressed,compressed_size)==-1);
        assert(!game.image && t3p_game_read(&game,0,buffer,1)==-1);
        free(compressed);
    }
#endif
    assert(t3p_game_open(&game,image,63)==-1);
    assert(t3p_game_read(&game,0,buffer,1)==-1);
    assert(t3p_game_open(&game,image,packed_size-1)==-1);
    image[24]^=1; assert(t3p_game_open(&game,image,packed_size)==-1); image[24]^=1;
    image[64]^=1; assert(t3p_game_open(&game,image,packed_size)==-1); image[64]^=1;
    /* Header stays valid, but a payload bit flips: decoder or page CRC rejects. */
    uint32_t payload=64+pages*16;
#ifdef T3P_GAME_LZ4
    if(image[8]==2) {
        /* Recompute the table CRC so rejection tests codec/version semantics. */
        uint32_t entry=0;
        for(uint32_t i=0;i<pages;i++)if(image[64+i*16+7]&0x40u) {
            entry=64+i*16;break;
        }
        assert(entry);
        image[8]=1;
        assert(t3p_game_open(&game,image,packed_size)==-1);
        image[8]=3;
        assert(t3p_game_open(&game,image,packed_size)==-1);
        image[8]=2;
        image[entry+7]|=0x80u; /* RAW and LZ4 cannot both be selected. */
        put32(image+28,crc32_table(image+64,pages*16));
        assert(t3p_game_open(&game,image,packed_size)==-1);
        image[entry+7]&=0x7fu;
        put32(image+28,crc32_table(image+64,pages*16));
        assert(!t3p_game_open(&game,image,packed_size));
    }
#endif
    image[payload]^=1;
    assert(!t3p_game_open(&game,image,packed_size));
    assert(t3p_game_read(&game,0,buffer,1)==-1);
    image[payload]^=1;
    printf("PASS: %u pages, 5000 random ranges, bounds/corruption; cache %u hits/%u misses; state %u bytes (host ABI)\n",
           pages,hits,misses,(unsigned)sizeof(game));
    free(image); free(raw);
    return 0;
}
