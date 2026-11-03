/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_BOOT_H
#define T3P_BOOT_H
#include <stdint.h>
#define T3P_BOOT_MAGIC 0x54334254u
struct t3p_boot_snapshot {
    uint32_t context[6];
    uint8_t auxiliary[32];
    uint32_t valid;
};
/* Reads are constrained to ordinary main SRAM. Never dereference untrusted
 * boot addresses as MMIO or Flash. The native entry supplies r0 saved above.
 */
typedef int (*t3p_boot_read)(void *,uint32_t,void *,uint32_t);
int t3p_boot_capture(struct t3p_boot_snapshot *,uint32_t,t3p_boot_read,void *);
extern uint32_t t3p_boot_parameters[4];
extern struct t3p_boot_snapshot t3p_boot_snapshot;
void t3p_runtime_capture_boot(void);
#endif
