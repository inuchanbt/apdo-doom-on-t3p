/*
 * tinflate - tiny inflate
 *
 * Copyright (c) 2003-2019 Joergen Ibsen
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 *   1. The origin of this software must not be misrepresented; you must
 *      not claim that you wrote the original software. If you use this
 *      software in a product, an acknowledgment in the product
 *      documentation would be appreciated but is not required.
 *
 *   2. Altered source versions must be plainly marked as such, and must
 *      not be misrepresented as being the original software.
 *
 *   3. This notice may not be removed or altered from any source
 *      distribution.
 */

#include "tinf.h"

#include <assert.h>
#include <limits.h>
#ifdef T3P_TINF_PROFILE
#include "t3p_tinf_profile.h"
#endif

#if defined(UINT_MAX) && (UINT_MAX) < 0xFFFFFFFFUL
#  error "tinf requires unsigned int to be at least 32-bit"
#endif

/* T3P modification: configurable 7/8-bit prefix tables. At most one byte is
 * peeked so no complete prefetched byte survives a stored-block boundary. */
#ifdef T3P_TINF_PREFIX
#ifndef T3P_TINF_PREFIX_BITS
#define T3P_TINF_PREFIX_BITS 7
#endif
#if T3P_TINF_PREFIX_BITS < 7 || T3P_TINF_PREFIX_BITS > 8
#error "T3P prefix lookup supports 7 or 8 bits"
#endif
#define T3P_TINF_PREFIX_SIZE (1u << T3P_TINF_PREFIX_BITS)
#endif

#if defined(T3P_TINF_PREFIX_CONTINUE) && (!defined(T3P_TINF_PREFIX) || !defined(T3P_TINF_LOCAL_BITS))
#error "Prefix continuation requires the prefix table and local bit reader"
#endif

/* V39/V40 compile identical hot-loop objects. The linker places this
 * section in Flash for V39 or initialized ordinary SRAM for V40. */
#ifdef T3P_TINF_HOT_SECTION
#define T3P_TINF_HOT __attribute__((section(".tinf_hot"), noinline, aligned(4)))
#else
#define T3P_TINF_HOT
#endif

/* -- Internal data structures -- */

struct tinf_tree {
#ifdef T3P_TINF_PREFIX
    unsigned short fast[T3P_TINF_PREFIX_SIZE];
#endif
	unsigned short counts[16]; /* Number of codes with a given length */
	unsigned short symbols[288]; /* Symbols sorted by code */
	int max_sym;
};

struct tinf_data {
	const unsigned char *source;
	const unsigned char *source_end;
	unsigned int tag;
	int bitcount;
	int overflow;

	unsigned char *dest_start;
	unsigned char *dest;
	unsigned char *dest_end;

	struct tinf_tree ltree; /* Literal/length tree */
	struct tinf_tree dtree; /* Distance tree */
};

/* -- Utility functions -- */

static unsigned int read_le16(const unsigned char *p)
{
	return ((unsigned int) p[0])
	     | ((unsigned int) p[1] << 8);
}

#ifdef T3P_TINF_PREFIX
/* Entries contain code length in the high bits and the symbol in nine bits. */
static void tinf_build_fast(struct tinf_tree *t)
{
    unsigned code=0, base=0;
    for(unsigned i=0;i<T3P_TINF_PREFIX_SIZE;i++)t->fast[i]=0;
    for(unsigned length=1;length<=T3P_TINF_PREFIX_BITS;length++) {
        for(unsigned i=0;i<t->counts[length];i++) {
            unsigned bits=code+i, reversed=0;
            for(unsigned n=0;n<length;n++) {
                reversed=(reversed<<1)|(bits&1u);
                bits>>=1;
            }
            for(unsigned at=reversed;at<T3P_TINF_PREFIX_SIZE;at+=1u<<length)
                t->fast[at]=(length<<9)|t->symbols[base+i];
        }
        base+=t->counts[length];
        code=(code+t->counts[length])<<1;
    }
#ifdef T3P_TINF_PREFIX_CONTINUE
    /* A missing prefix is an internal node, not a reason to reread its
     * bits. Store its residual tree offset; counts[0] (unused by decoding)
     * holds the common symbol base after the prefix depth. */
    unsigned first=code>>1, reversed=0;
    for(unsigned n=0;n<T3P_TINF_PREFIX_BITS;n++)
        reversed=(reversed<<1)|((first>>n)&1u);
    for(unsigned offset=0;first+offset<T3P_TINF_PREFIX_SIZE;offset++) {
        t->fast[reversed]=0x8000u|offset;
        unsigned step=T3P_TINF_PREFIX_SIZE>>1;
        while(step && (reversed&step)) { reversed^=step; step>>=1; }
        reversed^=step;
    }
    t->counts[0]=base;
#endif
}
#endif

