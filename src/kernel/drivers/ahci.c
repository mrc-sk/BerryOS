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
#include "ahci.h"
#include "pci.h"

/* =====================================================================
 * AHCI (Serial ATA Advanced Host Controller Interface) driver.
 *
 * Why this exists: the old ATA driver only speaks legacy IDE I/O ports
 * (0x1F0), which QEMU's default IDE disk happens to answer.  VirtualBox
 * templates and VMware default to AHCI/SATA controllers (and the q35
 * machine type uses ICH9 AHCI), where there is simply no 0x1F0 to talk
 * to -- the disk is reachable only as a memory-mapped HBA register set
 * plus DMA.  Without this driver those VMs boot to a desktop with no
 * BerryFS at all.
 *
 * Scope: one port, polling, 48-bit LBA READ/WRITE DMA EXT + IDENTIFY.
 * Enough for BerryFS; NCQ, ATAPI, port multipliers etc. are out of scope.
 *
 * DMA coherency: the command list, FIS buffer and sector bounce buffer
 * live in one physical page mapped uncached (PTE_PCD|PTE_PWT), so CPU
 * writes go straight to memory and HBA reads see them; conversely HBA
 * writes land in memory and CPU reads never hit a stale cache line.
 * This matters on real hardware, where PCI DMA does not snoop the CPU
 * cache; the hypervisors we test under keep things coherent anyway.
 * =================================================================== */

/* High-half windows for the HBA register block and our DMA arena.  Chosen
 * far away from FB_VADDR (0xFFFF900000000000) and from each other. */
#define HBA_VADDR  0xFFFF900010000000ULL
#define DMA_VADDR  0xFFFF900010001000ULL
#define HBA_MAP_SIZE 0x2000              /* regs + up to 32 port blocks      */
#define DMA_MAP_SIZE PAGE_SIZE           /* one page holds everything below  */

/* ---- HBA global registers ------------------------------------------- */
#define HBA_CAP  0x00
#define HBA_GHC  0x04
#define HBA_PI   0x0C

/* ---- per-port registers (base = 0x100 + port*0x80) ------------------- */
#define P_REG(n, off) (*(volatile uint32_t*)(HBA_VADDR + 0x100 + (uint64_t)(n) * 0x80 + (off)))
#define P_CLB   0x00   /* command list base (lo/hi)                        */
#define P_CLBU  0x04
#define P_FB    0x08   /* FIS base (lo/hi)                                 */
#define P_FBU   0x0C
#define P_IS    0x10   /* interrupt status (W1C)                           */
#define P_IE    0x14   /* interrupt enable                                 */
#define P_CMD   0x18   /* command / status                                 */
#define P_TFD   0x20   /* task file data                                   */
#define P_SIG   0x24   /* signature (0x00000101 = SATA ATA device)         */
#define P_SSTS  0x28   /* SATA status: DET in bits 3:0                     */
#define P_SCTL  0x2C   /* SATA control                                     */
#define P_SERR  0x30   /* SATA error (W1C)                                 */
#define P_CI    0x38   /* command issue (non-NCQ): one bit per slot        */

/* PxCMD bits */
#define CMD_ST   (1u << 0)   /* start command list processing              */
#define CMD_SUD  (1u << 1)   /* spin-up device                             */
#define CMD_POD  (1u << 2)   /* power on device                            */
#define CMD_FRE  (1u << 4)   /* FIS receive enable                         */
#define CMD_CR   (1u << 8)   /* command list running (RO)                  */
#define CMD_FR   (1u << 14)  /* FIS receive running (RO)                   */

/* ---- DMA arena layout inside the single allocated page ---------------
 * Every offset satisfies its hardware alignment requirement:
 *   CLB  must be 1024-byte aligned -> 0x000
 *   FB   must be 256-byte aligned  -> 0x400
 *   CT   must be 128-byte aligned  -> 0x600
 *   BUF  word aligned              -> 0x800 (fits 4 sectors; we use 1)     */
#define DMA_CLB  0x000
#define DMA_FB   0x400
#define DMA_CT   0x600
#define DMA_BUF  0x800

/* ---- ATA commands ----------------------------------------------------- */
#define ATAPI_CMD_IDENTIFY     0xEC
#define ATAPI_CMD_READ_DMA_EXT  0x25
#define ATAPI_CMD_WRITE_DMA_EXT 0x35

#define FIS_TYPE_H2D 0x27

static int      g_port = -1;        /* AHCI port backing the disk, or -1   */
static uint64_t g_dma_phys;         /* physical base of the DMA arena      */
static uint32_t g_sectors;          /* total sectors reported by IDENTIFY  */
static char     g_model[41];

/* Wait until a port status bit (masked) equals `want`.  Bounded spin so a
 * dead controller can never hang the boot. */
static int port_wait(int port, uint32_t off, uint32_t mask, uint32_t want, int iters){
    int t;
    for (t = 0; t < iters; t++){
        if (((P_REG(port, off) & mask) != 0) == (want != 0)) return 0;
    }
    return -1;
}

