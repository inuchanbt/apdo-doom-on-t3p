/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef T3P_SIGNAL_H
#define T3P_SIGNAL_H
void t3p_runtime_signal_entry(void);
void t3p_signal_failure(unsigned stage) __attribute__((noreturn));
#endif
