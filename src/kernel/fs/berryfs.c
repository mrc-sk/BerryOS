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
#include "berryfs.h"

/* =====================================================================
 * BerryFS -- a minimal persistent filesystem (M4).
 *
 * Everything sits on the verified ATA PIO driver.  The filesystem occupies
 * a fixed region of the boot disk starting at BFS_FS_START; because mkimage.py
 * now preserves bytes past the kernel, the FS survives a rebuild.  The
 * kernel can dereference user buffers directly (a syscall runs with the
 * calling process's page tables still active, ring 0), so read/write copy
 * straight to/from user memory.
 * =================================================================== */

#define BFS_FS_START     4096      /* sector (2 MiB) -- safely beyond the kernel */
#define BFS_INODES       64
#define BFS_INODE_SIZE   64
#define BFS_NAME         24
#define BFS_DIRECT       8
#define BFS_DATA_BLOCKS  4096
#define BFS_MAX_FD       16

#define BFS_SB_SECTOR     (BFS_FS_START + 0)
#define BFS_INODE_SECTOR  (BFS_FS_START + 1)
#define BFS_INODE_SECTORS ((BFS_INODES * BFS_INODE_SIZE + 511) / 512)  /* 8 */
#define BFS_BITMAP_SECTOR (BFS_INODE_SECTOR + BFS_INODE_SECTORS)       /* +9 */
#define BFS_DATA_SECTOR   (BFS_BITMAP_SECTOR + 1)                      /* +10 */

struct bfs_sb {
    char     magic[4];
    uint32_t inode_count;
    uint32_t data_blocks;
    uint32_t inode_sector;
    uint32_t bitmap_sector;
    uint32_t data_sector;
    uint8_t  _pad[512 - 24];
};

struct bfs_inode {
    char     name[BFS_NAME];
    uint32_t size;
    uint8_t  used;
    uint8_t  _pad;
    uint32_t blocks[BFS_DIRECT];
    uint8_t  _pad2[2];
};

struct bfs_file {
    int      ino;
    uint32_t off;
    int      mode;
    int      used;
};

static uint8_t           g_sec[512];
static struct bfs_file   g_fds[BFS_MAX_FD];

