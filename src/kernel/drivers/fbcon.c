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

/* Cell metrics (8x8 glyph + 1px spacing). */
static int g_cw    = 8;
static int g_ch    = 8;
static int g_cellw = 9;
static int g_cellh = 9;

static int      g_cx   = 0;
static int      g_cy   = 0;
static int      g_cols = 0;
static int      g_rows = 0;
static uint32_t g_fg   = 0xD8DCE0;
static uint32_t g_bg   = 0x0E1116;
static int      g_on   = 0;

void fbcon_init(void){
    if (!g_fb.vbase) return;
    g_on   = 1;
    g_cx   = 0;
    g_cy   = 0;
    g_cols = (int)(g_fb.width  / g_cellw);
    g_rows = (int)(g_fb.height / g_cellh);
    fb_set_font(font8x8, g_cw, g_ch);
    fb_clear(g_bg);
}

void fbcon_set_cursor(int cx, int cy){
    g_cx = cx;
    g_cy = cy;
}

void fbcon_clear(void){
    if (!g_on) return;
    fb_clear(g_bg);
    g_cx = 0;
    g_cy = 0;
}

static void scroll(void){
    uint32_t* fb = (uint32_t*)g_fb.vbase;
    int stride = (int)(g_fb.pitch / 4);
    int y, x;
    for (y = g_cellh; y < (int)g_fb.height; y++){
        uint32_t* dst = &fb[(y - g_cellh) * stride];
        uint32_t* src = &fb[y * stride];
        for (x = 0; x < (int)g_fb.width; x++) dst[x] = src[x];
    }
    for (y = (int)g_fb.height - g_cellh; y < (int)g_fb.height; y++)
        for (x = 0; x < (int)g_fb.width; x++)
            fb[y * stride + x] = g_bg;
}

static void draw_glyph(int cx, int cy, char c){
    int px = cx * g_cellw;
    int py = cy * g_cellh;
    int ry, rx;
    int gi = (unsigned char)c - 32;
    if (gi < 0 || gi > 94) gi = '?' - 32;
    for (ry = 0; ry < g_ch; ry++){
        uint8_t row = font8x8[gi * g_ch + ry];
        for (rx = 0; rx < g_cw; rx++){
            uint32_t col = (row & (0x80 >> rx)) ? g_fg : g_bg;
            fb_put_pixel(px + rx, py + ry, col);
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
