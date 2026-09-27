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
#include "fb.h"
#include "bui.h"

/* See bui.h for the language.  Two halves live here:
 *   1. a parser that turns Bui text into struct bui_doc (no drawing at all),
 *   2. the only drawing code in the system that knows about clipping.
 * Keeping them in one file is deliberate: the clip rect below is what makes
 * "redraw just this rectangle" possible, and that is a rendering concern. */

extern const uint8_t font8x8[95 * 8];

/* ------------------------------------------------------------------ clip */
static int cl_x, cl_y, cl_w, cl_h;

void bui_clip_reset(void){
    cl_x = 0; cl_y = 0;
    cl_w = (int)fb_width();
    cl_h = (int)fb_height();
}

void bui_clip(int x, int y, int w, int h){
    cl_x = x; cl_y = y; cl_w = w; cl_h = h;
}

void bui_clip_save(int* st){
    st[0] = cl_x; st[1] = cl_y; st[2] = cl_w; st[3] = cl_h;
}

void bui_clip_restore(const int* st){
    cl_x = st[0]; cl_y = st[1]; cl_w = st[2]; cl_h = st[3];
}

int bui_clip_narrow(int x, int y, int w, int h){
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    int cx1 = cl_x + cl_w, cy1 = cl_y + cl_h;
    if (x0 < cl_x) x0 = cl_x;
    if (y0 < cl_y) y0 = cl_y;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    if (x1 <= x0 || y1 <= y0){
        cl_x = cl_y = 0; cl_w = cl_h = 0;
        return 0;
    }
    cl_x = x0; cl_y = y0; cl_w = x1 - x0; cl_h = y1 - y0;
    return 1;
}

int bui_hit(int x, int y, int w, int h){
    if (w <= 0 || h <= 0) return 0;
    if (x >= cl_x + cl_w) return 0;
    if (x + w <= cl_x)    return 0;
    if (y >= cl_y + cl_h) return 0;
    if (y + h <= cl_y)    return 0;
    return 1;
}

int bui_inside(int x, int y){
    return (x >= cl_x && x < cl_x + cl_w && y >= cl_y && y < cl_y + cl_h);
}

/* ------------------------------------------------------------ primitives */
void bui_px(int x, int y, uint32_t rgb){
    if (!bui_inside(x, y)) return;
    fb_put_pixel(x, y, rgb);
}

void bui_fill(int x, int y, int w, int h, uint32_t rgb){
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < cl_x) x0 = cl_x;
    if (y0 < cl_y) y0 = cl_y;
    if (x1 > cl_x + cl_w) x1 = cl_x + cl_w;
    if (y1 > cl_y + cl_h) y1 = cl_y + cl_h;
    if (x1 <= x0 || y1 <= y0) return;
    fb_fill_rect(x0, y0, x1 - x0, y1 - y0, rgb);
}

void bui_frame(int x, int y, int w, int h, uint32_t rgb){
    bui_fill(x, y, w, 1, rgb);
    bui_fill(x, y + h - 1, w, 1, rgb);
    bui_fill(x, y, 1, h, rgb);
    bui_fill(x + w - 1, y, 1, h, rgb);
}

int bui_cell_w(int scale){ return 9 * scale; }      /* 8px glyph + 1px gap */

/* Draw one glyph.  Each font row is emitted as horizontal RUNS rather than
 * one fill per font pixel: a glyph needs ~30 fills this way instead of 64,
 * and each fill is a wide rectangle, which is what fb_fill_rect() is fast at.
 * A 1280x1024 desktop repaint is dominated by glyph runs, so this is the
 * difference between a desktop that snaps and one that visibly stalls. */