/* Build fixed Huffman trees */
static void tinf_build_fixed_trees(struct tinf_tree *lt, struct tinf_tree *dt)
{
	int i;

	/* Build fixed literal/length tree */
	for (i = 0; i < 16; ++i) {
		lt->counts[i] = 0;
	}

	lt->counts[7] = 24;
	lt->counts[8] = 152;
	lt->counts[9] = 112;

	for (i = 0; i < 24; ++i) {
		lt->symbols[i] = 256 + i;
	}
	for (i = 0; i < 144; ++i) {
		lt->symbols[24 + i] = i;
	}
	for (i = 0; i < 8; ++i) {
		lt->symbols[24 + 144 + i] = 280 + i;
	}
	for (i = 0; i < 112; ++i) {
		lt->symbols[24 + 144 + 8 + i] = 144 + i;
	}

	lt->max_sym = 285;

	/* Build fixed distance tree */
	for (i = 0; i < 16; ++i) {
		dt->counts[i] = 0;
	}

	dt->counts[5] = 32;

	for (i = 0; i < 32; ++i) {
		dt->symbols[i] = i;
	}

	dt->max_sym = 29;
#ifdef T3P_TINF_PREFIX
    tinf_build_fast(lt);
    tinf_build_fast(dt);
#endif
}

/* Given an array of code lengths, build a tree */
static int tinf_build_tree(struct tinf_tree *t, const unsigned char *lengths,
                           unsigned int num)
{
	unsigned short offs[16];
	unsigned int i, num_codes, available;

	assert(num <= 288);

	for (i = 0; i < 16; ++i) {
		t->counts[i] = 0;
	}

	t->max_sym = -1;

	/* Count number of codes for each non-zero length */
	for (i = 0; i < num; ++i) {
		assert(lengths[i] <= 15);

		if (lengths[i]) {
			t->max_sym = i;
			t->counts[lengths[i]]++;
		}
	}

	/* Compute offset table for distribution sort */
	for (available = 1, num_codes = 0, i = 0; i < 16; ++i) {
		unsigned int used = t->counts[i];

		/* Check length contains no more codes than available */
		if (used > available) {
			return TINF_DATA_ERROR;
		}
		available = 2 * (available - used);

		offs[i] = num_codes;
		num_codes += used;
	}

	/*
	 * Check all codes were used, or for the special case of only one
	 * code that it has length 1
	 */
	if ((num_codes > 1 && available > 0)
	 || (num_codes == 1 && t->counts[1] != 1)) {
		return TINF_DATA_ERROR;
	}

	/* Fill in symbols sorted by code */
	for (i = 0; i < num; ++i) {
		if (lengths[i]) {
			t->symbols[offs[lengths[i]]++] = i;
		}
	}

	/*
	 * For the special case of only one code (which will be 0) add a
	 * code 1 which results in a symbol that is too large
	 */
	if (num_codes == 1) {
		t->counts[1] = 2;
		t->symbols[1] = t->max_sym + 1;
	}

#ifdef T3P_TINF_PREFIX
    tinf_build_fast(t);
#endif
	return TINF_OK;
}

/* -- Decode functions -- */

static void tinf_refill(struct tinf_data *d, int num)
{
	assert(num >= 0 && num <= 32);

	/* Read bytes until at least num bits available */
	while (d->bitcount < num) {
		if (d->source != d->source_end) {
			d->tag |= (unsigned int) *d->source++ << d->bitcount;
		}
		else {
			d->overflow = 1;
		}
		d->bitcount += 8;
	}

	assert(d->bitcount <= 32);
}

static unsigned int tinf_getbits_no_refill(struct tinf_data *d, int num)
{
	unsigned int bits;

	assert(num >= 0 && num <= d->bitcount);

	/* Get bits from tag */
	bits = d->tag & ((1UL << num) - 1);

	/* Remove bits from tag */
	d->tag >>= num;
	d->bitcount -= num;

	return bits;
}

/* Get num bits from source stream */
static unsigned int tinf_getbits(struct tinf_data *d, int num)
{
	tinf_refill(d, num);
	return tinf_getbits_no_refill(d, num);
}

