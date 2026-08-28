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
