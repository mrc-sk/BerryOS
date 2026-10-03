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
 * Physical memory manager (buddy system)
 *
 * Manages physical pages in [mem_base, mem_top) using a binary buddy
 * allocator.  Free blocks of order o (size = PAGE_SIZE << o) are kept on
 * free_lists[o]; a bitmap tracks allocation state of every 4 KiB page so
 * that a buddy pair can be merged on free.
 * =================================================================== */

/* Fallback size when the bootloader handed us no usable E820 map (QEMU-sized).
 * Real machines are detected instead -- see pmm_init(). */
#ifndef PHYS_MEM_SIZE
#define PHYS_MEM_SIZE (128UL * 1024 * 1024)
#endif

/* Hard ceiling on what we will ever hand out.
 *
 * The bootloader's page tables (boot.S) only cover 0..1 GiB with 512 x 2 MiB
 * pages, so any mem_top above that would hand out physical pages the kernel
 * cannot actually address: the first task to touch one would page-fault for no
 * visible reason.  Raising this means growing those page tables first. */
#define PMM_MAX_MANAGED (1UL * 1024 * 1024 * 1024)

/* The E820 map copied out of low memory by start.S. */
struct e820_entry g_e820[E820_MAX];
uint32_t g_e820_count;

struct buddy_block {
    struct buddy_block* next;
};

static struct buddy_block* free_lists[PMM_MAX_ORDER + 1];

/* Bitmap of page allocation state, 1 bit per 4 KiB page (1 = allocated).
 *
 * This used to be `static uint8_t page_map[PHYS_MEM_SIZE / PAGE_SIZE / 8]`,
 * which silently ties the managed memory to a compile-time constant.  Making
 * it a pointer and placing it just past the kernel image is what lets us
 * manage however much RAM E820 actually reported.  It must be sized at
 * runtime, and it must NOT be kmalloc()'d -- kmalloc depends on this
 * allocator, so we bump-allocate out of the space right after __kernel_end. */
static uint8_t* page_map;

static uint64_t mem_base;   /* first managed physical address */
static uint64_t mem_top;    /* one past last managed address */

/* ---- bitmap helpers (1 bit per 4 KiB page, 1 = allocated) ---- */
static void bm_set(uint64_t idx, int val){
    uint8_t m = (uint8_t)(1u << (idx & 7));
    if (val) page_map[idx >> 3] |= m;
    else     page_map[idx >> 3] &= (uint8_t)~m;
}
static int bm_get(uint64_t idx){
    return (page_map[idx >> 3] >> (idx & 7)) & 1;
}
static uint64_t pidx(uint64_t phys){
    return (phys - mem_base) >> PAGE_SHIFT;
}

static void push_free(uint64_t phys, int order){
    struct buddy_block* b = (struct buddy_block*)phys;
    b->next = free_lists[order];
    free_lists[order] = b;
}

/* A buddy block is free if every page in it is unallocated. */
static int block_is_free(uint64_t phys, int order){
    uint64_t start = pidx(phys);
    uint64_t n = 1UL << order;
    uint64_t i;
    for (i = 0; i < n; i++)
        if (bm_get(start + i)) return 0;
    return 1;
}

static void block_mark(uint64_t phys, int order, int alloc){
    uint64_t start = pidx(phys);
    uint64_t n = 1UL << order;
    uint64_t i;
    for (i = 0; i < n; i++)
        bm_set(start + i, alloc);
}

