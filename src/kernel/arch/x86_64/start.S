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

    /* ---- Copy the E820 memory map out of low memory --------------------
     * The real-mode probe (boot.S on the ISO path, mbr.S on the raw-disk
     * path) stashes it at 0x6800: u16 count, then `count` x 24-byte entries.
     *
     * Two constraints shape this:
     *  - it must happen here, not in kmain(): the boot page tables still
     *    identity-map low memory, and kmain() installs page tables of its own
     *    that are not required to keep 0x6800 reachable;
     *  - it must happen BEFORE the framebuffer arguments are loaded below.
     *    The copy uses rax/rcx/rsi/rdi, and rdi/rsi/rcx are exactly the
     *    registers kmain() takes fb_phys/width/pitch in -- clobbering them
     *    hands kmain a garbage framebuffer and the desktop faults on the
     *    first fill.  (Getting this backwards produces a page fault in
     *    fb_fill_rect with an absurd width and pitch=0.) */
    movl 0x6800, %ecx
    andl $0x1F, %ecx                 /* clamp to E820_MAX (32) */
    movl %ecx, g_e820_count
    testl %ecx, %ecx
    jz   e820_copy_done
    movq $0x6804, %rsi               /* source: first entry */
    movq $g_e820, %rdi               /* destination: BSS array */
e820_copy:
    movq (%rsi), %rax
    movq %rax, (%rdi)
    movq 8(%rsi), %rax
    movq %rax, 8(%rdi)
    movq 16(%rsi), %rax
    movq %rax, 16(%rdi)
    addq $24, %rsi
    addq $24, %rdi
    decl %ecx
    jnz  e820_copy
e820_copy_done:

    /* Load framebuffer boot-info (written by boot.S in real mode) into
     * registers for kmain.  The boot page tables are still active here, so low
     * memory is identity-mapped and 0x6000 is directly readable.  Fields are
     * stored as zero-extended qwords: {phys, width, height, pitch, bpp}.
     * Keep this last: these registers are kmain()'s arguments. */
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
