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
/* BerryOS self-written bootloader (x86_64).
 *
 * Boot method: El Torito *no-emulation*.  The ISO boot image is the combined
 * (boot sector + raw kernel); the BIOS loads the whole thing into RAM at
 * 0x7C00 (boot sector) and 0x7E00 (kernel).  No int 0x13 disk reads are
 * needed here, which sidesteps floppy-emulation geometry/drive pitfalls.
 *
 * Flow (all in one 512-byte boot sector, loaded by BIOS at 0x7C00):
 *   1. 16-bit real mode: kernel is already at 0x7E00 (BIOS-loaded).
 *   2. 16-bit -> 32-bit protected mode (flat GDT)
 *   3. 32-bit: build identity page tables (1GB pages, low 4GB + high half)
 *   4. 32-bit -> 64-bit long mode (PAE + EFER.LME + CR0.PG)
 *   5. 64-bit: copy kernel from 0x7E00 to 0x100000 and jump to it
 *      (Kernel is linked at 0x100000; see kernel/arch/x86_64/kernel.ld.)
 *
 * Assembled with the LLVM integrated assembler (clang -target x86_64-none-elf).
 */

.code16
#ifdef DISK_BOOT
/* Stage2 variant (raw hard disk / USB): MBR loads this at 0x7000 (low enough
 * for 16-bit addressing, clear of the MBR @0x7C00, VBE @0x5000/0x6000 and the
 * kernel loaded at 0x7E00). */
.org 0x7000
#else
/* El Torito no-emulation ISO variant: BIOS loads boot sector at 0x7C00. */
.org 0x7C00
#endif
.global _start
_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x0600, %sp          /* stack grows down from 0x600 (away from boot code & kernel) */

    /* --- Kernel image source (two boot paths) ---
     *  a) El Torito *no-emulation* ISO: the BIOS loads (boot+kernel) into
     *     RAM (kernel at 0x7E00); there is no boot disk, so the int 0x13
     *     read below fails (CF=1) on the first sector and we fall through,
     *     using the kernel that is already in RAM.
     *  b) raw hard disk / USB: the BIOS only loaded the 512-byte boot
     *     sector; we must read the kernel (LBA 1+) into 0x7E00 ourselves.
     * We always attempt the read; on failure we simply keep what is in RAM.
     * The loop stops at the first failed sector (end of image / no disk). */
    movb $0x80, %dl              /* raw hard disk drive number */
    movw $dap, %si               /* ds:si -> DAP (ds = 0) */
    movw $0x07E0, %ax
    movw %ax, %es
    xorw %bx, %bx
#ifdef DISK_BOOT
    movw $2, %di                 /* disk: LBA0=MBR, LBA1=stage2, kernel at LBA2 */
#else
    movw $1, %di                 /* ISO: kernel at LBA1 (after boot sector) */
#endif
load_loop:
    movw %di, dap + 8            /* DAP LBA low field */
    movw %bx, dap + 4            /* DAP buffer offset (advance with bx) */
    movb $0x42, %ah
    int  $0x13
#ifdef DISK_BOOT
    jc   disk_fail               /* disk: a failed read is fatal (no kernel in RAM) */
#else
    jc   skip_disk               /* ISO: fail -> kernel already in RAM */
#endif
    addw $0x200, %bx             /* advance buffer offset (kernel < 64 KiB, single block) */
    incw %di
#ifdef DISK_BOOT
    cmpw $106, %di               /* read LBA 2..105 (104 sectors >= kernel size) */
#else
    cmpw $257, %di               /* LBA 1..256 */
#endif
    jne  load_loop
skip_disk:
    /* --- VBE: set a 1024x768x32 linear-framebuffer (mode 0x4119) ---
     * QEMU's standard VGA (Bochs VBE) supports this LFB mode.  On success we
     * query the mode info for the framebuffer physical address and stash
     * {phys, 1024, 768, 4096, 32} at 0x6000 for the kernel to read in long
     * mode.  On failure the fields stay zero -> text-mode fallback. */
    movw $0x4119, %bx
    movw $0x4F02, %ax
    int  $0x10
    cmpw $0x4F, %ax
    jne  vbe_done
    movw $0x0500, %cx      /* es = 0x500: mode-info buffer at 0x5000 */
    movw %cx, %es
    xorw %di, %di
    movw $0x4F01, %ax      /* get VBE mode info */
    movw %bx, %cx
    int  $0x10
    movl 0x5028, %eax      /* PhysBasePtr (offset 0x28) */
    movl %eax, 0x6000
    movl $1024, 0x6008     /* width  */
    movl $768,  0x6010     /* height */
    movl $4096, 0x6018     /* pitch = 1024 * 4 */
    movl $32,   0x6020     /* bpp    */
