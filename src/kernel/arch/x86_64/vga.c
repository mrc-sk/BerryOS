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

static uint16_t* const vga = (uint16_t*)0xB8000;
static int row = 0;
static int col = 0;

/* In graphics mode the legacy text-framebuffer aperture at 0xB8000 is unsafe
 * to write (it aliases VGA memory and can corrupt kernel data), so text-buffer
 * writes are skipped once the linear framebuffer is bound.  The framebuffer
 * text console (fbcon) renders shell output instead, driven from sys_write. */
static int text_ok(void){ return g_fb.vbase == 0; }

void vga_init(void){
    for (int i = 0; i < 80 * 25; i++)
        vga[i] = 0x0F20;   /* white-on-black space */
    row = 0;
    col = 0;
}

void vga_putc(char c){
    if (c == '\n'){
        col = 0;
        row = (row + 1) % 25;
        return;
    }
    if (c == '\b'){
        if (col == 0){
            if (row > 0){ row--; col = 79; }
        } else {
            col--;
        }
        if (text_ok()) vga[row * 80 + col] = 0x0F20;
        return;
    }
    if (text_ok())
        vga[row * 80 + col] = (uint16_t)(0x0F00 | (uint8_t)c);
    col++;
    if (col >= 80){
        col = 0;
        row = (row + 1) % 25;
    }
}

void vga_puts(const char* s){
    while (*s){
        vga_putc(*s++);
    }
}

void vga_clear(void){
    for (int i = 0; i < 80 * 25; i++)
        vga[i] = 0x0F20;   /* white-on-black space */
    row = 0;
    col = 0;
}
