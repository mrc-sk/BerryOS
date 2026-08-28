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
#include "berryos.h"
#include "fb.h"

/* =====================================================================
 * Framebuffer driver (M4 framework).
 *
 * Drawing primitives for a packed-32bpp linear framebuffer.  The actual
 * device binding (VBE mode set in real mode + fb_bind) lands in the
 * day-2 graphics work; until then vbase is NULL and every primitive is a
 * no-op guarded by a single check.  The font is pluggable so the WM can
 * register a real glyph set without touching this file.
 * =================================================================== */

struct framebuffer g_fb = { 0 };

static const uint8_t* g_font = NULL;
static int g_font_cw = 8;
static int g_font_ch = 8;

void fb_init(void){
    /* vbase stays NULL until fb_bind() is called from VBE setup. */
    serial_puts("  fb: framebuffer framework ready (bind via fb_bind after VBE)\r\n");
}

void fb_bind(uint64_t phys, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp){
    uint64_t size = (uint64_t)pitch * h;
    size = (size + PAGE_SIZE - 1) & ~((uint64_t)PAGE_SIZE - 1);

    /* Map the LFB at a fixed HIGH-HALF virtual address.  This lives in the
     * kernel's upper-half page tables, which every user page directory clones
     * (pgdir_new copies PML4[256..511]), so the framebuffer stays mapped and
     * accessible even while a syscall runs with the *user's* CR3 active.
     * (Mapping at vaddr==phys would land in the low identity range, invisible
     * from user page tables -> page fault on the first fbcon draw.) */
    /* map_region(vaddr, phys, size, flags): bind the high-half FB_VADDR
     * onto the physical LFB.  Order matters -- a swapped call would map
     * va=phys -> phys=FB_VADDR and leave FB_VADDR itself unmapped. */
    map_region(FB_VADDR, phys, size, (int)(PTE_P | PTE_W | PTE_PCD | PTE_PWT));

    g_fb.vbase = (void*)(uintptr_t)FB_VADDR;
    g_fb.width = w;
    g_fb.height = h;
    g_fb.pitch = pitch;
    g_fb.bpp = bpp;

    serial_puts("  fb: bound LFB phys=");
    serial_hex(phys);
    serial_puts(" ");
    serial_hex(w);
    serial_puts("x");
    serial_hex(h);
    serial_puts(" bpp=");
    serial_hex(bpp);
    serial_puts("\r\n");
}

void fb_set_font(const uint8_t* font, int cw, int ch){
    g_font = font;
    g_font_cw = cw;
    g_font_ch = ch;
}

void fb_put_pixel(int x, int y, uint32_t rgb){
    uint32_t* fb;
    int stride;
    if (!g_fb.vbase) return;
    if (x < 0 || (uint32_t)x >= g_fb.width)  return;
    if (y < 0 || (uint32_t)y >= g_fb.height) return;
    fb = (uint32_t*)g_fb.vbase;
    stride = (int)(g_fb.pitch / 4);
    fb[y * stride + x] = rgb;
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t rgb){
    int ex = x + w, ey = y + h, i, j;
    if (!g_fb.vbase) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (ex > (int)g_fb.width)  ex = (int)g_fb.width;
    if (ey > (int)g_fb.height) ey = (int)g_fb.height;
    for (j = y; j < ey; j++)
        for (i = x; i < ex; i++)
            fb_put_pixel(i, j, rgb);
}

void fb_draw_rect(int x, int y, int w, int h, uint32_t rgb){
    fb_fill_rect(x, y, w, 1, rgb);
    fb_fill_rect(x, y + h - 1, w, 1, rgb);
    fb_fill_rect(x, y, 1, h, rgb);
    fb_fill_rect(x + w - 1, y, 1, h, rgb);
}

void fb_clear(uint32_t rgb){
    fb_fill_rect(0, 0, (int)g_fb.width, (int)g_fb.height, rgb);
}

void fb_draw_char(int x, int y, uint32_t fg, uint32_t bg, char c){
    int ry, rx;
    if (!g_fb.vbase) return;
    if (!g_font){
        /* No font yet: draw a filled box as a visible placeholder. */
        fb_fill_rect(x, y, g_font_cw, g_font_ch, fg);
        return;
    }
    if (c < 32 || c > 126) c = '?';
    for (ry = 0; ry < g_font_ch; ry++){
        uint8_t row = g_font[(c - 32) * g_font_ch + ry];
        for (rx = 0; rx < g_font_cw; rx++){
            if (row & (0x80 >> rx))
                fb_put_pixel(x + rx, y + ry, fg);
            else if (bg)
                fb_put_pixel(x + rx, y + ry, bg);
        }
    }
}

void fb_draw_string(int x, int y, uint32_t fg, uint32_t bg, const char* s){
    int cx = x;
    if (!g_fb.vbase) return;
    while (*s){
        fb_draw_char(cx, y, fg, bg, *s++);
        cx += g_font_cw + 1;
    }
}
