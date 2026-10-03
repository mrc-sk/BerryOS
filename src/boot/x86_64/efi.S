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
/* BerryOS UEFI boot stub (x86_64 EFI application).
 *
 * WHY this exists: boot.S queries a framebuffer through VBE -- an int 0x10 BIOS
 * call.  A UEFI box has no BIOS int 0x10 at all, so on firmware without CSM
 * (Hyper-V generation 2, VirtualBox/VMware with EFI enabled, most 2020+ laptops)
 * BerryOS would have no way to find a screen.  Firmware there exposes the
 * Graphics Output Protocol instead, and expects a PE32+ "EFI application"
 * rather than a boot sector.
 *
 * This file is that application.  It is deliberately NOT a general-purpose
 * loader: it carries the kernel inside its own image (see KERNEL_RVA) and
 * re-creates EXACTLY the handover the legacy path produces, so start.S and
 * kmain() need no UEFI-specific code at all:
 *
 *     0x6000  qword  framebuffer physical address
 *     0x6008  qword  width
 *     0x6010  qword  height
 *     0x6018  qword  pitch (bytes per scanline)
 *     0x6020  qword  bits per pixel
 *     0x6800  u16    E820 entry count, then 24-byte entries, synthesised from
 *                    the UEFI memory map (conventional memory only)
 *     0x100000        kernel image
 *
 * Everything after the headers is position independent (RIP-relative), because
 * the firmware may load the image anywhere and there is no relocation table.
 *
 * Built by tools/mkefi.py: efi.S -> flat binary, kernel.bin appended at
 * KERNEL_RVA, result is build/bootx64.efi.
 */

#ifndef KERNEL_BYTES
#define KERNEL_BYTES 0x40000
#endif

/* Where inside this image the kernel starts.  Fixed (rather than derived) so
 * assembly does not depend on the final size; the stub must stay below it. */
#define KERNEL_RVA    0x40000
#define IMAGE_BASE    0x1000000     /* hint only; the code is PIC */

/* Our page tables live at 0x1000..0x4000, so the stub's own data must start
 * above that. */
#define DATA_RVA      0x4000

#define IMG_END_RVA   (((KERNEL_RVA + KERNEL_BYTES) + 0x1FF) & ~0x1FF)

/* ---- UEFI structure offsets -----------------------------------------
 * Counted from the UEFI 2.10 tables.  Two of these were wrong in the first
 * draft and both failed silently:
 *   - LocateProtocol is at 0x140, not 0x148 (that one is
 *     InstallMultipleProtocolInterfaces, whose first three parameters have the
 *     same shape, so calling it "worked" and returned a garbage pointer);
 *   - MaxMode lives in Mode, not in the protocol -- and at least one
 *     implementation reports a nonsensical value there, so the mode scan is
 *     hard-capped instead of trusted. */
#define ST_BOOT_SERVICES      0x60
#define ST_CON_OUT            0x40

#define BS_LOCATE_PROTOCOL     0x140
#define BS_GET_MEMORY_MAP      0x38
#define BS_EXIT_BOOT_SERVICES  0xE8

#define GOP_QUERY_MODE         0x00
#define GOP_SET_MODE           0x08
#define GOP_MODE               0x18

#define MODE_MAX_MODE          0x00
#define MODE_INFO              0x08
#define MODE_FB_BASE           0x18

#define INFO_HRES              0x04
#define INFO_VRES              0x08
#define INFO_PIXEL_FORMAT      0x0C
#define INFO_PIXELS_PER_LINE   0x20

#define PIXEL_BGR_RESERVED     1       /* matches what fb.c writes at 32bpp */
#define PIXEL_BLT_ONLY         3

#define EFI_MEMORY_DESCRIPTOR_SIZE 48
#define MAX_SCAN_MODES         8       /* real GOPs have a handful          */

#define E820_MAX               32

.code64
.section .text

/* =====================================================================
 * PE32+ headers (file offset == RVA: FileAlignment == SectionAlignment
 * == 512, so the firmware's loaded image is byte-identical to this file).
 * =================================================================== */
.org 0
    .ascii "MZ"
.org 0x3C
    .long 0x80                          /* e_lfanew -> "PE\0\0"            */
