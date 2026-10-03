#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Copyright (C) mrc-sk and imjumping
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version. See LICENSE for the full license text and the additional
# non-commercial restriction terms that apply to this software.
"""Pack BerryOS raw disk image (two-stage boot):

    LBA 0    : MBR (with partition table, loaded by BIOS at 0x7C00)
    LBA 1..2 : stage2 bootloader (two sectors, loaded by MBR to 0x7000)
    LBA 3+   : raw kernel image (copied by stage2 to 0x7E00)

stage2 grew to two sectors because 494 bytes left no room for the A20 / VBE
portability work; the MBR reads both and the kernel consequently starts one
sector later.  Keep these three numbers in sync with src/boot/x86_64/*.S.

Usage: mkimage.py <mbr.bin> <stage2.bin> <kernel.bin> <disk.img>
"""
import os
import sys

MBR_SECTORS = 1
STAGE2_SECTORS = 2


def read_tail(path, what, nbytes):
    """Return the last `nbytes` of a bootloader image.

    objcopy -O binary emits the whole `.org`-based section, so the files start
    with `org` bytes of zero padding and the real image is the tail.
    """
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < nbytes:
        sys.exit("error: %s too small (%d bytes, need >= %d)" % (what, len(data), nbytes))
    return data[-nbytes:]


def main():
    if len(sys.argv) != 5:
        sys.exit("usage: mkimage.py <mbr.bin> <stage2.bin> <kernel.bin> <disk.img>")

    mbr_path, st2_path, kern_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

    mbr = read_tail(mbr_path, "mbr.bin", MBR_SECTORS * 512)
    st2 = read_tail(st2_path, "stage2.bin", STAGE2_SECTORS * 512)
    with open(kern_path, "rb") as f:
        kern = f.read()

    # The signature sits at the end of each image, so its offset scales with
    # the sector count.
    for what, blob in (("mbr", mbr), ("stage2", st2)):
        if blob[-2] != 0x55 or blob[-1] != 0xAA:
            sys.exit("error: missing 0xAA55 boot signature in %s" % what)

    size = 16 * 1024 * 1024  # 16 MiB image

    # Preserve any data beyond the kernel region (e.g. a BerryFS filesystem
    # living at sector 4096) so rebuilding the kernel doesn't wipe persistent
    # storage.  Only the MBR, stage2 and kernel image are overwritten.
    if os.path.exists(out_path):
        with open(out_path, "rb") as f:
            existing = f.read()
        if len(existing) < size:
            existing = existing + b"\x00" * (size - len(existing))
        img = bytearray(existing[:size])
    else:
        img = bytearray(size)

    st2_off = MBR_SECTORS * 512
    kern_off = st2_off + STAGE2_SECTORS * 512
    img[0:st2_off] = mbr
    img[st2_off:kern_off] = st2
    img[kern_off:kern_off + len(kern)] = kern

    with open(out_path, "wb") as f:
        f.write(img)

    print("wrote %s (mbr=%d stage2=%d kernel=%d)" % (out_path, len(mbr), len(st2), len(kern)))


if __name__ == "__main__":
    main()
