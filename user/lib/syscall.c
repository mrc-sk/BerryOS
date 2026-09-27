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
#include "syscall.h"

/* Generic int 0x80 call gate.  The trap does not clobber rcx/r11 the way
 * the `syscall` instruction does, so only "memory" needs to be listed. */
static inline long do_syscall(long num, long a, long b, long c){
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a), "S"(b), "d"(c)
        : "memory"
    );
    return ret;
}

long sys_write(int fd, const char* buf, unsigned long len){
    return do_syscall(0, (long)fd, (long)buf, (long)len);
}

long sys_getpid(void){
    return do_syscall(1, 0, 0, 0);
}

void sys_exit(long code){
    do_syscall(2, code, 0, 0);
    for (;;) __asm__ volatile ("hlt");
}

/* M2 multiprocess. */
long sys_fork(void){
    return do_syscall(3, 0, 0, 0);
}

long sys_wait(long pid){
    return do_syscall(4, pid, 0, 0);
}

long sys_exec(long idx){
    return do_syscall(5, idx, 0, 0);
}

/* M3: read a line from the keyboard (blocks until input is available). */
long sys_read(int fd, void* buf, unsigned long len){
    return do_syscall(6, (long)fd, (long)buf, (long)len);
}

/* M3: clear the VGA text screen. */
long sys_clear(void){
    return do_syscall(7, 0, 0, 0);
}

/* M4: BerryFS file I/O. */
long sys_open(const char* path, long flags){
    return do_syscall(8, (long)path, flags, 0);
}
long sys_fwrite(long fd, const void* buf, unsigned long n){
    return do_syscall(9, fd, (long)buf, (long)n);
}
long sys_fread(long fd, void* buf, unsigned long n){
    return do_syscall(10, fd, (long)buf, (long)n);
}
long sys_close(long fd){
    return do_syscall(11, fd, 0, 0);
}
long sys_ls(char* buf, unsigned long n){
    return do_syscall(12, (long)buf, (long)n, 0);
}
long sys_mkfs(void){
    return do_syscall(13, 0, 0, 0);
}
long sys_unlink(const char* path){
    return do_syscall(14, (long)path, 0, 0);
}

/* Shell introspection.  The kernel writes both memory figures into a
 * caller-supplied 2-slot array, so the wrapper copies them out. */
long sys_meminfo(unsigned long* total, unsigned long* free){
    unsigned long v[2];
    long r = do_syscall(18, (long)v, 0, 0);
    if (r != 0) return r;
    if (total) *total = v[0];
    if (free)  *free  = v[1];
    return 0;
}
long sys_tasks(char* buf, unsigned long n){
    return do_syscall(19, (long)buf, (long)n, 0);
}
unsigned long sys_uptime(void){
    return (unsigned long)do_syscall(20, 0, 0, 0);
}

/* M4+: graphics syscalls pack up to 5 args into a struct passed by pointer.
 * Only rdi (already delivered by the call gate) is needed, so this is portable
 * across compilers and avoids relying on r10/r8 register bindings. */
struct gfx_args { long p[5]; };
static struct gfx_args g_gfx;
static inline long do_syscall_gfx(long num, long a, long b, long c, long d, long e){
    g_gfx.p[0] = a; g_gfx.p[1] = b; g_gfx.p[2] = c; g_gfx.p[3] = d; g_gfx.p[4] = e;
    return do_syscall(num, (long)&g_gfx, 0, 0);
}

void gfx_fill(int x, int y, int w, int h, unsigned int color){
    do_syscall_gfx(15, (long)x, (long)y, (long)w, (long)h, (long)color);
}
void gfx_text(int x, int y, unsigned int fg, unsigned int bg, const char* s){
    do_syscall_gfx(16, (long)x, (long)y, (long)fg, (long)bg, (long)s);
}
void gfx_clear(unsigned int color){
    do_syscall(17, (long)color, 0, 0);
}

/* M5: drive the desktop window manager from the shell. */
long sys_desktop(int op, char* buf, unsigned long n){
    return do_syscall(21, (long)op, (long)buf, (long)n);
}
