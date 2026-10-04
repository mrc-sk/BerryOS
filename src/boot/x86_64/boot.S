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
 * This one source assembles into TWO boot loaders:
 *
 *   default      ISO variant -- El Torito *no-emulation*.  The ISO boot image
 *                is the combined (boot sector + raw kernel); the BIOS loads the
 *                whole thing into RAM at 0x7C00 (boot sector) and 0x7E00
 *                (kernel).  No int 0x13 disk reads are needed, which sidesteps
 *                floppy-emulation geometry/drive pitfalls.
 *                Built as build/boot.bin, loaded by the BIOS at 0x7C00.
 *   -DDISK_BOOT  raw hard disk / USB variant ("stage2").  The MBR reads these
 *                TWO sectors, so it must fetch the kernel (LBA 3+) itself.
 *                Built as build/stage2.bin, loaded by the MBR at 0x7000.
 *
 * Flow:
 *   1. 16-bit real mode: kernel at 0x7E00 (BIOS-preloaded / int 0x13 read).
 *   2. 16-bit -> 32-bit protected mode (flat GDT)
 *   3. 32-bit: build identity page tables (1GB pages, low 4GB + high half)
 *   4. 32-bit -> 64-bit long mode (PAE + EFER.LME + CR0.PG)
 *   5. 64-bit: copy kernel from 0x7E00 to 0x100000 and jump to it
 *      (Kernel is linked at 0x100000; see kernel/arch/x86_64/kernel.ld.)
 *
 * Assembled with the LLVM integrated assembler (clang -target x86_64-none-elf).
 */

/* ---- how much kernel to transfer (injected by the build) -------------------
 * The loader must move the WHOLE kernel image up to 0x100000.  This length
 * used to be hardcoded (0x18000 = 96 KiB), which silently truncated the image
 * the moment it grew past that -- adding the boot-splash wordmark (>100 KiB of
 * read-only data) pushed kernel.bin to ~170 KiB, so everything past 96 KiB
 * stayed zero and rendered as garbage.
 *
 * build.ps1 / Makefile now measure the real kernel.bin and pass the exact
 * values (via tools/kern_size.py).  The defaults below keep a hand assembly
 * working even without the -D, at the cost of copying a little zero padding --
 * over-reading is harmless because the extra bytes land at/after the kernel's
 * .bss, which start.S zeroes before kmain() runs.
 * ------------------------------------------------------------------------- */
#ifndef KERNEL_COPY_BYTES
#define KERNEL_COPY_BYTES 0x40000      /* fallback: transfer 256 KiB */
#endif
#ifndef KERNEL_SECTORS
#define KERNEL_SECTORS 512             /* fallback: 256 KiB / 512-byte sectors */
#endif

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
    cld                        /* lodsw in the VBE loop below relies on DF=0 */

    /* --- Enable the A20 line ---------------------------------------------
     * The kernel is copied to 0x100000, which is NOT reachable while A20 is
     * held low: the address wraps back to 0x000000, so the copy quietly
     * overwrites the IVT, the BDA and this very boot sector, and the jump
     * lands in garbage.  On a virtual machine that surfaces as "the CPU has
     * been disabled by the guest OS" (VMware) or a triple fault -- an
     * unrecoverable error with no kernel output at all.
     *
     * SeaBIOS (QEMU) happens to hand us A20 already enabled, which is why the
     * bug hid for so long; the Phoenix-derived BIOS in VMware makes no such
     * promise.  Fast A20 via port 0x92 is the most portable single method;
     * the MBR additionally issues int 0x15 AX=0x2401 on the raw-disk path. */
    inb  $0x92, %al
    orb  $0x02, %al
    outb %al, $0x92

    /* --- Kernel image source (two boot paths) ----------------------------
     *  a) El Torito *no-emulation* ISO: the BIOS has ALREADY loaded the whole
     *     boot image (boot sector + raw kernel) into RAM -- the kernel sits at
     *     0x7E00 and there is nothing left to read.
     *
     *     We deliberately do NOT probe a disk here.  An earlier revision did,
     *     "just in case", but that is actively unsafe: on a machine that boots
     *     from CD *and* has a hard disk attached, drive 0x80 is the hard disk,
     *     so the read succeeds and lands on top of the kernel the BIOS just
     *     placed in RAM.  tools/make_iso.py never writes the boot image to
     *     LBA 1 of the ISO either, so such a read has no legitimate use.
     *
     *  b) raw hard disk / USB: the BIOS only loaded this 512-byte boot sector
     *     (stage2, in this variant); we fetch the kernel ourselves from LBA 2
     *     (LBA 0 = MBR, LBA 1 = stage2).
     * -------------------------------------------------------------------- */
