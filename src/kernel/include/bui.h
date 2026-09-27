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
#ifndef BERRYOS_BUI_H
#define BERRYOS_BUI_H

#include "berryos.h"

/* =====================================================================
 * Bui -- Berry User Interface.
 *
 * Bui is both the OS's rendering layer and a tiny declarative language that
 * describes an interface.  Everything the desktop paints goes through the
 * bui_* primitives below (so there is exactly one place that knows about
 * clipping, fonts and pixel packing), and windows that were declared in Bui
 * text are drawn by bui_draw() with no widget-specific C code involved.
 *
 * The language is line based.  Tokens are separated by spaces; a token may be
 * double quoted to keep spaces.  A line whose first token starts with '#' is a
 * comment.  Directives:
 *
 *   bui <ver>                        magic, first line of a standalone file
 *   theme <key> <#rrggbb>            key: wall panel ink accent termfg termbg
 *                                         face facehi
 *   grid <x> <y> <dx> <dy>           desktop icon grid (desktop.bui only)
 *   window "<title>" x y w h         the window this interface lives in
 *   rect   x y w h   <#rrggbb>
 *   label  x y       "<text>" <#rrggbb>
 *   button x y w h   "<text>" <#rrggbb> [action]
 *   term   x y w h                   live terminal surface
 *
 * An action is a colon-verb followed by arguments:
 *   :close                 close the window
 *   :say <text...>         append a line to the terminal surface
 *   :fill <#rrggbb>        recolour the first `rect` of the same interface
 *   :open <app>            launch another application by name
 *
 * Coordinates are relative to the window's client area, so the same interface
 * definition renders identically wherever the user drags the window.
 * =================================================================== */

#define BUI_MAX_NODES 16
#define BUI_MAX_TEXT  48

#define BUI_RECT   1
#define BUI_LABEL  2
#define BUI_BUTTON 3
#define BUI_TERM   4

struct bui_theme {
    uint32_t wall;      /* desktop background            */
    uint32_t panel;     /* title bars, taskbar           */
    uint32_t ink;       /* default text                  */
    uint32_t accent;    /* highlights, borders           */
    uint32_t term_fg;   /* terminal surface foreground   */
    uint32_t term_bg;   /* terminal surface background   */
    uint32_t face;      /* widget face                   */
    uint32_t face_hi;   /* widget face under the pointer */
};

struct bui_node {
    int      kind;
    int      x, y, w, h;
    uint32_t color;
    char     text[BUI_MAX_TEXT];     /* label / button caption */
    char     action[BUI_MAX_TEXT];   /* ":close", ":say ...", ... */
};

struct bui_doc {
    struct bui_theme th;
    char     title[BUI_MAX_TEXT];
    int      has_window;
    int      win_x, win_y, win_w, win_h;
    int      has_grid;                       /* icon grid, desktop.bui only */
    int      grid_x, grid_y, grid_dx, grid_dy;
    int      n;
    struct bui_node node[BUI_MAX_NODES];
};

void bui_theme_default(struct bui_theme* t);
void bui_doc_init(struct bui_doc* d);                /* empty doc + default theme */
int  bui_parse_line(struct bui_doc* d, const char* line);   /* 0 ok, -1 bad line */
int  bui_parse(struct bui_doc* d, const char* src);         /* whole definition  */

/* The node the point lands on, topmost first; -1 when nothing is there. */
int  bui_pick(const struct bui_doc* d, int ox, int oy, int px, int py);
const struct bui_node* bui_first(const struct bui_doc* d, int kind);
int  bui_count(const struct bui_doc* d);

/* Render every node except BUI_TERM (the desktop owns that surface, because it
 * paints live text into it).  `hot` is the node index under the pointer, or -1. */
void bui_draw(const struct bui_doc* d, int ox, int oy, int hot);

/* ---- rendering primitives (all clipped to the current clip rect) ---- */
void bui_clip(int x, int y, int w, int h);
void bui_clip_reset(void);
/* Save/restore the clip so a caller can narrow it for one window's content and
 * then carry on.  bui_clip_narrow intersects instead of replacing, which is
 * what keeps a widget from painting outside its own window when the enclosing
 * repaint region is bigger than the window. */
void bui_clip_save(int* st);
void bui_clip_restore(const int* st);
int  bui_clip_narrow(int x, int y, int w, int h);   /* 0 = nothing left visible */
int  bui_hit(int x, int y, int w, int h);   /* rect intersects the clip */
int  bui_inside(int x, int y);              /* point inside the clip    */
void bui_px(int x, int y, uint32_t rgb);
void bui_fill(int x, int y, int w, int h, uint32_t rgb);
void bui_frame(int x, int y, int w, int h, uint32_t rgb);
void bui_text(int x, int y, uint32_t fg, const char* s, int scale);
void bui_text_c(int x, int y, uint32_t fg, const char* s, int scale);  /* centred */
void bui_cell(int x, int y, char c, uint32_t fg, uint32_t bg, int scale);
int  bui_text_w(const char* s, int scale);
int  bui_cell_w(int scale);

#endif /* BERRYOS_BUI_H */