/* Read a num bit value from stream and add base */
static unsigned int tinf_getbits_base(struct tinf_data *d, int num, int base)
{
#ifdef T3P_TINF_PROFILE
    uint32_t sample=t3p_tinf_sample_begin(TINF_P_EXTRA_CALLS);
#endif
    unsigned value=base + (num ? tinf_getbits(d, num) : 0);
#ifdef T3P_TINF_PROFILE
    t3p_tinf_sample_end(TINF_P_EXTRA_CALLS,sample);
#endif
    return value;
}

/* Given a data stream and a tree, decode a symbol */
static T3P_TINF_HOT int tinf_decode_symbol(struct tinf_data *d, const struct tinf_tree *t)
{
#ifdef T3P_TINF_PROFILE
    uint32_t sample=t3p_tinf_sample_begin(TINF_P_SYMBOL_CALLS);
#endif
	int base = 0, offs = 0;
	int len;
#ifdef T3P_TINF_LOCAL_BITS
	unsigned int tag = d->tag;
	int bitcount = d->bitcount;
	const unsigned char *source = d->source;
#ifdef T3P_TINF_PREFIX
    /* Peek at the next byte, committing it only if this code consumes it.
     * Never retain a whole prefetched byte across a stored-block boundary. */
    unsigned peek=tag;
    if(bitcount<T3P_TINF_PREFIX_BITS && source!=d->source_end)
        peek|=(unsigned)*source<<bitcount;
    unsigned entry=t->fast[peek&(T3P_TINF_PREFIX_SIZE-1u)];
    unsigned used=entry>>9;
#ifdef T3P_TINF_PREFIX_CONTINUE
    if(entry&0x8000u) {
        if(bitcount>=T3P_TINF_PREFIX_BITS || source!=d->source_end) {
            if(bitcount<T3P_TINF_PREFIX_BITS) {
                tag|=(unsigned)*source++<<bitcount;
                bitcount+=8;
            }
            tag>>=T3P_TINF_PREFIX_BITS;
            bitcount-=T3P_TINF_PREFIX_BITS;
            base=t->counts[0];
            offs=entry&0x7fffu;
            len=T3P_TINF_PREFIX_BITS+1;
            goto continue_code;
        }
    } else
#endif
    if(used && (used<=(unsigned)bitcount || source!=d->source_end)) {
        if(used>(unsigned)bitcount) {
            tag|=(unsigned)*source++<<bitcount;
            bitcount+=8;
        }
        d->tag=tag>>used;
        d->bitcount=bitcount-used;
        d->source=source;
#ifdef T3P_TINF_PROFILE
        t3p_tinf_sample_end(TINF_P_SYMBOL_CALLS,sample);
#endif
        return entry&511u;
    }
#endif
#endif

	/*
	 * Get more bits while code index is above number of codes
	 *
	 * Rather than the actual code, we are computing the position of the
	 * code in the sorted order of codes, which is the index of the
	 * corresponding symbol.
	 *
	 * Conceptually, for each code length (level in the tree), there are
	 * counts[len] leaves on the left and internal nodes on the right.
	 * The index we have decoded so far is base + offs, and if that
	 * falls within the leaves we are done. Otherwise we adjust the range
	 * of offs and add one more bit to it.
	 */
	for (len = 1; ; ++len) {
#ifdef T3P_TINF_PREFIX_CONTINUE
continue_code:
#endif
#ifdef T3P_TINF_LOCAL_BITS
		if (!bitcount) {
			if (source != d->source_end) {
				tag = *source++;
			} else {
				d->overflow = 1;
				tag = 0;
			}
			bitcount = 8;
		}
		offs = 2 * offs + (int)(tag & 1u);
		tag >>= 1;
		--bitcount;
#else
		offs = 2 * offs + tinf_getbits(d, 1);
#endif

		assert(len <= 15);

		if (offs < t->counts[len]) {
			break;
		}

		base += t->counts[len];
		offs -= t->counts[len];
	}

#ifdef T3P_TINF_LOCAL_BITS
	d->tag = tag;
	d->bitcount = bitcount;
	d->source = source;
#endif
	assert(base + offs >= 0 && base + offs < 288);

#ifdef T3P_TINF_PROFILE
    t3p_tinf_sample_end(TINF_P_SYMBOL_CALLS,sample);
#endif
	return t->symbols[base + offs];
}

/* Given a data stream, decode dynamic trees from it */
static int tinf_decode_trees(struct tinf_data *d, struct tinf_tree *lt,
                             struct tinf_tree *dt)
{
	unsigned char lengths[288 + 32];

