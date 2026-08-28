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
"""One-shot toolchain installer for BerryOS (Windows, via winget).

Installs QEMU (run/test), LLVM/Clang (freestanding compile + link), and NASM
(optional assembler). Run this once, then `make run` from the project root.

    python tools/setup_toolchain.py
"""
import subprocess
import sys


PACKAGES = [
    "SoftwareFreedomConservancy.QEMU",  # qemu-system-x86_64
    "LLVM.LLVM",                        # clang / ld.lld / llvm-objcopy
    "NASM.NASM",                        # optional assembler
]


def run(cmd):
    print("+ " + cmd)
    rc = subprocess.run(cmd, shell=True).returncode
    return rc


def main():
    print("Installing BerryOS toolchain via winget ...")
    for pkg in PACKAGES:
        rc = run(
            "winget install --accept-package-agreements "
            "--accept-source-agreements -e " + pkg
        )
        if rc != 0:
            print("  [warn] package %s returned %d (you may install it manually)" % (pkg, rc))
    print("Done. Close and reopen your terminal, then run: make run")


if __name__ == "__main__":
    sys.exit(main())
