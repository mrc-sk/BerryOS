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
#include "ata.h"
#include "ahci.h"

/* =====================================================================
 * ATA/IDE PIO driver (M4 framework).
 *
 * Talks to the primary IDE bus directly (no BIOS, no DMA).  Works in long
 * mode because the controller lives at fixed I/O ports and the kernel
 * already enables port I/O.  A bounded poll loop (no infinite spin) makes
 * the probe safe even if no disk is attached.
 *
 * Day-2 work (BerryFS) will sit on top of ata_read/write_sectors using a
 * reserved region of the disk image that mkimage.py does not overwrite.
 * =================================================================== */

#define ATA_DATA    0x1F0
#define ATA_SECCNT  0x1F2
#define ATA_LBA0    0x1F3
#define ATA_LBA1    0x1F4
#define ATA_LBA2    0x1F5
#define ATA_DRVHD   0x1F6
#define ATA_STATUS  0x1F7   /* same port, write = command */
#define ATA_CMD     ATA_STATUS

#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define CMD_IDENTIFY 0xEC
#define CMD_READ     0x20
#define CMD_WRITE    0x30
#define CMD_FLUSH    0xE7

static int   g_ata_ok = 0;
static int   g_use_ahci = 0;   /* 1 = sectors go through the AHCI DMA driver */
static char  g_model[ATA_MODEL_LEN];

/* Wait until BSY clears. Returns 0 on success, -1 on timeout. */
static int ata_poll_bsy(void){
    int t;
    for (t = 0; t < 2000000; t++){
        if (!(inb(ATA_STATUS) & ATA_SR_BSY)) return 0;
    }
    return -1;
}

/* Wait until BSY clear and DRQ set (data ready). -1 on timeout or ERR. */
static int ata_poll_drq(void){
    int t;
    for (t = 0; t < 2000000; t++){
        uint8_t s = inb(ATA_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

int ata_present(void){ return g_ata_ok; }

const char* ata_model(void){ return g_model; }

void ata_init(void){
    uint16_t id[256];
    int i;

    /* Prefer AHCI: VMware / VirtualBox-SATA / ICH9-q35 present the boot disk
     * behind a PCI AHCI controller that legacy port 0x1F0 cannot reach.  Only
     * when no usable AHCI disk turns up do we fall back to PIO on the primary
     * IDE bus (QEMU's default i440fx IDE disk, old boards). */
    if (ahci_init(g_model, ATA_MODEL_LEN)){
        g_use_ahci = 1;
        g_ata_ok   = 1;
        return;
    }

    /* Select primary master (LBA mode). */
    outb(ATA_DRVHD, 0xE0);
    io_wait();
    outb(ATA_SECCNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_CMD, CMD_IDENTIFY);

    if (ata_poll_bsy())  { g_ata_ok = 0; return; }
    if (ata_poll_drq())  { g_ata_ok = 0; return; }

    for (i = 0; i < 256; i++) id[i] = inw(ATA_DATA);

    /* Model string: words 27..46, byte-swapped little-endian words. */
    for (i = 0; i < 20; i++){
        uint16_t w = id[27 + i];
        g_model[2 * i]     = (char)(w >> 8);
        g_model[2 * i + 1] = (char)(w & 0xFF);
    }
    g_model[40] = 0;
    /* trim trailing spaces */
    for (i = 39; i >= 0 && g_model[i] == ' '; i--) g_model[i] = 0;

    g_ata_ok = 1;
}

int ata_read_sectors(uint32_t lba, uint32_t count, void* buf){
    uint16_t* p = (uint16_t*)buf;
    uint32_t i;
    if (!g_ata_ok || count == 0) return -1;
    if (count > 256) count = 256;
    if (g_use_ahci) return ahci_read_sectors(lba, count, buf);

    outb(ATA_DRVHD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECCNT, (uint8_t)(count == 256 ? 0 : count));
    outb(ATA_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_CMD, CMD_READ);

    for (i = 0; i < count; i++){
        if (ata_poll_bsy()) return -1;
        if (ata_poll_drq()) return -1;
        for (int j = 0; j < 256; j++) *p++ = inw(ATA_DATA);
    }
    return (int)count;
}

int ata_write_sectors(uint32_t lba, uint32_t count, const void* buf){
    const uint16_t* p = (const uint16_t*)buf;
    uint32_t i;
    if (!g_ata_ok || count == 0) return -1;
    if (count > 256) count = 256;
    if (g_use_ahci) return ahci_write_sectors(lba, count, buf);

    outb(ATA_DRVHD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECCNT, (uint8_t)(count == 256 ? 0 : count));
    outb(ATA_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_CMD, CMD_WRITE);

    for (i = 0; i < count; i++){
        if (ata_poll_bsy()) return -1;
        if (ata_poll_drq()) return -1;
        for (int j = 0; j < 256; j++) outw(ATA_DATA, *p++);
    }
    outb(ATA_CMD, CMD_FLUSH);
    ata_poll_bsy();
    return (int)count;
}