	/* Special ordering of code length codes */
	static const unsigned char clcidx[19] = {
		16, 17, 18, 0,  8, 7,  9, 6, 10, 5,
		11,  4, 12, 3, 13, 2, 14, 1, 15
	};

	unsigned int hlit, hdist, hclen;
	unsigned int i, num, length;
	int res;

	/* Get 5 bits HLIT (257-286) */
	hlit = tinf_getbits_base(d, 5, 257);

	/* Get 5 bits HDIST (1-32) */
	hdist = tinf_getbits_base(d, 5, 1);

	/* Get 4 bits HCLEN (4-19) */
	hclen = tinf_getbits_base(d, 4, 4);

	/*
	 * The RFC limits the range of HLIT to 286, but lists HDIST as range
	 * 1-32, even though distance codes 30 and 31 have no meaning. While
	 * we could allow the full range of HLIT and HDIST to make it possible
	 * to decode the fixed trees with this function, we consider it an
	 * error here.
	 *
	 * See also: https://github.com/madler/zlib/issues/82
	 */
	if (hlit > 286 || hdist > 30) {
		return TINF_DATA_ERROR;
	}

	for (i = 0; i < 19; ++i) {
		lengths[i] = 0;
	}

	/* Read code lengths for code length alphabet */
	for (i = 0; i < hclen; ++i) {
		/* Get 3 bits code length (0-7) */
		unsigned int clen = tinf_getbits(d, 3);

		lengths[clcidx[i]] = clen;
	}

	/* Build code length tree (in literal/length tree to save space) */
	res = tinf_build_tree(lt, lengths, 19);

	if (res != TINF_OK) {
		return res;
	}

	/* Check code length tree is not empty */
	if (lt->max_sym == -1) {
		return TINF_DATA_ERROR;
	}

	/* Decode code lengths for the dynamic trees */
	for (num = 0; num < hlit + hdist; ) {
		int sym = tinf_decode_symbol(d, lt);

		if (sym > lt->max_sym) {
			return TINF_DATA_ERROR;
		}

		switch (sym) {
		case 16:
			/* Copy previous code length 3-6 times (read 2 bits) */
			if (num == 0) {
				return TINF_DATA_ERROR;
			}
			sym = lengths[num - 1];
			length = tinf_getbits_base(d, 2, 3);
			break;
		case 17:
			/* Repeat code length 0 for 3-10 times (read 3 bits) */
			sym = 0;
			length = tinf_getbits_base(d, 3, 3);
			break;
		case 18:
			/* Repeat code length 0 for 11-138 times (read 7 bits) */
			sym = 0;
			length = tinf_getbits_base(d, 7, 11);
			break;
		default:
			/* Values 0-15 represent the actual code lengths */
			length = 1;
			break;
		}

		if (length > hlit + hdist - num) {
			return TINF_DATA_ERROR;
		}

		while (length--) {
			lengths[num++] = sym;
		}
	}

	/* Check EOB symbol is present */
	if (lengths[256] == 0) {
		return TINF_DATA_ERROR;
	}

	/* Build dynamic trees */
	res = tinf_build_tree(lt, lengths, hlit);

	if (res != TINF_OK) {
		return res;
	}

	res = tinf_build_tree(dt, lengths + hlit, hdist);

	if (res != TINF_OK) {
		return res;
	}

	return TINF_OK;
}

/* -- Block inflate functions -- */

/* Given a stream and two trees, inflate a block of data */
static T3P_TINF_HOT int tinf_inflate_block_data(struct tinf_data *d, struct tinf_tree *lt,
                                   struct tinf_tree *dt)
{
	/* Extra bits and base tables for length codes */
	static const unsigned char length_bits[30] = {
		0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
		1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
		4, 4, 4, 4, 5, 5, 5, 5, 0, 127
	};

	static const unsigned short length_base[30] = {
		 3,  4,  5,   6,   7,   8,   9,  10,  11,  13,
		15, 17, 19,  23,  27,  31,  35,  43,  51,  59,
		67, 83, 99, 115, 131, 163, 195, 227, 258,   0
	};

	/* Extra bits and base tables for distance codes */
	static const unsigned char dist_bits[30] = {
		0, 0,  0,  0,  1,  1,  2,  2,  3,  3,
		4, 4,  5,  5,  6,  6,  7,  7,  8,  8,
		9, 9, 10, 10, 11, 11, 12, 12, 13, 13
	};

