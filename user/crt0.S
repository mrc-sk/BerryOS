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
/* BerryOS user-mode entry (M2 libc).
 * The kernel drops us into ring 3 with rsp already pointing at the top of
 * the user stack (high address, growing down).  We call main() and feed its
 * return value to sys_exit(). */
.section .text
.globl _start
_start:
    movq $0, %rdi        /* argc */
    movq $0, %rsi        /* argv */
    call main
    movq %rax, %rdi      /* exit code = main() return value */
    movq $2, %rax        /* SYS_EXIT */
    int  $0x80
    /* unreachable */
1:  hlt
    jmp  1b
