#include <assert.h>
#include <stdint.h>
#include <stdio.h>
void t3p_runtime_zero_words(volatile uint32_t *,uint32_t);
void t3p_runtime_copy_words(volatile uint32_t *,const volatile uint32_t *,uint32_t);
static uint32_t src[1030],dst[1030];
int main(void)
{
    for(unsigned n=0;n<=1024;n++) {
        for(unsigned i=0;i<1030;i++){src[i]=i*0x10001u+0x5a17u;dst[i]=0xdeadbeef;}
        t3p_runtime_copy_words(dst+3,src+3,n);
        for(unsigned i=0;i<1030;i++)assert(dst[i]==(i>=3 && i<3+n ? src[i] : 0xdeadbeef));
        t3p_runtime_zero_words(dst+3,n);
        for(unsigned i=0;i<1030;i++)assert(dst[i]==(i>=3 && i<3+n ? 0u : 0xdeadbeef));
    }
    puts("PASS: startup word copy/zero, zero-length through1024 words, adjacent guards");
}
