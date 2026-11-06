/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_LZ4_H
#define T3P_LZ4_H
#include <stdint.h>
/* Decode one independent raw LZ4 block to exactly length bytes.
 * On error, destination bytes may have changed within the supplied bounds. */
int t3p_lz4_decode(void *dest,uint32_t length,const void *source,uint32_t stored);
#endif