/* Bring one port from idle to "command engine running, device present". */
static int port_start(int p){
    uint32_t cmd;

    /* Stop the command engine, then FIS reception (spec order). */
    cmd = P_REG(p, P_CMD);
    if (cmd & CMD_ST) P_REG(p, P_CMD) = cmd & ~CMD_ST;
    if (port_wait(p, P_CMD, CMD_CR, 0, 1000000)) return 0;
    cmd = P_REG(p, P_CMD);
    if (cmd & CMD_FRE) P_REG(p, P_CMD) = cmd & ~CMD_FRE;
    if (port_wait(p, P_CMD, CMD_FR, 0, 1000000)) return 0;

    /* Point the port at our (uncached) DMA structures and clear leftovers. */
    P_REG(p, P_CLB)  = (uint32_t)(g_dma_phys + DMA_CLB);
    P_REG(p, P_CLBU) = 0;
    P_REG(p, P_FB)   = (uint32_t)(g_dma_phys + DMA_FB);
    P_REG(p, P_FBU)  = 0;
    P_REG(p, P_IS)   = 0xFFFFFFFFu;     /* W1C: clear every pending bit    */
    P_REG(p, P_IE)   = 0;               /* polling only, no interrupts     */
    P_REG(p, P_SERR) = 0xFFFFFFFFu;     /* W1C: clear stale errors         */

    /* Spin up, power on, enable FIS receive and start the command engine --
     * in a SINGLE write.  Some HBAs (QEMU among them) act on the bits as
     * presented in one write; a read-modify-write sequence can leave the
     * command list engine off even though every individual bit reads back
     * as set. */
    P_REG(p, P_CMD) = P_REG(p, P_CMD) | CMD_SUD | CMD_POD | CMD_FRE | CMD_ST;

    /* Device must have negotiated a link (DET == 3). */
    return port_wait(p, P_SSTS, 0x0F, 3, 1000000) == 0;
}

/* Run one ATA command through command slot 0.  For reads the HBA DMAs into
 * the bounce buffer, which is then copied out to `buf`; for writes `buf` is
 * staged into the bounce buffer first.  Only ONE sector is transferred per
 * call (the BerryFS layer reads/writes one sector at a time). */
static int ahci_exec(uint8_t cmd, uint64_t lba, int is_write, void* buf){
    volatile uint8_t* clb  = (volatile uint8_t*)(DMA_VADDR + DMA_CLB);
    volatile uint8_t* ct   = (volatile uint8_t*)(DMA_VADDR + DMA_CT);
    volatile uint8_t* prdt = ct + 0x80;
    int p = g_port, t;

    if (is_write) memcpy((void*)(DMA_VADDR + DMA_BUF), buf, 512);
    memset((void*)ct, 0, 0x90);

    /* Register Host-to-Device FIS (20 bytes = 5 dwords).  Byte 1 is NOT
     * the command: [7]=C, [6:4]=reserved, [3:0]=port-multiplier port.
     * Putting the command there (e.g. 0x80|0xEC = 0xEC) makes bit 3:0 read
     * as PMP=12, and HBAs that validate the FIS (QEMU does) drop the command
     * silently -- CI never clears and no error interrupt is raised. */
    ct[0]  = FIS_TYPE_H2D;
    ct[1]  = 0x80;                      /* C=1, PMP=0: update command reg  */
    ct[2]  = cmd;                       /* command                         */
    ct[3]  = 0;                         /* features                        */
    ct[4]  = (uint8_t)(lba);            /* LBA 7:0                         */
    ct[5]  = (uint8_t)(lba >> 8);       /* LBA 15:8                        */
    ct[6]  = (uint8_t)(lba >> 16);      /* LBA 23:16                       */
    ct[7]  = 0x40;                      /* device: LBA mode                */
    ct[8]  = (uint8_t)(lba >> 24);      /* LBA 31:24                       */
    ct[9]  = (uint8_t)(lba >> 32);      /* LBA 39:32                       */
    ct[10] = (uint8_t)(lba >> 40);      /* LBA 47:40                       */
    ct[11] = 0;                         /* features (exp)                  */
    ct[12] = 1;                         /* sector count lo                 */
    ct[13] = 0;                         /* sector count hi                 */
    ct[14] = 0;                         /* ICC                             */
    ct[15] = 0;                         /* control                         */
    ct[16] = 0; ct[17] = 0; ct[18] = 0; ct[19] = 0;

    /* One PRDT entry describing the 512-byte bounce buffer. */
    *(volatile uint32_t*)(prdt + 0x0) = (uint32_t)(g_dma_phys + DMA_BUF);
    *(volatile uint32_t*)(prdt + 0x4) = 0;
    *(volatile uint32_t*)(prdt + 0x8) = 0;
    *(volatile uint32_t*)(prdt + 0xC) = (512 - 1) | (1u << 31);  /* +IOC   */

    /* Command header (32 bytes):
     *   DW0 [15:0]  flags: CFL=5, W for writes
     *   DW0 [31:16] PRDTL = 1 descriptor        <- bytes 2..3, NOT 4..5
     *   DW1         reserved (byte count legacy)
     *   DW2/DW3     command table base
     * The first cut put PRDTL at byte 4 (DW1); the HBA then sees PRDTL=0,
     * runs the command, has nowhere to DMA the data, and "succeeds" with an
     * empty buffer. */
    clb[0] = 5 | (is_write ? (1u << 6) : 0);
    clb[1] = 0;
    clb[2] = 1;                         /* PRDTL = 1 (DW0 bits 31:16)      */
    clb[3] = 0;
    clb[4] = 0; clb[5] = 0; clb[6] = 0; clb[7] = 0;
    *(volatile uint32_t*)(clb + 0x8) = (uint32_t)(g_dma_phys + DMA_CT);
    *(volatile uint32_t*)(clb + 0xC) = 0;

    /* Wait for the task file to go idle, then fire slot 0. */
    if (port_wait(p, P_TFD, 0x88, 0, 1000000)) return -1;   /* BSY|DRQ    */
    P_REG(p, P_IS) = 0xFFFFFFFFu;
    P_REG(p, P_CI) = 1;

    /* Poll: TFES (IS bit30) means the command failed; PxCI bit0 clearing
     * means the HBA took the command off the list (completion). */
    for (t = 0; t < 5000000; t++){
        if (P_REG(p, P_IS) & (1u << 30)) return -1;
        if (!(P_REG(p, P_CI) & 1)) break;
    }
    if (t >= 5000000) return -1;
    if (P_REG(p, P_TFD) & 0x01) return -1;               /* ERR in TFD     */

    if (!is_write) memcpy(buf, (void*)(DMA_VADDR + DMA_BUF), 512);
    return 0;
}

