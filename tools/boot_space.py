#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Report how many bytes are still free in each 512-byte boot sector.

The boot sectors carry hard `. = 0x7DEE` / `. = 0x71EE` overflow asserts, so
once code runs past that point the build fails.  That is a coarse signal: this
script prints the *exact* headroom (bytes of zero padding between the last real
byte of code and the assert address), which is what you need when deciding
whether a new feature still fits.

Usage:  python tools/boot_space.py
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")

# (file, origin of the sector, address of the overflow assert)
TARGETS = [
    ("boot.bin", 0x7C00, 0x7DFE),   # no DAP in the ISO variant, only 0xAA55
    ("stage2.bin", 0x7000, 0x73EE), # two sectors: DAP lives at the very end
    ("mbr.bin", 0x7C00, 0x7DBE),   # partition table starts here
]


def report(name, org, limit):
    path = os.path.join(BUILD, name)
    if not os.path.isfile(path):
        print("%-12s missing" % name)
        return
    with open(path, "rb") as f:
        data = f.read()
    # objcopy -O binary emits the whole section, so the file starts with `org`
    # bytes of zero padding; the real 512-byte sector is the tail.  (tools/
    # make_iso.py relies on this too: `boot[-512:]`.)
    if len(data) < 512:
        print("%-12s image is %d bytes (too small)" % (name, len(data)))
        return
    data = data[-512:]
    off = limit - org
    # Only look at the region before the assert; the DAP / partition table and
    # the 0xAA55 signature live at/after it.
    region = data[:off]
    last = 0
    for i, b in enumerate(region):
        if b:
            last = i
    free = off - (last + 1)
    print("%-12s code ends at 0x%04X, assert at 0x%04X -> %d bytes free"
          % (name, org + last + 1, limit, free))


def main():
    for name, org, limit in TARGETS:
        report(name, org, limit)


if __name__ == "__main__":
    main()