static int list_remove(uint64_t phys, int order){
    struct buddy_block** pp = &free_lists[order];
    while (*pp){
        if ((uint64_t)*pp == phys){
            *pp = (*pp)->next;
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

/* ---- public API ---- */

uint64_t pmm_alloc_pages(int order){
    int o;
    uint64_t phys;
    if (order < 0) order = 0;
    if (order > PMM_MAX_ORDER) return 0;
    o = order;
    while (o <= PMM_MAX_ORDER && free_lists[o] == 0) o++;
    if (o > PMM_MAX_ORDER) return 0;                    /* out of memory */
    phys = (uint64_t)free_lists[o];
    free_lists[o] = free_lists[o]->next;
    /* split larger block down to requested order */
    while (o > order){
        o--;
        push_free(phys + ((uint64_t)1 << (o + PAGE_SHIFT)), o);
    }
    block_mark(phys, order, 1);
    return phys;
}

uint64_t pmm_alloc_page(void){
    return pmm_alloc_pages(0);
}

void pmm_free_pages(uint64_t phys, int order){
    uint64_t blk;
    if (order < 0) order = 0;
    if (order > PMM_MAX_ORDER) return;
    if (phys < mem_base) return;
    blk = (uint64_t)1 << (order + PAGE_SHIFT);
    if (phys + blk > mem_top) return;
    block_mark(phys, order, 0);
    /* merge with buddy while possible */
    while (order < PMM_MAX_ORDER){
        uint64_t buddy = phys ^ ((uint64_t)1 << (order + PAGE_SHIFT));
        if (buddy < mem_base ||
            buddy + ((uint64_t)1 << (order + PAGE_SHIFT)) > mem_top) break;
        if (!block_is_free(buddy, order)) break;
        list_remove(buddy, order);
        if (buddy < phys) phys = buddy;
        order++;
    }
    push_free(phys, order);
}

void pmm_free_page(uint64_t phys){
    pmm_free_pages(phys, 0);
}

uint64_t pmm_total(void){
    return mem_top - mem_base;
}

uint64_t pmm_free_bytes(void){
    uint64_t n = 0, i, pages = (mem_top - mem_base) >> PAGE_SHIFT;
    for (i = 0; i < pages; i++)
        if (!bm_get(i)) n++;
    return n << PAGE_SHIFT;
}

uint64_t pmm_allocated(void){
    return pmm_total() - pmm_free_bytes();
}

/* Carve [start, end) into aligned buddy blocks, largest first, and mark the
 * pages free in the bitmap.  Only ever called on ranges that E820 reported as
 * usable, so reserved holes simply never get pushed. */
static void carve_range(uint64_t start, uint64_t end){
    uint64_t addr = start, blk;
    int order;
    while (addr < end){
        order = PMM_MAX_ORDER;
        blk = (uint64_t)1 << (order + PAGE_SHIFT);
        while (order > 0 && (addr + blk > end || (addr & (blk - 1)))){
            order--;
            blk >>= 1;
        }
        push_free(addr, order);
        block_mark(addr, order, 0);
        addr += blk;
    }
}

void pmm_init(void){
    extern char __kernel_end[];
    uint64_t detected = 0, map_bytes, pages, bm_addr;
    uint32_t i;

    /* The early kernel stack (set in start.S: `movq $0x200000, %rsp`) sits
     * at 0x200000 and grows DOWN.  It lives inside the low-memory region, so
     * we must keep the entire [0, KERNEL_STACK_TOP) range out of the buddy
     * allocator.  Otherwise kmalloc can hand pages that overlap the live
     * kernel stack and silently corrupt it.  __kernel_end is below the stack
     * top, so reserving from 0 is the safe (and simplest) choice. */
    mem_base = 0x200000;   /* KERNEL_STACK_TOP; keep in sync with start.S */

    /* ---- How much RAM is really there? --------------------------------
     * Prefer E820.  Take the highest end address of any usable region, so a
     * machine with RAM spread across several regions still gets all of it
     * (the individual regions are carved separately below). */
    for (i = 0; i < g_e820_count && i < E820_MAX; i++){
        if (g_e820[i].type != E820_TYPE_USABLE) continue;
        if (g_e820[i].base + g_e820[i].len > detected)
            detected = g_e820[i].base + g_e820[i].len;
    }
    if (detected == 0)
        mem_top = PHYS_MEM_SIZE;              /* no map: old behaviour */
    else if (detected > PMM_MAX_MANAGED)
        mem_top = PMM_MAX_MANAGED;            /* page tables only cover 1 GiB */
    else
        mem_top = detected;
    if (mem_top <= mem_base) mem_top = mem_base + (16UL << 20);   /* paranoia */

    for (i = 0; i <= PMM_MAX_ORDER; i++) free_lists[i] = 0;

    /* ---- Size and place the bitmap ------------------------------------
     * One bit per page of [mem_base, mem_top).  It goes immediately after the
     * kernel image, which is the only memory we know for certain is ours and
     * is not yet claimed by anything else. */
    pages     = (mem_top - mem_base) >> PAGE_SHIFT;
    map_bytes = (pages + 7) / 8;
    bm_addr   = ((uint64_t)__kernel_end + 15) & ~(uint64_t)15;
    page_map  = (uint8_t*)bm_addr;

    /* The whole low region below mem_base -- kernel image, page tables, the
     * early stack, and now this bitmap -- is off-limits to the allocator, so
     * the bitmap must fit underneath.  If it ever does not, clamp instead of
     * corrupting whatever sits above (and main.c prints the guard line). */
    if (bm_addr + map_bytes > mem_base){
        /* 1 bitmap byte covers 8 pages, so `avail` bytes of bitmap describe
         * avail*8*PAGE_SIZE bytes of RAM.  Give up the tail instead of
         * scribbling over the early stack. */
        uint64_t avail = mem_base - bm_addr;
        mem_top   = mem_base + ((avail * 8) << PAGE_SHIFT);
        pages     = (mem_top - mem_base) >> PAGE_SHIFT;
        map_bytes = (pages + 7) / 8;
    }

    /* Default everything to "not ours".  Only pages inside a usable E820
     * region get cleared below.  That inversion is deliberate: it means an
     * unlisted hole (ACPI, APIC, MMIO) is reserved by construction rather
     * than by remembering to exclude it. */
    memset(page_map, 0xFF, map_bytes);

    /* ---- Hand out exactly what E820 called usable --------------------- */
    if (detected == 0){
        carve_range(mem_base, mem_top);       /* fallback: one flat region */
    } else {
        for (i = 0; i < g_e820_count && i < E820_MAX; i++){
            uint64_t s, e;
            if (g_e820[i].type != E820_TYPE_USABLE) continue;
            s = g_e820[i].base;
            e = g_e820[i].base + g_e820[i].len;
            if (s < mem_base) s = mem_base;
            if (e > mem_top)  e = mem_top;
            s = (s + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
            e = e & ~(uint64_t)(PAGE_SIZE - 1);
            if (s >= e) continue;
            carve_range(s, e);
        }
    }
}
