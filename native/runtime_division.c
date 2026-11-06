/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>

/* Signed 64-bit division ABI for the archive-free pi32v2 release link. */
int64_t __divdi3(int64_t numerator, int64_t denominator)
{
    uint64_t dividend = (uint64_t)numerator;
    uint64_t divisor = (uint64_t)denominator;
    uint64_t quotient = 0;
    uint64_t bit = 1;

    if (numerator < 0) dividend = 0 - dividend;
    if (denominator < 0) divisor = 0 - divisor;
    if (divisor == 0) __builtin_trap();

    /* Align to the highest usable bit without overflowing the divisor. */
    while (divisor <= (dividend >> 1)) {
        divisor <<= 1;
        bit <<= 1;
    }
    while (bit != 0) {
        if (dividend >= divisor) {
            dividend -= divisor;
            quotient |= bit;
        }
        divisor >>= 1;
        bit >>= 1;
    }
    if ((numerator < 0) != (denominator < 0)) quotient = 0 - quotient;
    return (int64_t)quotient;
}
