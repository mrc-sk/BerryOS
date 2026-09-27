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
#include "fb.h"
#include "fbcon.h"
#include "tcon.h"
#include "desktop.h"

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
    (void)fd;   /* stdout only: the serial log, plus the screen */
    for (i = 0; i < len; i++){
        serial_putc(buf[i]);
        /* The screen has two possible owners.  With the desktop running, text
         * goes into the terminal surface and the desktop paints it into the
         * Terminal window -- that is what made the shell movable/closable.
         * Without a desktop (text mode, or no VBE mode) it still goes to the
         * framebuffer console the old way. */
        if (desktop_active()) tcon_putc((char)buf[i]);
        else                  fbcon_putc((char)buf[i]);
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

/* M4+: paint a rectangle from user space.
 * uargs points at a 5-long user struct {x,y,w,h,color}.
 *
 * Where "the screen" is depends on who owns it:
 *   - no desktop  -> the framebuffer, whole screen (the original behaviour)
 *   - desktop, focused window is the Canvas  -> its offscreen surface
 *   - desktop, anything else focused -> nowhere.  Drawing over the desktop
 *     because a program asked for pixels would be worse than doing nothing,
 *     and the Canvas window is one "pane open Canvas" away. */
static int gfx_target(void){
    int x, y, w, h;
    if (!desktop_active()) return 0;
    return desktop_canvas(&x, &y, &w, &h) ? 1 : -1;
}

static uint64_t sys_gfx_fill(uint64_t uargs){
    const long* a = (const long*)uargs;
    int ix = (int)a[0], iy = (int)a[1], iw = (int)a[2], ih = (int)a[3];
    uint32_t color = (uint32_t)a[4];
    int t = gfx_target();
    if (t < 0) return 0;
    if (t == 1){
        desktop_canvas_fill(ix, iy, iw, ih, color);
        return 0;
    }
    if (ix < 0){ iw += ix; ix = 0; }
    if (iy < 0){ ih += iy; iy = 0; }
    if (ix + iw > (int)fb_width())  iw = (int)fb_width()  - ix;
    if (iy + ih > (int)fb_height()) ih = (int)fb_height() - iy;
    if (iw <= 0 || ih <= 0) return 0;
    fb_fill_rect(ix, iy, iw, ih, color);
    return 0;
}

/* M4+: draw a string (scaled 2x for readability).
 * uargs points at {x,y,fg,bg,strptr}. */
static uint64_t sys_gfx_text(uint64_t uargs){
    const long* a = (const long*)uargs;
    const char* s = (const char*)a[4];
    char buf[256];
    int i, x, y, t;
    if (!s) return 0;
    for (i = 0; i < 255 && s[i]; i++) buf[i] = s[i];
    buf[i] = 0;
    x = (int)a[0];
    y = (int)a[1];
    t = gfx_target();
    if (t < 0) return 0;
    if (t == 1){
        desktop_canvas_text(x, y, (uint32_t)a[2], (uint32_t)a[3], buf);
        return 0;
    }
    fb_draw_string_scaled((uint32_t)x, (uint32_t)y, (uint32_t)a[2], (uint32_t)a[3], buf, 2);
    return 0;
}

/* M4+: clear the target surface to a solid colour. */
static uint64_t sys_gfx_clear(uint64_t color){
    int t = gfx_target();
    if (t < 0) return 0;
    if (t == 1){ desktop_canvas_clear((uint32_t)color); return 0; }
    fb_fill_rect(0, 0, fb_width(), fb_height(), (uint32_t)color);
    return 0;
}

/* ---- shell introspection (so the shell can report on the machine) ----
 * out[0] = total physical bytes, out[1] = free bytes.  User pointers are
 * directly readable/writable here: user space lives in the same page tables
 * the syscall runs under (see sys_gfx_fill). */
static uint64_t sys_meminfo(uint64_t* out){
    if (!out) return ~0ULL;
    out[0] = pmm_total();
    out[1] = pmm_free_bytes();
    return 0;
}

static uint64_t sys_tasks(char* buf, uint64_t n){
    return (uint64_t)sched_tasks(buf, (unsigned long)n);
}

/* System ticks since boot (PIT runs at 100 Hz). */
static uint64_t sys_uptime(void){
    return timer_ticks();
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
            if (desktop_active()) tcon_clear();
            else                  fbcon_clear();
            vga_clear();
            r->rax = 0;
            break;
        case SYS_OPEN:   r->rax = bfs_open((const char*)r->rdi, (long)r->rsi); break;
        case SYS_FWRITE: r->rax = bfs_write((long)r->rdi, (const void*)r->rsi, (unsigned long)r->rdx); break;
        case SYS_FREAD:  r->rax = bfs_read((long)r->rdi, (void*)r->rsi, (unsigned long)r->rdx); break;
        case SYS_CLOSE:  r->rax = bfs_close((long)r->rdi); break;
        case SYS_LS:     r->rax = bfs_ls((char*)r->rdi, (unsigned long)r->rsi); break;
        case SYS_MKFS:
            /* Report failure when there is no disk to format, so the shell can
             * say so rather than claiming success. */
            if (!bfs_mounted()){ r->rax = ~0ULL; break; }
            bfs_format();
            r->rax = 0;
            break;
        case SYS_UNLINK: r->rax = bfs_unlink((const char*)r->rdi); break;
        case SYS_GFX_FILL:
            r->rax = sys_gfx_fill(r->rdi);
            break;
        case SYS_GFX_TEXT:
            r->rax = sys_gfx_text(r->rdi);
            break;
        case SYS_GFX_CLEAR:
            r->rax = sys_gfx_clear(r->rdi);
            break;
        case SYS_MEMINFO:
            r->rax = sys_meminfo((uint64_t*)r->rdi);
            break;
        case SYS_TASKS:
            r->rax = sys_tasks((char*)r->rdi, r->rsi);
            break;
        case SYS_UPTIME:
            r->rax = sys_uptime();
            break;
        case SYS_DESKTOP:
            /* rdi = op, rsi = name/scratch buffer, rdx = its size.  Lets the
             * shell drive the window manager ("pane open Terminal") without
             * the kernel knowing anything about the command vocabulary. */
            r->rax = (uint64_t)desktop_ctl((int)r->rdi, (char*)r->rsi, r->rdx);
            break;
        default:
            r->rax = ~0ULL;   /* -1: unknown syscall */
            break;
    }
}
