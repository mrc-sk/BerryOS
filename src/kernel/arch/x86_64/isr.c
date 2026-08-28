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

static void dump_regs(struct regs* r){
    char tmp[64];
    serial_puts("\r\n=== EXCEPTION: ");
    if (r->int_no < 32)
        serial_puts(exception_names[r->int_no]);
    else
        serial_puts("Unknown");
    serial_puts(" (vector ");
    /* simple number print */
    tmp[0] = '0' + (r->int_no / 10) % 10;
    tmp[1] = '0' + r->int_no % 10;
    tmp[2] = 0;
    serial_puts(tmp);
    serial_puts(") ===\r\n");

    serial_puts("  RAX="); serial_hex(r->rax); serial_puts("  RBX="); serial_hex(r->rbx); serial_puts("\r\n");
    serial_puts("  RCX="); serial_hex(r->rcx); serial_puts("  RDX="); serial_hex(r->rdx); serial_puts("\r\n");
    serial_puts("  RSI="); serial_hex(r->rsi); serial_puts("  RDI="); serial_hex(r->rdi); serial_puts("\r\n");
    serial_puts("  RBP="); serial_hex(r->rbp); serial_puts("  RSP="); serial_hex(r->rsp); serial_puts("\r\n");
    serial_puts("  RIP="); serial_hex(r->rip); serial_puts("  RFL="); serial_hex(r->rflags); serial_puts("\r\n");
    serial_puts("  CR2(if PF)=");
    {
        uint64_t cr2;
        __asm__ volatile ("movq %%cr2, %0" : "=r"(cr2));
        serial_hex(cr2);
    }
    serial_puts("\r\n");
}

static void panic(struct regs* r){
    dump_regs(r);
    serial_puts("KERNEL PANIC: stopping.\r\n");
    cli();
    for (;;) hlt();
}

/* Generic IRQ handlers (default: ignore + EOI) */
void isr_handler(struct regs* r){
    if (r->int_no < 32){
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
        }
        pic_send_eoi(irq);
    }
}
