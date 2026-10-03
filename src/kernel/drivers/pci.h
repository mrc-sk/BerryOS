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
#ifndef BERRYOS_PCI_H
#define BERRYOS_PCI_H

#include "berryos.h"

/* PCI configuration space access (type 1: ports 0xCF8/0xCFC) plus the small
 * device search the storage stack needs.  This is deliberately the generic
 * floor, not an AHCI helper: USB xHCI, NVMe and anything else that hangs off
 * the PCI bus will reuse pci_read/pci_write and pci_find_class(). */

uint32_t pci_read(uint32_t bus, uint32_t dev, uint32_t func, uint32_t off);
void     pci_write(uint32_t bus, uint32_t dev, uint32_t func, uint32_t off, uint32_t val);

/* Return 1 and fill *bus/dev/func when a device with the given class code is
 * present anywhere on the bus, else 0.  prog_if < 0 matches any prog-if. */
int      pci_find_class(uint8_t class_code, uint8_t subclass, int prog_if,
                        uint32_t* bus, uint32_t* dev, uint32_t* func);

/* Convenience wrapper for the storage stack: locate an AHCI 1.0 controller
 * (class 0x01 / subclass 0x06 / prog-if 0x01), enable bus mastering + memory
 * space decoding on it and return its BAR5 (the AHCI ABAR). */
int      pci_find_ahci(uint32_t* abar_out);

#endif /* BERRYOS_PCI_H */
