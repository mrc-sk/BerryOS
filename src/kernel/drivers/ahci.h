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
#ifndef BERRYOS_AHCI_H
#define BERRYOS_AHCI_H

#include "berryos.h"

/* SATA AHCI driver.  Implements the same sector API as the legacy ATA PIO
 * driver so ata.c can transparently prefer it whenever a PCI AHCI controller
 * is present (VMware / VirtualBox SATA / ICH9-q35 / most modern boards).
 *
 * Interrupts are deliberately not used: polling PxCI keeps the driver free of
 * IDT/PIC coupling, which matters while the rest of the kernel is still
 * single-cpu and the desktop owns the screen. */

int  ahci_init(char* model, int model_len);
int  ahci_read_sectors(uint32_t lba, uint32_t count, void* buf);
int  ahci_write_sectors(uint32_t lba, uint32_t count, const void* buf);

#endif /* BERRYOS_AHCI_H */
