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
#include "berryos.h"
#include "pci.h"

/* =====================================================================
 * PCI configuration space, type 1 mechanism.
 *
 * Address port 0xCF8 carries {enable | bus | device | function | dword
 * offset}; the data port 0xCFC then reads/writes the selected dword.  Works
 * on every x86 PC BIOS / VM BIOS; MMCONFIG (ECAM) is a PCIe nicety we do not
 * need for discovering the handful of devices a VM presents.
 * =================================================================== */

#define PCI_ADDR_PORT 0xCF8
#define PCI_DATA_PORT 0xCFC

#define REG_VENDOR_DEVICE 0x00
#define REG_COMMAND       0x04
#define REG_CLASS         0x08
#define REG_HEADER_TYPE   0x0C
#define REG_BAR5          0x24

uint32_t pci_read(uint32_t bus, uint32_t dev, uint32_t func, uint32_t off){
    uint32_t addr = 0x80000000u
                  | ((bus  & 0xFF) << 16)
                  | ((dev  & 0x1F) << 11)
                  | ((func & 0x07) << 8)
                  | (off & 0xFC);
    outl(PCI_ADDR_PORT, addr);
    return inl(PCI_DATA_PORT);
}

void pci_write(uint32_t bus, uint32_t dev, uint32_t func, uint32_t off, uint32_t val){
    uint32_t addr = 0x80000000u
                  | ((bus  & 0xFF) << 16)
                  | ((dev  & 0x1F) << 11)
                  | ((func & 0x07) << 8)
                  | (off & 0xFC);
    outl(PCI_ADDR_PORT, addr);
    outl(PCI_DATA_PORT, val);
}

int pci_find_class(uint8_t class_code, uint8_t subclass, int prog_if,
                   uint32_t* bus_out, uint32_t* dev_out, uint32_t* func_out){
    uint32_t bus, dev, func;

    for (bus = 0; bus < 256; bus++){
        for (dev = 0; dev < 32; dev++){
            uint32_t hdr, nfunc;
            if ((pci_read(bus, dev, 0, REG_VENDOR_DEVICE) & 0xFFFF) == 0xFFFF)
                continue;                      /* no device in this slot      */
            hdr = pci_read(bus, dev, 0, REG_HEADER_TYPE);
            nfunc = (hdr & 0x00800000) ? 8 : 1;/* header-type bit7 = multifunction */
            for (func = 0; func < nfunc; func++){
                uint32_t class_reg;
                if (func && ((pci_read(bus, dev, func, REG_VENDOR_DEVICE) & 0xFFFF) == 0xFFFF))
                    continue;                  /* empty function              */
                class_reg = pci_read(bus, dev, func, REG_CLASS);
                if (((class_reg >> 24) & 0xFF) != class_code)  continue;
                if (((class_reg >> 16) & 0xFF) != subclass)    continue;
                if (prog_if >= 0 && (int)((class_reg >> 8) & 0xFF) != prog_if) continue;
                if (bus_out)  *bus_out  = bus;
                if (dev_out)  *dev_out  = dev;
                if (func_out) *func_out = func;
                return 1;
            }
        }
    }
    return 0;
}

int pci_find_ahci(uint32_t* abar_out){
    uint32_t bus, dev, func, bar5, cmd;
    if (!pci_find_class(0x01, 0x06, 0x01, &bus, &dev, &func))
        return 0;
    bar5 = pci_read(bus, dev, func, REG_BAR5);
    if ((bar5 & 0x00000001) || !bar5)      /* must be a 32-bit memory BAR   */
        return 0;
    /* The device must decode its register window and act as a bus master,
     * otherwise the HBA is unreachable and DMA cannot run. */
    cmd = pci_read(bus, dev, func, REG_COMMAND);
    pci_write(bus, dev, func, REG_COMMAND, cmd | 0x06);
    if (abar_out) *abar_out = bar5 & 0xFFFFFFF0u;
    return 1;
}
