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
"""Assemble BerryOS into a bootable UEFI application (bootx64.efi).

src/boot/x86_64/efi.S carries its own PE32+ headers, and the kernel is not read
from disk -- it is APPENDED to the stub at KERNEL_RVA and copied to 0x100000 by
the stub itself.  So this tool only has to:

    1. assemble efi.S with -DKERNEL_BYTES=<real kernel size>
    2. objcopy to a flat binary (the PE headers make this the whole image)
    3. append kernel.bin at KERNEL_RVA (padding the gap with zeros)

The flat binary must start at file offset 0 == RVA 0, which is what
src/boot/x86_64/boot.ld gives us (". = 0").

Usage: mkefi.py <clang> <ld.lld> <objcopy> <efi.S> <kernel.bin> <out.efi>
"""
import os
import subprocess
import sys

# Must match KERNEL_RVA in src/boot/x86_64/efi.S.
KERNEL_RVA = 0x40000


def run(cmd, what):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("[mkefi] %s failed:\n%s\n%s" % (what, r.stdout, r.stderr))
    return r.stdout


def main():
    if len(sys.argv) != 7:
        sys.exit("usage: mkefi.py <clang> <ldlld> <objcopy> <efi.S> <kernel.bin> <out.efi>")
    clang, ldlld, objcopy, src, kern, out = sys.argv[1:7]
    for p in (src, kern):
        if not os.path.isfile(p):
            sys.exit("[mkefi] missing input: %s" % p)

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    boot_dir = os.path.dirname(os.path.abspath(src))
    ld_script = os.path.join(boot_dir, "boot.ld")
    build = os.path.dirname(os.path.abspath(out))
    obj = os.path.join(build, "efi.o")
    elf = os.path.join(build, "efi.elf")
    stub = os.path.join(build, "_efi_stub.bin")

    kern_size = os.path.getsize(kern)
    kern_bytes = (kern_size + 511) // 512 * 512

    run([clang, "-target", "x86_64-none-elf", "-c", src,
         "-DKERNEL_BYTES=%d" % kern_bytes, "-o", obj], "assemble")
    run([ldlld, "-T", ld_script, "-o", elf, obj], "link")
    run([objcopy, "-O", "binary", elf, stub], "objcopy")

    with open(stub, "rb") as f:
        image = bytearray(f.read())
    with open(kern, "rb") as f:
        kdata = f.read()

    if len(image) > KERNEL_RVA:
        sys.exit("[mkefi] stub is %d bytes, exceeds KERNEL_RVA 0x%X -- raise it "
                 "in efi.S" % (len(image), KERNEL_RVA))
    if len(image) < KERNEL_RVA:
        image += b"\x00" * (KERNEL_RVA - len(image))
    image += kdata
    pad = (512 - (len(image) % 512)) % 512
    image += b"\x00" * pad

    with open(out, "wb") as f:
        f.write(image)
    print("[mkefi] wrote %s (%d bytes: stub %d + kernel %d)"
          % (out, len(image), KERNEL_RVA, kern_size))


if __name__ == "__main__":
    main()
