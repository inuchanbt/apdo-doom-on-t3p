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
int main(int argc,char **argv)
{
    assert(argc==3);
    uint32_t packed_size,raw_size;
    uint8_t *image=file_read(argv[1],&packed_size),*raw=file_read(argv[2],&raw_size);
    struct t3p_game game;
    uint8_t buffer[8192];
    assert(!t3p_game_open(&game,image,packed_size));
    assert(game.raw_size==raw_size);
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
    assert(t3p_game_open(&game,image,63)==-1);
    assert(t3p_game_read(&game,0,buffer,1)==-1);
    assert(t3p_game_open(&game,image,packed_size-1)==-1);
    image[24]^=1; assert(t3p_game_open(&game,image,packed_size)==-1); image[24]^=1;
    image[64]^=1; assert(t3p_game_open(&game,image,packed_size)==-1); image[64]^=1;
    /* Header stays valid, but a payload bit flips: decoder or page CRC rejects. */
    uint32_t payload=64+pages*16;
    image[payload]^=1;
    assert(!t3p_game_open(&game,image,packed_size));
    assert(t3p_game_read(&game,0,buffer,1)==-1);
    image[payload]^=1;
    printf("PASS: %u pages, 5000 random ranges, bounds/corruption; cache %u hits/%u misses; state %u bytes (host ABI)\n",
           pages,hits,misses,(unsigned)sizeof(game));
    free(image); free(raw);
    return 0;
}
