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
#ifndef BERRYOS_USER_SYSCALL_H
#define BERRYOS_USER_SYSCALL_H

/* BerryOS ring-3 syscall wrappers (call gate == int 0x80).
 * Convention: rax = syscall number, rdi/rsi/rdx = args, rax = return. */
long sys_write(int fd, const char* buf, unsigned long len);
long sys_getpid(void);
void sys_exit(long code) __attribute__((noreturn));

/* M2 multiprocess */
long sys_fork(void);          /* returns child pid in child, 0 in parent */
long sys_wait(long pid);      /* wait for child, return its exit code */
long sys_exec(long idx);      /* replace image: 0 = init, 1 = worker */

/* M3 */
long sys_read(int fd, void* buf, unsigned long len);
long sys_clear(void);

/* M4: BerryFS persistent filesystem.  Values MUST match the kernel's
 * BFS_O_* flags and the SYS_* numbers in src/kernel/include/berryos.h. */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT  2
#define O_TRUNC  4
long sys_open(const char* path, long flags);
long sys_fwrite(long fd, const void* buf, unsigned long n);
long sys_fread(long fd, void* buf, unsigned long n);
long sys_close(long fd);
long sys_ls(char* buf, unsigned long n);
long sys_mkfs(void);
long sys_unlink(const char* path);

/* Shell introspection: let the shell report memory, tasks and uptime without
 * the kernel having to print anything itself. */
long sys_meminfo(unsigned long* total, unsigned long* free);
long sys_tasks(char* buf, unsigned long n);
unsigned long sys_uptime(void);

/* M4+: user-space graphics (draw into the framebuffer).  With the desktop
 * running, "the framebuffer" for a user program is the client area of the
 * focused window: coordinates are relative to it and outside it is clipped. */
void gfx_fill(int x, int y, int w, int h, unsigned int color);
void gfx_text(int x, int y, unsigned int fg, unsigned int bg, const char* s);
void gfx_clear(unsigned int color);

/* M5: desktop window manager control.  op values MUST match desktop.h. */
#define DESK_LIST  1
#define DESK_OPEN  2
#define DESK_CLOSE 3
long sys_desktop(int op, char* buf, unsigned long n);

#endif /* BERRYOS_USER_SYSCALL_H */
