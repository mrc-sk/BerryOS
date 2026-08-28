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
"""Pack BerryOS disk image: boot sector at LBA 0, raw kernel at LBA 1."""
import os
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: mkimage.py <boot.bin> <kernel.bin> <disk.img>")

    boot_path, kern_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

    with open(boot_path, "rb") as f:
        boot = f.read()
    with open(kern_path, "rb") as f:
        kern = f.read()

    # Boot image may be padded with leading zeros (flat VMA 0x7C00 layout from
    # the linker). The valid boot sector is the final 512 bytes.
    if len(boot) < 512:
        sys.exit("error: boot.bin too small (%d bytes, need >= 512)" % len(boot))
    if len(boot) != 512:
        boot = boot[-512:]
    if boot[510] != 0x55 or boot[511] != 0xAA:
        sys.exit("error: missing 0xAA55 boot signature in boot.bin")

    size = 16 * 1024 * 1024  # 16 MiB image

    # Preserve any data beyond the kernel region (e.g. a BerryFS filesystem
    # living at sector 4096) so rebuilding the kernel doesn't wipe persistent
    # storage.  Only the boot sector and the kernel image are overwritten.
    if os.path.exists(out_path):
        with open(out_path, "rb") as f:
            existing = f.read()
        if len(existing) < size:
            existing = existing + b"\x00" * (size - len(existing))
        img = bytearray(existing[:size])
    else:
        img = bytearray(size)

    img[0:512] = boot
    img[512:512 + len(kern)] = kern

    with open(out_path, "wb") as f:
        f.write(img)

    print("wrote %s (%d bytes boot, %d bytes kernel)" % (out_path, len(boot), len(kern)))


if __name__ == "__main__":
    main()
