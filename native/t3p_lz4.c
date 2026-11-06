/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Independent bounded implementation of the LZ4 block format:
 * https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md
 * No frame headers, dictionaries, heap allocation or speculative wide reads. */
#include "t3p_lz4.h"
#include <string.h>

static int extended_length(const uint8_t **cursor,const uint8_t *end,
                           uint32_t base,uint32_t limit,uint32_t *length)
{
    uint32_t value=base;
    unsigned extra;
    if(value>limit)return -1;
    do {
        if(*cursor==end)return -1;
        extra=*(*cursor)++;
        if(extra>limit-value)return -1;
        value+=extra;
    } while(extra==255);
    *length=value;
    return 0;
}

int t3p_lz4_decode(void *dest,uint32_t length,const void *source,uint32_t stored)
{
    if(!dest || !source || !stored)return -1;
    uint8_t *begin=dest,*out=begin,*out_end=begin+length;
    const uint8_t *in=source,*in_end=in+stored;
    while(in<in_end) {
        unsigned token=*in++;
        uint32_t count=token>>4;
        if(count==15 && extended_length(&in,in_end,15,
                (uint32_t)(out_end-out),&count))return -1;
        if(count>(uint32_t)(in_end-in) || count>(uint32_t)(out_end-out))return -1;
        memcpy(out,in,count);out+=count;in+=count;
        if(in==in_end) {
            /* Final sequence is literals only, at least five unless tiny. */
            return out==out_end && (length<5 || count>=5) ? 0 : -1;
        }
        if(in_end-in<2)return -1;
        unsigned distance=(unsigned)in[0]|(unsigned)in[1]<<8;
        in+=2;
        if(!distance || distance>(uint32_t)(out-begin) || out_end-out<12)return -1;
        count=(token&15u)+4;
        if(count==19 && extended_length(&in,in_end,19,
                (uint32_t)(out_end-out),&count))return -1;
        if(count>(uint32_t)(out_end-out))return -1;
        /* Forward byte copies are required when length exceeds distance. */
        const uint8_t *match=out-distance;
        while(count--)*out++=*match++;
    }
    return -1; /* A block must terminate with a literal-only sequence. */
}
