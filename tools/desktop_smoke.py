#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Copyright (C) mrc-sk and imjumping
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the Free
# Software Foundation, version 3 of the License, or (at your option) any later
# version. See LICENSE for the full license text and the additional
# non-commercial restriction terms that apply to this software.
"""Headless smoke test for the BerryOS Bui desktop.

Boots QEMU with no window and checks the desktop three ways, because "did it
draw?" cannot be answered by a log:

  1. PIXELS.  A screendump is analysed with Pillow: the wallpaper, the taskbar
     band, the icon tint and the terminal's text colours each have to appear
     where they belong.  Exact colour matches are valid here because QEMU's
     PPM is RGB888 and the VBE mode is 24bpp, so no rounding happens between
     what the kernel wrote and what the dump holds.
  2. THE KEYBOARD.  Commands are typed with the monitor's `sendkey`, so real
     scan codes go through the real PS/2 driver, and the transcript is read
     from the debugcon log (serial_putc writes port 0xE9, NOT COM1, so
     `-debugcon file:` is what records everything sys_write() emits).
  3. THE MOUSE.  `mouse_move` / `mouse_button` drive the real PS/2 mouse.  The
     pointer is located by finding the only pure-black pixels on screen (the
     cursor's outline -- nothing else in the theme is #000000), and a click on
     a desktop icon is verified by its selection highlight appearing.

Usage:
    python tools/desktop_smoke.py            # against build/disk.img
    python tools/desktop_smoke.py iso        # against build/berryos.iso

The ISO has no IDE disk, so BerryFS is unavailable: the desktop then falls back
to its built-in theme and its four built-in applications (no *.bppg icons), and
the Basket window says so instead of listing files.  That path is worth
checking separately, because it is the only case where desktop.bui cannot be
loaded at all.

Exit status is non-zero if any check fails.
"""
import glob
import os
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
QEMU = os.environ.get("QEMU", r"C:\Program Files\qemu\qemu-system-x86_64.exe")
PORT = 55611

# Colours MUST match the defaults in desktop.c / desktop.bui.
WALL   = (0x10, 0x18, 0x20)
PANEL  = (0x1B, 0x24, 0x30)
ACCENT = (0x6C, 0xA8, 0xFF)
TERMFG = (0xC8, 0xD8, 0xC0)
ICON_T = (0x2F, 0x4A, 0x6E)   # the Terminal icon tint
SEL    = (0x1B, 0x2C, 0x44)   # selected-icon highlight
CURSOR = (0x00, 0x00, 0x00)   # only the pointer uses pure black

