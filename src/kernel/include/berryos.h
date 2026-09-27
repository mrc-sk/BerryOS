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
#ifndef BERRYOS_H
#define BERRYOS_H

/* Minimal freestanding type definitions (no host <stdint.h>). */
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long long uint64_t;
typedef char           int8_t;
typedef short          int16_t;
typedef int            int32_t;
typedef long long      int64_t;
typedef unsigned long  size_t;
typedef long           ssize_t;
typedef unsigned long  uintptr_t;
typedef int            bool;
#define true  1
#define false 0

#define NULL ((void*)0)

/* Page size (physical memory manager) */
#define PAGE_SHIFT 12
#define PAGE_SIZE  (1UL << PAGE_SHIFT)
#define PMM_MAX_ORDER 10   /* largest buddy block: 4 KiB << 10 = 4 MiB */

/* User process memory layout (M2 multiprocess; shared by host + syscalls).
 * Every user program is linked at USER_ENTRY_VADDR and gets a fresh stack at
 * USER_STACK_VADDR; the ring-0 trap stack (istack) lives in kernel space. */
#define USER_ENTRY_VADDR  0x40000000
#define USER_STACK_VADDR  0x48000000
#define USER_STACK_PAGES  4
#define USER_ISTACK_SIZE  (32 * 1024)
#define USER_CS   0x1B   /* GDT index 3, RPL=3: iretq drops CPL to 3 */
#define USER_SS   0x23   /* GDT index 4, RPL=3 */
#define RFLAGS_IF 0x202  /* reserved(1) | interrupt-enable */

