#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Copyright (C) mrc-sk and imjumping
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option) any
# later version. See LICENSE for the full license text and the additional
# non-commercial restriction terms that apply to this software.
"""Report (and sanity-check) how much kernel the self-written bootloader must load.

The bootloader stages the kernel in LOW conventional memory, then copies it up
to 0x100000:

    ISO  (boot.S)             BIOS loads the combined boot+kernel image at 0x7C00,
                              so the kernel lands at 0x7E00.
    disk (boot.S DISK_BOOT)   stage2 reads the kernel sectors into 0x7E00.

Two failure modes are guarded here, both of which used to be silent:

1. TRUNCATION -- the transfer length was hardcoded (96 KiB) and silently cut
   the image once it grew past that.  The boot-splash wordmark alone is
   >100 KiB of .rodata, so the wordmark's tail read back as zeros and rendered
   as a flat block.  The build now measures kernel.bin and injects the real
   size as -DKERNEL_SECTORS (disk path) / -DKERNEL_COPY_BYTES (copy path).

2. LOW-MEMORY OVERRUN -- the staging destination must stay below the Extended
   BIOS Data Area (the BIOS normally puts it at 0x9FC00, the top of the 640 KiB
   conventional block).  A kernel big enough to run into it corrupts BIOS state
   and crashes in a way that looks nothing like a size problem, so we refuse to
   build instead of shipping a bomb.

Usage:
    kern_size.py <kernel.bin>              # sector count (default), rounded up
    kern_size.py <kernel.bin> --bytes      # byte count, rounded up to a sector
    kern_size.py <kernel.bin> --info       # human-readable report
Exit status is non-zero (with a message on stderr) if the image does not fit.
"""
import os
import sys

# Real-mode staging addresses (must match src/boot/x86_64/boot.S).
LOAD_BASE = 0x7E00          # where the kernel sits before the copy to 0x100000
EBDA_BASE = 0x9FC00         # start of the Extended BIOS Data Area
MAX_IMAGE_BYTES = EBDA_BASE - LOAD_BASE      # 0x91E00 = 622,080 bytes (~607 KiB)


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: kern_size.py <kernel.bin> [--bytes|--sectors|--info]")
    path = sys.argv[1]
    if not os.path.isfile(path):
        sys.exit("error: no such file: %s" % path)

    mode = sys.argv[2] if len(sys.argv) > 2 else "--sectors"
    size = os.path.getsize(path)
    sectors = (size + 511) // 512          # round up to whole 512-byte sectors
    staged = sectors * 512

    if staged > MAX_IMAGE_BYTES:
        sys.exit(
            "error: kernel image does not fit in low memory\n"
            "       kernel.bin       : %d bytes (%d sectors, %d bytes staged)\n"
            "       staging window   : 0x%05X..0x%05X = %d bytes (~%d KiB)\n"
            "       overflow         : %d bytes past the EBDA\n"
            "       The bootloader stages the kernel at 0x%05X and the BIOS keeps\n"
            "       its Extended Data Area at 0x%05X; growing past it corrupts the\n"
            "       machine.  Trim .rodata (e.g. shrink the boot-splash logo) or\n"
            "       lower the KB of embedded assets.\n"
            % (size, sectors, staged,
               LOAD_BASE, EBDA_BASE - 1, MAX_IMAGE_BYTES, MAX_IMAGE_BYTES // 1024,
               staged - MAX_IMAGE_BYTES, LOAD_BASE, EBDA_BASE))

    if mode == "--bytes":
        print(staged)
    elif mode == "--sectors":
        print(sectors)
    elif mode == "--info":
        print(
            "kernel.bin        : %d bytes\n"
            "sectors to load   : %d (%d bytes incl. padding)\n"
            "staging window    : 0x%05X..0x%05X (%d bytes, %.1f%% used)\n"
            "copy destination  : 0x100000"
            % (size, sectors, staged,
               LOAD_BASE, EBDA_BASE - 1, MAX_IMAGE_BYTES,
               100.0 * staged / MAX_IMAGE_BYTES)
        )
    else:
        sys.exit("error: unknown mode %r (use --bytes, --sectors or --info)" % mode)


if __name__ == "__main__":
    main()
