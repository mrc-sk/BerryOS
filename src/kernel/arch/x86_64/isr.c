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

static const char* const exception_names[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint", "Overflow",
    "Bound Range", "Invalid Opcode", "Device Not Available", "Double Fault",
    "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault",
    "Reserved", "x87 FPU", "Alignment Check", "Machine Check", "SIMD",
    "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Security", "Reserved",
};

/* ------------------------------------------------------------------
 * Crash reporter.  Print the register frame over the serial port -- the
 * UART is initialised very early in kmain, so it is always usable here.
 *
 * We deliberately do NOT write to a fixed physical page.  After
 * paging_init the low 1:1 identity mapping is gone, so dereferencing a
 * physical address as a virtual one (e.g. writing 0x6000) faults and
 * masks the real crash behind a triple fault.  Serial output needs no
 * page mapping and cannot itself fault, so it always reaches the log.
 * ------------------------------------------------------------------ */
static void dump_regs(struct regs* r){
    uint64_t cr2;
    __asm__ volatile ("movq %%cr2, %0" : "=r"(cr2));

    serial_puts("\r\n!!! KERNEL PANIC / EXCEPTION !!!\r\n");
    serial_puts(" int_no = "); serial_hex(r->int_no);
    serial_puts(" (");
    if (r->int_no < 32) serial_puts(exception_names[r->int_no]);
    serial_puts(")\r\n err    = "); serial_hex(r->err_code);
    serial_puts("\r\n rip    = "); serial_hex(r->rip);
    serial_puts("  cs = "); serial_hex(r->cs);
    serial_puts("\r\n rsp    = "); serial_hex(r->rsp);
    serial_puts("  rbp = "); serial_hex(r->rbp);
    serial_puts("\r\n cr2    = "); serial_hex(cr2);
    serial_puts("  rflags = "); serial_hex(r->rflags);
    serial_puts("\r\n rax    = "); serial_hex(r->rax);
    serial_puts("  rbx = "); serial_hex(r->rbx);
    serial_puts("\r\n rcx    = "); serial_hex(r->rcx);
    serial_puts("  rdx = "); serial_hex(r->rdx);
    serial_puts("\r\n rsi    = "); serial_hex(r->rsi);
    serial_puts("  rdi = "); serial_hex(r->rdi);
    serial_puts("\r\n caller stack trace (return addrs in 0x100000..0x110000):\r\n");
    if ((r->cs & 3) == 0 && r->rsp >= 0x1000 && r->rsp < 0x7fffffffffffULL){
        uint64_t* sp = (uint64_t*)(uint64_t)r->rsp;
        int i;
        for (i = 0; i < 48; i++){
            uint64_t v = sp[i];
            if (v >= 0x100000 && v < 0x110000){
                serial_puts("   ["); serial_hex((uint64_t)(uint64_t)(sp + i));
                serial_puts("] -> "); serial_hex(v); serial_puts("\r\n");
            }
        }
    }
}

static void panic(struct regs* r){
    dump_regs(r);
    cli();
    for (;;) hlt();
}

/* Generic IRQ handlers (default: ignore + EOI) */
void isr_handler(struct regs* r){
    if (r->int_no < 32){
        /* CPU exception.  A page fault (#14) taken from ring 3 is a *user*
         * bug, not a kernel bug: kill the offending process and resume the
         * scheduler so the rest of the OS (GUI, shell, other tasks) keeps
         * running instead of taking the whole machine down with a panic.
         * A fault from kernel mode (cs & 3 == 0) stays a hard panic, because
         * that indicates a real kernel defect that must not be silently
         * swallowed. */
        if (r->int_no == 14 && (r->cs & 3)){
            uint64_t cr2;
            __asm__ volatile ("movq %%cr2, %0" : "=r"(cr2));
            serial_puts("[fault] user process killed by page fault: cr2=");
            serial_hex(cr2);
            serial_puts(" rip=");
            serial_hex(r->rip);
            serial_puts("\r\n");
            sched_exit(139);     /* 128+11 ~= SIGSEGV */
            return;              /* unreachable */
        }
        panic(r);
        return;
    }
    if (r->int_no == 128){
        /* syscall via int 0x80 (trap gate, DPL=3) */
        syscall_dispatch(r);
        return;   /* no PIC EOI needed */
    }
    if (r->int_no >= 32 && r->int_no <= 47){
        /* routed to timer/keyboard/etc below */
        uint8_t irq = r->int_no - 32;
        if (irq == 0){
            timer_tick();
            scheduler_tick();   /* preemptive scheduling on PIT tick */
        } else if (irq == 1){
            keyboard_isr();     /* PS/2 keyboard (M3) */
        } else if (irq == 12){
            mouse_isr();        /* PS/2 mouse (M4 GUI) */
        }
        pic_send_eoi(irq);
    }
}
