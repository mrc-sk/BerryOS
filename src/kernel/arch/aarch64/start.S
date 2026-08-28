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
/* BerryOS aarch64 kernel entry stub (PLACEHOLDER).
 *
 * Real firmware handoff (UEFI/Device Tree) and MMU setup are TODO (M2).
 * For now this just spins, so the file assembles and the multi-arch
 * framework is in place. */
.section .text
.global _start
_start:
    /* Placeholder: set up SP and spin. Real kernel brings up MMU + UART. */
    ldr x1, =0x40000000 + 0x10000
    mov sp, x1
1:
    wfe
    b 1b