#ifdef DISK_BOOT
    movb $0x80, %dl              /* raw hard disk drive number */
    movw $dap, %si               /* ds:si -> DAP (ds = 0) */
    /* Read the kernel one 512-byte sector at a time.  int13 AH=42h takes the
     * buffer address from the DAP, not from es:bx, so the buffer is pinned at
     * OFFSET 0 and only the SEGMENT advances by one 0x20-paragraph sector: a
     * >64 KiB image then never needs a wrapped bx, and the loop stays small
     * enough to fit inside the 512-byte sector (see the .org asserts below).
     * The destination (0x7E00 + KERNEL_SECTORS*512) must stay clear of the
     * EBDA at 0x9FC00 -- tools/kern_size.py enforces that at build time. */
    movw $0x07E0, %bx            /* buffer segment cursor: bx:0 = 0x7E00 */
    movw $3, %di                 /* disk: LBA0=MBR, LBA1..2=stage2 (2 sectors), kernel at LBA3 */
load_loop:
    movw %di, dap + 8            /* DAP LBA low field */
    movw %bx, dap + 6            /* DAP buffer segment (offset field stays 0) */
    movb $0x42, %ah
    int  $0x13
    jc   disk_fail               /* a failed read is fatal (no kernel in RAM) */
    movw %bx, %es                /* mirror the segment for BIOSes that look at es */
    addw $0x20, %bx              /* advance the buffer by one 512-byte sector */
    incw %di
    cmpw $(3 + KERNEL_SECTORS), %di   /* LBA 3 .. 2+KERNEL_SECTORS (whole image) */
    jne  load_loop
#endif
    /* --- VBE: pick a 32bpp LFB mode, then read the REAL geometry ----------
     * Portable path: standard VESA BIOS Extensions via int 0x10, which works
     * on QEMU, VirtualBox, VMware and real hardware booted in legacy/CSM mode.
     * (A pure-UEFI box has no VBE int 0x10 at all and needs a GOP bootloader;
     * that is a separate piece of work.)
     *
     * We deliberately do NOT hardcode the resulting geometry.  A VBE is free to
     * answer with something other than what we asked for -- QEMU's SeaBIOS, for
     * example, accepts the mode but reports its own 1280x1024 RGB555 surface --
     * and assuming "1024x768x32" there is exactly what produced the garbled,
     * striped display.  So we stash whatever the BIOS actually reports and let
     * the kernel render to that bpp (see fb.c: fb_put_pixel).
     *
     * The CANDIDATE LIST matters more than it looks.  VBE 2.0 has no
     * "give me your best mode" call -- the only portable way is to walk the
     * mode table pointed at by VbeInfoBlock.VideoModePtr, and that does not
     * fit in a 512-byte sector.  So we try the standard 7-bit VESA mode
     * numbers (bit 14 set = ask for a linear framebuffer) from the largest
     * down and take the first one the BIOS accepts.  Every emulator and most
     * real BIOSes implement these.  The old list had just three entries and
     * lost the moment a BIOS did not happen to offer 1280x1024x24 -- leaving
     * 0x6000 all zero, i.e. no framebuffer and a text-only console.
     * Note the mode numbers are 0x4000 | <7-bit VESA number>: 0x11B is
     * 1280x1024x24, NOT "1024x768x32" as an earlier comment wrongly claimed. */
    movw $0x0500, %cx        /* es = 0x500 -> mode info block at 0x5000 */
    movw %cx, %es
    movw $vbe_modes, %si
vbe_try:
    lodsw                    /* next candidate mode; also advances si by 2 */
    movw %ax, %bx
    testw %bx, %bx           /* 0 marks the end of the list */
    jz   vbe_done
    movw $0x4F02, %ax        /* VBE: set mode */
    int  $0x10
    cmpb $0x4F, %al          /* AL == 0x4F: the call succeeded */
    jne  vbe_try
    xorw %di, %di
    movw $0x4F01, %ax        /* VBE: get mode info for the mode just set */
    movw %bx, %cx
    int  $0x10
    cmpb $0x4F, %al
    jne  vbe_try
    testb $0x80, 0x5000      /* ModeAttributes bit7: linear framebuffer */
    jz   vbe_try
    movl 0x5028, %eax        /* PhysBasePtr */
    testl %eax, %eax
    jz   vbe_try

    /* Accepted: stash the real geometry at 0x6000 for the kernel. */
    movl %eax, 0x6000        /* phys   */
    xorl %eax, %eax
    movw 0x5012, %ax         /* XResolution */
    movl %eax, 0x6008        /* width  */
    xorl %eax, %eax
    movw 0x5014, %ax         /* YResolution */
    movl %eax, 0x6010        /* height */
    /* pitch (stride).
     *
     * BytesPerScanLine at 0x10 is the stride for every VBE since 1.2, and in
     * a linear mode it is exactly what the pixel writer wants.  The old code
     * preferred 0x40, which is MaxPixelClock -- a pixel clock in Hz
     * (148500000 on real hardware).  SeaBIOS leaves 0x40 zero, so the 0x10
     * fallback ran and everything looked fine; on any firmware that fills it,
     * the clock was taken as a stride and the image came out sheared.
     * tools/vbe_probe.py confirms 0x32 (LinBytesPerScanLine) matches 0x10 in
     * every linear mode, so it only serves as a second chance. */
    xorl %eax, %eax
    movw 0x5010, %ax         /* BytesPerScanLine: the stride */
    testw %ax, %ax
    jnz  pitch_ok
    movw 0x5032, %ax         /* LinBytesPerScanLine (VBE 3.0) */
