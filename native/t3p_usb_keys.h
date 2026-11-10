/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_USB_KEYS_H
#define T3P_USB_KEYS_H
#include <stdint.h>

struct t3p_bsp_io;
void t3p_usb_keys_start(const struct t3p_bsp_io *io, uint32_t now);
uint32_t t3p_usb_keys_debug(unsigned row);
void t3p_usb_keys_poll(uint32_t now);
uint32_t t3p_usb_keys_take(uint32_t now);
#ifdef T3P_USB_CAPTURE
void t3p_usb_capture_frame(const uint8_t *pixels, const uint16_t *palette);
#endif
#endif
