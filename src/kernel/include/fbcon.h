/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Copyright (C) mrc-sk and imjumping
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See LICENSE for the full license text and the additional
 * non-commercial restriction terms that apply to this software.
 */
#ifndef BERRYOS_FBCON_H
#define BERRYOS_FBCON_H

#include "berryos.h"

/* Framebuffer text console (M4 graphics).  Routes the kernel's existing
 * character stream (vga_putc/vga_puts, driven by serial_puts + the shell)
 * onto the linear framebuffer, so boot logs and the interactive shell stay
 * visible once VBE switches the display out of text mode. */
void fbcon_init(void);
void fbcon_putc(char c);
void fbcon_write(const char* s, int n);
void fbcon_set_cursor(int cx, int cy);
void fbcon_clear(void);

#endif /* BERRYOS_FBCON_H */
