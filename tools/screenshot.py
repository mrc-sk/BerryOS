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
"""Boot BerryOS headless, screenshot the VGA screen, and save as PNG.

Useful when the QEMU window flashes/closes: this verifies what is actually
displayed on screen without needing a window.
"""
import subprocess
import time
import os
import re
import struct
import zlib
import sys

qemu = r"C:\Program Files\qemu\qemu-system-x86_64.exe"
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
img = os.path.join(root, "build", "disk.img")
outdir = os.path.join(root, "build")
os.makedirs(outdir, exist_ok=True)

ppm = os.path.join(outdir, "shot.ppm")
png = os.path.join(outdir, "berryos-screen.png")

args = [
    qemu, "-cpu", "max",
    "-drive", "file=%s,format=raw,if=ide" % img,
    "-boot", "c",
    "-serial", "null",
    "-vga", "std",
    "-display", "none",
    "-no-reboot",
    "-no-shutdown",
    "-monitor", "stdio",
]

p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.PIPE, cwd=root)
time.sleep(2.0)

try:
    p.stdin.write(("screendump %s\n" % ppm.replace("\\", "/")).encode())
    p.stdin.flush()
    time.sleep(0.8)
    p.stdin.write(b"quit\n")
    p.stdin.flush()
    out, err = p.communicate(timeout=6)
except Exception:
    p.kill()
    out, err = p.communicate()

if not os.path.exists(ppm):
    raw = out.decode("latin-1", errors="replace")
    clean = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", raw)
    print("screendump failed. Monitor output:")
    print(clean[-1200:])
    sys.exit(1)

# Convert PPM (P6) to PNG (pure stdlib).
with open(ppm, "rb") as f:
    data = f.read()

# parse PPM header: P6 <w> <h> <maxval> <raw>
m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
if not m:
    print("unexpected PPM header")
    sys.exit(1)
w, h, maxv = int(m.group(1)), int(m.group(2)), int(m.group(3))
pixel = data[m.end():]

def write_png(path, w, h, rgb):
    def chunk(tag, payload):
        c = struct.pack(">I", len(payload)) + tag + payload
        c += struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff)
        return c
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)  # 8-bit RGB
    raw = b""
    for y in range(h):
        raw += b"\x00" + bytes(rgb[y * w * 3:(y + 1) * w * 3])
    idat = zlib.compress(raw, 6)
    pngdata = (sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) +
               chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(pngdata)

write_png(png, w, h, pixel[: w * h * 3])
print("screenshot saved: %s (%dx%d)" % (png, w, h))