static void glyph(int x, int y, char c, uint32_t fg, uint32_t bg, int scale, int opaque){
    int ry, gi;
    if (c < 32 || c > 126) c = '?';
    gi = (int)(unsigned char)c - 32;
    for (ry = 0; ry < 8; ry++){
        uint8_t row = font8x8[gi * 8 + ry];
        int rx = 0;
        while (rx < 8){
            int on = (row & (0x80 >> rx)) != 0;
            int start = rx;
            while (rx < 8 && (((row & (0x80 >> rx)) != 0) == on)) rx++;
            if (on || opaque)
                bui_fill(x + start * scale, y + ry * scale,
                         (rx - start) * scale, scale, on ? fg : bg);
        }
    }
}

void bui_text(int x, int y, uint32_t fg, const char* s, int scale){
    int cx = x;
    if (scale < 1) scale = 1;
    while (s && *s){
        glyph(cx, y, *s++, fg, 0, scale, 0);
        cx += bui_cell_w(scale);
    }
}

int bui_text_w(const char* s, int scale){
    int n = 0;
    if (scale < 1) scale = 1;
    while (s && s[n]) n++;
    if (!n) return 0;
    return n * bui_cell_w(scale) - scale;    /* no gap after the last glyph */
}

void bui_text_c(int x, int y, uint32_t fg, const char* s, int scale){
    bui_text(x - bui_text_w(s, scale) / 2, y, fg, s, scale);
}

/* One terminal cell: opaque background, glyph on top, then the 1px gap stays
 * background so neighbouring glyphs never touch. */
void bui_cell(int x, int y, char c, uint32_t fg, uint32_t bg, int scale){
    int cw = bui_cell_w(scale);
    if (!bui_hit(x, y, cw, cw)) return;
    bui_fill(x, y, cw, cw, bg);
    if (c > 32 && c < 127) glyph(x, y, c, fg, bg, scale, 0);
}

/* ----------------------------------------------------------------- theme */
void bui_theme_default(struct bui_theme* t){
    t->wall    = 0x101820;
    t->panel   = 0x1B2430;
    t->ink     = 0xE6E6E6;
    t->accent  = 0x6CA8FF;
    t->term_fg = 0xC8D8C0;
    t->term_bg = 0x06090C;
    t->face    = 0x27324A;
    t->face_hi = 0x3A4A6E;
}

void bui_doc_init(struct bui_doc* d){
    int i;
    char* p = (char*)d;
    for (i = 0; i < (int)sizeof(struct bui_doc); i++) p[i] = 0;
    bui_theme_default(&d->th);
    /* A sane default window so a document without `window` is still usable. */
    d->win_x = 120; d->win_y = 90; d->win_w = 720; d->win_h = 460;
    d->grid_x = 40; d->grid_y = 60; d->grid_dx = 132; d->grid_dy = 128;
}

/* --------------------------------------------------------------- parsing */
#define BUI_TOK_MAX 12
#define BUI_TOK_LEN 64
static char g_tok[BUI_TOK_MAX][BUI_TOK_LEN];

static int tok_split(const char* s){
    int n = 0;
    while (*s && n < BUI_TOK_MAX){
        int k = 0;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        if (*s == '"'){
            s++;
            while (*s && *s != '"' && k < BUI_TOK_LEN - 1) g_tok[n][k++] = *s++;
            if (*s == '"') s++;
        } else {
            while (*s && *s != ' ' && *s != '\t' && k < BUI_TOK_LEN - 1)
                g_tok[n][k++] = *s++;
        }
        g_tok[n][k] = 0;
        n++;
    }
    return n;
}

static int ieq(const char* a, const char* b){
    while (*a && *b){
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        a++; b++;
    }
    return (*a == 0 && *b == 0);
}

static int num(const char* t){
    int v = 0, neg = 0;
    if (*t == '-'){ neg = 1; t++; }
    while (*t >= '0' && *t <= '9') v = v * 10 + (*t++ - '0');
    return neg ? -v : v;
}

/* "#rrggbb" -> 0x00RRGGBB.  0 means "token is not a colour" (no channel is
 * lost, because an all-black colour still parses as 1). */
