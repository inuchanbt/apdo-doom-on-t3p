/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { bool armed,used; uint64_t deadline,low_since; } attach_gate_t;
enum { ATTACH_WAIT,ATTACH_FIRE,ATTACH_EXPIRED };
enum { ATTACH_TIMEOUT_S=600 };
static inline bool attach_arm(attach_gate_t *g,bool dp,bool dm,uint64_t now)
{
    if (g->used || g->armed || !dp || !dm) return false;
    g->armed=true; g->deadline=now+(uint64_t)ATTACH_TIMEOUT_S*1000000; g->low_since=0; return true;
}
static inline unsigned attach_poll(attach_gate_t *g,bool dp,bool dm,uint64_t now)
{
    if (!g->armed) return ATTACH_WAIT;
    if (now>=g->deadline) { g->armed=false; return ATTACH_EXPIRED; }
    if (dp || dm) { g->low_since=0; return ATTACH_WAIT; }
    if (!g->low_since) { g->low_since=now; return ATTACH_WAIT; }
    if (now-g->low_since<100) return ATTACH_WAIT;
    g->armed=false; g->used=true; return ATTACH_FIRE;
}
