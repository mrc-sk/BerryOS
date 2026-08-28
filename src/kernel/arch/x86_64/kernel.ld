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
/* BerryOS x86_64 kernel linker script.
 * VMA starts at 0x100000 (where the bootloader copies the raw image).
 * LMA is kept as (VMA - 0x100000) so the raw binary has no holes and every
 * section's file offset matches its in-memory offset. This guarantees that
 * section VMA alignment (e.g. 16-byte .rodata) does not desync LMA. */
ENTRY(_start)

SECTIONS {
    . = 0x100000;

    .text : AT(0) {
        *(.text*)
    }

    . = ALIGN(16);
    .rodata : AT(. - 0x100000) {
        *(.rodata*)
    }

    . = ALIGN(16);
    .data : AT(. - 0x100000) {
        *(.data*)
    }

    . = ALIGN(16);
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(4096);
    __kernel_end = .;
}
