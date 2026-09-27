/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Copyright (C) mrc-sk and imjumping
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, version 3 of the License, or (at your option) any later
 * version. See the LICENSE file for the full license text and the additional
 * non-commercial restriction terms that apply to this software.
 */
#ifndef BERRYOS_DESKTOP_H
#define BERRYOS_DESKTOP_H

#include "berryos.h"

/* The BerryOS desktop (M5).
 *
 * Full-screen window manager: wallpaper, icon grid, taskbar and movable,
 * closable windows.  Everything is painted through the Bui renderer, and Bui
 * text is what defines an interface -- the desktop's own palette and icon grid
 * come from desktop.bui on BerryFS, and programs bring their window with them
 * in their .bppg file.
 *
 * The command-line shell does not disappear: it now runs *inside a window*.
 * sys_write() is routed to the terminal surface (tcon.h) instead of the old
 * framebuffer console, and the desktop paints that surface into the Terminal
 * window each frame -- which is what makes the shell movable and closable. */

void desktop_bind(void);          /* claim the screen; call right after fb_bind() */
int  desktop_active(void);        /* 1 while the desktop owns the screen */
void desktop_seed(void);          /* plant desktop.bui + sample apps on a disk */
void desktop_task(void* arg);

/* The drawing canvas: the client area of the focused CANVAS window, in
 * absolute screen coordinates.  SYS_GFX_* draws here instead of scribbling
 * over the whole screen, so the old user-space graphics demo now lands inside
 * a window that can be moved, closed and covered.
 *
 * The drawing itself goes into a real offscreen buffer (see
 * desktop_canvas_*), not straight onto the framebuffer: otherwise the next
 * repaint of that window -- which happens whenever the pointer drags it or
 * the shell prints a line -- would erase what the program painted. */
int  desktop_canvas(int* x, int* y, int* w, int* h);
void desktop_canvas_fill(int x, int y, int w, int h, uint32_t color);
void desktop_canvas_text(int x, int y, uint32_t fg, uint32_t bg, const char* s);
void desktop_canvas_clear(uint32_t color);
int  desktop_canvas_w(void);
int  desktop_canvas_h(void);

/* Implementation of SYS_DESKTOP.  Interrupts must be handled by the caller. */
#define DESK_CTL_LIST   1
#define DESK_CTL_OPEN   2   /* buf = application name -> 0 ok, -1 unknown */
#define DESK_CTL_CLOSE  3   /* close the focused window */
long desktop_ctl(int op, char* buf, unsigned long n);

#endif /* BERRYOS_DESKTOP_H */