/* Port I/O (x86_64). */
static inline void outb(uint16_t port, uint8_t val){
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port){
    uint8_t r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outw(uint16_t port, uint16_t val){
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port){
    uint16_t r;
    __asm__ volatile ("inw %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outl(uint16_t port, uint32_t val){
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port){
    uint32_t r;
    __asm__ volatile ("inl %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

/* Memory barriers / control */
static inline void io_wait(void){
    outb(0x80, 0);   /* unused port, small delay */
}
static inline void sti(void){ __asm__ volatile ("sti"); }
static inline void cli(void){ __asm__ volatile ("cli"); }
static inline void hlt(void){ __asm__ volatile ("hlt"); }

/* Serial (COM1) */
void serial_init(void);
void serial_putc(char c);
void serial_puts(const char* s);
void serial_hex(uint64_t v);

/* VGA text mode */
void vga_init(void);
void vga_putc(char c);
void vga_puts(const char* s);
void vga_clear(void);

/* GDT */
void gdt_init(void);

/* TSS (user-mode trap stack / syscall entry) */
void tss_init(void);
void tss_set_rsp0(uint64_t rsp0);

/* IDT / interrupts.
 * Layout MUST match interrupt.S stack order (LOWEST address first):
 *   isr_common pushes GPRs in this order:  rax, rbx, rcx, rdx, rsi, rdi,
 *   rbp, r8, r9, r10, r11, r12, r13, r14, r15.
 *   With a downward-growing stack the FIRST pushed register (rax) lands at
 *   the lowest address, so the GPR fields below are in that SAME order
 *   (rax first, r15 last).
 *   Then the per-vector stub pushes:  pushq $0 (dummy err_code) FIRST/lowest,
 *                                    pushq $\num (vector) SECOND.
 *   (For ISR_ERR vectors the CPU pushes the real error code instead of $0,
 *    so it occupies the same err_code slot.)
 *   Then the CPU pushes the hardware frame.  iretq pops it in this order
 *   (memory low -> high):  rip, cs, rflags, rsp, ss  (5 qwords for a
 *   cross-privilege entry; 3 for a same-privilege/kernel entry, where rsp/ss
 *   are absent and the struct slots below read the caller's stack contents).
 *   isr_common does NOT relayout the frame, so the C struct must mirror the
 *   iretq pop order exactly:  rip, cs, rflags, rsp, ss.
 *
 *   CRITICAL: the x86 stack grows DOWNWARD.  isr_common pushes rax FIRST and
 *   r15 LAST, so the LAST-pushed register (r15) lands at the LOWEST address
 *   (struct offset 0), and rax lands at the highest of the GPR block.  The
 *   GPR fields below are therefore listed in reverse push order: r15 first,
 *   rax last.  (The per-vector stub pushes $0 then $\num, so int_no/err_code
 *   follow the GPRs in that SAME order.) */
struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};
void idt_init(void);
void isr_install_irq(uint8_t irq, uint64_t handler);
void isr_handler(struct regs* r);

/* PIC 8259 */
void pic_init(void);
void pic_send_eoi(uint8_t irq);
void pic_unmask(uint8_t irq);

/* PIT timer */
void timer_init(uint32_t hz);
uint64_t timer_ticks(void);
void timer_tick(void);

/* Physical memory manager (buddy system) */
void pmm_init(void);
uint64_t pmm_alloc_pages(int order);
uint64_t pmm_alloc_page(void);
void pmm_free_pages(uint64_t phys, int order);
void pmm_free_page(uint64_t phys);
uint64_t pmm_total(void);
uint64_t pmm_free_bytes(void);
uint64_t pmm_allocated(void);

/* Slab allocator (kmalloc/kfree) */
void slab_init(void);
void* kmalloc(size_t size);
void kfree(void* ptr);

/* Paging (x86_64) */
void paging_init(void);
void map_page(uint64_t vaddr, uint64_t phys, int flags);
void unmap_page(uint64_t vaddr);
/* Map a contiguous [phys, phys+size) region with the given PTE flags. Used to
 * bind device MMIO / the VBE linear framebuffer into the kernel address space. */
void map_region(uint64_t vaddr, uint64_t phys, uint64_t size, int flags);

/* Page-table entry flags (x86_64). */
#define PTE_P    1ULL
#define PTE_W    2ULL
#define PTE_U    4ULL   /* user-accessible */
#define PTE_PS   (1ULL << 7)   /* page size (2 MiB / 1 GiB large page) */
#define PTE_PWT  (1ULL << 3)   /* write-through */
#define PTE_PCD  (1ULL << 4)   /* cache disable (device / framebuffer memory) */
#define PTE_ADDR 0x000FFFFFFFFFF000ULL

/* Per-process page table support (M2 init/multiprocess) */
uint64_t paging_kernel_cr3(void);
uint64_t pgdir_new(void);
void pgdir_activate(uint64_t cr3);

/* Scheduler (M:N, M1 stage-3) */
#define PRIO_REALTIME 0
#define PRIO_NORMAL   1
void sched_init(void);
int  sched_create(void (*entry)(void*), void* arg, int prio, uint32_t quantum);
void sched_start(void);
void scheduler_tick(void);
void sched_yield(void);
void sched_task_exit(void);
uint32_t sched_task_count(void);
uint32_t sched_self_id(void);
long    sched_tasks(char* buf, unsigned long n);  /* task table as text */
void    sched_report_usage(void);   /* Part B: peak stack usage dump */
void sched_set_istack(uint64_t istack);
void sched_set_my_cr3(uint64_t cr3);
uint64_t sched_current_cr3(void);   /* M2: cr3 of the running process */

/* M3: blocking read support (used by sys_read + keyboard wake) */
void sched_self_block(void);
void sched_wake_task(int id);

/* M2 multiprocess support */
int  sched_fork(struct regs* r);   /* returns child pid in r->rax, 0 in child */
int  sched_wait(int pid);          /* block until child exits; return code */
void sched_exit(int code);         /* exit as zombie, wake parent, switch */
void pgdir_free(uint64_t cr3);     /* free an entire process page dir */
void free_user_space(uint64_t cr3);/* free only the user portion (exec) */

/* PS/2 keyboard (M3) */
void keyboard_init(void);
void keyboard_isr(void);
void keyboard_selftest(void);
void keyboard_set_waiter(int id);
int  keyboard_available(void);
int  keyboard_dequeue(void);
/* Inject text as if typed (used to run a .bppg program's script in the shell). */
void keyboard_inject(const char* s);

/* PS/2 mouse (M4 GUI) */
void mouse_init(void);
void mouse_isr(void);
void mouse_get_xy(int* x, int* y);
int  mouse_get_buttons(void);
void mouse_set_xy(int x, int y);

/* Syscall / call gate (M1 stage-4) */
#define SYS_WRITE  0
#define SYS_GETPID 1
#define SYS_EXIT   2
#define SYS_FORK   3
#define SYS_WAIT   4
#define SYS_EXEC   5
#define SYS_READ   6
#define SYS_CLEAR  7
#define SYS_OPEN   8
#define SYS_FWRITE 9
#define SYS_FREAD  10
#define SYS_CLOSE  11
#define SYS_LS     12
#define SYS_MKFS   13
#define SYS_UNLINK 14

/* M4+: user-space graphics — draw directly into the framebuffer. */
#define SYS_GFX_FILL  15
#define SYS_GFX_TEXT  16
#define SYS_GFX_CLEAR 17

/* Shell introspection: memory, task table and uptime.  Added so the shell can
 * report on the machine without the kernel having to print it itself. */
#define SYS_MEMINFO 18
#define SYS_TASKS   19
#define SYS_UPTIME  20

/* Desktop window manager control (M5).  op = DESK_CTL_* (see desktop.h);
 * for DESK_CTL_OPEN, rdi points at the application name, rsi at a scratch
 * buffer and rdx at its size. */
#define SYS_DESKTOP 21
void syscall_dispatch(struct regs* r);

/* ELF loader (M1 stage-4) */
int elf_load(const unsigned char* img, uint64_t* entry_out);

/* Embedded user ELFs (auto-generated by tools/embed_elf.py, M2). */
extern const unsigned char user_elf_init_bin[];
extern const unsigned int  user_elf_init_len;
extern const unsigned char user_elf_worker_bin[];
extern const unsigned int  user_elf_worker_len;

/* User process host (user/user_main.c) */
void user_process_main(void* arg);

/* M5 desktop window manager (drivers/desktop.c) -- see desktop.h. */

/* =====================================================================
 * M4 framework: device / driver registry, ATA PIO, framebuffer.
 * =================================================================== */
struct driver;          /* see drivers/dev.h */
void drivers_init(void);

/* ATA/IDE PIO (drivers/ata.h) */
int        ata_present(void);
const char* ata_model(void);
void       ata_init(void);
int        ata_read_sectors(uint32_t lba, uint32_t count, void* buf);
int        ata_write_sectors(uint32_t lba, uint32_t count, const void* buf);

/* Framebuffer (drivers/fb.h) -- full definition lives there. */
struct framebuffer;
extern struct framebuffer g_fb;
void fb_init(void);
void fb_bind(uint64_t phys, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp);
void fb_set_font(const uint8_t* font, int cw, int ch);
void fb_put_pixel(int x, int y, uint32_t rgb);
void fb_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void fb_draw_rect(int x, int y, int w, int h, uint32_t rgb);
void fb_clear(uint32_t rgb);
void fb_draw_char(int x, int y, uint32_t fg, uint32_t bg, char c);
void fb_draw_string(int x, int y, uint32_t fg, uint32_t bg, const char* s);

/* BerryFS persistent filesystem (M4) -- layered on ata_read/write_sectors.
 * Mounts the FS region of the boot disk; auto-formats on first boot. */
void   bfs_mount(void);
void   bfs_format(void);
int    bfs_mounted(void);   /* 0 when no disk is present: every call fails */
long   bfs_open(const char* path, long flags);
long   bfs_write(long fd, const void* buf, unsigned long n);
long   bfs_read(long fd, void* buf, unsigned long n);
long   bfs_close(long fd);
long   bfs_ls(char* buf, unsigned long n);
long   bfs_unlink(const char* path);
/* open flags -- values MUST match user-side O_* (user/lib/syscall.h) */
#define BFS_O_RD    0
#define BFS_O_WR    1
#define BFS_O_CREAT 2
#define BFS_O_TRUNC 4

/* Kernel entry (defined in main.c) */
void kmain(uint64_t fb_phys, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp);

#endif /* BERRYOS_H */
