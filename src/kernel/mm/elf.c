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

/* =====================================================================
 * Minimal ELF64 loader (M1 stage-4).
 *
 * Parses an in-memory ELF64 executable, maps every PT_LOAD segment into
 * the kernel's (currently shared) address space with user permission
 * (PTE_P|PTE_W|PTE_US), and returns the entry point.
 *
 * Two passes: first the union page range of all PT_LOAD segments is
 * mapped once (so segments sharing a page are not double-mapped), then
 * each segment's contents are copied into place.  Pages are zeroed, so
 * the .bss tail (memsz - filesz) is automatically zero.
 * =================================================================== */

#define ELF_MAGIC_OK(b) ((b)[0]==0x7F && (b)[1]=='E' && (b)[2]=='L' && (b)[3]=='F')

struct elf64_hdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed));

#define PT_LOAD 1

static void zero_page(uint64_t phys){
    uint64_t* p = (uint64_t*)phys;
    int i;
    for (i = 0; i < 512; i++) p[i] = 0;
}

int elf_load(const unsigned char* img, uint64_t* entry_out){
    const struct elf64_hdr* eh;
    uint64_t lo_page = ~0ULL;
    uint64_t hi_page = 0;
    int i;

    if (!img || !entry_out) return -1;
    if (!ELF_MAGIC_OK(img)) return -1;
    if (img[4] != 2 || img[5] != 1) return -1;   /* 64-bit, little-endian */

    eh = (const struct elf64_hdr*)img;
    if (eh->e_phentsize < sizeof(struct elf64_phdr)) return -1;

    /* Pass 1: compute the union page range of all PT_LOAD segments. */
    for (i = 0; i < eh->e_phnum; i++){
        const struct elf64_phdr* ph = (const struct elf64_phdr*)
            (img + eh->e_phoff + (uint64_t)i * eh->e_phentsize);
        uint64_t ps, pe;
        if (ph->p_type != PT_LOAD) continue;
        ps = ph->p_vaddr & ~(uint64_t)(PAGE_SIZE - 1);
        pe = (ph->p_vaddr + ph->p_memsz + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
        if (ps < lo_page) lo_page = ps;
        if (pe > hi_page) hi_page = pe;
    }
    if (hi_page == 0) return -1;

    /* Map the whole union range (user-accessible). */
    {
        uint64_t a;
        for (a = lo_page; a < hi_page; a += PAGE_SIZE){
            uint64_t phys = pmm_alloc_page();
            if (!phys) return -1;
            zero_page(phys);
            map_page(a, phys, 6);   /* PTE_P | PTE_W | PTE_US */
        }
    }

    /* Pass 2: copy segment payloads. */
    for (i = 0; i < eh->e_phnum; i++){
        const struct elf64_phdr* ph = (const struct elf64_phdr*)
            (img + eh->e_phoff + (uint64_t)i * eh->e_phentsize);
        uint64_t j;
        if (ph->p_type != PT_LOAD) continue;
        for (j = 0; j < ph->p_filesz; j++)
            ((unsigned char*)ph->p_vaddr)[j] = img[ph->p_offset + j];
        /* memsz - filesz tail is already zero (fresh pages) */
    }

    *entry_out = eh->e_entry;
    return 0;
}
