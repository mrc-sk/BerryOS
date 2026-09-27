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
 * Drawing primitives for a linear framebuffer at the depth the VBE actually
 * programmed (15 / 16 / 24 / 32bpp).  Geometry comes from fb_bind(), which the
 * bootloader fills in from the real VBE mode info -- never a hardcoded guess.
 * Until then vbase is NULL and every primitive is a no-op guarded by a single
 * check.  The font is pluggable so the WM can register a real glyph set
 * without touching this file.
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
    uint64_t size;

    /* Only direct-colour depths we know how to pack.  A palettized mode
     * (4/8bpp) would need the VBE DAC palette programmed too; rather than draw
     * garbage we refuse to bind and the kernel stays on the text console.
     * This matters on real hardware, where mode 0x411B is not guaranteed. */
    if (bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32){
        serial_puts("  fb: unsupported bpp ");
        serial_hex(bpp);
        serial_puts(" -- staying in text mode\r\n");
        return;
    }

    size = (uint64_t)pitch * h;
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
    serial_puts(" pitch=");
    serial_hex(pitch);
    serial_puts(" bpp=");
    serial_hex(bpp);
    serial_puts("\r\n");
}

void fb_set_font(const uint8_t* font, int cw, int ch){
    g_font = font;
    g_font_cw = cw;
    g_font_ch = ch;
}

/* Pack a 0x00RRGGBB colour into the framebuffer at the depth the VBE actually
 * programmed.  Every draw primitive (fill/draw rect, clear, glyphs, scaled
 * text) funnels through here, so supporting the real bpp in this one place
 * makes the whole graphics stack portable.
 *
 * Channel layouts follow the VBE direct-colour convention used by every BIOS,
 * VM and GPU we can realistically meet:
 *   15bpp -> RGB555   : R[10:14] G[5:9]  B[0:4]   2 bytes/px
 *   16bpp -> RGB565   : R[11:15] G[5:10] B[0:4]   2 bytes/px
 *   24bpp -> RGB888   : {B, G, R} in memory        3 bytes/px
 *   32bpp -> XRGB8888 : 0x00RRGGBB dword           4 bytes/px
 *
 * Assuming "always 32bpp" is what produced the garbled/striped screen: QEMU's
 * SeaBIOS reports 15bpp, so writing 4 bytes per pixel mis-strided every row. */
static void put_raw(int x, int y, uint32_t rgb){
    uint8_t* row = (uint8_t*)g_fb.vbase + (uint64_t)y * g_fb.pitch;
    uint32_t r = (rgb >> 16) & 0xFF;
    uint32_t g = (rgb >> 8)  & 0xFF;
    uint32_t b = (rgb >> 0)  & 0xFF;

    switch (g_fb.bpp){
    case 32:
        ((uint32_t*)row)[x] = rgb;                              /* 0x00RRGGBB */
        break;
    case 24: {
        uint8_t* p = row + (uint64_t)x * 3;
        p[0] = (uint8_t)b;
        p[1] = (uint8_t)g;
        p[2] = (uint8_t)r;
        break;
    }
    case 16:                                                    /* RGB565 */
        ((uint16_t*)row)[x] = (uint16_t)(((r >> 3) << 11) |
                                         ((g >> 2) << 5)  |
                                          (b >> 3));
        break;
    case 15:                                                    /* RGB555 */
        ((uint16_t*)row)[x] = (uint16_t)(((r >> 3) << 10) |
                                         ((g >> 3) << 5)  |
                                          (b >> 3));
        break;
    default:
        break;   /* fb_bind() never accepts anything else */
    }
}

/* Counterpart to put_raw(): read a pixel back as 0x00RRGGBB.  Used by the boot
 * self-test, and needed so the test compares like-for-like at any depth. */
