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
 * Slab allocator (small-object cache on top of the buddy PMM).
 *
 * Objects of size 32,64,128,...,4096 bytes are served from slab pages.
 * A slab page is one 4 KiB page holding a small header plus a linked
 * list of free objects (the free-list pointer lives inside each free
 * object slot).  Larger requests are satisfied directly from the buddy
 * allocator as whole pages, with a small header recording the order.
 * =================================================================== */

#define SLAB_PAGE_MAGIC 0x534C4142u   /* "SLAB" */
#define LARGE_MAGIC     0x4C524147u   /* "LRAG" */
/* 32,64,128,256,512,1024,2048 bytes.  A 4096-byte object cannot fit a
 * slab page (the page header takes space), so larger requests go
 * straight to the buddy allocator as whole pages. */
#define SLAB_CACHE_N    7

struct slab_cache;

struct slab_page {
    uint32_t           magic;
    struct slab_cache* cache;
    struct slab_page*  next;
    uint32_t           free_count;
    int32_t            first_free;    /* object index, or -1 */
};

struct slab_cache {
    uint32_t          obj_size;
    uint32_t          objs_per_page;
    uint32_t          active_pages;
    struct slab_page* partial;
    struct slab_page* full;
};

struct large_hdr {
    uint32_t magic;
    int32_t  order;
};

static struct slab_cache caches[SLAB_CACHE_N];

static void list_add(struct slab_page** head, struct slab_page* p){
    p->next = *head;
    *head = p;
}

static void list_remove(struct slab_page** head, struct slab_page* p){
    struct slab_page** pp = head;
    while (*pp){
        if (*pp == p){ *pp = p->next; return; }
        pp = &(*pp)->next;
    }
}

void slab_init(void){
    int i;
    for (i = 0; i < SLAB_CACHE_N; i++){
        caches[i].obj_size       = 32u << i;
        caches[i].objs_per_page  = 0;
        caches[i].active_pages   = 0;
        caches[i].partial        = 0;
        caches[i].full           = 0;
    }
}

static struct slab_page* slab_new_page(struct slab_cache* c){
    uint64_t phys = pmm_alloc_page();
    struct slab_page* p;
    uint64_t off, a;
    uint32_t n, i;
    if (!phys) return 0;
    p = (struct slab_page*)phys;
    off = sizeof(struct slab_page);
    n = (uint32_t)((PAGE_SIZE - off) / c->obj_size);
    if (n == 0){ pmm_free_page(phys); return 0; }
    c->objs_per_page = n;
    /* chain free objects: slot i stores index of next free slot (or -1) */
    for (i = 0; i < n; i++){
        a = phys + off + (uint64_t)i * c->obj_size;
        *(int32_t*)(uintptr_t)a = (i + 1 < n) ? (int32_t)(i + 1) : -1;
    }
    p->magic      = SLAB_PAGE_MAGIC;
    p->cache      = c;
    p->next       = 0;
    p->free_count = n;
    p->first_free = 0;
    return p;
}

static void* slab_alloc(struct slab_cache* c){
    struct slab_page* p = c->partial;
    int32_t idx;
    uint8_t* obj;
    if (!p){
        p = slab_new_page(c);
        if (!p) return 0;
        list_add(&c->partial, p);
        c->active_pages++;
    }
    idx = p->first_free;
    if (idx < 0) return 0;                    /* partial list corrupt */
    obj = (uint8_t*)p + sizeof(struct slab_page) + (uint64_t)idx * c->obj_size;
    p->first_free = *(int32_t*)obj;
    p->free_count--;
    if (p->free_count == 0){
        list_remove(&c->partial, p);
        list_add(&c->full, p);
    }
    return obj;
}

void* kmalloc(size_t size){
    int i;
    if (size == 0) size = 1;
    for (i = 0; i < SLAB_CACHE_N; i++)
        if (size <= caches[i].obj_size)
            return slab_alloc(&caches[i]);
    /* large object: allocate whole buddy block, record order */
    {
        size_t need = size + sizeof(struct large_hdr);
        int order = 0;
        size_t blk = PAGE_SIZE;
        struct large_hdr* h;
        while (blk < need){ blk <<= 1; order++; }
        if (order > PMM_MAX_ORDER) return 0;
        h = (struct large_hdr*)pmm_alloc_pages(order);
        if (!h) return 0;
        h->magic = LARGE_MAGIC;
        h->order = order;
        return (void*)(h + 1);
    }
}

void kfree(void* ptr){
    uint64_t addr = (uint64_t)ptr;
    struct slab_page* p;
    struct slab_cache* c;
    uint64_t off;
    if (!ptr) return;
    p = (struct slab_page*)(addr & ~(uint64_t)(PAGE_SIZE - 1));
    if (p->magic == SLAB_PAGE_MAGIC){
        off = addr - (uint64_t)p;
        c = p->cache;
        if (off < sizeof(struct slab_page) || off >= PAGE_SIZE) return;
        /* return the object slot to the page's free chain */
        *(int32_t*)ptr = p->first_free;
        p->first_free = (int32_t)((off - sizeof(struct slab_page)) / c->obj_size);
        p->free_count++;
        if (p->free_count == 1){
            list_remove(&c->full, p);
            list_add(&c->partial, p);
        }
        /* whole slab page free again -> give it back to the PMM */
        if (p->free_count == c->objs_per_page){
            list_remove(&c->partial, p);
            pmm_free_pages((uint64_t)p, 0);
            c->active_pages--;
        }
        return;
    }
    {
        struct large_hdr* h = (struct large_hdr*)ptr - 1;
        if (h->magic == LARGE_MAGIC)
            pmm_free_pages((uint64_t)h, h->order);
    }
}