vbe_done:

    /* Load GDT and switch to protected mode */
    lgdt gdt_ptr
    movl %cr0, %eax
    orl  $1, %eax
    movl %eax, %cr0
    pushw $CODE32_SEL
    pushw $protected_mode
    lretw

.code32
protected_mode:
    movw $DATA32_SEL, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl $0x90000, %esp

    /* Build page tables: PML4 @ 0x1000, PDPT @ 0x2000, PD @ 0x3000.
     * 2 MB pages covering 0..1 GB (portable across QEMU and VMware). */
    movl $0x1000, %edi
    movl $0x0C00, %ecx        /* clear 12 KiB (0x1000..0x4000) = 0xC00 dwords */
    xorl %eax, %eax
    rep  stosl

    movl $0x2003, (0x1000)      /* PML4[0]   -> PDPT */
    movl $0x2003, (0x1800)      /* PML4[256] -> PDPT (high half) */
    movl $0x3003, (0x2000)      /* PDPT[0]   -> PD */

    /* PD entries are 8 bytes each. Fill 512 x 2 MB pages (0..1 GB). */
    movl $0x3000, %edi
    movl $0x00000083, %eax      /* 2 MB page: base 0, PS=1, RW, P */
    movl $512, %ecx
1:
    movl %eax, (%edi)           /* low 32 bits (base + flags) */
    movl $0, 4(%edi)            /* high 32 bits (base high) */
    addl $0x200000, %eax
    addl $8, %edi
    loop 1b

    movl %cr4, %eax
    orl  $0x20, %eax            /* CR4.PAE */
    movl %eax, %cr4
    movl $0x1000, %eax
    movl %eax, %cr3

    movl $0xC0000080, %ecx      /* EFER */
    rdmsr
    orl  $0x100, %eax           /* EFER.LME */
    wrmsr

    movl %cr0, %eax
    orl  $0x80000000, %eax      /* CR0.PG */
    movl %eax, %cr0

    pushl $CODE64_SEL
    pushl $long_mode
    lretl

.code64
long_mode:
    movw $DATA64_SEL, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movq $0x90000, %rsp

    /* Copy kernel: 256 sectors (0x20000 bytes) from 0x7E00 to 0x100000 */
    movq $0x20000, %rcx
    movq $0x7E00, %rsi
    movq $0x100000, %rdi
    rep  movsb

    /* Enable SSE (CR0.MP=1, CR0.EM=0, CR4.OSFXSR=1) so the kernel may use FP/SSE */
    movq %cr0, %rax
    orq  $0x2, %rax          /* CR0.MP */
    andq $~0x4, %rax         /* CR0.EM = 0 */
    movq %rax, %cr0
    movq %cr4, %rax
    orq  $0x200, %rax        /* CR4.OSFXSR */
    movq %rax, %cr4

    /* DEBUG marker: emit 'K' on COM1 just before entering the kernel, so we
     * can tell from the serial capture whether control reached the kernel. */
    movw $0x3F8, %dx
    movb $'K', %al
    outb %al, %dx

    movabsq $0x100000, %rax
    jmp  *%rax

disk_fail:
    movw $0x3F8, %dx
    movb $'E', %al
    outb %al, %dx
    movb %ah, %al           /* BIOS error code in AH -> two hex digits */
    shrb $4, %al
    andb $0x0f, %al
    orb  $0x30, %al
    outb %al, %dx
    movb %ah, %al
    andb $0x0f, %al
    orb  $0x30, %al
    outb %al, %dx
    hlt
    jmp  disk_fail

.align 8
gdt:
    .quad 0x0000000000000000    /* null */
    .quad 0x00CF9A000000FFFF    /* code32 (0x08) */
    .quad 0x00CF92000000FFFF    /* data32 (0x10) */
    .quad 0x00AF9A000000FFFF    /* code64 (0x18) */
    .quad 0x00AF92000000FFFF    /* data64 (0x20) */
gdt_end:
gdt_ptr:
    .word gdt_end - gdt - 1
    .long gdt

CODE32_SEL = 0x08
DATA32_SEL = 0x10
CODE64_SEL = 0x18
DATA64_SEL = 0x20

#ifdef DISK_BOOT
. = 0x71EE
#else
. = 0x7DEE
#endif
/* Disk Address Packet for int 0x13 AH=42h (LBA extended read). */
dap:
    .byte 0x10                  /* packet size */
    .byte 0                     /* reserved */
    .word 1                     /* sectors per read (updated per iteration) */
    .word 0x0000                /* offset   (0x07E0:0x0000 = 0x7E00) */
    .word 0x07E0                /* segment */
    .long 1                     /* LBA low  (start sector 1) */
    .long 0                     /* LBA high */

#ifdef DISK_BOOT
. = 0x71FE
#else
. = 0x7DFE
#endif
.word 0xAA55                   /* boot signature */
