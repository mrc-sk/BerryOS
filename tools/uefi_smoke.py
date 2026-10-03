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
"""Boot BerryOS under UEFI firmware (OVMF) and assert it reaches the shell.

This is the counterpart to desktop_smoke.py for the UEFI path: the legacy
bootloader's VBE probe is impossible there, so the only way to know whether
src/boot/x86_64/efi.S works is to run real firmware against it.

What it checks, in order:
  1. the EFI stub ran at all            -> "[EFI] BerryOS UEFI stub"
  2. it got a framebuffer from GOP      -> "fb: bound LFB"  (not a blank box)
  3. the synthesized E820 map arrived   -> "e820: N entries" with N > 0
  4. the disk is reachable              -> "[BFS] mounted"
  5. the desktop came up and the shell  -> "berry>"

Usage:  uefi_smoke.py [esp.img] [disk.img]
"""
import os
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
QEMU = r"C:\Program Files\qemu\qemu-system-x86_64.exe"
OVMF = r"C:\Program Files\qemu\share\edk2-x86_64-code.fd"
SHOT_DIR = os.path.join(BUILD, "uefi_smoke")
PORT = 4557

CHECKS = [
    ("EFI stub",        "[EFI] BerryOS UEFI stub"),
    ("GOP framebuffer", "fb: bound LFB"),
    ("E820 synthesised", None),          # handled specially (needs a count)
    ("BerryFS mounted", "[BFS] mounted"),
    ("shell up",        "berry> "),
]


def main():
    esp = sys.argv[1] if len(sys.argv) > 1 else os.path.join(BUILD, "esp.img")
    disk = sys.argv[2] if len(sys.argv) > 2 else os.path.join(BUILD, "disk.img")
    for p in (esp, disk):
        if not os.path.isfile(p):
            sys.exit("[uefi_smoke] missing %s" % p)
    if not os.path.isfile(OVMF):
        sys.exit("[uefi_smoke] OVMF firmware not found: %s" % OVMF)

    os.makedirs(SHOT_DIR, exist_ok=True)
    klog = os.path.join(SHOT_DIR, "transcript.log")
    if os.path.exists(klog):
        os.remove(klog)
    errf = open(os.path.join(SHOT_DIR, "qemu.err"), "wb")

    cmd = [QEMU,
           "-machine", "q35",
           "-drive", "if=pflash,format=raw,unit=0,file=%s,readonly=on" % OVMF,
           "-m", "512",
           # disk.img FIRST: the AHCI driver takes the first port it finds, and
           # if that were the ESP it would helpfully format the very thing it
           # booted from.
           "-drive", "file=%s,format=raw,if=ide,index=0" % disk,
           "-drive", "file=%s,format=raw,if=ide,index=1" % esp,
           "-vga", "std",
           "-display", "none",
           "-monitor", "tcp:127.0.0.1:%d,server,nowait" % PORT,
           "-debugcon", "file:%s" % klog,
           "-no-reboot"]
    proc = subprocess.Popen(cmd, stdout=errf, stderr=subprocess.STDOUT)
    time.sleep(14)

    # A frame, so a human can confirm the desktop really rendered.
    try:
        s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
        s.sendall(b"screendump " + os.path.join(SHOT_DIR, "uefi_desktop.ppm").encode() + b"\n")
        time.sleep(2)
        s.close()
    except Exception as e:
        print("  (screendump skipped: %s)" % e)

    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    if not os.path.exists(klog):
        sys.exit("[uefi_smoke] no debugcon output -- the VM never booted")

    with open(klog, "r", errors="replace") as f:
        log = f.read()

    ok = True
    for name, needle in CHECKS:
        if needle is None:
            found = any(("e820: %d entries" % n) in log for n in range(1, 33))
            print("  %-18s %s" % (name, "ok" if found else "FAIL"))
            ok = ok and found
            continue
        hit = needle in log
        print("  %-18s %s" % (name, "ok" if hit else "FAIL"))
        ok = ok and hit

    print("\nlog + screenshot: %s" % SHOT_DIR)
    if not ok:
        print("---- log tail ----")
        print("\n".join(log.splitlines()[-25:]))
        sys.exit("[uefi_smoke] FAIL")
    print("[uefi_smoke] PASS")


if __name__ == "__main__":
    main()
