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

/* Physical memory size to manage (QEMU default: 128 MiB).
 * The kernel image lives in low memory; everything from __kernel_end up
 * to PHYS_MEM_SIZE is carved into buddy blocks at pmm_init(). */
#ifndef PHYS_MEM_SIZE
#define PHYS_MEM_SIZE (128UL * 1024 * 1024)
#endif

struct buddy_block {
    struct buddy_block* next;
};

static struct buddy_block* free_lists[PMM_MAX_ORDER + 1];
static uint8_t page_map[PHYS_MEM_SIZE / PAGE_SIZE / 8];  /* 1 bit per page */

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

void pmm_init(void){
    extern char __kernel_end[];
    uint64_t addr, end, blk;
    int i, order;

    /* The early kernel stack (set in start.S: `movq $0x200000, %rsp`) sits
     * at 0x200000 and grows DOWN.  It lives inside the low-memory region, so
     * we must keep the entire [0, KERNEL_STACK_TOP) range out of the buddy
     * allocator.  Otherwise kmalloc can hand pages that overlap the live
     * kernel stack and silently corrupt it.  __kernel_end is below the stack
     * top, so reserving from 0 is the safe (and simplest) choice. */
    mem_base = 0x200000;   /* KERNEL_STACK_TOP; keep in sync with start.S */
    mem_top  = PHYS_MEM_SIZE;

    for (i = 0; i <= PMM_MAX_ORDER; i++) free_lists[i] = 0;
    for (i = 0; i < (int)sizeof(page_map); i++) page_map[i] = 0;

    /* carve the whole region into aligned buddy blocks, largest first */
    addr = mem_base;
    end  = mem_top;
    while (addr < end){
        order = PMM_MAX_ORDER;
        blk = (uint64_t)1 << (order + PAGE_SHIFT);
        while (order > 0 && (addr + blk > end || (addr & (blk - 1)))){
            order--;
            blk >>= 1;
        }
        push_free(addr, order);
        addr += blk;
    }
}
