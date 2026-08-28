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
/* BerryOS x86_64 bootloader linker script.
 * Flat 512-byte image: VMA == LMA == 0, all content placed by boot.S's .org.
 * The binary is exactly 512 bytes (boot signature at offset 510). */
SECTIONS {
    . = 0;
    .text : {
        *(.text*)
        *(.rodata*)
        *(.data*)
    }
    /DISCARD/ : {
        *(.note*)
        *(.eh_frame*)
        *(.comment*)
        *(.bss*)
    }
}
