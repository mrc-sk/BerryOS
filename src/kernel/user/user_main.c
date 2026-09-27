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
 * User process host (M1 stage-4).
 *
 * A kernel task hosts a user program: it loads the embedded user ELF
 * into user-accessible pages, allocates a ring-0 trap stack (istack),
 * then drops to ring 3 with iretq.  While in user mode, syscalls enter
 * the kernel through vector 128 (int 0x80) and use TSS.rsp0 = istack.
 * =================================================================== */

extern const unsigned char user_elf_init_bin[];
extern const unsigned int  user_elf_init_len;

#define ISTACK_SIZE USER_ISTACK_SIZE

/* Drop to ring 3 and start executing entry with user_rsp.  Never returns. */
static void user_enter(uint64_t entry, uint64_t user_rsp) __attribute__((noreturn));

static void user_enter(uint64_t entry, uint64_t user_rsp){
    uint64_t istack = (uint64_t)kmalloc(ISTACK_SIZE);
    uint64_t* sp;
    if (!istack){
        serial_puts("[user] no memory for istack\r\n");
        sched_task_exit();
    }
    {
        unsigned char* p = (unsigned char*)istack;
        uint32_t i;
        for (i = 0; i < ISTACK_SIZE; i++) p[i] = 0xCC;  /* watermark sentinel */
    }
    sched_set_istack(istack);
    tss_set_rsp0(istack + ISTACK_SIZE);

    /* Build the iretq frame (RIP, CS, RFLAGS, RSP, SS). */
    sp = (uint64_t*)(istack + ISTACK_SIZE);
    *--sp = USER_SS;
    *--sp = user_rsp;
    *--sp = RFLAGS_IF;
    *--sp = USER_CS;
    *--sp = entry;

    serial_puts("[user] entering ring 3 at ");
    serial_hex(entry);
    serial_puts("\r\n");

    __asm__ volatile ("movq %0, %%rsp\n\tiretq" : : "r"((uint64_t)sp) : "memory");
    for (;;) hlt();   /* unreachable */
}

void user_process_main(void* arg){
    uint64_t entry = 0;
    uint64_t phys;
    int i;

    (void)arg;

    /* Give this process its own page directory (isolated user space). */
    {
        uint64_t cr3 = pgdir_new();
        if (!cr3){
            serial_puts("[user] no memory for page table\r\n");
            sched_task_exit();
        }
        sched_set_my_cr3(cr3);
        pgdir_activate(cr3);
    }

    /* Map the user stack (user-accessible). */
    for (i = 0; i < USER_STACK_PAGES; i++){
        phys = pmm_alloc_page();
        if (!phys){
            serial_puts("[user] no memory for user stack\r\n");
            sched_task_exit();
        }
        {
            uint64_t* p = (uint64_t*)phys;
            int k;
            for (k = 0; k < 512; k++) p[k] = 0;
        }
        map_page(USER_STACK_VADDR + (uint64_t)i * PAGE_SIZE, phys, 6);
    }

    if (elf_load(user_elf_init_bin, &entry) != 0){
        serial_puts("[user] ELF load failed\r\n");
        sched_task_exit();
    }
    serial_puts("[user] ELF loaded (entry @");
    serial_hex(entry);
    serial_puts(")\r\n");

    user_enter(entry, USER_STACK_VADDR + USER_STACK_PAGES * PAGE_SIZE);
}