.org 0x80
    .ascii "PE\0\0"
    /* ---- COFF file header (20 bytes) ---- */
    .word 0x8664                        /* Machine: AMD64                  */
    .word 1                             /* NumberOfSections                */
    .long 0                             /* TimeDateStamp                   */
    .long 0                             /* PointerToSymbolTable            */
    .long 0                             /* NumberOfSymbols                 */
    .word 240                           /* SizeOfOptionalHeader (PE32+)    */
    .word 0x0026                        /* Characteristics                 */
    /* ---- optional header, PE32+ (240 bytes) ---- */
    .word 0x020B                        /* Magic                           */
    .byte 14, 0                         /* linker version                  */
    .long 0                             /* SizeOfCode                      */
    .long 0                             /* SizeOfInitializedData           */
    .long 0                             /* SizeOfUninitializedData         */
    .long 0x200                         /* AddressOfEntryPoint             */
    .long 0x200                         /* BaseOfCode                      */
    .quad IMAGE_BASE                    /* ImageBase (hint; code is PIC)   */
    .long 0x200                         /* SectionAlignment                */
    .long 0x200                         /* FileAlignment                   */
    .word 0, 0                          /* OS version                      */
    .word 0, 0                          /* image version                   */
    .word 0, 0                          /* subsystem version               */
    .long 0                             /* Win32VersionValue               */
    .long IMG_END_RVA                   /* SizeOfImage                     */
    .long 0x200                         /* SizeOfHeaders                   */
    .long 0                             /* CheckSum                        */
    .word 10                            /* Subsystem: EFI application      */
    .word 0                             /* DllCharacteristics              */
    .quad 0                             /* SizeOfStackReserve              */
    .quad 0x10000                       /* SizeOfStackCommit               */
    .quad 0                             /* SizeOfHeapReserve               */
    .quad 0                             /* SizeOfHeapCommit                */
    .long 0                             /* LoaderFlags                     */
    .long 16                            /* NumberOfRvaAndSizes             */
    .fill 128, 1, 0                     /* 16 data directories             */
    /* ---- one section header (40 bytes) ---- */
    .ascii ".text\0\0\0"
    .long IMG_END_RVA - 0x200           /* VirtualSize                     */
    .long 0x200                         /* VirtualAddress                  */
    .long IMG_END_RVA - 0x200           /* SizeOfRawData                   */
    .long 0x200                         /* PointerToRawData                */
    .long 0                             /* PointerToRelocations            */
    .long 0                             /* PointerToLinenumbers            */
    .word 0                             /* NumberOfRelocations             */
    .word 0                             /* NumberOfLinenumbers             */
    .long 0xE0000020                    /* CODE|EXECUTE|READ|WRITE         */

/* =====================================================================
 * Entry point (RVA 0x200)
 *   rcx = EFI_HANDLE ImageHandle
 *   rdx = EFI_SYSTEM_TABLE *SystemTable
 * =================================================================== */
.org 0x200
.global _start
_start:
    cli

    /* Everything below is RIP-relative, so derive the ACTUAL load base from
     * our own address (this symbol is at RVA 0x200). */
    leaq _start(%rip), %r12
    subq $0x200, %r12

    movq %rcx, %r15                     /* ImageHandle                     */
    movq %rdx, %r14                     /* SystemTable                     */

    leaq stack_top(%rip), %rsp
    andq $-16, %rsp                     /* Microsoft x64 ABI: 16-aligned   */

    movq ST_BOOT_SERVICES(%r14), %r13   /* BootServices                    */
    movq ST_CON_OUT(%r14), %rbp         /* ConOut (diagnostics only)       */

    leaq msg_hello(%rip), %rcx
    call dbg_print

    /* ---- Graphics Output Protocol -----------------------------------
     * fb.c stores colours as 0x00RRGGBB, which a little-endian 32-bit store
     * lays down as the bytes B,G,R,x.  So the mode we want is
     * PixelBlueGreenRedReserved8BitPerColor; taking a RedGreenBlue mode would
     * render every colour with red and blue swapped.  If the mode we woke up
     * in already matches, take it and change nothing. */
    leaq gop_guid(%rip), %rcx
    xorl %edx, %edx                     /* Registration = NULL             */
    leaq g_gop(%rip), %r8
    movq BS_LOCATE_PROTOCOL(%r13), %rax
    subq $32, %rsp
    call *%rax
    addq $32, %rsp
    testq %rax, %rax
    jnz  gop_missing

    movq g_gop(%rip), %rbx
    movq GOP_MODE(%rbx), %rax
    movq MODE_INFO(%rax), %r10
    cmpl $PIXEL_BGR_RESERVED, INFO_PIXEL_FORMAT(%r10)
    jne  scan_modes
    jmp  have_mode

    /* Not BGR: look for a better mode.  The scan is hard-capped because
     * Mode->MaxMode is not trustworthy (one implementation reports 0xE1000000
     * there, which turns a loop bounded by it into an effective hang). */
