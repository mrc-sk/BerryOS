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
 * Paging (x86_64).
 *
 * The bootloader already set up a 2 MiB-page identity map of the low
 * 1 GiB plus a high-half alias at 0xFFFF800000000000.  This module
 * rebuilds a fresh kernel page table (so the PMM owns those pages),
 * then provides map_page/unmap_page for finer 4 KiB mappings that the
 * upcoming user-space (M2) will use.  Mapping a 4 KiB page inside a
 * region currently covered by a 2 MiB large page transparently splits
 * the large page into a real page table.
 *
 * Page-table walking assumes physical == virtual identity for the low
 * 1 GiB, which the kernel page tables guarantee.
 * =================================================================== */

/* Root of the kernel address space (PML4), identity mapped. */
static uint64_t kernel_pml4;

static void cr3_write(uint64_t v){
    __asm__ volatile ("movq %0, %%cr3" : : "r"(v) : "memory");
}
static void invlpg(uint64_t va){
    __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory");
}
static void zero_page(uint64_t phys){
    uint64_t* p = (uint64_t*)phys;
    int i;
    for (i = 0; i < 512; i++) p[i] = 0;
}

/* Return the currently active PML4 root (from CR3).  map_page/unmap_page
 * must operate on whatever page table the running process owns, not a
 * hardcoded kernel root -- now that every process has its own page tables. */
static uint64_t* active_pml4(void){
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return (uint64_t*)(cr3 & PTE_ADDR);
}

/* Locate / create the PDPT page referenced by PML4[i]. */
static uint64_t pdpt_or_create(uint64_t* pml4, int i){
    uint64_t entry = pml4[i];
    uint64_t phys;
    if (entry & PTE_P)
        return entry & PTE_ADDR;
    phys = pmm_alloc_page();
    if (!phys) return 0;
    zero_page(phys);
    pml4[i] = phys | PTE_P | PTE_W;
    return phys;
}

/* Locate / create the PD page referenced by PDPT[i]. */
static uint64_t pd_or_create(uint64_t* pdpt, int i){
    uint64_t entry = pdpt[i];
    uint64_t phys;
    if (entry & PTE_P)
        return entry & PTE_ADDR;
    phys = pmm_alloc_page();
    if (!phys) return 0;
    zero_page(phys);
    pdpt[i] = phys | PTE_P | PTE_W;
    return phys;
}

/* Split a 2 MiB large page (present in pd[pdi]) into a 512-entry page
 * table so an individual 4 KiB page can be remapped.  Returns the page
 * table physical address, or 0 on OOM. */
static uint64_t split_large_2m(uint64_t* pd, int pdi){
    uint64_t large_base = pd[pdi] & PTE_ADDR;
    uint64_t pt = pmm_alloc_page();
    int i;
    if (!pt) return 0;
    zero_page(pt);
    for (i = 0; i < 512; i++)
        ((uint64_t*)pt)[i] = (large_base + (uint64_t)i * PAGE_SIZE) | PTE_P | PTE_W;
    pd[pdi] = pt | PTE_P | PTE_W;   /* clear PS */
    return pt;
}

/* Map a single 4 KiB page at [va] -> [phys]. flags: PTE_W etc. */
void map_page(uint64_t va, uint64_t phys, int flags){
    uint64_t* pml4 = active_pml4();
    uint64_t* pdpt;
    uint64_t* pd;
    uint64_t* pt;
    uint64_t pt_phys;
    int pml4i = (int)((va >> 39) & 0x1FF);
    int pdpti = (int)((va >> 30) & 0x1FF);
    int pdi   = (int)((va >> 21) & 0x1FF);
    int pti   = (int)((va >> 12) & 0x1FF);

    pdpt = (uint64_t*)pdpt_or_create(pml4, pml4i);
    if (!pdpt) return;
    pd = (uint64_t*)pd_or_create(pdpt, pdpti);
    if (!pd) return;

    /* x86-64: a page is user-accessible only when U/S=1 in EVERY level of
     * the walk (PML4E, PDPTE, PDE, PTE).  Propagate US up the path when
     * mapping a user page so ring-3 can actually reach it. */
    if (flags & 4){
        pml4[pml4i] |= 4;
        pdpt[pdpti] |= 4;
    }

    if ((pd[pdi] & PTE_P) && (pd[pdi] & PTE_PS)){
        pt_phys = split_large_2m(pd, pdi);
        if (!pt_phys) return;
        if (flags & 4) pd[pdi] |= 4;   /* PDE (now pointing at the pt) needs US */
    } else if (pd[pdi] & PTE_P){
        pt_phys = pd[pdi] & PTE_ADDR;
        if (flags & 4) pd[pdi] |= 4;
    } else {
        pt_phys = pmm_alloc_page();
        if (!pt_phys) return;
        zero_page(pt_phys);
        pd[pdi] = pt_phys | PTE_P | PTE_W;
        if (flags & 4) pd[pdi] |= 4;
    }
    pt = (uint64_t*)pt_phys;
    pt[pti] = (phys & PTE_ADDR) | PTE_P | (uint64_t)flags;
    invlpg(va);
}

