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
#ifndef BERRYOS_TCON_H
#define BERRYOS_TCON_H

#include "berryos.h"

/* Terminal console: a scroll-back text surface owned by the desktop.
 *
 * Before the desktop existed the kernel's character stream (sys_write) went
 * straight into the framebuffer console, which owned a fixed band of the
 * screen.  Now the shell runs inside a movable, closable WINDOW, so the
 * characters are first collected here and the desktop paints this surface
 * into the terminal window's client area on every frame.
 *
 * The surface is a simple grid of rows of characters: no attributes, no
 * fonts here -- just text plus a cursor.  Unprintable control characters are
 * honoured the same way the old console did ('\n', '\r', '\b', '\t'), and a
 * line longer than the row is truncated rather than wrapped, so screen
 * coordinates map 1:1 onto grid coordinates (which keeps the cursor honest).
 */
#define TCON_MAX_COLS 120
#define TCON_MAX_ROWS 80

void tcon_reset(void);                       /* clear + default geometry */
void tcon_resize(int cols, int rows);        /* clear + new geometry */
void tcon_putc(char c);
void tcon_write(const char* s, unsigned long n);
void tcon_puts(const char* s);
void tcon_clear(void);                       /* clear contents, keep geometry */

int  tcon_cols(void);
int  tcon_rows(void);                        /* rows in use (for the painter) */
int  tcon_row_len(int r);                    /* characters actually on row r */
const char* tcon_row(int r);                 /* NUL-terminated, never NULL */
int  tcon_cursor(int* row, int* col);        /* 1 when the cursor is on screen */

/* Bumped on every change, so the desktop can redraw only when text moved. */
unsigned long tcon_seq(void);

#endif /* BERRYOS_TCON_H */