scan_modes:
    movl $-1, g_best_mode(%rip)
    xorl %r11d, %r11d                   /* best area so far                */
    xorl %ecx, %ecx                     /* mode index                      */
scan_loop:
    cmpl $MAX_SCAN_MODES, %ecx
    jae  scan_done
    movq %rbx, %rdi
    movq %rcx, %rdx
    leaq g_info_size(%rip), %r8
    leaq g_info(%rip), %r9
    movq GOP_QUERY_MODE(%rbx), %rax
    subq $32, %rsp
    call *%rax
    addq $32, %rsp
    testq %rax, %rax
    jnz  scan_next
    movq g_info(%rip), %rdi
    testq %rdi, %rdi
    jz   scan_next
    cmpl $PIXEL_BGR_RESERVED, INFO_PIXEL_FORMAT(%rdi)
    jne  scan_next
    movl INFO_HRES(%rdi), %eax
    movl INFO_VRES(%rdi), %edx
    imull %edx, %eax                    /* area                            */
    cmpl %r11d, %eax
    jbe  scan_next
    movl %eax, %r11d
    movl %ecx, g_best_mode(%rip)
scan_next:
    incl %ecx
    jmp  scan_loop
scan_done:
    movl g_best_mode(%rip), %ecx
    cmpl $-1, %ecx
    je   have_mode
    movq %rbx, %rdi
    movl %ecx, %edx                     /* ModeNumber (32-bit, zero-extend) */
    movq GOP_SET_MODE(%rbx), %rax
    subq $32, %rsp
    call *%rax
    addq $32, %rsp

have_mode:
    movq GOP_MODE(%rbx), %rax
    movq MODE_INFO(%rax), %r10
    movq MODE_FB_BASE(%rax), %r8
    testq %r10, %r10
    jz   gop_missing
    testq %r8, %r8
    jz   gop_missing
    cmpl $PIXEL_BLT_ONLY, INFO_PIXEL_FORMAT(%r10)
    je   gop_missing

    /* Stage the geometry in our own image for now.  Writing it straight to
     * 0x6000 does not survive: ExitBootServices releases the pools the
     * firmware was using and low memory gets reused, so the kernel finds
     * zeros there and drops to the text console ("no VBE mode").  The real
     * handover block is published after ExitBootServices.
     *
     * The stores go through a RIP-relative pointer on purpose: a label in a
     * flat .org image is an RVA, so `movq %r8, fb_geom` would write to
     * physical 0x41xx and quietly lose the data. */
    leaq fb_geom(%rip), %r11
    movq %r8, 0x00(%r11)                 /* phys                            */
    movl INFO_HRES(%r10), %eax
    movq %rax, 0x08(%r11)                 /* width                           */
    movl INFO_VRES(%r10), %eax
    movq %rax, 0x10(%r11)                 /* height                          */
    movl INFO_PIXELS_PER_LINE(%r10), %eax
    shlq $2, %rax                         /* 4 bytes per pixel at 32bpp      */
    movq %rax, 0x18(%r11)                 /* pitch                           */
    movq $32, 0x20(%r11)                  /* bpp                             */
    leaq msg_gop(%rip), %rcx
    call dbg_print
    jmp  have_fb

gop_missing:
    leaq fb_geom(%rip), %r11
    movq $0, 0x00(%r11)                  /* no framebuffer: text fallback   */
    leaq msg_nogop(%rip), %rcx
    call dbg_print
    leaq msg_nogop(%rip), %rcx
    call efi_print                      /* only valid before ExitBoot...   */
have_fb:

    /* ---- UEFI memory map -> E820, staged in our image ---------------- */
    movq $0x7000, mmap_size(%rip)       /* capacity of mmap_buf            */
map_again:
    leaq mmap_size(%rip), %rcx
    leaq mmap_buf(%rip), %rdx
    leaq map_key(%rip), %r8
    leaq desc_size(%rip), %r9
    subq $48, %rsp                      /* 32 shadow + 8 for arg 5         */
    leaq desc_version(%rip), %rax
    movq %rax, 32(%rsp)
    movq BS_GET_MEMORY_MAP(%r13), %rax
    call *%rax
    addq $48, %rsp
    testq %rax, %rax
    jnz  map_failed

    /* Only EfiConventionalMemory becomes type 1.  Everything else is left out
     * entirely: pmm_init() fills its bitmap with 0xFF first and clears only
     * the regions reported as usable, so unreported memory is never handed
     * out.  That keeps MMIO and ACPI areas safe by construction. */
    movq desc_size(%rip), %rdx
    movq mmap_size(%rip), %rcx          /* bytes actually filled            */
    leaq mmap_buf(%rip), %rsi
    leaq e820_area(%rip), %rdi
    xorl %ebx, %ebx                     /* entries written                  */
