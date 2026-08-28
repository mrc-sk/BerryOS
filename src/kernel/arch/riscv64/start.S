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
/* BerryOS riscv64 kernel entry stub (PLACEHOLDER).
 *
 * Real supervisor-mode setup (SATP/MMU, PLIC, UART) is TODO (M2).
 * For now this just spins via WFI, so the file assembles and the
 * multi-arch framework is in place. */
.section .text
.global _start
_start:
    /* Placeholder: set up SP and spin. */
    la   sp, 0x80000000 + 0x10000
1:
    wfi
    j 1b
