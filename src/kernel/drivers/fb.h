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
#ifndef BERRYOS_FB_H
#define BERRYOS_FB_H

#include "berryos.h"

/* Where the VBE linear framebuffer is mapped in the kernel virtual address
 * space.  It MUST live in the kernel high-half (PML4[256..511], i.e.
 * 0xFFFF800000000000 and up) because pgdir_new() clones exactly that range
 * into every user page directory.  A syscall runs with the *user's* CR3
 * loaded, so the framebuffer has to be reachable from those cloned tables --
 * mapping it at vaddr==phys (low half / PML4[0]) would be invisible from the
 * user pages and fault on the first fbcon draw.  The address below is in
 * PML4[288], safely clear of the PML4[256] 1 GiB identity alias. */
#define FB_VADDR  0xFFFF900000000000ULL

/* Framebuffer driver (M4 framework).
 *
 * A framebuffer is just a linear pixel buffer plus geometry.  The binding to
 * real hardware (a VBE linear framebuffer captured by the bootloader in real
 * mode, day-2 work) happens via fb_bind(), which maps the physical LFB into
 * the kernel address space with cache-disabled attributes.  Until then the
 * drawing primitives are inert (g_fb.vbase == NULL) but fully implemented,
 * so the window manager can be built against this API immediately. */

struct framebuffer {
    void*    vbase;   /* kernel-virtual base (== phys until remapped) */
    uint32_t width;
    uint32_t height;
    uint32_t pitch;   /* bytes per scanline */
    uint32_t bpp;     /* bits per pixel: 15 / 16 / 24 / 32 supported */
};

extern struct framebuffer g_fb;

void fb_init(void);
void fb_bind(uint64_t phys, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp);

/* Plug in an 8xN bitmap font (one byte per row, MSB = leftmost pixel). */
void fb_set_font(const uint8_t* font, int cw, int ch);

void fb_put_pixel(int x, int y, uint32_t rgb);
uint32_t fb_get_pixel(int x, int y);   /* read back as 0x00RRGGBB, any depth */
void fb_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void fb_draw_rect(int x, int y, int w, int h, uint32_t rgb);  /* border only */
void fb_clear(uint32_t rgb);
void fb_draw_char(int x, int y, uint32_t fg, uint32_t bg, char c);
void fb_draw_string(int x, int y, uint32_t fg, uint32_t bg, const char* s);
void fb_draw_string_scaled(int x, int y, uint32_t fg, uint32_t bg, const char* s, int scale);
uint32_t fb_width(void);
uint32_t fb_height(void);

#endif /* BERRYOS_FB_H */
