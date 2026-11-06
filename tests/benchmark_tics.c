/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the real engine scheduler used by the paired device benchmarks. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_loop.h"

int consoleplayer;
unsigned char playeringame[MAXPLAYERS];
boolean advancedemo;
extern void abort(void) __attribute__((noreturn));
static unsigned builds,runs,events,menus;
static int now_ms;
void I_StartTic(void) {}
void D_ProcessEvents(void) { events++; }
void M_Ticker(void) { menus++; }
void G_BuildTiccmd(ticcmd_t *cmd,int tic)
{
    (void)cmd;
    assert(tic==(int)builds);
    builds++;
}
void G_Ticker(void) { runs++; }
void D_DoAdvanceDemo(void) { abort(); }
int I_GetTimeMS(void) { return now_ms; }
int I_GetTime(void) { return now_ms*35/1000; }
void I_Sleep(int ms) { (void)ms;abort(); }
void I_Error(char *format,...) { (void)format;abort(); }

int main(void)
{
    singletics=1;
    D_CheckNetGame();D_StartGameLoop();D_ResetTics();
    for(unsigned frame=0;frame<40;frame++) {
        /* Include very short and very long render intervals. The scheduler
         * still must build and execute exactly one tic for every frame. */
        now_ms+=(frame%3==0) ? 1 : (frame%3==1) ? 750 : 1900;
        TryRunTics();
        assert(gametic==(int)frame+1);
        assert(builds==frame+1 && runs==frame+1);
        assert(events==frame+1 && menus==frame+1);
    }
    printf("PASS: real engine scheduler advances exactly 40 tics in 40 benchmark frames\n");
    return 0;
}