static uint32_t col(const char* t){
    uint32_t v = 0;
    int i;
    if (!t || t[0] != '#' || !t[1]) return 0;
    for (i = 1; i <= 6; i++){
        char c = t[i];
        if (!c) return 0;
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return 0;
    }
    if (t[7]) return 0;
    return (v & 0x00FFFFFF) | 0x01000000;   /* tag bit: "parsed" */
}

static uint32_t col_or(const char* t, uint32_t dflt){
    uint32_t v = col(t);
    if (!v) return dflt;
    return v & 0x00FFFFFF;
}

static void copy_str(char* dst, const char* src, int cap){
    int i = 0;
    while (src && src[i] && i < cap - 1){ dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* Join tokens [from..n-1] with single spaces: that is how ":say hello there"
 * survives tokenisation as an action string. */
static void join(char* dst, int from, int n, int cap){
    int i, p = 0;
    for (i = from; i < n && p < cap - 1; i++){
        int k = 0;
        while (g_tok[i][k] && p < cap - 1) dst[p++] = g_tok[i][k++];
        if (i + 1 < n && p < cap - 1) dst[p++] = ' ';
    }
    dst[p] = 0;
}

static int theme_set(struct bui_doc* d, const char* key, const char* val){
    uint32_t c = col_or(val, ~0u);
    if (c == ~0u) return -1;
    if      (ieq(key, "wall"))    d->th.wall = c;
    else if (ieq(key, "panel"))   d->th.panel = c;
    else if (ieq(key, "ink"))     d->th.ink = c;
    else if (ieq(key, "accent"))  d->th.accent = c;
    else if (ieq(key, "termfg"))  d->th.term_fg = c;
    else if (ieq(key, "termbg"))  d->th.term_bg = c;
    else if (ieq(key, "face"))    d->th.face = c;
    else if (ieq(key, "facehi"))  d->th.face_hi = c;
    else return -1;
    return 0;
}

static struct bui_node* node_add(struct bui_doc* d, int kind){
    struct bui_node* nd;
    if (d->n >= BUI_MAX_NODES) return 0;
    nd = &d->node[d->n++];
    nd->kind = kind;
    nd->x = nd->y = nd->w = nd->h = 0;
    nd->color = d->th.ink;
    nd->text[0] = 0;
    nd->action[0] = 0;
    return nd;
}

int bui_parse_line(struct bui_doc* d, const char* line){
    int n;
    const char* k;
    struct bui_node* nd;

    if (!line) return 0;
    n = tok_split(line);
    if (n == 0) return 0;
    k = g_tok[0];
    if (k[0] == '#') return 0;                     /* comment */
    if (ieq(k, "bui")) return 0;                   /* magic line of a file */

    if (ieq(k, "theme")){
        if (n < 3) return -1;
        return theme_set(d, g_tok[1], g_tok[2]);
    }
    if (ieq(k, "grid")){
        if (n < 5) return -1;
        d->has_grid = 1;
        d->grid_x  = num(g_tok[1]);
        d->grid_y  = num(g_tok[2]);
        d->grid_dx = num(g_tok[3]);
        d->grid_dy = num(g_tok[4]);
        return 0;
    }
    if (ieq(k, "window")){
        if (n < 6) return -1;
        copy_str(d->title, g_tok[1], BUI_MAX_TEXT);
        d->has_window = 1;
        d->win_x = num(g_tok[2]);
        d->win_y = num(g_tok[3]);
        d->win_w = num(g_tok[4]);
        d->win_h = num(g_tok[5]);
        return 0;
    }
    if (ieq(k, "rect")){
        if (n < 6) return -1;
        nd = node_add(d, BUI_RECT);
        if (!nd) return -1;
        nd->x = num(g_tok[1]); nd->y = num(g_tok[2]);
        nd->w = num(g_tok[3]); nd->h = num(g_tok[4]);
        nd->color = col_or(g_tok[5], d->th.accent);
        return 0;
    }
    if (ieq(k, "label")){
        if (n < 5) return -1;
        nd = node_add(d, BUI_LABEL);
        if (!nd) return -1;
        nd->x = num(g_tok[1]); nd->y = num(g_tok[2]);
        copy_str(nd->text, g_tok[3], BUI_MAX_TEXT);
        nd->color = col_or(g_tok[4], d->th.ink);
        return 0;
    }
    if (ieq(k, "button")){
        if (n < 7) return -1;
        nd = node_add(d, BUI_BUTTON);
        if (!nd) return -1;
        nd->x = num(g_tok[1]); nd->y = num(g_tok[2]);
        nd->w = num(g_tok[3]); nd->h = num(g_tok[4]);
        copy_str(nd->text, g_tok[5], BUI_MAX_TEXT);
        nd->color = col_or(g_tok[6], d->th.accent);
        if (n > 7) join(nd->action, 7, n, BUI_MAX_TEXT);
        return nd->action[0] == ':' ? 0 : -1;
    }
    if (ieq(k, "term")){
        if (n < 5) return -1;
        nd = node_add(d, BUI_TERM);
        if (!nd) return -1;
        nd->x = num(g_tok[1]); nd->y = num(g_tok[2]);
        nd->w = num(g_tok[3]); nd->h = num(g_tok[4]);
        return 0;
    }
    return -1;                                     /* unknown directive */
}

int bui_parse(struct bui_doc* d, const char* src){
    char line[BUI_TOK_MAX * BUI_TOK_LEN];
    int p = 0, bad = 0;
    if (!src) return -1;
    while (1){
        char c = *src++;
        if (c == '\n' || c == 0){
            line[p] = 0;
            if (bui_parse_line(d, line) != 0) bad = 1;
            p = 0;
            if (c == 0) break;
            continue;
        }
        if (c == '\r') continue;
        if (p < (int)sizeof(line) - 1) line[p++] = c;
    }
    return bad ? -1 : 0;
}

/* ---------------------------------------------------------------- render */
int bui_count(const struct bui_doc* d){ return d->n; }

const struct bui_node* bui_first(const struct bui_doc* d, int kind){
    int i;
    for (i = 0; i < d->n; i++)
        if (d->node[i].kind == kind) return &d->node[i];
    return 0;
}

int bui_pick(const struct bui_doc* d, int ox, int oy, int px, int py){
    int i;
    for (i = d->n - 1; i >= 0; i--){        /* topmost first */
        const struct bui_node* nd = &d->node[i];
        int w = nd->w, h = nd->h;
        if (nd->kind == BUI_LABEL){          /* labels are text, not chrome */
            w = bui_text_w(nd->text, 2);
            h = 18;
        }
        if (w <= 0 || h <= 0) continue;
        if (px >= ox + nd->x && px < ox + nd->x + w &&
            py >= oy + nd->y && py < oy + nd->y + h) return i;
    }
    return -1;
}

void bui_draw(const struct bui_doc* d, int ox, int oy, int hot){
    int i;
    for (i = 0; i < d->n; i++){
        const struct bui_node* nd = &d->node[i];
        int x = ox + nd->x, y = oy + nd->y;
        switch (nd->kind){
        case BUI_RECT:
            if (!bui_hit(x, y, nd->w, nd->h)) break;
            bui_fill(x, y, nd->w, nd->h, nd->color);
            bui_frame(x, y, nd->w, nd->h, d->th.accent);
            break;
        case BUI_LABEL:
            if (!bui_hit(x, y, bui_text_w(nd->text, 2), 18)) break;
            bui_text(x, y, nd->color, nd->text, 2);
            break;
        case BUI_BUTTON: {
            uint32_t face = (hot == i) ? d->th.face_hi : nd->color;
            if (!bui_hit(x, y, nd->w, nd->h)) break;
            bui_fill(x, y, nd->w, nd->h, face);
            bui_frame(x, y, nd->w, nd->h, d->th.accent);
            bui_text_c(x + nd->w / 2, y + (nd->h - 16) / 2, d->th.ink, nd->text, 2);
            break;
        }
        case BUI_TERM:
        default:
            break;      /* the desktop paints the live terminal surface */
        }
    }
}