	static const unsigned short dist_base[30] = {
		   1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
		  33,   49,   65,   97,  129,  193,  257,   385,   513,   769,
		1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
	};

	for (;;) {
#ifdef T3P_TINF_LITERAL_RUN
#if !defined(T3P_TINF_LOCAL_BITS) || !defined(T3P_TINF_PREFIX) || defined(T3P_TINF_PROFILE)
#error Literal runs require local bits and prefix lookup, without helper profiling
#endif
        /* Keep the bit cursor and output pointer local across short literals.
         * A peek never commits the following byte unless this code uses it:
         * stored-block boundaries must retain the original byte alignment. */
        unsigned tag=d->tag;
        int bitcount=d->bitcount;
        const unsigned char *source=d->source;
        unsigned char *output=d->dest;
        while(output!=d->dest_end && !d->overflow) {
            unsigned peek=tag;
            if(bitcount<T3P_TINF_PREFIX_BITS && source!=d->source_end)
                peek|=(unsigned)*source<<bitcount;
            unsigned entry=lt->fast[peek&(T3P_TINF_PREFIX_SIZE-1u)];
            unsigned used=entry>>9;
            if((entry&0x8000u) || !used || (entry&511u)>=256u ||
               (used>(unsigned)bitcount && source==d->source_end))break;
            if(used>(unsigned)bitcount) {
                tag|=(unsigned)*source++<<bitcount;
                bitcount+=8;
            }
            tag>>=used;
            bitcount-=used;
            *output++=(unsigned char)entry;
        }
        d->tag=tag;d->bitcount=bitcount;d->source=source;d->dest=output;
        /* Long literals, length/distance pairs, EOB and error handling use
         * the unchanged bounded decoder after publishing the local cursor. */
#endif
		int sym = tinf_decode_symbol(d, lt);

		/* Check for overflow in bit reader */
		if (d->overflow) {
			return TINF_DATA_ERROR;
		}

		if (sym < 256) {
			if (d->dest == d->dest_end) {
				return TINF_BUF_ERROR;
			}
			*d->dest++ = sym;
		}
		else {
			int length, dist, offs;
#ifndef T3P_TINF_MATCH_COPY
			int i;
#endif

			/* Check for end of block */
			if (sym == 256) {
				return TINF_OK;
			}

			/* Check sym is within range and distance tree is not empty */
			if (sym > lt->max_sym || sym - 257 > 28 || dt->max_sym == -1) {
				return TINF_DATA_ERROR;
			}

			sym -= 257;

			/* Possibly get more bits from length code */
			length = tinf_getbits_base(d, length_bits[sym],
			                           length_base[sym]);

			dist = tinf_decode_symbol(d, dt);

			/* Check dist is within range */
			if (dist > dt->max_sym || dist > 29) {
				return TINF_DATA_ERROR;
			}

			/* Possibly get more bits from distance code */
			offs = tinf_getbits_base(d, dist_bits[dist],
			                         dist_base[dist]);

			if (offs > d->dest - d->dest_start) {
				return TINF_DATA_ERROR;
			}

			if (d->dest_end - d->dest < length) {
				return TINF_BUF_ERROR;
			}

			/* Forward byte copies preserve overlapping DEFLATE matches. */
#ifdef T3P_TINF_MATCH_COPY
			unsigned char *output = d->dest;
			const unsigned char *match = output - offs;
			int remaining = length;
			while (remaining >= 4) {
				output[0] = match[0];
				output[1] = match[1];
				output[2] = match[2];
				output[3] = match[3];
				output += 4;
				match += 4;
				remaining -= 4;
			}
			while (remaining--) {
				*output++ = *match++;
			}
			d->dest = output;
#else
			for (i = 0; i < length; ++i) {
				d->dest[i] = d->dest[i - offs];
			}
			d->dest += length;
#endif
		}
	}
}

/* Inflate an uncompressed block of data */
static int tinf_inflate_uncompressed_block(struct tinf_data *d)
{
	unsigned int length, invlength;

	if (d->source_end - d->source < 4) {
		return TINF_DATA_ERROR;
	}

	/* Get length */
	length = read_le16(d->source);

	/* Get one's complement of length */
	invlength = read_le16(d->source + 2);

	/* Check length */
	if (length != (~invlength & 0x0000FFFF)) {
		return TINF_DATA_ERROR;
	}

	d->source += 4;

	if (d->source_end - d->source < length) {
		return TINF_DATA_ERROR;
	}

	if (d->dest_end - d->dest < length) {
		return TINF_BUF_ERROR;
	}

	/* Copy block */
	while (length--) {
		*d->dest++ = *d->source++;
	}

	/* Make sure we start next block on a byte boundary */
	d->tag = 0;
	d->bitcount = 0;

	return TINF_OK;
}

