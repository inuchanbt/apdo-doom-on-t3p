/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include "../recovery/cm_wave_scope/wave_plan.h"
int main(void)
{
    uint32_t words[WAVE_WORDS]; wave_plan(words);
    for (unsigned i=0;i<WAVE_WORDS;i++) printf("%08x\n",(unsigned)words[i]);
    return 0;
}