map_loop:
    cmpq $EFI_MEMORY_DESCRIPTOR_SIZE, %rcx
    jb   map_done
    cmpl $7, (%rsi)                     /* EfiConventionalMemory           */
    jne  map_skip
    cmpq $E820_MAX, %rbx
    jae  map_done
    movq 0x08(%rsi), %rax               /* PhysicalStart                   */
    movq 0x18(%rsi), %r8                /* NumberOfPages                   */
    shlq $12, %r8
    testq %r8, %r8
    jz   map_skip
    movq %rax, (%rdi)
    movq %r8, 8(%rdi)
    movl $1, 16(%rdi)                   /* type 1 = usable                 */
    movl $0, 20(%rdi)
    addq $24, %rdi
    incl %ebx
map_skip:
    addq %rdx, %rsi
    subq %rdx, %rcx
    jmp  map_loop
map_done:
    movw %bx, e820_count(%rip)

    /* ---- ExitBootServices ------------------------------------------- */
    movq %r15, %rcx                     /* ImageHandle                      */
    movq map_key(%rip), %rdx
    movq BS_EXIT_BOOT_SERVICES(%r13), %rax
    subq $32, %rsp
    call *%rax
    addq $32, %rsp
    testq %rax, %rax
    jnz  map_again                      /* map changed: refresh and retry   */
    jmp  boot_services_gone

map_failed:
    leaq msg_mapfail(%rip), %rcx
    call dbg_print
    leaq msg_mapfail(%rip), %rcx
    call efi_print
    /* Carry on: a zero count makes pmm_init() fall back to its default. */
    movw $0, e820_count(%rip)

boot_services_gone:
    cli

    /* ---- Our own page tables: identity 0..1 GiB + high half ---------
     * We cannot run the kernel on the firmware's tables: we do not know what
     * they map, and we are about to write the kernel to 0x100000, which has
     * to be writable now and executable a moment later. */
    movq $0x1000, %rdi
    movq $0x600, %rcx                   /* 0x600 qwords = 12 KiB           */
    xorl %eax, %eax
    rep  stosq

    movq $0x2003, (0x1000)              /* PML4[0]   -> PDPT               */
    movq $0x2003, (0x1800)              /* PML4[256] -> high-half alias    */
    movq $0x3003, (0x2000)              /* PDPT[0]   -> PD                 */
    movq $0x3000, %rdi
    movq $0x00000083, %rax              /* 2 MiB page: PS|RW|P             */
    movq $512, %rcx
.Lpt_fill:
    movq %rax, (%rdi)
    addq $0x200000, %rax
    addq $8, %rdi
    loop .Lpt_fill
    movq $0x1000, %rax
    movq %rax, %cr3

    /* ---- Our own GDT, then CS=0x18 like the legacy path ------------- */
    leaq gdt(%rip), %rax
    leaq gdt_ptr(%rip), %rdi
    movw $39, (%rdi)
    movq %rax, 2(%rdi)
    lgdt (%rdi)
    pushq $0x18
    leaq flat64(%rip), %rax
    pushq %rax
    lretq
flat64:
    movw $0x20, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    /* ---- Publish the handover block (see start.S) --------------------
     * Only safe now: the firmware is gone and nothing else will touch low
     * memory.  Layout matches boot.S exactly. */
    leaq fb_geom(%rip), %rsi
    movq $0x6000, %rdi
    movq $5, %rcx
    rep  movsq
    movzwl e820_count(%rip), %ecx
    movw %cx, 0x6800
    testl %ecx, %ecx
    jz   .Lno_map
    imull $3, %ecx, %ecx              /* 24 bytes = 3 qwords per entry   */
    leaq e820_area(%rip), %rsi
    movq $0x6804, %rdi
    rep  movsq
.Lno_map:

    /* ---- Kernel: image -> 0x100000 ---------------------------------- */
    leaq kernel_image(%rip), %rsi
    movq $0x100000, %rdi
    movq $KERNEL_BYTES, %rcx
    rep  movsb

    leaq msg_jump(%rip), %rcx
    call dbg_print

    movq $0x100000, %rax
    jmp  *%rax

