#!/usr/bin/env python3
"""Minimal probe: boot with no interaction, dump two frames 3 s apart, and
report what the taskbar band and a few landmarks actually contain.  Used to
tell "the desktop paints it wrong" apart from "an input step broke it"."""
import os
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
QEMU = os.environ.get("QEMU", r"C:\Program Files\qemu\qemu-system-x86_64.exe")
PORT = 55621
OUT = os.path.join(BUILD, "probe")

sys.path.insert(0, os.path.join(ROOT, "tools"))
from PIL import Image   # noqa: E402


def main():
    os.makedirs(OUT, exist_ok=True)
    klog = os.path.join(OUT, "transcript.log")
    if os.path.isfile(klog):
        os.remove(klog)
    errf = open(os.path.join(OUT, "qemu.err"), "wb")
    proc = subprocess.Popen(
        [QEMU, "-cpu", "max", "-m", "256",
         "-drive", "file=%s,format=raw,if=ide" % os.path.join(BUILD, "disk.img"),
         "-boot", "c", "-vga", "std", "-display", "none",
         "-monitor", "tcp:127.0.0.1:%d,server,nowait" % PORT,
         "-debugcon", "file:%s" % klog, "-no-reboot"],
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
        sys.exit("no monitor")

    def mon(cmd, wait=0.05):
        sock.sendall((cmd + "\n").encode())
        time.sleep(wait)

    def text():
        try:
            with open(klog, "rb") as f:
                return f.read().decode("latin-1")
        except OSError:
            return ""

    for _ in range(300):
        if "in a window of the Bui desktop" in text():
            break
        time.sleep(0.1)
    time.sleep(2.0)

    def shot(name):
        p = os.path.join(OUT, name + ".ppm").replace("\\", "/")
        mon("screendump %s" % p, wait=0.5)
        im = Image.open(p).convert("RGB")
        im.save(os.path.join(OUT, name + ".png"))
        return im

    def row(im, y):
        px = im.load()
        out, prev, start = [], None, 0
        for x in range(im.width):
            c = px[x, y]
            if c != prev:
                if prev is not None and x - start >= 6:
                    out.append((start, x, "#%02X%02X%02X" % prev))
                prev, start = c, x
        out.append((start, im.width, "#%02X%02X%02X" % prev))
        return out

    KEYMAP = {c: c for c in "abcdefghijklmnopqrstuvwxyz0123456789"}
    KEYMAP.update({c: "shift-" + c.lower() for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"})
    KEYMAP.update({" ": "spc", "\n": "ret", "/": "slash", "-": "minus",
                   ".": "dot", "?": "shift-slash"})

    def keys(t, gap=0.06):
        for ch in t:
            mon("sendkey " + KEYMAP[ch], gap)
        time.sleep(0.4)

    def health(im, label):
        px = im.load()
        y = im.height - 19
        # Buttons are counted on their top-border row: an unbroken 156 px run,
        # whereas the face is chopped up by the caption text.
        border = im.height - 29
        btns = runs = 0
        for x in range(im.width):
            if px[x, border] in ((0x2C, 0x3A, 0x4E), (0x6C, 0xA8, 0xFF)):
                runs += 1
            else:
                if runs >= 150:
                    btns += 1
                runs = 0
        if runs >= 150:
            btns += 1
        panel = sum(1 for x in range(200, im.width - 200) if px[x, y] == (0x1B, 0x24, 0x30))
        wall = sum(1 for x in range(200, im.width - 200) if px[x, y] == (0x10, 0x18, 0x20))
        print("  %-18s buttons=%d  panel=%d  wall=%d" % (label, btns, panel, wall))

    STEPS = [
        ("help",       "?\n"),
        ("uptime",     "sea\n"),
        ("pane",       "pane\n"),
        ("open bui",   "pane open Bui Demo\n"),
        ("pane 2",     "pane\n"),
        ("bloom",      "bloom\n"),
        ("pane 3",     "pane\n"),
    ]

    im = shot("t0")
    print("--- no input (H=%d) ---" % im.height)
    health(im, "baseline")

    for i, (label, seq) in enumerate(STEPS):
        keys(seq)
        health(shot("s%d_%s" % (i, label.replace(" ", "_"))), label)

    im = shot("t1")
    print("--- after 3 s idle ---")
    health(im, "idle")

    sock.sendall(b"quit\n")
    try:
        proc.wait(timeout=10)
    except Exception:
        proc.kill()
    errf.close()
    print("images in", OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
