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
"""Spawn QEMU with -monitor stdio, dump VGA text memory, verify BerryOS text."""
import subprocess
import time
import re
import os

qemu = r"C:\Program Files\qemu\qemu-system-x86_64.exe"
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
img = os.path.join(root, "build", "disk.img")

args = [
    qemu, "-cpu", "max",
    "-drive", "file=%s,format=raw,if=ide" % img,
    "-boot", "c",
    "-serial", "file:%s" % os.path.join(root, "build", "serial.log"),
    "-vga", "std",
    "-display", "none",
    "-no-reboot",
    "-no-shutdown",
    "-monitor", "stdio",
]

p = subprocess.Popen(
    args,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    cwd=root,
)

time.sleep(2.0)  # let it boot
try:
    p.stdin.write(b"xp /80bx 0xb8000\n")
    p.stdin.flush()
    time.sleep(0.5)
    p.stdin.write(b"quit\n")
    p.stdin.flush()
    out, err = p.communicate(timeout=5)
except Exception:
    p.kill()
    out, err = p.communicate()

raw = out.decode("latin-1", errors="replace")
# strip ANSI escapes
clean = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", raw)

print("=== VGA memory dump (0xB8000, bytes) ===")
for line in clean.splitlines():
    if "b8000" in line.lower() or ": " in line and "0x" in line:
        print(line)

# Parse hex bytes after 'b8000:' and decode as VGA text (byte pairs char/attr)
m = re.search(r"0*b8000:\s+((?:0x[0-9a-fA-F]{2}\s*)+)", clean)
if m:
    bytes_ = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", m.group(1))]
    chars = []
    for i in range(0, min(len(bytes_), 160), 2):
        c = bytes_[i]
        chars.append(chr(c) if 32 <= c < 127 else ".")
    text = "".join(chars)
    print("=== decoded first 80 chars ===")
    print(text)
    if "BerryOS" in text:
        print("RESULT: PASS - VGA text output OK")
    else:
        print("RESULT: no BerryOS text found in first 80 cells")
else:
    print("RESULT: could not parse VGA dump")

if err:
    print("=== qemu stderr ===")
    print(err[:500])
