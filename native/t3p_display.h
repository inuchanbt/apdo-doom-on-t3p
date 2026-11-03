/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_DISPLAY_H
#define T3P_DISPLAY_H
#include <stdint.h>

#ifdef T3P_DBI_ASYNC
#define T3P_DISPLAY_COLUMNS 4u
#define T3P_DISPLAY_BUFFERS 2u
#else
#define T3P_DISPLAY_COLUMNS 8u
#define T3P_DISPLAY_BUFFERS 1u
#endif
#define T3P_DISPLAY_HEIGHT 240u

/* Controller command transport, supplied by a future board adapter. A command
 * and all its payload must complete synchronously before returning 0. The
 * adapter owns CS/DC/SPI/DMA and timeout handling; a queued DMA transfer is not
 * completion. Nonzero means failure. No callback may reenter this instance.
 * This layer does NOT initialize the panel, set MADCTL/RGB-BGR, or touch GPIO.
 */
struct t3p_display_bus {
    void *context;
    int (*command)(void *context, uint8_t command, const uint8_t *data, uint32_t size);
    /* Alternative to command: native RGB565 words, inclusive coordinates.
     * Matches the pixel/coordinate ABI established by the stock LCD probes.
     * Exactly one transport must be provided. The callback must finish using
     * the buffer before returning, including any DMA; this layer owns it.
     * In the native async build the rectangle callback may retain the submitted
     * buffer until the next submission returns or the board explicitly drains.
     * This is an adapter contract, not a stock driver initialization routine.
     */
    int (*rectangle)(void *context, uint16_t xs, uint16_t xe,
                     uint16_t ys, uint16_t ye, const uint16_t *pixels,
                     uint32_t pixel_count);
};
struct t3p_display_config {
    uint16_t width, height;         /* Game viewport, not controller RAM size. */
    uint16_t offset_x, offset_y;    /* Board-verified visible origin required. */
    uint16_t address_width, address_height;
};
struct t3p_display {
    struct t3p_display_bus bus;
    struct t3p_display_config config;
    uint32_t windows, pixel_bytes;
    uint16_t x, y, height;
    uint8_t count, ready, failed;
#ifdef T3P_DBI_ASYNC
    uint8_t buffer;
#endif
#ifdef T3P_DBI_DMA
    _Alignas(4)
#endif
    uint16_t pixels[T3P_DISPLAY_BUFFERS*T3P_DISPLAY_COLUMNS*T3P_DISPLAY_HEIGHT];
};
/* Returns 0 on success, -1 for invalid arguments, -2 after a transport error.
 * A transport error latches until reinitialization, preventing continued writes
 * after a partially accepted address window. Reinitializing does not repair
 * hardware: the board must recover the bus/panel, then request a full redraw.
 */
int t3p_display_init(struct t3p_display *display,
                     const struct t3p_display_bus *bus,
                     const struct t3p_display_config *config);
int t3p_display_column(struct t3p_display *display, int x, int y, int height,
                       const uint16_t *rgb565);
int t3p_display_flush(struct t3p_display *display);
#endif
