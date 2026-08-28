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

#define GDT_ENTRIES 7   /* 0..4 segments, 5..6 = 16-byte TSS descriptor */

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_lim_high;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* x86_64 TSS (104 bytes, packed) */
struct tss {
    uint32_t reserved0;
    uint64_t rsp[3];        /* rsp0 / rsp1 / rsp2 */
    uint64_t reserved1;
    uint64_t ist[7];
    uint32_t reserved2;
    uint32_t iomap_base;
} __attribute__((packed));

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr   gdt_p;
static struct tss       tss;

/* Segment selectors */
#define KERNEL_CODE 0x08
#define KERNEL_DATA 0x10
#define USER_CODE   0x18
#define USER_DATA   0x20
#define TSS_SEL     0x28

static void gdt_set(int idx, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags){
    gdt[idx].limit_low   = limit & 0xFFFF;
    gdt[idx].base_low    = base & 0xFFFF;
    gdt[idx].base_mid    = (base >> 16) & 0xFF;
    gdt[idx].access      = access;
    gdt[idx].flags_lim_high = (flags << 4) | ((limit >> 16) & 0x0F);
    gdt[idx].base_high   = (base >> 24) & 0xFF;
}

static void gdt_reload(void){
    gdt_p.limit = sizeof(gdt) - 1;
    gdt_p.base  = (uint64_t)gdt;
    __asm__ volatile ("lgdt %0" : : "m"(gdt_p));
    /* Reload segment registers with kernel data selector */
    __asm__ volatile (
        "movw %0, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        : : "i"(KERNEL_DATA) : "rax");
    /* Far jump to reload CS */
    __asm__ volatile (
        "pushq %0\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        : : "i"(KERNEL_CODE) : "rax", "memory");
}

/* Install the 16-byte TSS descriptor at GDT index 5 (and 6 for the
 * upper 32 bits of the base).  Access byte 0x89 = present, available
 * 64-bit TSS (type 9). */
static void gdt_install_tss(uint64_t base, uint32_t limit){
    struct gdt_entry* lo = &gdt[5];
    struct gdt_entry* hi = &gdt[6];
    uint32_t* hi_words;

    lo->limit_low      = limit & 0xFFFF;
    lo->base_low       = base & 0xFFFF;
    lo->base_mid       = (base >> 16) & 0xFF;
    lo->access         = 0x89;
    lo->flags_lim_high = (limit >> 16) & 0x0F;
    lo->base_high      = (base >> 24) & 0xFF;

    hi_words = (uint32_t*)hi;
    hi_words[0] = (uint32_t)(base >> 32);
    hi_words[1] = 0;
}

void tss_set_rsp0(uint64_t rsp0){
    tss.rsp[0] = rsp0;
}

void tss_init(void){
    uint64_t base = (uint64_t)&tss;
    int i;

    for (i = 0; i < 7; i++) tss.ist[i] = 0;
    tss.rsp[0] = 0;
    tss.iomap_base = sizeof(struct tss);   /* no I/O bitmap: user can't do port I/O */

    gdt_install_tss(base, (uint32_t)sizeof(struct tss) - 1);
    gdt_reload();
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)TSS_SEL));
}

void gdt_init(void){
    gdt_set(0, 0, 0, 0, 0);                                /* null */
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xA);                     /* kernel code, L=1 */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xC);                     /* kernel data */
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xA);                     /* user code, L=1, non-conforming (0xFA: P=1,DPL=3,S=1,type=1010b) */
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xC);                     /* user data */
    gdt_set(5, 0, 0, 0, 0);                                /* TSS low  (set by tss_init) */
    gdt_set(6, 0, 0, 0, 0);                                /* TSS high */
    gdt_reload();
}

