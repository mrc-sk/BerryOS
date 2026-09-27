#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Copyright (C) mrc-sk and imjumping
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option) any
# later version. See LICENSE for the full license text and the additional
# non-commercial restriction terms that apply to this software.
"""Headless smoke test for the BerryOS shell.

Boots QEMU with no window, types a scripted session into the shell through the
QEMU monitor's `sendkey` (so real scan codes go through the real PS/2 driver),
captures a screen dump after each step and asserts on the transcript.

The transcript is easy to find because serial_putc() writes to the QEMU
debugcon port 0xE9 rather than COM1 (see src/kernel/arch/x86_64/serial.c), so
`-debugcon file:...` is what actually records everything sys_write() emits.

Usage:
    python tools/shell_smoke.py            # against build/disk.img (BerryFS works)
    python tools/shell_smoke.py iso        # against build/berryos.iso (no disk,
                                           # so the package commands are skipped)

Exit status is non-zero if any expected marker is missing from the transcript.
"""
import os
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
QEMU = os.environ.get("QEMU", r"C:\Program Files\qemu\qemu-system-x86_64.exe")
PORT = 55603

# The shell announces itself through sys_write, so this lands in the debugcon
# log.  (It is also the text that appears in the desktop's Terminal window.)
READY = "in a window of the Bui desktop"