/* =====================================================================
 * Diagnostics.  dbg_print() writes to the debugcon port 0xE9 (what the
 * kernel's serial_putc() uses and what QEMU captures with -debugcon);
 * efi_print() writes to the UEFI console, the only thing a user can see on
 * real firmware.  Boot services may only be called before ExitBootServices,
 * which is why efi_print() is confined to the failure paths above.
 * =================================================================== */
dbg_print:
    pushq %rsi
    pushq %rdx
    pushq %rax
    movq %rcx, %rsi
    movq $0xE9, %rdx
.Ldbg_next:
    movb (%rsi), %al
    testb %al, %al
    jz   .Ldbg_done
    outb %al, %dx
    incq %rsi
    jmp  .Ldbg_next
.Ldbg_done:
    popq %rax
    popq %rdx
    popq %rsi
    ret

/* Print a qword as 16 hex digits on the debugcon port (rdi = value).
 * Preserves everything it touches: callers use it mid-sequence. */
print_qword:
    pushq %rax
    pushq %rdx
    pushq %rcx
    pushq %rbx
    movq %rdi, %r11
    movq $60, %rcx
.Lpq_next:
    decq %rcx
    movq %r11, %rax
    shrq $4, %r11
    andq $0xF, %rax
    leaq hexdigits(%rip), %rbx
    movb (%rbx,%rax), %al
    movq $0xE9, %rdx
    outb %al, %dx
    testq %rcx, %rcx
    jnz  .Lpq_next
    popq %rbx
    popq %rcx
    popq %rdx
    popq %rax
    ret

efi_print:
    pushq %rbp
    pushq %rbx
    movq %rcx, %rbx                     /* ASCII source                    */
    leaq con_buf(%rip), %rdi
.Lefi_next:
    movb (%rbx), %al
    testb %al, %al
    jz   .Lefi_done
    movb %al, (%rdi)
    movb $0, 1(%rdi)
    addq $2, %rdi
    incq %rbx
    jmp  .Lefi_next
.Lefi_done:
    movw $0, (%rdi)
    movq %rbp, %rcx                     /* ConOut                          */
    leaq con_buf(%rip), %rdx
    movq 0x08(%rbp), %rax               /* OutputString                    */
    subq $32, %rsp
    call *%rax
    addq $32, %rsp
    popq %rbx
    popq %rbp
    ret

/* ---- data ----------------------------------------------------------- */
.org DATA_RVA
g_gop:          .quad 0
g_info:         .quad 0
g_info_size:    .quad 0
g_best_mode:    .long -1
map_key:        .quad 0
desc_size:      .quad EFI_MEMORY_DESCRIPTOR_SIZE
desc_version:   .quad 0
mmap_size:      .quad 0
/* Staging for the two handover blocks, published to 0x6000 / 0x6800 only
 * after ExitBootServices -- see the copy site. */
fb_geom:        .fill 5, 8, 0             /* phys, width, height, pitch, bpp */
e820_count:     .word 0
                .word 0
e820_area:      .fill E820_MAX * 24, 1, 0
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF            /* code32 (0x08)                   */
    .quad 0x00CF92000000FFFF            /* data32 (0x10)                   */
    .quad 0x00AF9A000000FFFF            /* code64 (0x18)                   */
    .quad 0x00AF92000000FFFF            /* data64 (0x20)                   */
gdt_ptr:        .quad 0                 /* 10 bytes are written here        */
                .quad 0

msg_hello:      .asciz "[EFI] BerryOS UEFI stub\r\n"
msg_gop:        .asciz "[EFI] GOP framebuffer acquired\r\n"
msg_nogop:      .asciz "[EFI] no GOP: text console fallback\r\n"
msg_mapfail:    .asciz "[EFI] GetMemoryMap failed\r\n"
msg_jump:       .asciz "[EFI] jumping to kernel\r\n"
hexdigits:      .ascii "0123456789abcdef"

/* EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID
 *   { 0x9042a9de, 0x23dc, 0x4a38,
 *     { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } } */
gop_guid:
    .long 0x9042a9de
    .word 0x23dc
    .word 0x4a38
    .byte 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a

.org 0x5000
mmap_buf:                               /* 0x5000 .. 0xC000 (28 KiB)        */
.org 0xC000
con_buf:                                /* 512 bytes of UTF-16              */
.org 0xC200
stack_bottom:                           /* ~75 KiB, growing down            */
.org 0x20000
stack_top:

.org KERNEL_RVA
kernel_image:
