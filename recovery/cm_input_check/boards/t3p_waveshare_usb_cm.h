/* SPDX-License-Identifier: MIT */
/* Custom board header: no Pico 2 LED, SMPS, VBUS or VSYS GPIO assumptions. */
#ifndef T3P_WAVESHARE_USB_CM_H
#define T3P_WAVESHARE_USB_CM_H
pico_board_cmake_set(PICO_PLATFORM, rp2350)
#define PICO_RP2350A 1
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#define PICO_FLASH_SPI_CLKDIV 4
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#define PICO_RP2350_A2_SUPPORTED 1
#endif
