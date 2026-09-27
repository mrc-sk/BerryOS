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
#include "fbcon.h"

extern const uint8_t font8x8[95 * 8];

/* Cell metrics (8x8 glyph + 1px spacing, scaled by g_scale for readability). */
static int g_cw    = 8;
static int g_ch    = 8;
static int g_scale = 2;       /* render each font pixel as scale x scale block */
static int g_cellw = 18;      /* 8*scale + scale */
static int g_cellh = 18;

static int      g_cx   = 0;
static int      g_cy   = 0;
static int      g_cols = 0;
static int      g_rows = 0;
static uint32_t g_fg   = 0xD8DCE0;
static uint32_t g_bg   = 0x0E1116;
static int      g_on   = 0;
static int      g_vtop = 0;   /* first scanline of the console band */
static int      g_vh   = 0;   /* console band height in px (0 = full screen) */

void fbcon_set_viewport(int top, int height){
    g_vtop = top;
    g_vh   = height;
}

void fbcon_init(void){
    if (!g_fb.vbase) return;
    g_on   = 1;
    g_cx   = 0;
    g_cy   = 0;
    if (g_vh <= 0 || g_vtop < 0 || g_vtop + g_vh > (int)g_fb.height){
        g_vtop = 0;
        g_vh   = (int)g_fb.height;
    }
    g_cols = (int)(g_fb.width  / g_cellw);
    g_rows = g_vh / g_cellh;
    fb_set_font(font8x8, g_cw, g_ch);
    fb_fill_rect(0, g_vtop, (int)g_fb.width, g_vh, g_bg);
}

void fbcon_set_cursor(int cx, int cy){
    g_cx = cx;
    g_cy = cy;
}

void fbcon_clear(void){
    if (!g_on) return;
    /* Only our own band -- fb_clear() would wipe the whole screen, including
     * the GUI owned by gui_task. */
    fb_fill_rect(0, g_vtop, (int)g_fb.width, g_vh, g_bg);
    g_cx = 0;
    g_cy = 0;
}

/* Scroll the console up by one text cell.  The blit moves whole scanlines in
 * bytes using the real pitch, so it is correct at any framebuffer depth -- a
 * uint32_t row copy would silently assume 32bpp and shred a 15/16/24bpp
 * surface (this is the same class of bug that garbled the display). */
static void scroll(void){
    uint8_t* fb = (uint8_t*)g_fb.vbase;
    uint64_t rowbytes = g_fb.pitch;
    int y, x;
    /* Scroll only inside the console band; the title bar above it and the GUI
     * below it must not move. */
    for (y = g_vtop + g_cellh; y < g_vtop + g_vh; y++){
        uint8_t* dst = fb + (uint64_t)(y - g_cellh) * rowbytes;
        uint8_t* src = fb + (uint64_t)y * rowbytes;
        for (x = 0; x < (int)rowbytes; x++) dst[x] = src[x];
    }
    /* Clear the vacated bottom row of our band through the bpp-aware primitive. */
    fb_fill_rect(0, g_vtop + g_vh - g_cellh, (int)g_fb.width, g_cellh, g_bg);
}

static void draw_glyph(int cx, int cy, char c){
    int base_x = cx * g_cellw;
    int base_y = g_vtop + cy * g_cellh;
    int ry, rx, sy, sx;
    int gi = (unsigned char)c - 32;
    if (gi < 0 || gi > 94) gi = '?' - 32;
    for (ry = 0; ry < g_ch; ry++){
        uint8_t row = font8x8[gi * g_ch + ry];
        for (rx = 0; rx < g_cw; rx++){
            uint32_t col = (row & (0x80 >> rx)) ? g_fg : g_bg;
            int px = base_x + rx * g_scale;
            int py = base_y + ry * g_scale;
            for (sy = 0; sy < g_scale; sy++)
                for (sx = 0; sx < g_scale; sx++)
                    fb_put_pixel(px + sx, py + sy, col);
        }
    }
}

void fbcon_putc(char c){
    if (!g_on) return;
    if (c == '\n'){ g_cx = 0; g_cy++; }
    else if (c == '\r'){ g_cx = 0; }
    else if (c == '\b'){ if (g_cx > 0) g_cx--; }
    else if (c == '\t'){ g_cx = (g_cx + 4) & ~3; }
    else if (c == ' '){ g_cx++; }
    else { draw_glyph(g_cx, g_cy, c); g_cx++; }

    if (g_cx >= g_cols){ g_cx = 0; g_cy++; }
    if (g_cy >= g_rows){ scroll(); g_cy = g_rows - 1; }
}

void fbcon_write(const char* s, int n){
    int i;
    for (i = 0; i < n; i++) fbcon_putc(s[i]);
}
