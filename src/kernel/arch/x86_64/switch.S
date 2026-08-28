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
/* M1 stage-3: task context switch (x86_64).
 *
 * switch_to(old_rsp_ptr, new_rsp):
 *   Saves the callee-saved registers of the current execution flow onto
 *   its stack, stores the resulting stack pointer into *old_rsp_ptr, then
 *   loads new_rsp and restores the callee-saved registers from there.
 *
 * A fresh task's initial stack frame is crafted in C (sched.c) so that the
 * first switch_to reaches sched_entry_stub(), which calls the task entry
 * with the argument passed in %r15 (arg) / entry pointer in %rbx. */

.section .text

/* void switch_to(uint64_t* old_rsp, uint64_t new_rsp) */
.globl switch_to
.type switch_to, @function
switch_to:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq  %rsp, (%rdi)        /* save current stack -> *old_rsp */
    movq  %rsi, %rsp          /* load new stack pointer */
    popq  %r15
    popq  %r14
    popq  %r13
    popq  %r12
    popq  %rbx
    popq  %rbp
    ret
.size switch_to, . - switch_to

/* First-run entry point for new tasks.
 * Initial frame: %r15 = arg, %rbx = entry function. */
.globl sched_entry_stub
.type sched_entry_stub, @function
sched_entry_stub:
    sti                       /* enable interrupts for preemption */
    movq %r15, %rdi           /* arg */
    call *%rbx                /* entry(arg) */
    call sched_task_exit      /* never returns (falls to hlt if it does) */
    hlt
.size sched_entry_stub, . - sched_entry_stub

/* First-run entry point for FORKED child tasks (M2).
 *
 * When the scheduler first dispatches a forked child, switch_to() returns here.
 * The child's kernel stack was crafted in sched_fork() so that, on entry to
 * this stub, %rsp points just above a single slot holding `child_r` (a pointer
 * to the child's trap frame on its own ISTAK).  We load that pointer into %rsp
 * and then replay the exact interrupt-return sequence, restoring the GPRs and
 * iret'ing back into ring 3 at the parent's fork-return point with rax = 0.
 *
 * This avoids relying on the copied parent kernel stack: the child's user-mode
 * resume is driven entirely by its own ISTAK frame. */
.globl child_resume_stub
.type child_resume_stub, @function
child_resume_stub:
    popq %rsp                 /* %rsp = child_r (GPR area on the child ISTAK) */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp            /* skip int_no + err_code */
    iretq                     /* back to ring 3 at the fork-return point */
.size child_resume_stub, . - child_resume_stub
