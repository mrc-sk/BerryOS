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
"""Build a bootable El Torito ISO for BerryOS using pycdlib.

BerryOS ships a self-written 512-byte boot sector + a raw kernel image.  The
bootloader expects the kernel raw image to live in RAM right after the boot
sector (at 0x7E00).  We therefore boot via El Torito *no-emulation*: the BIOS
loads the whole (boot + kernel) combined image straight into RAM at 0x7C00
(boot sector) and 0x7E00 (kernel).  The bootloader then needs NO int 0x13 disk
reads -- the 64-bit stage copies 0x7E00 -> 0x100000 and jumps to it.

This avoids the floppy-emulation read pitfalls (CHS/LBA geometry, drive
number, SeaBIOS "code 0005") that broke earlier ISO builds.  pycdlib generates
a fully spec-compliant ISO-9660 + El Torito structure.

Usage:  python tools/make_iso.py [boot.bin] [kernel.bin] [out.iso]
"""
import os
import sys

try:
    import pycdlib
except ImportError:
    sys.exit("[make_iso] pycdlib is required (pip install pycdlib)")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BOOT = os.path.join(ROOT, "build", "boot.bin")
DEFAULT_KERN = os.path.join(ROOT, "build", "kernel.bin")
DEFAULT_ISO = os.path.join(ROOT, "build", "berryos.iso")


def build_combined_image(boot_path, kern_path):
    """Return boot_sector (512B) + kernel, kernel padded to a 512-byte boundary.

    The combined image is what the BIOS loads into RAM for El Torito
    no-emulation: byte 0..511 = boot sector (executed at 0x7C00), the rest is
    the raw kernel image (seen by the bootloader at 0x7E00).
    """
    with open(boot_path, "rb") as f:
        boot = f.read()
    with open(kern_path, "rb") as f:
        kern = f.read()
    if len(boot) < 512:
        sys.exit("[make_iso] boot.bin too small")
    boot_sector = boot[-512:]  # clang pads the front (org 0x7C00); real sector is last 512B
    if boot_sector[510] != 0x55 or boot_sector[511] != 0xAA:
        sys.exit("[make_iso] boot.bin missing 0xAA55 signature")
    pad = (512 - (len(kern) % 512)) % 512
    img = boot_sector + kern + b"\x00" * pad
    return img


def main():
    boot = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_BOOT
    kern = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_KERN
    iso = sys.argv[3] if len(sys.argv) > 3 else DEFAULT_ISO
    for p in (boot, kern):
        if not os.path.isfile(p):
            sys.exit("[make_iso] missing input: %s" % p)

    combined = build_combined_image(boot, kern)
    img_path = os.path.join(os.path.dirname(iso), "_combined.img")
    with open(img_path, "wb") as f:
        f.write(combined)
    n_sect = len(combined) // 512

    iso_obj = pycdlib.PyCdlib()
    iso_obj.new()
    # Put the combined image into the ISO namespace first (fs_path, iso_path),
    # then point El Torito's boot file at the in-ISO path.
    iso_obj.add_file(img_path, "/BERRYOS.BIN")
    # El Torito NO-EMULATION: the BIOS loads the whole boot image (boot+kernel)
    # into RAM at 0x7C00.  boot_load_size is the number of 512-byte sectors.
    iso_obj.add_eltorito(
        "/BERRYOS.BIN",
        media_name="noemul",
        platform_id=0,            # x86
        bootable=True,
        boot_load_size=n_sect,
        boot_load_seg=0,          # 0 -> 0x7C0 (standard boot segment)
    )
    os.makedirs(os.path.dirname(iso), exist_ok=True)
    iso_obj.write(iso)
    iso_obj.close()
    print("[make_iso] wrote %s (%d bytes)" % (iso, os.path.getsize(iso)))
    print("[make_iso] El Torito no-emulation, boot load size %d sectors" % n_sect)


if __name__ == "__main__":
    main()