/* ---- tiny local string/mem helpers (no libc) ---- */
static int kstrcmp(const char* a, const char* b){
    while (*a && *a == *b){ a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static void kmemcpy(void* d, const void* s, unsigned long n){
    unsigned char* dd = d; const unsigned char* ss = s;
    while (n--) *dd++ = *ss++;
}
static void kmemset(void* d, int c, unsigned long n){
    unsigned char* dd = d; while (n--) *dd++ = (unsigned char)c;
}
static void kstrcpy(char* d, const char* s){ while ((*d++ = *s++)); }

/* ---- sector primitives ---- */
static void sec_read(uint32_t lba, void* buf){ ata_read_sectors(lba, 1, buf); }
static void sec_write(uint32_t lba, const void* buf){ ata_write_sectors(lba, 1, buf); }

static void inode_read(int idx, struct bfs_inode* out){
    uint32_t off = (uint32_t)idx * BFS_INODE_SIZE;
    sec_read(BFS_INODE_SECTOR + off / 512, g_sec);
    kmemcpy(out, g_sec + off % 512, BFS_INODE_SIZE);
}
static void inode_write(int idx, const struct bfs_inode* in){
    uint32_t off = (uint32_t)idx * BFS_INODE_SIZE;
    sec_read(BFS_INODE_SECTOR + off / 512, g_sec);
    kmemcpy(g_sec + off % 512, in, BFS_INODE_SIZE);
    sec_write(BFS_INODE_SECTOR + off / 512, g_sec);
}

static int block_alloc(void){
    int i;
    sec_read(BFS_BITMAP_SECTOR, g_sec);
    for (i = 0; i < BFS_DATA_BLOCKS; i++){
        int byte = i / 8, bit = i % 8;
        if (!(g_sec[byte] & (1 << bit))){
            g_sec[byte] |= (1 << bit);
            sec_write(BFS_BITMAP_SECTOR, g_sec);
            return i;
        }
    }
    return -1;
}
static void block_free(int idx){
    int byte = idx / 8, bit = idx % 8;
    sec_read(BFS_BITMAP_SECTOR, g_sec);
    g_sec[byte] &= ~(1 << bit);
    sec_write(BFS_BITMAP_SECTOR, g_sec);
}

static int inode_find(const char* name){
    int i;
    for (i = 0; i < BFS_INODES; i++){
        struct bfs_inode in;
        inode_read(i, &in);
        if (in.used && kstrcmp(in.name, name) == 0) return i;
    }
    return -1;
}
static int inode_alloc(const char* name){
    int i;
    for (i = 0; i < BFS_INODES; i++){
        struct bfs_inode in;
        inode_read(i, &in);
        if (!in.used){
            kmemset(&in, 0, sizeof(in));
            kstrcpy(in.name, name);
            in.used = 1;
            in.size = 0;
            inode_write(i, &in);
            return i;
        }
    }
    return -1;
}

/* ---- public API ---- */
void bfs_format(void){
    struct bfs_sb sb;
    int i;
    kmemset(&sb, 0, sizeof(sb));
    sb.magic[0] = 'B'; sb.magic[1] = 'F'; sb.magic[2] = 'S'; sb.magic[3] = '1';
    sb.inode_count   = BFS_INODES;
    sb.data_blocks   = BFS_DATA_BLOCKS;
    sb.inode_sector  = BFS_INODE_SECTOR;
    sb.bitmap_sector = BFS_BITMAP_SECTOR;
    sb.data_sector   = BFS_DATA_SECTOR;
    kmemcpy(g_sec, &sb, sizeof(sb));
    sec_write(BFS_SB_SECTOR, g_sec);

    kmemset(g_sec, 0, 512);
    for (i = 0; i < BFS_INODE_SECTORS; i++) sec_write(BFS_INODE_SECTOR + i, g_sec);
    sec_write(BFS_BITMAP_SECTOR, g_sec);
    kmemset(g_fds, 0, sizeof(g_fds));
}

void bfs_mount(void){
    struct bfs_sb sb;
    if (!ata_present()){
        serial_puts("[BFS] no disk: filesystem disabled\r\n");
        return;
    }
    sec_read(BFS_SB_SECTOR, g_sec);
    kmemcpy(&sb, g_sec, sizeof(sb));
    if (sb.magic[0] != 'B' || sb.magic[1] != 'F' || sb.magic[2] != 'S' || sb.magic[3] != '1'){
        serial_puts("[BFS] no filesystem found, auto-formatting\r\n");
        bfs_format();
    } else {
        serial_puts("[BFS] mounted (BerryFS)\r\n");
    }
    kmemset(g_fds, 0, sizeof(g_fds));
}

long bfs_open(const char* path, long flags){
    int ino;
    int fd;
    if ((int)flags & BFS_O_CREAT){
        ino = inode_find(path);
        if (ino < 0) ino = inode_alloc(path);
        if (ino < 0) return -1;
        if ((int)flags & BFS_O_TRUNC){
            struct bfs_inode in;
            int k;
            inode_read(ino, &in);
            for (k = 0; k < BFS_DIRECT; k++)
                if (in.blocks[k]){ block_free(in.blocks[k]); in.blocks[k] = 0; }
            in.size = 0;
            inode_write(ino, &in);
        }
    } else {
        ino = inode_find(path);
        if (ino < 0) return -1;
    }
    for (fd = 0; fd < BFS_MAX_FD; fd++) if (!g_fds[fd].used) break;
    if (fd == BFS_MAX_FD) return -1;
    g_fds[fd].used = 1;
    g_fds[fd].ino  = ino;
    g_fds[fd].off  = 0;
    g_fds[fd].mode = (int)flags;
    return fd;
}

long bfs_write(long fd, const void* buf, unsigned long n){
    struct bfs_file* f;
    struct bfs_inode in;
    unsigned long done = 0;
    const uint8_t* src = (const uint8_t*)buf;
    if (fd < 0 || fd >= BFS_MAX_FD || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    inode_read(f->ino, &in);
    while (done < n){
        uint32_t boff   = f->off % 512;
        uint32_t blkidx = f->off / 512;
        uint32_t chunk, sec;
        int b;
        if (blkidx >= BFS_DIRECT) break;            /* file too large */
        if (in.blocks[blkidx] == 0){
            b = block_alloc();
            if (b < 0) break;
            in.blocks[blkidx] = (uint32_t)b;
            inode_write(f->ino, &in);              /* persist allocation */
        }
        sec = BFS_DATA_SECTOR + in.blocks[blkidx];
        chunk = 512 - boff;
        if (chunk > n - done) chunk = n - done;
        sec_read(sec, g_sec);
        kmemcpy(g_sec + boff, src + done, chunk);
        sec_write(sec, g_sec);
        f->off += chunk;
        done  += chunk;
        if (f->off > in.size) in.size = f->off;
    }
    inode_write(f->ino, &in);                       /* commit size + blocks */
    return (long)done;
}

long bfs_read(long fd, void* buf, unsigned long n){
    struct bfs_file* f;
    struct bfs_inode in;
    unsigned long done = 0;
    uint8_t* dst = (uint8_t*)buf;
    if (fd < 0 || fd >= BFS_MAX_FD || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    inode_read(f->ino, &in);
    if (f->off >= in.size) return 0;                /* EOF */
    while (done < n && f->off < in.size){
        uint32_t boff   = f->off % 512;
        uint32_t blkidx = f->off / 512;
        uint32_t sec    = BFS_DATA_SECTOR + in.blocks[blkidx];
        uint32_t chunk  = 512 - boff;
        uint32_t remain = in.size - f->off;
        if (chunk > remain) chunk = remain;
        if (chunk > n - done) chunk = n - done;
        sec_read(sec, g_sec);
        kmemcpy(dst + done, g_sec + boff, chunk);
        f->off += chunk;
        done  += chunk;
    }
    return (long)done;
}

long bfs_close(long fd){
    if (fd < 0 || fd >= BFS_MAX_FD) return -1;
    g_fds[fd].used = 0;
    return 0;
}

long bfs_unlink(const char* path){
    int ino = inode_find(path);
    struct bfs_inode in;
    int k;
    if (ino < 0) return -1;
    inode_read(ino, &in);
    for (k = 0; k < BFS_DIRECT; k++)
        if (in.blocks[k]){ block_free(in.blocks[k]); in.blocks[k] = 0; }
    in.used = 0; in.size = 0; in.name[0] = 0;
    inode_write(ino, &in);
    return 0;
}

long bfs_ls(char* buf, unsigned long n){
    int i;
    unsigned long pos = 0;
    if (!buf || n == 0) return -1;
    for (i = 0; i < BFS_INODES; i++){
        struct bfs_inode in;
        char digits[12];
        int d, q;
        uint32_t s;
        inode_read(i, &in);
        if (!in.used) continue;
        for (d = 0; in.name[d] && d < BFS_NAME && pos + 1 < n; d++)
            buf[pos++] = in.name[d];
        if (pos + 4 < n){ buf[pos++] = ' '; buf[pos++] = '('; }
        /* decimal size */
        s = in.size; d = 0;
        if (s == 0) digits[d++] = '0';
        else { char tmp[11]; int k = 0; while (s){ tmp[k++] = '0' + (s % 10); s /= 10; } while (k) digits[d++] = tmp[--k]; }
        for (q = 0; q < d && pos + 1 < n; q++) buf[pos++] = digits[q];
        if (pos + 3 < n){ buf[pos++] = ')'; buf[pos++] = '\r'; buf[pos++] = '\n'; }
    }
    if (pos < n) buf[pos] = 0;
    return (long)pos;
}