pitch_ok:
    movl %eax, 0x6018        /* pitch  */
    xorl %eax, %eax
    movb 0x5019, %al         /* BitsPerPixel */
    movl %eax, 0x6020        /* bpp    */
    jmp  vbe_done

vbe_modes:                   /* 0x4000 | standard VESA number, best first, 0-terminated */
    .word 0x411B             /* 1280x1024x24 */
    .word 0x4118             /* 1024x768x24  */
    .word 0x411A             /* 1280x1024x16 */
    .word 0x4117             /* 1024x768x16  */
    .word 0x4116             /* 1024x768x15  */
    .word 0x4115             /*  800x600x24  */
    .word 0x4114             /*  800x600x16  */
    .word 0x4113             /*  800x600x15  */
    .word 0x4112             /*  640x480x24  */
    .word 0x4111             /*  640x480x16  */
    .word 0x4110             /*  640x480x15  */
    .word 0x0000
vbe_done:

#ifndef DISK_BOOT
    /* --- E820: ask the BIOS for the real memory map ----------------------
     * The kernel used to assume a fixed 128 MiB, which is right for QEMU but
     * wrong on real hardware: those machines have 4..32 GiB *and* reserved
     * holes (ACPI tables, local APIC, ...) that must never be handed out as
     * free RAM.  Stash what the BIOS reports at 0x6800 -- a u16 entry count
     * followed by 24-byte entries -- where start.S picks it up.
     *
     * Why here and not in stage2: this is the ISO variant.  The raw-disk path
     * does the same walk in mbr.S, because stage2's sector has only ~8 bytes
     * left and cannot hold it.  Both write to the same 0x6800 layout.
     *
     * es:di is the BIOS's destination; ds (still 0) is used for the count. */
    xorw %ax, %ax
    movw %ax, %es            /* es = 0 (the VBE probe left it at 0x500) */
    movw %ax, 0x6800         /* entry count = 0 */
    movw $0x6804, %di        /* first entry slot */
    xorl %ebx, %ebx          /* continuation value: 0 starts the walk */
e820_next:
    movl $0x0000E820, %eax
    movl $0x534D4150, %edx   /* 'SMAP' */
    movl $24, %ecx           /* ask for the full ACPI 3.x entry */
    int  $0x15
    jc   e820_done           /* CF set: end of map (or E820 unsupported) */
    cmpl $0x534D4150, %eax   /* the BIOS must echo 'SMAP' */
    jne  e820_done
    incw 0x6800              /* count the entry we just received */
    testl %ebx, %ebx         /* ebx == 0 -> that was the last one */
    jz   e820_done
    addw $24, %di
    cmpw $(0x6804 + 32 * 24), %di   /* cap at 32 entries (0x6B04) */
    jb   e820_next
e820_done:
#endif

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

    /* Copy kernel from 0x7E00 (BIOS-preloaded for ISO / disk-read for raw disk)
     * to 0x100000, then jump to it.  The length is the REAL image size, injected
     * by the build and rounded up to a whole sector -- copying the complete
     * image is what makes large read-only data (e.g. the boot-splash wordmark)
     * actually present in memory. */
    movq $KERNEL_COPY_BYTES, %rcx
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

    movabsq $0x100000, %rax
    jmp  *%rax

#ifdef DISK_BOOT
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
#endif

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
/* stage2 is TWO sectors (0x7000..0x7400) so the boot code is not squeezed into
 * 494 bytes; tools/mkimage.py writes both and the kernel starts at LBA 3.
 * Our own MBR far-jumps here, so the 0xAA55 at the end is only a marker. */
. = 0x73EE
/* Disk Address Packet for int 0x13 AH=42h (LBA extended read). */
dap:
    .byte 0x10                  /* packet size */
    .byte 0                     /* reserved */
    .word 1                     /* sectors per read (updated per iteration) */
    .word 0x0000                /* offset   (0x07E0:0x0000 = 0x7E00) */
    .word 0x07E0                /* segment (0x07E0:0x0000 = 0x7E00) */
    .long 1                     /* LBA low  (start sector 1) */
    .long 0                     /* LBA high */

. = 0x73FE
#else
/* The ISO variant never issues an int 0x13 read (the BIOS already placed the
 * whole image in RAM), so it carries no DAP at all -- which is 16 more bytes
 * of code space in the tightest sector of the whole project. */
. = 0x7DFE
#endif
.word 0xAA55                   /* boot signature */
