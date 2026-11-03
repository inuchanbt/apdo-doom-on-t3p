/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native entry. Preserve loader registers before any C call. The first
 * helper uses only stack and explicit MMIO, before initialized RAM exists. */
.section .runtime_entry,"ax",@progbits
.global t3p_runtime_start
t3p_runtime_start:
    cli r3
    sp = __runtime_stack_end
    ssp = __runtime_stack_end
    r4 = t3p_boot_parameters
    [r4+0] = r0
    [r4+4] = r1
    [r4+8] = r2
    [r4+12] = r3
    call t3p_runtime_signal_entry
    call t3p_runtime_capture_boot
    call t3p_runtime_memory_init
    call t3p_board_start
.Lreturned:
    goto .Lreturned
