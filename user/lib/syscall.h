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

#endif /* BERRYOS_USER_SYSCALL_H */
