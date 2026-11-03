// SPDX-License-Identifier: GPL-2.0-or-later
// Projection algorithm derived from DOOM, Copyright (C) 1993-1996 Id Software, Inc.
// The vanilla 90-degree projection, rebuilt for a 240x200 world viewport.
// UI coordinates remain 320 pixels wide so every HUD field is retained.
#ifdef KM_SQUARE_VIEW
#include <stdint.h>
#include "doomdef.h"
#include "m_fixed.h"
#include "trig.h"

static uint16_t angle_to_x[4096];
uint16_t km_square_xtva[321];
int32_t km_square_yslope[200];

void km_square_projection(int width) {
    int center = width / 2;
    int32_t focal = FixedDiv(center << 16, dmc_finetangent(3072));
    for (unsigned i = 0; i < 4096; i++) {
        int32_t tangent = dmc_finetangent(i);
        int x = tangent > 131072 ? -1 : tangent < -131072 ? width + 1 :
            ((center << 16) - FixedMul(tangent, focal) + 65535) >> 16;
        if (x < -1) x = -1;
        if (x > width + 1) x = width + 1;
        // Store the two fenceposts without losing -1 in an unsigned table.
        angle_to_x[i] = x + 1;
    }
    for (int x = 0; x <= width; x++) {
        unsigned i = 0;
        while (i < 4095 && angle_to_x[i] > x + 1) i++;
        km_square_xtva[x] = i;
    }
    for (unsigned i = 0; i < 4096; i++) {
        int x = angle_to_x[i] - 1;
        angle_to_x[i] = x < 0 ? 0 : x > width ? width : x;
    }
    // Vertical projection uses physical pixels even in doubled-column mode.
    for (int y = 0; y < 200; y++) {
        int dy = (y - 100) * 65536 + 32768;
        if (dy < 0) dy = -dy;
        km_square_yslope[y] = FixedDiv(120 << 16, dy);
    }
}
int km_square_angle_to_x(unsigned angle) { return angle_to_x[angle]; }
#endif