int ahci_init(char* model, int model_len){
    uint32_t abar, pi;
    int p, i;

    if (!pci_find_ahci(&abar)) return 0;
    if (!abar) return 0;

    map_region(HBA_VADDR, abar, HBA_MAP_SIZE, (int)(PTE_P | PTE_W | PTE_PCD | PTE_PWT));

    /* Global AHCI enable, then find an implemented port with a SATA disk. */
    *(volatile uint32_t*)(HBA_VADDR + HBA_GHC) |= (1u << 31);
    pi = *(volatile uint32_t*)(HBA_VADDR + HBA_PI);
    if (!pi) return 0;

    g_dma_phys = pmm_alloc_page();
    if (!g_dma_phys) return 0;
    map_region(DMA_VADDR, g_dma_phys, DMA_MAP_SIZE, (int)(PTE_P | PTE_W | PTE_PCD | PTE_PWT));
    memset((void*)DMA_VADDR, 0, PAGE_SIZE);

    for (p = 0; p < 32; p++){
        if (!(pi & (1u << p))) continue;
        if ((P_REG(p, P_SSTS) & 0x0F) != 3) continue;     /* link up       */
        if (P_REG(p, P_SIG) != 0x00000101u) continue;     /* SATA ATA only */
        if (!port_start(p)) continue;
        g_port = p;
        break;
    }
    if (g_port < 0) return 0;

    /* IDENTIFY DEVICE -> sector count + model string. */
    {
        uint16_t* id = (uint16_t*)(DMA_VADDR + DMA_BUF);
        memset(id, 0, 512);
        if (ahci_exec(ATAPI_CMD_IDENTIFY, 0, 0, id)) return 0;
        g_sectors = (uint32_t)id[100] | ((uint32_t)id[101] << 16);
        if (!g_sectors)
            g_sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
        for (i = 0; i < 20; i++){
            uint16_t w = id[27 + i];
            g_model[2 * i]     = (char)(w >> 8);
            g_model[2 * i + 1] = (char)(w & 0xFF);
        }
        g_model[40] = 0;
        for (i = 39; i >= 0 && g_model[i] == ' '; i--) g_model[i] = 0;
        if (!g_sectors) return 0;
    }

    if (model && model_len > 0){
        for (i = 0; i < model_len - 1 && g_model[i]; i++) model[i] = g_model[i];
        model[i] = 0;
    }

    serial_puts("[AHCI] port ");
    serial_hex((uint64_t)g_port);
    serial_puts(" disk '");
    serial_puts(g_model);
    serial_puts("' sectors=");
    serial_hex((uint64_t)g_sectors);
    serial_puts("\r\n");
    return 1;
}

int ahci_read_sectors(uint32_t lba, uint32_t count, void* buf){
    uint32_t i;
    uint8_t* out = (uint8_t*)buf;
    if (g_port < 0 || count == 0) return -1;
    for (i = 0; i < count; i++)
        if (ahci_exec(ATAPI_CMD_READ_DMA_EXT, (uint64_t)lba + i, 0, out + i * 512))
            return -1;
    return (int)count;
}

int ahci_write_sectors(uint32_t lba, uint32_t count, const void* buf){
    uint32_t i;
    const uint8_t* in = (const uint8_t*)buf;
    if (g_port < 0 || count == 0) return -1;
    for (i = 0; i < count; i++)
        if (ahci_exec(ATAPI_CMD_WRITE_DMA_EXT, (uint64_t)lba + i, 1, (void*)(in + i * 512)))
            return -1;
    return (int)count;
}