KEYMAP = {c: c for c in "abcdefghijklmnopqrstuvwxyz0123456789"}
KEYMAP.update({c: "shift-" + c.lower() for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"})
KEYMAP.update({" ": "spc", "\n": "ret", "/": "slash", "-": "minus",
               ".": "dot", "?": "shift-slash"})

# (label, what to type, marker that must appear afterwards)
COMMON = [
    ("help",       "?\n",   "commands take any unique prefix"),
    ("memory",     "sap\n", "MiB free"),
    ("tasks",      "gro\n", "prio"),
    ("uptime",     "sea\n", "season: up"),
    ("ambiguous",  "s\n",   "ambiguous: 's' could be"),
    ("unknown",    "zzz\n", "unknown: zzz"),
]

# The desktop window manager, exercised through the shell (SYS_DESKTOP).
DESKTOP = [
    ("pane",       "pane\n",              "applications (pane open"),
    ("pane open",  "pane open Bui Demo\n", "raised Bui Demo"),
    ("pane list",  "pane\n",              "Bui Playground"),
    ("pane close", "pane close\n",        "closed the topmost window"),
    ("gfx in win", "bloom\n",             "bloomed"),
]

# Only meaningful when a real disk is behind BerryFS: booting the ISO has no
# IDE disk, so BerryFS reports itself unavailable and nothing can be stored.
DISK_ONLY = [
    ("basket",     "bas\n",  "selftest.txt"),
]

NO_DISK_ONLY = [
    ("basket",     "bas\n",          "BerryFS not readable"),
    ("till",       "till\n",         "no disk to turn"),
    ("plant",      "plant x.txt hi\n", "open failed"),
]

PACKAGES = [
    ("weave open",  "weave\n",                    "ctrl+X save"),
    ("weave line1", "say hello from a package\n", None),
    ("weave save",  "\x18",                       None),
    ("weave name",  "smoke\n",                    "woven '/smoke'"),
    ("run package", "/smoke\n",                   "hello from a package"),
    ("clash",       "weave\n",                    None),
    ("clash body",  "say nope\n",                 None),
    ("clash save",  "\x18",                       None),
    ("clash name",  "basket\n",                   "is a builtin command"),
    ("clash retry", "smoke2\n",                   "woven '/smoke2'"),
    ("list",        "/\n",                        "/smoke"),
    ("drop",        "unweave smoke2\n",           "unwoven '/smoke2'"),
]


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "disk"
    if mode == "iso":
        img = os.path.join(BUILD, "berryos.iso")
        drive = "file=%s,format=raw,if=ide,media=cdrom" % img
        boot = "d"
    else:
        img = os.path.join(BUILD, "disk.img")
        drive = "file=%s,format=raw,if=ide" % img
        boot = "c"
    if not os.path.isfile(img):
        sys.exit("[shell_smoke] missing image: %s (run build.ps1 first)" % img)

    shot_dir = os.path.join(BUILD, "shell_smoke")
    os.makedirs(shot_dir, exist_ok=True)
    klog = os.path.join(shot_dir, "transcript.log")
    errlog = os.path.join(shot_dir, "qemu.err")

    errf = open(errlog, "wb")
    proc = subprocess.Popen(
        [QEMU, "-cpu", "max", "-m", "256", "-drive", drive, "-boot", boot,
         "-vga", "std", "-display", "none",
         "-monitor", "tcp:127.0.0.1:%d,server,nowait" % PORT,
         "-debugcon", "file:%s" % klog,
         "-no-reboot"],
        stdout=errf, stderr=subprocess.STDOUT)

    sock = None
    for _ in range(150):
        try:
            sock = socket.create_connection(("127.0.0.1", PORT), timeout=1.0)
            break
        except OSError:
            time.sleep(0.1)
    if sock is None:
        proc.kill()
        sys.exit("[shell_smoke] could not reach the QEMU monitor")

    def mon(cmd):
        sock.sendall((cmd + "\n").encode())
        time.sleep(0.02)

    def transcript():
        try:
            with open(klog, "rb") as f:
                return f.read().decode("latin-1")
        except OSError:
            return ""

    def keys(text, gap=0.05):
        for ch in text:
            if ch == "\x18":
                mon("sendkey ctrl-x")
            elif ch == "\x03":
                mon("sendkey ctrl-c")
            else:
                mon("sendkey " + KEYMAP[ch])
            time.sleep(gap)
        time.sleep(0.35)

    def snapshot(name):
        mon("screendump %s" % os.path.join(shot_dir, name + ".ppm").replace("\\", "/"))
        time.sleep(0.3)

    for _ in range(250):
        if READY in transcript():
            break
        time.sleep(0.1)
    if READY not in transcript():
        proc.kill()
        sys.exit("[shell_smoke] the shell never came up")

    if mode == "iso":
        # No IDE disk behind the CD-ROM: BerryFS correctly reports itself
        # unavailable, so only the honest-failure branches can be checked.
        steps = list(COMMON) + NO_DISK_ONLY + list(DESKTOP)
        print("[shell_smoke] booting the ISO: no IDE disk, so BerryFS is "
              "unavailable -- checking the failure paths instead")
    else:
        steps = list(COMMON) + DISK_ONLY + PACKAGES + list(DESKTOP)

    failures = []
    mark = len(transcript())
    for i, (label, seq, want) in enumerate(steps):
        keys(seq)
        snapshot("%02d_%s" % (i, label.replace(" ", "_")))
        if want is not None:
            seen = transcript()[mark:]
            ok = want in seen
            print("  %-14s %s" % (label, "ok" if ok else "MISSING -> %r" % want))
            if not ok:
                failures.append((label, want))

    time.sleep(0.5)
    snapshot("99_final")
    try:
        sock.sendall(b"quit\n")
    except Exception:
        pass
    try:
        proc.wait(timeout=10)
    except Exception:
        proc.kill()
    errf.close()

    with open(os.path.join(shot_dir, "session.txt"), "w", encoding="utf-8") as f:
        f.write(transcript()[mark:])

    print()
    print("=== shell session ===")
    print(transcript()[mark:])
    print("screenshots + transcript: %s" % shot_dir)

    if failures:
        print("[shell_smoke] FAILED: %d marker(s) missing" % len(failures))
        return 1
    print("[shell_smoke] PASS (%d steps)" % len(steps))
    return 0


if __name__ == "__main__":
    sys.exit(main())
