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
/* User program link script (M1 stage-4).
 * Force every section to start at 0x400000 so the ELF program headers
 * produce a single PT_LOAD at 0x400000.  Using plain `-Ttext=0x400000`
 * leaves the ELF header/phdr in a LOAD segment at the default address
 * (0x200000 on this toolchain), which overlaps the kernel's page tables
 * when the kernel ELF loader copies PT_LOAD payloads into place. */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) }

    /* Drop unwind/debug/comment sections so they do not create stray
     * LOAD segments that would bloat the page range or confuse the loader. */
    /DISCARD/ : { *(.eh_frame) *(.eh_frame_hdr) *(.note*) *(.comment) *(.debug*) }
}
