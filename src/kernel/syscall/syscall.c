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
#include "fbcon.h"

/* =====================================================================
 * Syscall dispatch (M1 stage-4 "call gate").
 *
 * A user program enters the kernel with `int $0x80` (vector 128, trap
 * gate with DPL=3).  The interrupt stub saves all GPRs into struct regs;
 * we use the SysV-like convention:
 *   rax = syscall number, rdi/rsi/rdx = args, rax = return value.
 * =================================================================== */

static void serial_u64(uint64_t v){
    char buf[24];
    int i = 23;
    buf[i--] = 0;
    if (v == 0) buf[i--] = '0';
    while (v){
        buf[i--] = '0' + (v % 10);
        v /= 10;
    }
    serial_puts(&buf[i + 1]);
}

static uint64_t sys_write(uint64_t fd, const char* buf, uint64_t len){
    uint64_t i;
    (void)fd;   /* only COM1 for now */
    for (i = 0; i < len; i++){
        serial_putc(buf[i]);
        fbcon_putc((char)buf[i]);   /* also render to the framebuffer console */
    }
    return len;
}

static uint64_t sys_getpid(void){
    return sched_self_id();
}

static void sys_exit(uint64_t code){
    serial_puts("[sys] user process exiting, code ");
    serial_u64(code);
    serial_puts("\r\n");
    sched_exit((int)code);   /* become a zombie, wake parent, switch */
    for (;;) hlt();           /* unreachable */
}

static uint64_t sys_fork(struct regs* r){
    return (uint64_t)sched_fork(r);
}

static uint64_t sys_wait(uint64_t pid){
    return (uint64_t)sched_wait((int)pid);
}

/* Replace the current process's user image with an embedded ELF (idx 0 = init,
 * idx 1 = worker).  We rewrite the saved iret frame so that, when the syscall
 * returns, the CPU drops back into ring 3 at the new program's entry point. */
static uint64_t sys_exec(struct regs* r, uint64_t idx){
    const unsigned char* img;
    uint64_t len;
    uint64_t entry;
    uint64_t phys;
    int i;

    if (idx == 1){ img = user_elf_worker_bin; len = user_elf_worker_len; }
    else        { img = user_elf_init_bin;   len = user_elf_init_len; }
    (void)len;

    /* Discard the old user image (keep the page directory itself). */
    free_user_space(sched_current_cr3());

    /* Load the new image into the current (still active) page tables. */
    if (elf_load(img, &entry) != 0){
        serial_puts("[sys] exec: ELF load failed\r\n");
        return ~0ULL;
    }

    /* Fresh, zeroed user stack. */
    for (i = 0; i < USER_STACK_PAGES; i++){
        phys = pmm_alloc_page();
        if (!phys) return ~0ULL;
        { uint64_t* p = (uint64_t*)phys; int k; for (k = 0; k < 512; k++) p[k] = 0; }
        map_page(USER_STACK_VADDR + (uint64_t)i * PAGE_SIZE, phys, 6);
    }

    /* Build a clean ring-3 entry frame on the trap stack. */
    r->rax = 0; r->rbx = 0; r->rcx = 0; r->rdx = 0;
    r->rsi = 0; r->rdi = 0; r->rbp = 0;
    r->r8 = r->r9 = r->r10 = r->r11 = r->r12 = r->r13 = r->r14 = r->r15 = 0;
    r->rip    = entry;
    r->cs     = USER_CS;
    r->rflags = RFLAGS_IF;
    r->rsp    = USER_STACK_VADDR + (uint64_t)USER_STACK_PAGES * PAGE_SIZE;
    r->ss     = USER_SS;
    return 0;
}

/* M3: read a line from the keyboard.  Blocks the calling task (via the
 * scheduler) until a full line is available in the keyboard ring buffer. */
static uint64_t sys_read(uint64_t fd, char* buf, uint64_t len){
    int got = 0;
    (void)fd;
    keyboard_set_waiter((int)sched_self_id());
    while (got < (int)len){
        int c;
        if (!keyboard_available()){
            sched_self_block();
            sched_yield();
            continue;
        }
        c = keyboard_dequeue();
        if (c < 0) continue;
        buf[got++] = (char)c;
        if (c == '\n') break;   /* end of line */
    }
    keyboard_set_waiter(0);
    return (uint64_t)got;
}

void syscall_dispatch(struct regs* r){
    switch (r->rax){
        case SYS_WRITE:
            r->rax = sys_write(r->rdi, (const char*)r->rsi, r->rdx);
            break;
        case SYS_GETPID:
            r->rax = sys_getpid();
            break;
        case SYS_EXIT:
            sys_exit(r->rdi);
            break;   /* unreachable */
        case SYS_FORK:
            r->rax = sys_fork(r);
            break;
        case SYS_WAIT:
            r->rax = sys_wait(r->rdi);
            break;
        case SYS_EXEC:
            r->rax = sys_exec(r, r->rdi);
            break;
        case SYS_READ:
            r->rax = sys_read(r->rdi, (char*)r->rsi, r->rdx);
            break;
        case SYS_CLEAR:
            vga_clear();
            r->rax = 0;
            break;
        case SYS_OPEN:   r->rax = bfs_open((const char*)r->rdi, (long)r->rsi); break;
        case SYS_FWRITE: r->rax = bfs_write((long)r->rdi, (const void*)r->rsi, (unsigned long)r->rdx); break;
        case SYS_FREAD:  r->rax = bfs_read((long)r->rdi, (void*)r->rsi, (unsigned long)r->rdx); break;
        case SYS_CLOSE:  r->rax = bfs_close((long)r->rdi); break;
        case SYS_LS:     r->rax = bfs_ls((char*)r->rdi, (unsigned long)r->rsi); break;
        case SYS_MKFS:   bfs_format(); r->rax = 0; break;
        case SYS_UNLINK: r->rax = bfs_unlink((const char*)r->rdi); break;
        default:
            r->rax = ~0ULL;   /* -1: unknown syscall */
            break;
    }
}
