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

    LBA 0 : MBR (with partition table, loaded by BIOS at 0x7C00)
    LBA 1 : stage2 bootloader (loaded by MBR to 0x20000)
    LBA 2+: raw kernel image (copied by stage2 to 0x7E00)

Usage: mkimage.py <mbr.bin> <stage2.bin> <kernel.bin> <disk.img>
"""
import os
import sys


def read_last_512(path, what):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 512:
        sys.exit("error: %s too small (%d bytes, need >= 512)" % (what, len(data)))
    return data[-512:]


def main():
    if len(sys.argv) != 5:
        sys.exit("usage: mkimage.py <mbr.bin> <stage2.bin> <kernel.bin> <disk.img>")

    mbr_path, st2_path, kern_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

    mbr = read_last_512(mbr_path, "mbr.bin")
    st2 = read_last_512(st2_path, "stage2.bin")
    with open(kern_path, "rb") as f:
        kern = f.read()

    for what, blob in (("mbr", mbr), ("stage2", st2)):
        if blob[510] != 0x55 or blob[511] != 0xAA:
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

    img[0:512] = mbr
    img[512:1024] = st2
    img[1024:1024 + len(kern)] = kern

    with open(out_path, "wb") as f:
        f.write(img)

    print("wrote %s (mbr=%d stage2=%d kernel=%d)" % (out_path, len(mbr), len(st2), len(kern)))


if __name__ == "__main__":
    main()
