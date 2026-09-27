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
#ifndef BERRYOS_BERRYFS_H
#define BERRYOS_BERRYFS_H

#include "berryos.h"

/* BerryFS: a tiny flat (single-directory) persistent filesystem.
 * Layout on the boot disk, starting at sector BFS_FS_START (2 MiB in):
 *   sector 0 : superblock
 *   sector 1 : inode table (BFS_INODES * BFS_INODE_SIZE bytes)
 *   sector 9 : data-block allocation bitmap (1 sector -> 4096 blocks)
 *   sector 10: data blocks (block k lives at BFS_DATA_SECTOR + k)
 * Block size == sector size (512 B).  Max file size = BFS_DIRECT * 512. */

#define BFS_O_RD    0
#define BFS_O_WR    1
#define BFS_O_CREAT 2
#define BFS_O_TRUNC 4

void   bfs_mount(void);
void   bfs_format(void);
int    bfs_mounted(void);       /* 0 when no disk: every BF call fails then */
long   bfs_open(const char* path, long flags);
long   bfs_write(long fd, const void* buf, unsigned long n);
long   bfs_read(long fd, void* buf, unsigned long n);
long   bfs_close(long fd);
long   bfs_ls(char* buf, unsigned long n);
long   bfs_unlink(const char* path);

#endif /* BERRYOS_BERRYFS_H */
