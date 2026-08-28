/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Copyright (C) mrc-sk and imjumping
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See LICENSE for the full license text and the additional
 * non-commercial restriction terms that apply to this software.
 */
/* BerryOS kernel entry (x86_64 long mode).
 * Called by the self-written bootloader after paging is enabled.
 * Selectors are already set: CS=0x18 (code64), DS/ES/SS=0x20 (data64). */
.section .text
.global _start
.type _start, @function
_start:
    cli
    movq $0x200000, %rsp        /* temporary kernel stack (identity-mapped) */

    /* Zero BSS */
    movq $__bss_start, %rdi
    movq $__bss_end, %rcx
    subq %rdi, %rcx
    shrq $3, %rcx
    xorq %rax, %rax
    rep stosq

    /* Load VBE boot-info (written by boot.S in real mode) into registers for
     * kmain.  The boot page tables are still active here, so low memory is
     * identity-mapped and 0x6000 is directly readable.  Fields are stored as
     * zero-extended qwords: {phys, width, height, pitch, bpp}. */
    movq 0x6000, %rdi
    movq 0x6008, %rsi
    movq 0x6010, %rdx
    movq 0x6018, %rcx
    movq 0x6020, %r8

    call kmain

1:
    hlt
    jmp 1b
.size _start, . - _start
