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
/* Interrupt entry stubs (x86_64).
 * Each CPU exception/IRQ gets a small stub that pushes the vector number
 * (and a dummy error code where the CPU does not provide one), then jumps to
 * the common handler which saves all GPRs and calls isr_handler() in C. */

.section .text

.extern isr_handler

/* Macro: interrupt gate with NO error code pushed by CPU */
.macro ISR_NOERR num
.global isr\num
isr\num:
    pushq $0                 /* dummy error code */
    pushq $\num              /* vector number */
    jmp  isr_common
.endm

/* Macro: interrupt gate WITH error code pushed by CPU */
.macro ISR_ERR num
.global isr\num
isr\num:
    pushq $\num              /* vector number (error code already on stack) */
    jmp  isr_common
.endm

/* Macro: IRQ (remapped vector) */
.macro IRQ vec irqno
.global irq\irqno
irq\irqno:
    pushq $0
    pushq $\vec
    jmp  isr_common
.endm

/* ---- CPU exceptions 0..31 ---- */
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

/* ---- Hardware IRQs 0..15 (vectors 32..47) ---- */
IRQ 32  0
IRQ 33  1
IRQ 34  2
IRQ 35  3
IRQ 36  4
IRQ 37  5
IRQ 38  6
IRQ 39  7
IRQ 40  8
IRQ 41  9
IRQ 42  10
IRQ 43  11
IRQ 44  12
IRQ 45  13
IRQ 46  14
IRQ 47  15

/* Syscall vector (future M2 userland) */
ISR_NOERR 128

/* ---- common handler ---- */
isr_common:
    /* Disable nesting while we save regs.  The hardware IRQs (32..47) use
     * interrupt gates so the CPU already cleared IF, but vector 128 (int
     * 0x80) is a trap gate that keeps IF=1; without a cli here a timer
     * interrupt could nest onto the same ring-0 stack and corrupt the
     * regs frame we are saving (observed: syscall args read as stack
     * garbage, then #GP after iret).  iretq restores IF from rflags. */
    cli
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq  %rsp, %rdi         /* rdi -> regs base (rax at offset 0) */

    call  isr_handler

    popq  %r15
    popq  %r14
    popq  %r13
    popq  %r12
    popq  %r11
    popq  %r10
    popq  %r9
    popq  %r8
    popq  %rbp
    popq  %rdi
    popq  %rsi
    popq  %rdx
    popq  %rcx
    popq  %rbx
    popq  %rax
    addq  $16, %rsp          /* pop vector number + error code */
    iretq

.size isr_common, . - isr_common

