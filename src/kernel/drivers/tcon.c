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
#include "berryos.h"
#include "tcon.h"

/* See tcon.h for the contract.  Nothing in here touches hardware: this is a
 * pure text buffer, which is what makes the shell's output survive the
 * terminal window being closed, moved or dragged off-screen. */

static char g_line[TCON_MAX_ROWS][TCON_MAX_COLS + 1];
static int  g_cols = TCON_MAX_COLS;   /* active width  (<= TCON_MAX_COLS) */
static int  g_rows = TCON_MAX_ROWS;   /* active height (<= TCON_MAX_ROWS) */
static int  g_r, g_c;                 /* cursor row / column */
static int  g_used;                   /* rows that hold anything (incl. cursor) */
static unsigned long g_seq;

static void clear_rows(void){
    int i, j;
    for (i = 0; i < TCON_MAX_ROWS; i++)
        for (j = 0; j <= TCON_MAX_COLS; j++)
            g_line[i][j] = 0;
}

void tcon_reset(void){
    clear_rows();
    g_cols = TCON_MAX_COLS;
    g_rows = TCON_MAX_ROWS;
    g_r = g_c = g_used = 0;
    g_seq++;
}

void tcon_resize(int cols, int rows){
    if (cols < 8)  cols = 8;
    if (rows < 2)  rows = 2;
    if (cols > TCON_MAX_COLS) cols = TCON_MAX_COLS;
    if (rows > TCON_MAX_ROWS) rows = TCON_MAX_ROWS;
    clear_rows();
    g_cols = cols;
    g_rows = rows;
    g_r = g_c = g_used = 0;
    g_seq++;
}

void tcon_clear(void){
    int r;
    for (r = 0; r < TCON_MAX_ROWS; r++) g_line[r][0] = 0;
    g_r = g_c = g_used = 0;
    g_seq++;
}

int tcon_cols(void){ return g_cols; }
int tcon_rows(void){ return g_used; }

int tcon_row_len(int r){
    int n = 0;
    if (r < 0 || r >= TCON_MAX_ROWS) return 0;
    if (r == g_r) return g_c;              /* the cursor row is not NUL-padded */
    while (n < g_cols && g_line[r][n]) n++;
    return n;
}

const char* tcon_row(int r){
    static const char empty = 0;
    if (r < 0 || r >= TCON_MAX_ROWS) return &empty;
    return g_line[r];
}

int tcon_cursor(int* row, int* col){
    if (row) *row = g_r;
    if (col) *col = g_c;
    return 1;
}

unsigned long tcon_seq(void){ return g_seq; }

/* Scroll one row up.  Only the active band moves; anything below it is dead. */
static void scroll_up(void){
    int r, c;
    for (r = 0; r < g_rows - 1; r++)
        for (c = 0; c <= TCON_MAX_COLS; c++)
            g_line[r][c] = g_line[r + 1][c];
    for (c = 0; c <= TCON_MAX_COLS; c++) g_line[g_rows - 1][c] = 0;
    g_r = g_rows - 1;
    g_used = g_rows;
}

static void newline(void){
    g_c = 0;
    g_r++;
    if (g_r >= g_rows) scroll_up();
    else if (g_r + 1 > g_used) g_used = g_r + 1;
}

void tcon_putc(char c){
    g_seq++;
    if (c == '\r'){ g_c = 0; return; }
    if (c == '\n'){ newline(); return; }
    if (c == '\b'){
        if (g_c > 0){ g_c--; g_line[g_r][g_c] = 0; }
        return;
    }
    if (c == '\t'){
        int stop = (g_c + 4) & ~3;
        if (stop > g_cols) stop = g_cols;
        while (g_c < stop) g_line[g_r][g_c++] = ' ';
        g_line[g_r][g_c] = 0;
        return;
    }
    if ((unsigned char)c < 32) return;     /* other control chars: ignored */
    if (g_c >= g_cols) return;             /* truncate instead of wrapping */
    g_line[g_r][g_c++] = c;
    g_line[g_r][g_c] = 0;
}

void tcon_write(const char* s, unsigned long n){
    unsigned long i;
    for (i = 0; i < n; i++) tcon_putc(s[i]);
}

void tcon_puts(const char* s){
    while (s && *s) tcon_putc(*s++);
}