void unmap_page(uint64_t va){
    uint64_t* pml4 = active_pml4();
    uint64_t* pdpt;
    uint64_t* pd;
    uint64_t* pt;
    int pml4i = (int)((va >> 39) & 0x1FF);
    int pdpti = (int)((va >> 30) & 0x1FF);
    int pdi   = (int)((va >> 21) & 0x1FF);
    int pti   = (int)((va >> 12) & 0x1FF);

    if (!(pml4[pml4i] & PTE_P)) return;
    pdpt = (uint64_t*)(pml4[pml4i] & PTE_ADDR);
    if (!(pdpt[pdpti] & PTE_P)) return;
    pd = (uint64_t*)(pdpt[pdpti] & PTE_ADDR);
    if (!(pd[pdi] & PTE_P)) return;

    if (pd[pdi] & PTE_PS){
        /* still a large page: split, then clear the one 4 KiB entry */
        uint64_t pt_phys = split_large_2m(pd, pdi);
        if (!pt_phys) return;
        pt = (uint64_t*)pt_phys;
        pt[pti] = 0;
        invlpg(va);
        return;
    }
    pt = (uint64_t*)(pd[pdi] & PTE_ADDR);
    pt[pti] = 0;
    invlpg(va);
}

/* Map a contiguous physical region vaddr<->phys with the given PTE flags.
 * Used to bind device MMIO / the VBE linear framebuffer.  Walks 4 KiB pages
 * so any alignment works; callers that map big device regions at boot pay a
 * one-time cost and then never touch it again. */
void map_region(uint64_t vaddr, uint64_t phys, uint64_t size, int flags){
    uint64_t end = vaddr + size;
    uint64_t v = vaddr;
    uint64_t p = phys;
    while (v < end){
        map_page(v, p, flags);
        v += PAGE_SIZE;
        p += PAGE_SIZE;
    }
}

/* Rebuild the kernel page tables: low 1 GiB identity + high-half alias,
 * using 2 MiB large pages, and switch CR3 to the new root. */
void paging_init(void){
    uint64_t pml4, pdpt, pd;
    int i;

    pml4 = pmm_alloc_page();
    pdpt = pmm_alloc_page();
    pd   = pmm_alloc_page();
    if (!pml4 || !pdpt || !pd){
        serial_puts("[M1] paging: out of memory\r\n");
        return;
    }
    zero_page(pml4);
    zero_page(pdpt);
    zero_page(pd);

    /* PML4[0]   = identity (0x0000000000000000) */
    /* PML4[256] = high-half (0xFFFF800000000000) */
    ((uint64_t*)pml4)[0]   = pdpt | PTE_P | PTE_W;
    ((uint64_t*)pml4)[256] = pdpt | PTE_P | PTE_W;
    ((uint64_t*)pdpt)[0]   = pd   | PTE_P | PTE_W;
    for (i = 0; i < 512; i++)
        ((uint64_t*)pd)[i] = ((uint64_t)i << 21) | PTE_P | PTE_W | PTE_PS;

    kernel_pml4 = pml4;
    cr3_write(pml4);
}

/* ---- per-process page directories (M2) ---- */

uint64_t paging_kernel_cr3(void){
    return kernel_pml4;   /* physical base */
}

void pgdir_activate(uint64_t cr3){
    cr3_write(cr3);
}

/* Create a fresh page directory for a new process: a copy of the kernel
 * mappings (so the kernel stays reachable) plus an empty, isolated user
 * space.  Returns the physical PML4 base (suitable for CR3). */
uint64_t pgdir_new(void){
    uint64_t pml4_phys = pmm_alloc_page();
    uint64_t pdpt_phys = pmm_alloc_page();
    uint64_t* np    = (uint64_t*)pml4_phys;
    uint64_t* npdpt = (uint64_t*)pdpt_phys;
    uint64_t* kp;
    uint64_t  k_pdpt_phys;
    uint64_t* kpdpt;
    int i;

    if (!pml4_phys || !pdpt_phys) return 0;

    for (i = 0; i < 512; i++){ np[i] = 0; npdpt[i] = 0; }

    kp = (uint64_t*)kernel_pml4;
    /* Copy the kernel high-half mappings (PML4[256..511]). */
    for (i = 256; i < 512; i++) np[i] = kp[i];

    /* Low half (PML4[0]) holds the kernel identity map in pdpt[0]; clone it
     * and clear pdpt[1..511] so the new process starts with empty user space
     * (user programs live at 0x40000000, i.e. PML4[0].pdpt[1]). */
    k_pdpt_phys = kp[0] & PTE_ADDR;
    kpdpt = (uint64_t*)k_pdpt_phys;
    for (i = 0; i < 512; i++) npdpt[i] = kpdpt[i];
    for (i = 1; i < 512; i++) npdpt[i] = 0;

    np[0] = pdpt_phys | PTE_P | PTE_W;
    return pml4_phys;
}
