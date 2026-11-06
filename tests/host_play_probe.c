/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host-only observations for the gameplay and first-person rendering checks. */
#include "doomstat.h"

__declspec(dllexport) int km_host_pistol_ammo(void)
{
    return players[consoleplayer].ammo[am_clip];
}

__declspec(dllexport) unsigned km_host_weapon_flags(void)
{
    const player_t *player = &players[consoleplayer];
    return (player->readyweapon == wp_fist)
         | ((player->psprites[ps_flash].state != 0) << 1)
         | ((player->psprites[ps_weapon].state == &dmc_states[S_PUNCH2]) << 2);
}

__declspec(dllexport) void km_host_redraw_without_psprites(void)
{
    extern void km_host_redraw(void);
    pspdef_t *sprites = players[consoleplayer].psprites;
    const state_t *states[NUMPSPRITES];
    for (unsigned i = 0; i < NUMPSPRITES; ++i) {
        states[i] = sprites[i].state;
        sprites[i].state = 0;
    }
    km_host_redraw();
    for (unsigned i = 0; i < NUMPSPRITES; ++i)
        sprites[i].state = states[i];
}