/* Inflate a block of data compressed with fixed Huffman trees */
static int tinf_inflate_fixed_block(struct tinf_data *d)
{
#ifdef T3P_TINF_PROFILE
    uint32_t start=t3p_tinf_clock();
    t3p_tinf_profile[TINF_P_FIXED]++;
#endif
    tinf_build_fixed_trees(&d->ltree, &d->dtree);
#ifdef T3P_TINF_PROFILE
    t3p_tinf_charge(TINF_P_TREE,start);
    start=t3p_tinf_clock();
    int result=tinf_inflate_block_data(d, &d->ltree, &d->dtree);
    t3p_tinf_charge(TINF_P_PAYLOAD,start);
    return result;
#else
    return tinf_inflate_block_data(d, &d->ltree, &d->dtree);
#endif
}

/* Inflate a block of data compressed with dynamic Huffman trees */
static int tinf_inflate_dynamic_block(struct tinf_data *d)
{
#ifdef T3P_TINF_PROFILE
    uint32_t start=t3p_tinf_clock();
    t3p_tinf_profile[TINF_P_DYNAMIC]++;
#endif
    int res = tinf_decode_trees(d, &d->ltree, &d->dtree);
#ifdef T3P_TINF_PROFILE
    t3p_tinf_charge(TINF_P_TREE,start);
#endif
    if (res != TINF_OK) return res;
#ifdef T3P_TINF_PROFILE
    start=t3p_tinf_clock();
    res=tinf_inflate_block_data(d, &d->ltree, &d->dtree);
    t3p_tinf_charge(TINF_P_PAYLOAD,start);
    return res;
#else
    return tinf_inflate_block_data(d, &d->ltree, &d->dtree);
#endif
}

/* -- Public functions -- */

/* Initialize global (static) data */
void tinf_init(void)
{
	return;
}

/* Inflate stream from source to dest */
int tinf_uncompress(void *dest, unsigned int *destLen,
                    const void *source, unsigned int sourceLen)
{
	struct tinf_data d;
	int bfinal;
#ifdef T3P_TINF_PROFILE
    t3p_tinf_profile[TINF_P_CALLS]++;
#endif

	/* Initialise data */
	d.source = (const unsigned char *) source;
	d.source_end = d.source + sourceLen;
	d.tag = 0;
	d.bitcount = 0;
	d.overflow = 0;

	d.dest = (unsigned char *) dest;
	d.dest_start = d.dest;
	d.dest_end = d.dest + *destLen;

	do {
		unsigned int btype;
		int res;

		/* Read final block flag */
		bfinal = tinf_getbits(&d, 1);

		/* Read block type (2 bits) */
		btype = tinf_getbits(&d, 2);

		/* Decompress block */
		switch (btype) {
		case 0:
            /* Stored blocks are separate from compressed payload work. */
#ifdef T3P_TINF_PROFILE
            {
                uint32_t start=t3p_tinf_clock();
                t3p_tinf_profile[TINF_P_STORED_BLOCKS]++;
                res=tinf_inflate_uncompressed_block(&d);
                t3p_tinf_charge(TINF_P_STORED,start);
            }
#else
            res = tinf_inflate_uncompressed_block(&d);
#endif
            break;
		case 1:
			/* Decompress block with fixed Huffman trees */
			res = tinf_inflate_fixed_block(&d);
			break;
		case 2:
			/* Decompress block with dynamic Huffman trees */
			res = tinf_inflate_dynamic_block(&d);
			break;
		default:
			res = TINF_DATA_ERROR;
			break;
		}

		if (res != TINF_OK) {
			return res;
		}
	} while (!bfinal);

	/* Check for overflow in bit reader */
	if (d.overflow) {
		return TINF_DATA_ERROR;
	}

	*destLen = d.dest - d.dest_start;

	return TINF_OK;
}

/* clang -g -O1 -fsanitize=fuzzer,address -DTINF_FUZZING tinflate.c */
#if defined(TINF_FUZZING)
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

unsigned char depacked[64 * 1024];

extern int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size > UINT_MAX / 2) { return 0; }
	unsigned int destLen = sizeof(depacked);
	tinf_uncompress(depacked, &destLen, data, size);
	return 0;
}
#endif
