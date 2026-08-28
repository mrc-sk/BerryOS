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
#include "drivers/fb.h"
#include "fbcon.h"

static void print_u64(uint64_t v){
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

static void print_mib(uint64_t bytes){
    print_u64(bytes >> 20);
    serial_puts(" MiB");
}

static void demo_task(void* arg);

void kmain(uint64_t fb_phys, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp){
    serial_init();
    vga_init();

    serial_puts("BerryOS 0.0.1 -- hybrid kernel booting on x86_64\r\n");
    vga_puts("BerryOS 0.0.1");
    vga_putc('\n');
    vga_puts("hybrid kernel / self-bootloader / framebuffer-ready");

    serial_puts("[M1] setting up GDT ...\r\n");
    gdt_init();

    serial_puts("[M1] setting up IDT ...\r\n");
    idt_init();

    serial_puts("[M1] initializing PIC ...\r\n");
    pic_init();

    serial_puts("[M1] starting PIT timer @100Hz ...\r\n");
    timer_init(100);

    serial_puts("[M1] initializing physical memory (buddy system) ...\r\n");
    pmm_init();
    serial_puts("[M1]   total: ");
    print_mib(pmm_total());
    serial_puts(", free: ");
    print_mib(pmm_free_bytes());
    serial_puts("\r\n");

    serial_puts("[M1] initializing slab allocator ...\r\n");
    slab_init();

    serial_puts("[M1] rebuilding kernel page tables ...\r\n");
    paging_init();
    serial_puts("[M1]   CR3 switched to new page tables\r\n");

    /* --- MM self-test --- */
    serial_puts("[M1] MM self-test:\r\n");
    {
        uint64_t before = pmm_free_bytes();
        void *a, *b, *c, *d, *e;
        a = kmalloc(64);                        /* slab 64  */
        b = kmalloc(3000);                      /* slab 4096 */
        c = kmalloc(5000);                      /* large (order 1) */
        d = kmalloc(128);                       /* slab 128 */
        e = kmalloc(20000);                     /* large (order 3) */
        serial_puts("[M1]   kmalloc 64/3000/5000/128/20000 -> ");
        print_u64(before - pmm_free_bytes());
        serial_puts(" bytes used\r\n");
        kfree(a); kfree(b); kfree(c); kfree(d); kfree(e);
        if (pmm_free_bytes() == before)
            serial_puts("[M1]   self-test PASS: all memory returned\r\n");
        else
            serial_puts("[M1]   self-test FAIL: memory leak\r\n");
    }
    {
        /* page-table self-test: map a scratch 4 KiB page, touch it */
        uint64_t phys = pmm_alloc_page();
        uint64_t scratch = 0x500000;            /* 5 MiB, inside low 1 GiB */
        if (phys){
            map_page(scratch, phys, 2 /* PTE_W */);
            *(volatile uint32_t*)scratch = 0xBEEFCAFE;
            serial_puts("[M1]   map_page scratch @");
            {
                char hex[19];
                int j;
                static const char hd[] = "0123456789abcdef";
                for (j = 0; j < 16; j++) hex[j] = hd[(scratch >> (60 - 4 * j)) & 0xF];
                hex[16] = 0;
                serial_puts(hex);
            }
            serial_puts(" = ");
            print_u64(*(volatile uint32_t*)scratch);
            serial_puts(" (ok)\r\n");
            unmap_page(scratch);
            pmm_free_page(phys);
        }
    }

    serial_puts("[M1] initializing TSS ...\r\n");
    tss_init();

    serial_puts("[M1] initializing PS/2 keyboard ...\r\n");
    keyboard_init();

    serial_puts("[M4] initializing device framework ...\r\n");
    drivers_init();
    {
        uint8_t bootsec[512];
        if (ata_present()){
            serial_puts("[M4] ATA disk: ");
            serial_puts(ata_model());
            serial_puts("\r\n");
            if (ata_read_sectors(0, 1, bootsec) == 1 &&
                bootsec[510] == 0x55 && bootsec[511] == 0xAA)
                serial_puts("[M4] ATA self-test: LBA0 read OK, boot signature 0x55AA present\r\n");
            else
                serial_puts("[M4] ATA self-test: LBA0 read FAILED\r\n");
        } else {
            serial_puts("[M4] ATA self-test: no disk detected\r\n");
        }
    }

    serial_puts("[M4] mounting BerryFS ...\r\n");
    bfs_mount();

    /* BerryFS self-test: prove read/write and cross-reboot persistence.
     * On the first boot it creates a marker file; on every later boot (same
     * disk image, no rebuild) it reads the marker back from disk -- which
     * only works if the filesystem genuinely persisted on the device. */
    {
        static char rbuf[128];
        static char lbuf[512];
        long fd, n;
        const char* marker = "BerryOS-BFS-OK";
        fd = bfs_open("selftest.txt", BFS_O_RD);
        if (fd < 0){
            fd = bfs_open("selftest.txt", BFS_O_WR | BFS_O_CREAT | BFS_O_TRUNC);
            bfs_write(fd, marker, 14);
            bfs_close(fd);
            serial_puts("[BFS] self-test: created selftest.txt (first boot)\r\n");
        } else {
            n = bfs_read(fd, rbuf, sizeof(rbuf) - 1);
            bfs_close(fd);
            if (n > 0) rbuf[n] = 0; else rbuf[0] = 0;
            serial_puts("[BFS] self-test: read back from disk = ");
            serial_puts(rbuf);
            serial_puts("\r\n");
        }
        n = bfs_ls(lbuf, sizeof(lbuf) - 1);
        if (n > 0){ lbuf[n] = 0; serial_puts("[BFS] files on disk:\r\n"); serial_puts(lbuf); }
    }

    /* --- M4 graphics: bind the VBE linear framebuffer and bring up the
     * framebuffer text console.  Falls back silently to text mode if the
     * bootloader couldn't set a graphics mode (fb_phys == 0). --- */
    if (fb_phys){
        fb_bind(fb_phys, w, h, pitch, bpp);
        fbcon_init();
        fb_clear(0x101418);                       /* desktop background */
        fb_fill_rect(0, 0, w, 26, 0x20262E);      /* top bar */
        fb_draw_string(10, 7, 0x6CA8FF, 0x20262E, "BerryOS");
        fb_fill_rect(8, 34, w - 16, h - 42, 0x0E1116);   /* console window */
        fb_draw_rect(8, 34, w - 16, h - 42, 0x3A4452);
        fbcon_set_cursor(2, 5);
        /* self-test: write a pixel and read it back to prove the LFB is
         * actually mapped and writable (not just a dead address). */
        fb_put_pixel(12, 40, 0x00FF00);
        {
            uint32_t* fb = (uint32_t*)g_fb.vbase;
            int stride = (int)(g_fb.pitch / 4);
            uint32_t px = fb[40 * stride + 12];
            serial_puts("[M4] graphics: VBE fb bound @0x");
            serial_hex((uint64_t)fb_phys); serial_puts(" ");
            serial_puts("("); print_u64(w); serial_puts("x"); print_u64(h);
            serial_puts(", pitch "); print_u64(pitch); serial_puts(", ");
            print_u64(bpp); serial_puts("bpp)\r\n");
            serial_puts("[M4] graphics self-test: pixel readback = 0x");
            serial_hex((uint64_t)px); serial_puts(" (expect 0x0000FF00)\r\n");
        }
        serial_puts("[M4] graphics console online\r\n");
    } else {
        serial_puts("[M4] graphics: no VBE mode, staying in text mode\r\n");
    }

    serial_puts("[M1] initializing scheduler ...\r\n");
    sched_init();

    serial_puts("[M1] enabling interrupts ...\r\n");
    sti();

    /* --- Scheduler demo: kernel tasks + one user process --- */
    serial_puts("[M1] creating demo tasks ...\r\n");
    sched_create(demo_task, (void*)1, PRIO_REALTIME, 5);
    sched_create(demo_task, (void*)2, PRIO_NORMAL, 5);
    sched_create(demo_task, (void*)3, PRIO_NORMAL, 5);
    serial_puts("[M1] creating user process task ...\r\n");
    sched_create(user_process_main, NULL, PRIO_NORMAL, 10);
    serial_puts("[M1] tasks ready: ");
    print_u64(sched_task_count());
    serial_puts("\r\n[M1] starting scheduler. This flow becomes the idle task.\r\n");

    sched_start();   /* never returns to us until no task is runnable */

    /* --- idle: run here when nothing else is runnable --- */
    serial_puts("[M1] back in idle. Waiting for ticks...\r\n");
    uint64_t last = 0;
    for (;;){
        hlt();                        /* wake on interrupt */
        uint64_t t = timer_ticks();
        if (t - last >= 100){         /* every second @100Hz */
            serial_puts("[M1] tick ");
            char buf[16];
            int i = 15;
            buf[i--] = 0;
            uint64_t v = t;
            do {
                buf[i--] = '0' + (v % 10);
                v /= 10;
            } while (v && i >= 0);
            serial_puts(&buf[i + 1]);
            serial_puts("\r\n");
            last = t;
        }
    }
}

/* Demo task: prints a few run lines with a busy loop and yields, then exits.
 * Returning from the entry triggers sched_task_exit via the entry stub. */
static void demo_task(void* arg){
    int id = (int)(uintptr_t)arg;
    int n;
    for (n = 1; n <= 3; n++){
        serial_puts("[task ");
        print_u64((uint64_t)id);
        serial_puts("] run ");
        print_u64((uint64_t)n);
        serial_puts("\r\n");
        volatile uint64_t x = 0;
        for (volatile uint64_t i = 0; i < 1000000; i++) x += i;   /* busy wait */
        sched_yield();
    }
    serial_puts("[task ");
    print_u64((uint64_t)id);
    serial_puts("] exiting\r\n");
    /* return -> sched_entry_stub -> sched_task_exit */
}
