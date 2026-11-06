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
#ifndef T3P_GAME_RAW_ONLY
    uint32_t valid[(T3P_GAME_SLOTS+31)/32],clock;
#endif
#ifdef T3P_PROFILE_HOT
#ifdef T3P_PROFILE_PAGE_TOTALS
#if !defined(T3P_PROFILE_STAGES) || !defined(T3P_PROFILE_RAW_RANK) || !defined(T3P_PROFILE_PAGE_CAPACITY)
#error Page totals require staged timing, raw rank and explicit page capacity
#endif
#define T3P_GAME_MAX_PAGES T3P_PROFILE_PAGE_CAPACITY
#else
#define T3P_GAME_MAX_PAGES (0x400000u/T3P_GAME_PAGE)
#endif
    uint32_t verified[(T3P_GAME_MAX_PAGES+31)/32];
#ifndef T3P_GAME_RAW_ONLY
    uint16_t page_decodes[T3P_GAME_MAX_PAGES];
#endif
#ifdef T3P_PROFILE_PAGE_TOTALS
    uint16_t page_decode_count[T3P_GAME_MAX_PAGES];
#endif
#endif
#ifdef T3P_PROFILE_STAGES
    uint32_t fetch_ms,decode_ms,crc_ms,repeat_decodes;
#ifdef T3P_PROFILE_BREAKDOWN
    uint32_t raw_copy_ms,cache_copy_ms,lz4_ms;
    uint32_t raw_bytes,fetch_bytes,cache_bytes,deflate_calls,lz4_calls;
#endif
#ifndef T3P_GAME_RAW_ONLY
    uint8_t compressed[T3P_GAME_PAGE];
#endif
#endif
#ifndef T3P_GAME_RAW_ONLY
    struct t3p_game_slot slot[T3P_GAME_SLOTS];
#endif
};
#ifdef T3P_PROFILE_PAGE_TOTALS
/* Reset measurement only: retain warmed cache contents and verification bits. */
void t3p_game_profile_reset(struct t3p_game *game);
/* Up to eight page IDs by elapsed time, then count, then ascending page ID.
 * Reporting never mutates the measurements or adds a decode. */
unsigned t3p_game_profile_top(const struct t3p_game *game,uint16_t out[8]);
#endif
int t3p_game_open(struct t3p_game *game,const void *image,uint32_t size);
/* On error, partial destination data may have been copied; caller must stop. */
int t3p_game_read(struct t3p_game *game,uint32_t offset,void *out,uint32_t count);
#endif
