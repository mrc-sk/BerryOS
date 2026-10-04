#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Run tools/vbe_probe.asm under QEMU and decode what the firmware reports.

The point of this tool is to settle the VESAModeInfoBlock field offsets by
observation.  They are not consistent across firmware revisions, boot.S in this
project reads XResolution from 0x12 and YResolution from 0x14, and dumping a
text mode shows 80 / 40 / 16 actually living at 0x10 / 0x12 / 0x19 -- so the
shipped guesses are off by two bytes.  A wrong offset is silent: you read a
bitfield and get a plausible-looking framebuffer address, and the screen is
black rather than reporting an error.

Usage: vbe_probe.py [extra qemu args...]
"""
import os
import re
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NASM = r"C:\Program Files\NASM\nasm.exe"
QEMU = r"C:\Program Files\qemu\qemu-system-x86_64.exe"
OUT = os.path.join(ROOT, "build", "vbe_probe")

# Offsets as actually observed (SeaBIOS, VBE 3.0), with the alternatives kept
# around so a different firmware can be compared against the same dump.
LAYOUTS = {
    "observed": {"lfb": 0x40, "x": 0x10, "y": 0x12, "bpp": 0x19, "bpl": 0x32},
    "boot.S":   {"lfb": 0x28, "x": 0x12, "y": 0x14, "bpp": 0x14, "bpl": 0x40},
    "vbe2":     {"lfb": 0x10, "x": 0x12, "y": 0x14, "bpp": 0x19, "bpl": 0x32},
}

ROW = re.compile(
    r"^([0-9A-F]{4})\s+([0-9A-F]{4})\s+([0-9A-F]{4})\s+([0-9A-F]{4})\s+"
    r"([0-9A-F]{2})\s+([0-9A-F]{4})\s+([0-9A-F]{2})\s+([0-9A-F]{2})$")
DUMP = re.compile(r"^([0-9A-F]{2}):\s+((?:[0-9A-F]{2}\s+){16})")


def main():
    os.makedirs(OUT, exist_ok=True)
    asm = os.path.join(ROOT, "tools", "vbe_probe.asm")
    binp = os.path.join(OUT, "vbe_probe.bin")
    log = os.path.join(OUT, "probe.log")

    r = subprocess.run([NASM, "-f", "bin", asm, "-o", binp],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("[probe] nasm failed:\n%s" % r.stderr)

    if os.path.exists(log):
        os.remove(log)

    # A 4 KiB image is not a floppy geometry QEMU accepts; pad to 1.44 MB.
    with open(binp, "rb") as f:
        raw = f.read()
    with open(binp, "wb") as f:
        f.write(raw + b"\x00" * (1474560 - len(raw)))

    cmd = [QEMU, "-drive", "file=%s,format=raw,if=floppy" % binp,
           "-display", "none",
           "-debugcon", "file:%s" % log, "-no-reboot", "-monitor", "none"]
    cmd += sys.argv[1:]
    p = subprocess.Popen(cmd)
    time.sleep(6)
    if p.poll() is None:
        p.terminate()
        try:
            p.wait(timeout=5)
        except Exception:
            p.kill()

    if not os.path.exists(log) or os.path.getsize(log) == 0:
        sys.exit("[probe] no debugcon output; nothing to decode")
    text = open(log, errors="replace").read()

    print("=== raw log ===")
    print(text)

    print("\n=== modes with a linear framebuffer, >= 640x400 ===")
    n = 0
    for line in text.splitlines():
        m = ROW.match(line.strip())
        if not m:
            continue
        mode, attr, x, y, bpp, l32, b28, b40 = [int(v, 16) for v in m.groups()]
        n += 1
        print("  mode %03X  attr=%04X  %4dx%-4d %2dbpp   "
              "LinBytesPerScanLine@0x32=%-5d [0x28]=%02X [0x40]=%02X"
              % (mode, attr, x, y, bpp, l32, b28, b40))
    if not n:
        print("  (none parsed)")

    print("\n=== raw mode-info block (of the first graphics mode) ===")
    raw2 = bytearray(128)
    seen = False
    for line in text.splitlines():
        m = DUMP.match(line.strip())
        if m:
            off = int(m.group(1), 16)
            raw2[off:off + 16] = bytes(int(v, 16) for v in m.group(2).split())
            seen = True
    if not seen:
        print("  (no dump found)")
        return
    for base in range(0, 0x80, 0x10):
        print("  %02X: %s" % (base, " ".join("%02X" % b for b in raw2[base:base + 16])))

    print("\n=== layouts applied to that dump ===")
    for name, L in LAYOUTS.items():
        lfb = struct.unpack_from("<I", raw2, L["lfb"])[0]
        x = struct.unpack_from("<H", raw2, L["x"])[0]
        y = struct.unpack_from("<H", raw2, L["y"])[0]
        bpp = raw2[L["bpp"]]
        bpl = struct.unpack_from("<H", raw2, L["bpl"])[0]
        sane = 0 < x < 16384 and 0 < y < 16384 and bpp in (15, 16, 24, 32)
        print("  %-9s lfb@0x%02X=0x%08X  x=%-5d y=%-5d bpp=%-3d stride=%-5d  %s"
              % (name, L["lfb"], lfb, x, y, bpp, bpl,
                 "plausible" if sane else "WRONG"))


if __name__ == "__main__":
    main()
