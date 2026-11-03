/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_GAME_H
#define T3P_GAME_H
#include <stdint.h>
#ifndef T3P_GAME_PAGE
#define T3P_GAME_PAGE 4096u
#endif
#ifndef T3P_GAME_SLOTS
#define T3P_GAME_SLOTS 8
#endif
/* The compressed image must remain readable and unchanged for the lifetime
 * of the reader. Intended for a future verified read-only Flash mapping.
 * A bounded page cache plus metadata is held in RAM; no heap allocation occurs.
 */
struct t3p_game_slot { uint32_t page,used; uint8_t data[T3P_GAME_PAGE]; };
struct t3p_game {
    const uint8_t *image;
    uint32_t image_size,raw_size,pages,hits,misses;
    uint32_t valid[(T3P_GAME_SLOTS+31)/32],clock;
#ifdef T3P_PROFILE_HOT
#define T3P_GAME_MAX_PAGES (0x400000u/T3P_GAME_PAGE)
    uint32_t verified[(T3P_GAME_MAX_PAGES+31)/32];
    uint16_t page_decodes[T3P_GAME_MAX_PAGES];
#endif
#ifdef T3P_PROFILE_STAGES
    uint32_t fetch_ms,decode_ms,crc_ms,repeat_decodes;
    uint8_t compressed[T3P_GAME_PAGE];
#endif
    struct t3p_game_slot slot[T3P_GAME_SLOTS];
};
int t3p_game_open(struct t3p_game *game,const void *image,uint32_t size);
/* On error, partial destination data may have been copied; caller must stop. */
int t3p_game_read(struct t3p_game *game,uint32_t offset,void *out,uint32_t count);
#endif