uint32_t fb_get_pixel(int x, int y){
    uint8_t* row;
    uint32_t v, r, g, b;
    if (!g_fb.vbase) return 0;
    if (x < 0 || (uint32_t)x >= g_fb.width)  return 0;
    if (y < 0 || (uint32_t)y >= g_fb.height) return 0;
    row = (uint8_t*)g_fb.vbase + (uint64_t)y * g_fb.pitch;

    switch (g_fb.bpp){
    case 32:
        return ((uint32_t*)row)[x] & 0x00FFFFFF;
    case 24: {
        uint8_t* p = row + (uint64_t)x * 3;
        return ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[0];
    }
    case 16:
        v = ((uint16_t*)row)[x];
        r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F;
        return ((r << 3) << 16) | ((g << 2) << 8) | (b << 3);
    case 15:
        v = ((uint16_t*)row)[x];
        r = (v >> 10) & 0x1F; g = (v >> 5) & 0x1F; b = v & 0x1F;
        return ((r << 3) << 16) | ((g << 3) << 8) | (b << 3);
    default:
        return 0;
    }
}

void fb_put_pixel(int x, int y, uint32_t rgb){
    if (!g_fb.vbase) return;
    if (x < 0 || (uint32_t)x >= g_fb.width)  return;
    if (y < 0 || (uint32_t)y >= g_fb.height) return;
    put_raw(x, y, rgb);
}

/* Fill one scanline with a solid colour.
 *
 * This exists because fb_fill_rect() used to call fb_put_pixel() per pixel:
 * a full-screen fill at 1280x1024 is 1.3 M function calls, each re-checking
 * bounds and re-switching on the depth.  With the Bui desktop repainting the
 * wallpaper, the windows and the taskbar, that added up to almost a second of
 * visible stall per repaint under emulation.  Packing the colour ONCE per
 * scanline and then writing words removes both the call overhead and the
 * per-pixel switch. */
static void fill_row(uint8_t* row, int x, int w, uint32_t rgb){
    uint32_t r = (rgb >> 16) & 0xFF;
    uint32_t g = (rgb >> 8)  & 0xFF;
    uint32_t b = (rgb >> 0)  & 0xFF;
    int i;

    switch (g_fb.bpp){
    case 32: {
        uint32_t* p = (uint32_t*)row + x;
        for (i = 0; i < w; i++) p[i] = rgb;
        break;
    }
    case 24: {
        uint8_t* p = row + (uint64_t)x * 3;
        for (i = 0; i < w; i++){ p[0] = (uint8_t)b; p[1] = (uint8_t)g; p[2] = (uint8_t)r; p += 3; }
        break;
    }
    case 16: {
        uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        uint16_t* p = (uint16_t*)row + x;
        for (i = 0; i < w; i++) p[i] = v;
        break;
    }
    case 15: {
        uint16_t v = (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
        uint16_t* p = (uint16_t*)row + x;
        for (i = 0; i < w; i++) p[i] = v;
        break;
    }
    default:
        break;   /* fb_bind() never accepts anything else */
    }
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t rgb){
    int j, ex = x + w, ey = y + h;
    if (!g_fb.vbase) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (ex > (int)g_fb.width)  ex = (int)g_fb.width;
    if (ey > (int)g_fb.height) ey = (int)g_fb.height;
    if (ex <= x || ey <= y) return;
    for (j = y; j < ey; j++)
        fill_row((uint8_t*)g_fb.vbase + (uint64_t)j * g_fb.pitch, x, ex - x, rgb);
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

uint32_t fb_width(void){  return g_fb.width;  }
uint32_t fb_height(void){ return g_fb.height; }

/* Like fb_draw_string but each font pixel is rendered as a scale x scale block,
 * so text stays legible at high framebuffer resolutions. */
void fb_draw_string_scaled(int x, int y, uint32_t fg, uint32_t bg, const char* s, int scale){
    int cx = x;
    if (!g_fb.vbase || !g_font || scale < 1) return;
    while (s && *s){
        char c = *s++;
        if (c == '\n'){ y += g_font_ch * scale; cx = x; continue; }
        if (c < 32 || c > 126) c = '?';
        const uint8_t* gl = &g_font[(c - 32) * g_font_ch];
        int base_px = cx;
        for (int ry = 0; ry < g_font_ch; ry++){
            uint8_t row = gl[ry];
            for (int rx = 0; rx < g_font_cw; rx++){
                uint32_t col = (row & (0x80 >> rx)) ? fg : bg;
                int px = base_px + rx * scale;
                int y2 = y + ry * scale;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        fb_put_pixel(px + sx, y2 + sy, col);
            }
        }
        cx += (g_font_cw + 1) * scale;
    }
}