KEYMAP = {c: c for c in "abcdefghijklmnopqrstuvwxyz0123456789"}
KEYMAP.update({c: "shift-" + c.lower() for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"})
KEYMAP.update({" ": "spc", "\n": "ret", "/": "slash", "-": "minus",
               ".": "dot", "?": "shift-slash"})

# (label, keys, marker that must appear in the transcript afterwards)
STEPS = [
    ("help",        "?\n",                  "commands take any unique prefix"),
    ("uptime",      "sea\n",                "season: up"),
    ("pane",        "pane\n",               "applications (pane open"),
    ("pane open",   "pane open Bui Demo\n", "raised Bui Demo"),
    ("pane list",   "pane\n",               "Bui Playground"),
    ("bloom",       "bloom\n",              "bloomed into the Canvas window"),
    ("pane list 2", "pane\n",               "Canvas"),
    ("close",       "pane close\n",         "closed the topmost window"),
    ("run bppg",    "pane open Sysinfo\n",  "M5 Bui desktop: window manager"),
    ("basket",      "bas\n",                "hello.bppg"),
]

# Without a disk there are no *.bppg applications and nothing to list.
ISO_STEPS = [
    ("help",        "?\n",                  "commands take any unique prefix"),
    ("pane",        "pane\n",               "Terminal, Basket, Bui Demo, About"),
    ("pane open",   "pane open Bui Demo\n", "raised Bui Demo"),
    ("bloom",       "bloom\n",              "bloomed into the Canvas window"),
    ("close",       "pane close\n",         "closed the topmost window"),
    ("basket",      "bas\n",                "BerryFS not readable"),
]

SCREENSHOTS = {"pane", "pane open", "pane list", "bloom", "run bppg", "basket"}


def main():
    # argv[1] = "disk" | "iso"; argv[2] = optional explicit image path, which
    # lets you test a freshly built ISO when the default one is still held
    # open by something (e.g. a preview process).
    mode = sys.argv[1] if len(sys.argv) > 1 else "disk"
    override = sys.argv[2] if len(sys.argv) > 2 else None
    if mode == "iso":
        img_path = override or os.path.join(BUILD, "berryos.iso")
        drive = "file=%s,format=raw,if=ide,media=cdrom" % img_path
        boot = "d"
    else:
        img_path = override or os.path.join(BUILD, "disk.img")
        drive = "file=%s,format=raw,if=ide" % img_path
        boot = "c"
    if not os.path.isfile(img_path):
        sys.exit("[desktop_smoke] missing %s (run build.ps1 first)" % img_path)
    try:
        from PIL import Image, ImageChops
    except ImportError:
        sys.exit("[desktop_smoke] Pillow is required: pip install Pillow")

    shot_dir = os.path.join(BUILD, "desktop_smoke" + ("_iso" if mode == "iso" else ""))
    os.makedirs(shot_dir, exist_ok=True)
    klog = os.path.join(shot_dir, "transcript.log")
    errlog = os.path.join(shot_dir, "qemu.err")
    if os.path.isfile(klog):
        os.remove(klog)

    errf = open(errlog, "wb")
    proc = subprocess.Popen(
        [QEMU, "-cpu", "max", "-m", "256",
         "-drive", drive, "-boot", boot,
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
        sys.exit("[desktop_smoke] could not reach the QEMU monitor")

    failures = []

    def mon(cmd, wait=0.05):
        sock.sendall((cmd + "\n").encode())
        time.sleep(wait)

    def transcript():
        try:
            with open(klog, "rb") as f:
                return f.read().decode("latin-1")
        except OSError:
            return ""

    def keys(text, gap=0.06):
        for ch in text:
            mon("sendkey " + KEYMAP[ch], gap)
        time.sleep(0.4)

    def shot(name):
        # A full desktop repaint is not instantaneous, and a dump taken in the
        # middle of one shows a half-painted frame.  Give the paint a generous
        # moment to settle before capturing.
        path = os.path.join(shot_dir, name + ".ppm").replace("\\", "/")
        time.sleep(0.5)
        mon("screendump %s" % path, wait=0.6)
        im = Image.open(path)
        im = im.convert("RGB")
        im.save(os.path.join(shot_dir, name + ".png"))
        return im

    def count(im, rgb, box=None):
        region = im.crop(box) if box else im
        cols = region.getcolors(maxcolors=1 << 24)
        for n, c in (cols or []):
            if c == rgb:
                return n
        return 0

    def locate(im, rgb):
        """Bounding box + pixel count of an exactly-`rgb` region.

        A per-channel max (not a luminance conversion) keeps the test exact:
        luminance would fold (0,0,1) into the same bucket as pure black."""
        diff = ImageChops.difference(im, Image.new("RGB", im.size, rgb))
        r, g, b = diff.split()
        mask = ImageChops.lighter(ImageChops.lighter(r, g), b) \
                        .point(lambda v: 255 if v == 0 else 0)
        bbox = mask.getbbox()
        if not bbox:
            return None
        return (bbox, mask.histogram()[255])

    def center(box):
        return ((box[0] + box[2]) // 2, (box[1] + box[3]) // 2)

    # A taskbar button is drawn as a filled rect plus a 1px border, and the
    # border's top line is an unbroken 156 px run -- unlike the face, which the
    # caption chops into little pieces.  So the top border row is where buttons
    # can be counted exactly.
    BTN_BORDER = {(0x2C, 0x3A, 0x4E), (0x6C, 0xA8, 0xFF)}

    def button_runs(im, y):
        """How many taskbar buttons are drawn on scanline y.

        This is what catches a window that appears on screen but never gets its
        taskbar button, or a repaint that never finishes."""
        px = im.load()
        n = runs = 0
        for x in range(im.width):
            if px[x, y] in BTN_BORDER:
                runs += 1
            else:
                if runs >= 150:
                    n += 1
                runs = 0
        if runs >= 150:
            n += 1
        return n

    def check(label, ok, detail=""):
        print("  %-22s %s%s" % (label, "ok" if ok else "FAIL", detail))
        if not ok:
            failures.append(label)

    # ---- wait for the shell to come up (its banner goes via sys_write) ----
    for _ in range(300):
        if "in a window of the Bui desktop" in transcript():
            break
        time.sleep(0.1)
    if "in a window of the Bui desktop" not in transcript():
        proc.kill()
        sys.exit("[desktop_smoke] the shell never reached its prompt")

    time.sleep(1.2)                      # let the first frame settle
    im = shot("01_desktop")
    W, H = im.size
    print("[desktop_smoke] framebuffer %dx%d" % (W, H))

    # ---- 1. pixels ------------------------------------------------------
    print("  pixels")
    wall_box = (0, H - 190, W, H - 40)          # below the windows, above the bar
    n_wall = count(im, WALL, wall_box)
    area = (wall_box[2] - wall_box[0]) * (wall_box[3] - wall_box[1])
    check("wallpaper", n_wall > area * 0.90, " (%d/%d px)" % (n_wall, area))

    bar_box = (0, H - 30, W, H - 6)
    n_bar = count(im, PANEL, bar_box)
    bar_area = (bar_box[2] - bar_box[0]) * (bar_box[3] - bar_box[1])
    check("taskbar", n_bar > bar_area * 0.70, " (%d/%d px)" % (n_bar, bar_area))
    check("taskbar ink", count(im, ACCENT, bar_box) > 20)

    check("icon grid", count(im, ICON_T, (30, 50, 120, 140)) > 500)

    term_box = (200, 80, 1200, 780)
    n_txt = count(im, TERMFG, term_box)
    check("terminal text", n_txt > 500, " (%d px)" % n_txt)

    # ---- 2. keyboard ----------------------------------------------------
    print("  keyboard (%s)" % mode)
    steps = ISO_STEPS if mode == "iso" else STEPS
    mark = len(transcript())
    for i, (label, seq, want) in enumerate(steps):
        keys(seq)
        if label in SCREENSHOTS:
            shot("%02d_%s" % (i + 2, label.replace(" ", "_")))
        seen = transcript()[mark:]
        mark = len(transcript())
        check(label, want in seen, "" if want in seen else " -> %r" % want)

    # The taskbar must show one button per open window.  Right after "bloom"
    # three windows are open in both modes -- Terminal, Bui Playground and
    # Canvas (the ISO differs in its *icons*, not in its windows).
    bloom_shots = sorted(glob.glob(os.path.join(shot_dir, "*_bloom.png")))
    if bloom_shots:
        nbtn = button_runs(Image.open(bloom_shots[0]).convert("RGB"), H - 29)
        check("taskbar buttons", nbtn == 3, " (%d buttons for 3 windows)" % nbtn)

    # ---- 3. mouse -------------------------------------------------------
    # The pointer starts dead centre (the desktop seeds it there).  Find it,
    # move it by a known delta, and check it landed where we asked.
    print("  mouse")
    mon("mouse_move -200 -120", wait=0.40)
    pos = locate(shot("11_cursor_moved"), CURSOR)
    if not pos:
        check("cursor visible", False)
        cx, cy = W // 2, H // 2
    else:
        bbox, npx = pos
        cx, cy = center(bbox)
        check("cursor visible", True, " (%d px at %d,%d)" % (npx, cx, cy))
        check("cursor moved", abs(cx - 440) < 30 and abs(cy - 392) < 30,
              " landed at %d,%d, expected ~440,392" % (cx, cy))

    # Click a desktop icon.  Icons are laid out column-major from the grid
    # origin (40,60) with a 132x128 pitch, so index 3 (About) is the 4th box
    # down the first column: (40,444)..(104,508).
    target = (72, 476)
    mon("mouse_move %d %d" % (target[0] - cx, target[1] - cy), wait=0.40)
    before = count(shot("12_cursor_on_icon"), SEL)
    mon("mouse_button 1", wait=0.25)
    after = count(shot("13_icon_clicked"), SEL)
    mon("mouse_button 0", wait=0.10)
    check("icon click selects", before == 0 and after > 100,
          " (highlight %d -> %d px)" % (before, after))

    # A second click on the same icon within 0.4 s opens the application.  The
    # pair has to run WITHOUT a screendump in between: a dump plus a PNG save
    # takes longer than the double-click window.
    mon("mouse_button 1", wait=0.10)
    mon("mouse_button 0", wait=0.10)
    mon("mouse_button 1", wait=0.10)
    mon("mouse_button 0", wait=0.10)
    time.sleep(0.9)
    mark = len(transcript())
    keys("pane\n")
    check("double click opens", "About BerryOS" in transcript()[mark:])

    time.sleep(0.4)
    shot("99_final")

    try:
        sock.sendall(b"quit\n")
    except Exception:
        pass
    try:
        proc.wait(timeout=10)
    except Exception:
        proc.kill()
    errf.close()

    print()
    print("screenshots + transcript: %s" % shot_dir)
    if failures:
        print("[desktop_smoke] FAILED: %s" % ", ".join(failures))
        return 1
    print("[desktop_smoke] PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
