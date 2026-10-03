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
/* BerryOS minimal MBR bootloader (raw hard disk / USB).
 *
 * SeaBIOS loads this 512-byte MBR to 0x7C00 only if it has a valid boot
 * signature AND an active partition entry, so we include a proper partition
 * table.  The MBR then reads the stage2 bootloader (LBA 1, one sector) into
 * 0x7000 and far-jumps to it.  stage2 (boot.S built with -DDISK_BOOT) loads
 * the kernel (LBA 2+) into 0x7E00 and enters long mode.
 *
 * Layout of build/disk.img:
 *   LBA 0    : this MBR (with partition table)
 *   LBA 1..2 : stage2 (two sectors, at 0x7000..0x7400)
 *   LBA 3+   : raw kernel image (copied to 0x7E00 by stage2)
 */
.code16
.org 0x7C00
.global _start
_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x0600, %sp

    /* Enable A20 so the kernel copied to 0x100000 is reachable above 1 MiB
     * (the ISO/SeaBIOS path does this automatically; raw disk does not).
     * Use fast-A20 (port 0x92) which is more reliable than int 0x15. */
    inb  $0x92, %al
    orb  $0x02, %al
    outb %al, $0x92
    movw $0x2401, %ax
    int  $0x15

    /* --- E820: ask the BIOS for the real memory map ----------------------
     * The kernel used to assume a fixed 128 MiB, which is right for QEMU but
     * wrong on real hardware: 4..32 GiB, plus reserved holes (ACPI tables,
     * local APIC, ...) that must never be handed out as free RAM.
     *
     * It happens here and not in stage2 because stage2's 512-byte sector is
     * already down to ~8 free bytes; the MBR has plenty.  The ISO path does
     * the identical walk in boot.S.  Both use the same 0x6800 layout:
     *   u16 entry count, then 24-byte entries.
     * Do this BEFORE the int 0x13 read below, which repoints es. */
    movw $0x6800, %di
    movw $0, 0x6800          /* entry count = 0 */
    addw $4, %di             /* first entry slot at 0x6804 */
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

    /* Read stage2 (LBA 1, 2 sectors) into 0x7000 via int 0x13 AH=42h.
     * es:bx = 0x0700:0x0000 = 0x7000. */
    movb $0x80, %dl              /* raw hard disk */
    movw $0x0700, %ax
    movw %ax, %es
    xorw %bx, %bx
    movw $dap, %si
    movb $0x42, %ah
    int  $0x13
    jc   fail

    /* Far-return to 0x0700:0x0000 to execute stage2. */
    pushw $0x0700
    pushw $0x0000
    lretw

fail:
    hlt
    jmp  fail

dap:
    .byte 0x10                  /* packet size */
    .byte 0                     /* reserved */
    .word 2                     /* sectors: stage2 is two sectors (0x7000..0x7400) */
    .word 0x0000                /* offset */
    .word 0x0700                /* segment -> 0x7000 */
    .long 1                     /* LBA low = 1 (stage2) */
    .long 0                     /* LBA high */

. = 0x7DBE
/* Partition table: one active partition starting at LBA 1 (the whole disk
 * image is the BerryOS boot area).  The MBR only needs a valid active entry
 * so SeaBIOS accepts this disk as bootable. */
part0:
    .byte 0x80                  /* bootable */
    .byte 0x00, 0x02, 0x00      /* CHS start (H=0, S=2, C=0) */
    .byte 0x0C                  /* partition type: FAT32 LBA */
    .byte 0xFE, 0xFF, 0xFF      /* CHS end */
    .long 1                     /* LBA start = 1 */
    .long 0x00007FFF            /* size in sectors (16 MiB image) */
part1:
    .fill 16, 1, 0
part2:
    .fill 16, 1, 0
part3:
    .fill 16, 1, 0

. = 0x7DFE
.word 0xAA55                    /* boot signature */
