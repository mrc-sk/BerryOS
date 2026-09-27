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
#include "bppg.h"
#include "tcon.h"
#include "desktop.h"

/* =====================================================================
 * BerryOS desktop (M5): a full-screen window manager drawn with Bui.
 *
 * Replaces the old split-screen demo (framebuffer console on top, a single
 * "Click +1" button below).  The screen now belongs entirely to the desktop:
 *
 *   wallpaper + icon grid     built-in apps, plus every *.bppg on BerryFS
 *   taskbar (bottom)          one button per open window + uptime
 *   windows                   title bar (drag / close) + client area
 *
 * Two ideas carry the design:
 *
 *  1. All painting goes through the Bui primitives, which are clipped.  That
 *     single detail is what makes PARTIAL redraw possible: moving the pointer
 *     repaints a 16x16 square, new shell output repaints one window's client
 *     area, dragging repaints the union of the window's old and new rects --
 *     never the whole framebuffer.  Every draw call early-outs on the clip
 *     rect, so the redraw functions can be written as "paint everything" and
 *     still cost nothing outside the dirty region.
 *
 *  2. The shell is just another window.  sys_write() feeds the terminal
 *     surface (tcon.h) and this file paints it; the keyboard keeps feeding the
 *     interactive shell in user space (user/init.c), unchanged.  Nothing had
 *     to be re-plumbed for the shell to become movable and closable.
 *
 * No floating point anywhere: the framebuffer must stay 100% integer.
 * =================================================================== */

#define TASKBAR_H   34
#define TITLE_H     26
#define FRAME       6
#define WIN_MAX     6
#define APP_MAX     12
#define BPPG_MAX    4
#define ICON_BOX    64
#define TEXT_SCALE  2                  /* labels, buttons, captions */
#define CELL_SCALE  2                  /* terminal cells: 18x18 px */

/* The Canvas window: a real offscreen surface for user-space drawing.
 * Sized so the client area is exactly CANVAS_W x CANVAS_H, which keeps the
 * canvas coordinate system identical to what SYS_GFX_* callers assume. */
#define CANVAS_W 760
#define CANVAS_H 480
#define CANVAS_WIN_W (CANVAS_W + 2 * FRAME)
#define CANVAS_WIN_H (CANVAS_H + TITLE_H + FRAME)

/* Default terminal window.  Its client area decides the terminal grid, so the
 * shell's screen size is a desktop decision, made once at start-up.  It sits
 * to the right of the icon column, which stays clickable. */
#define TERM_X 180
#define TERM_Y 60
#define TERM_W 1080
#define TERM_H 720

enum { AK_TERM = 1, AK_FILES, AK_BUI, AK_ABOUT, AK_BPPG, AK_CANVAS };

struct app {
    int         kind;
    const char* name;
    char        glyph;
    uint32_t    tint;
    int         bppgi;     /* AK_BPPG: index into g_bppg */
};

struct win {
    int                   used;
    int                   kind;
    int                   x, y, w, h;
    const char*           title;
    struct bui_doc*       doc;      /* AK_BUI / AK_ABOUT / AK_BPPG */
    int                   bppgi;
    char                  list[768]; /* AK_FILES: BerryFS listing snapshot */
    int                   hot;       /* widget node under the pointer */
};

/* ---- built-in interfaces, in Bui -------------------------------- */
static const char BUI_ABOUT[] =
"window \"About BerryOS\" 300 170 700 470\n"
"label 28 26 \"BerryOS 0.0.1\" #6CA8FF\n"
"label 28 62 \"hybrid x86_64 kernel, self-written bootloader\" #E6E6E6\n"
"rect 28 96 644 2 #3A4A6E\n"
"label 28 120 \"M1  boot mm paging sched\" #9BD770\n"
"label 28 152 \"M2  multiprocess    M3  keyboard + shell\" #9BD770\n"
"label 28 184 \"M4  device framework + BerryFS\" #9BD770\n"
"label 28 216 \"M5  desktop: Bui renderer + .bppg programs\" #9BD770\n"
"rect 28 254 644 2 #3A4A6E\n"
"label 28 278 \"double-click an icon to open it\" #C8D8C0\n"
"label 28 310 \"drag a title bar to move a window\" #C8D8C0\n"
"label 28 342 \"the command line lives in the Terminal window\" #C8D8C0\n"
"button 456 386 216 44 \"close\" #4A2530 :close\n";

static const char BUI_DEMO[] =
"window \"Bui Playground\" 240 150 780 500\n"
"rect 24 24 340 130 #1E2A3A\n"
"label 44 46 \"this window is just Bui text\" #9BD770\n"
"label 44 78 \"no C code knows about these widgets\" #6CA8FF\n"
"label 44 110 \"parsed, then painted through bui_*\" #C8D8C0\n"
"button 24 190 224 46 \"recolour the rect\" #27324A :fill #E05C7A\n"
"button 268 190 224 46 \"say hello\" #27324A :say hello from a Bui button\n"
"button 512 190 224 46 \"close\" #4A2530 :close\n"
"term 24 264 736 210\n";

/* ---- state ------------------------------------------------------ */
static int  g_active;                  /* desktop owns the screen */
static struct bui_doc g_desk;          /* desktop.bui: theme + icon grid */
static struct bui_doc g_about;         /* parsed built-ins */
static struct bui_doc g_demo;
static struct bppg   g_bppg[BPPG_MAX];
static int           g_nbppg;
static struct app    g_app[APP_MAX];
static int           g_napp;
static struct win    g_win[WIN_MAX];   /* index order == z order (last on top) */

static int  g_full_dirty = 1;          /* structural change: repaint all */
static int  g_term_dirty;              /* shell output arrived */
static int  g_canvas_dirty;            /* a SYS_GFX_* drew into a window */
static int  g_bar_dirty;               /* the clock or window list changed */
static int  g_drag_rx, g_drag_ry, g_drag_rw, g_drag_rh;  /* rect to repaint */

static int  g_drag = -1;               /* window index being dragged */
static int  g_drag_dx, g_drag_dy;
static int  g_mx = -1, g_my = -1;      /* last pointer position we saw */
static int  g_down_prev;
static int  g_sel_icon = -1;
static int  g_last_icon = -1;
static uint64_t g_last_click;
static uint64_t g_last_sec;
static unsigned long g_tcon_seen;

/* Offscreen canvas for user-space drawing (the Canvas application).
 *
 * From kmalloc, NOT a static array, and that is not a style choice: pmm.c
 * reserves everything below 0x200000 because the early kernel stack lives at
 * 0x200000, so the kernel image (its .bss included) must end before that line.
 * A 1.4 MB static canvas pushes __kernel_end past it, the PMM then hands out
 * pages that are still part of the kernel, a task stack lands inside this very
 * array, and the first frame written into the canvas destroys the stack --
 * which shows up as a page fault in completely unrelated code. */
static uint32_t* g_canvas;

/* Cursor backing store, kept as 0x00RRGGBB through fb_get/put_pixel so it is
 * lossless at every pixel depth the VBE can hand us. */
#define CURSOR_SZ 16
static uint32_t g_cback[CURSOR_SZ * CURSOR_SZ];
static int g_cur_x = -1, g_cur_y = -1, g_cur_valid;

/* ---- tiny helpers ------------------------------------------------ */
static int slen(const char* s){
    int n = 0;
    while (s && s[n]) n++;
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

static void copy_n(char* dst, const char* src, int cap){
    int i = 0;
    while (src && src[i] && i < cap - 1){ dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void u64_str(char* buf, int cap, uint64_t v){
    char tmp[24];
    int i = 23, len = 0;
    tmp[i--] = 0;
    if (v == 0) tmp[i--] = '0';
    while (v){ tmp[i--] = (char)('0' + (v % 10)); v /= 10; }
    while (tmp[++i] && len < cap - 1) buf[len++] = tmp[i];
    buf[len] = 0;
}

static int hexval(char c){
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* ---- geometry ---------------------------------------------------- */
static int scr_w(void){ return (int)g_fb.width; }
static int scr_h(void){ return (int)g_fb.height; }

static void client_of(const struct win* w, int* cx, int* cy, int* cw, int* ch){
    *cx = w->x + FRAME;
    *cy = w->y + TITLE_H;
    *cw = w->w - 2 * FRAME;
    *ch = w->h - TITLE_H - FRAME;
}

/* Grow a pending repaint rectangle to include (x,y,w,h). */
static void dirty_rect(int x, int y, int w, int h){
    int x1, y1;
    if (w <= 0 || h <= 0) return;
    if (!g_drag_rw){ g_drag_rx = x; g_drag_ry = y; g_drag_rw = w; g_drag_rh = h; return; }
    x1 = g_drag_rx + g_drag_rw;
    y1 = g_drag_ry + g_drag_rh;
    if (x < g_drag_rx) g_drag_rx = x;
    if (y < g_drag_ry) g_drag_ry = y;
    if (x + w > x1) x1 = x + w;
    if (y + h > y1) y1 = y + h;
    g_drag_rw = x1 - g_drag_rx;
    g_drag_rh = y1 - g_drag_ry;
}

/* ---- windows ----------------------------------------------------- */
static void win_close(int i){
    if (i < 0 || i >= WIN_MAX) return;
    g_win[i].used = 0;
    g_win[i].doc = 0;
    g_full_dirty = 1;
}

static void win_raise(int i){
    struct win tmp;
    int k;
    if (i < 0 || i >= WIN_MAX || i == WIN_MAX - 1) return;
    tmp = g_win[i];
    for (k = i; k + 1 < WIN_MAX; k++) g_win[k] = g_win[k + 1];
    g_win[WIN_MAX - 1] = tmp;
}

static int win_focused(void){
    int i;
    for (i = WIN_MAX - 1; i >= 0; i--) if (g_win[i].used) return i;
    return -1;
}

static int win_slot(void){
    int i;
    for (i = 0; i < WIN_MAX; i++) if (!g_win[i].used) return i;
    return -1;
}

static void files_snapshot(struct win* w){
    static char buf[768];
    long n = bfs_ls(buf, sizeof(buf) - 1);
    if (n < 0){
        copy_n(w->list,
               "BerryFS is not readable\r\n"
               "(no disk attached - booted from the ISO?)", sizeof(w->list));
        return;
    }
    if (n == 0){ copy_n(w->list, "(the basket is empty)", sizeof(w->list)); return; }
    buf[n] = 0;
    copy_n(w->list, buf, sizeof(w->list));
}

static void run_program(const char* script){
    /* A program's `run` script is executed by the real shell: the lines are
     * pushed into the keyboard queue, so a .bppg has exactly the vocabulary of
     * someone typing at the prompt.  Echo them first, so the window shows what
     * the program asked for. */
    if (!script || !*script) return;
    tcon_puts("+ ");
    tcon_write(script, (unsigned long)slen(script));
    keyboard_inject(script);
}

static int win_open_app(int appi){
    struct app* ap;
    struct win* w;
    int i;

    if (appi < 0 || appi >= g_napp) return -1;
    ap = &g_app[appi];

    /* Already open?  Focus it instead of stacking a second copy. */
    for (i = 0; i < WIN_MAX; i++){
        if (g_win[i].used && g_win[i].kind == ap->kind &&
            (ap->kind != AK_BPPG || g_win[i].bppgi == ap->bppgi)){
            win_raise(i);
            g_full_dirty = 1;
            return 0;
        }
    }

    i = win_slot();
    if (i < 0){                          /* stack full: drop the bottom one */
        win_close(0);
        i = win_slot();
        if (i < 0) return -1;
    }

    w = &g_win[i];
    w->used = 1;
    w->kind = ap->kind;
    w->title = ap->name;
    w->doc = 0;
    w->bppgi = ap->bppgi;
    w->hot = -1;
    w->list[0] = 0;

    if (ap->kind == AK_TERM){
        w->x = TERM_X; w->y = TERM_Y; w->w = TERM_W; w->h = TERM_H;
    } else if (ap->kind == AK_FILES){
        w->x = 300; w->y = 170; w->w = 640; w->h = 480;
        files_snapshot(w);
    } else if (ap->kind == AK_ABOUT){
        w->doc = &g_about;
        w->x = g_about.win_x; w->y = g_about.win_y;
        w->w = g_about.win_w; w->h = g_about.win_h;
    } else if (ap->kind == AK_CANVAS){
        w->x = 240; w->y = 140;
        w->w = CANVAS_WIN_W; w->h = CANVAS_WIN_H;
    } else if (ap->kind == AK_BUI){
        w->doc = &g_demo;
        w->x = g_demo.win_x; w->y = g_demo.win_y;
        w->w = g_demo.win_w; w->h = g_demo.win_h;
    } else {                             /* AK_BPPG */
        struct bppg* p = &g_bppg[ap->bppgi];
        if (p->has_ui && p->ui.has_window){
            w->doc = &p->ui;
            w->x = p->ui.win_x; w->y = p->ui.win_y;
            w->w = p->ui.win_w; w->h = p->ui.win_h;
        } else {
            w->x = 260; w->y = 160; w->w = 620; w->h = 420;
        }
    }

    /* A window declared in Bui names itself: the `window "..."` line is the
     * caption, not whatever the launcher happened to call the application.
     * The buffer lives in the doc, which outlives the window. */
    if (w->doc && w->doc->title[0]) w->title = w->doc->title;

    /* Cascade a window that would land exactly on another one. */
    {
        int clash = 1, guard = 0;
        while (clash && guard++ < 8){
            int j;
            clash = 0;
            for (j = 0; j < WIN_MAX; j++){
                if (j == i || !g_win[j].used) continue;
                if (g_win[j].x == w->x && g_win[j].y == w->y){ clash = 1; break; }
            }
            if (clash){ w->x += 28; w->y += 26; }
        }
    }

    /* Keep it on screen and clear of the taskbar. */
    if (w->w > scr_w()) w->w = scr_w();
    if (w->h > scr_h() - TASKBAR_H) w->h = scr_h() - TASKBAR_H;
    if (w->x + w->w > scr_w()) w->x = scr_w() - w->w;
    if (w->y + w->h > scr_h() - TASKBAR_H) w->y = scr_h() - TASKBAR_H - w->h;
    if (w->x < 0) w->x = 0;
    if (w->y < 0) w->y = 0;

    win_raise(i);
    g_full_dirty = 1;

    if (ap->kind == AK_BPPG) run_program(g_bppg[ap->bppgi].run);
    return 0;
}

static int win_open_name(const char* name){
    int i;
    for (i = 0; i < g_napp; i++)
        if (ieq(g_app[i].name, name)) return win_open_app(i);
    return -1;
}

/* ---- the terminal surface painter -------------------------------- */
static void paint_term(int x, int y, int w, int h){
    int cell = bui_cell_w(CELL_SCALE);
    int cols, rows, r, c, maxr, crow, ccol;

    if (!bui_hit(x, y, w, h)) return;
    bui_fill(x, y, w, h, g_desk.th.term_bg);
    cols = w / cell;
    rows = h / cell;
    maxr = tcon_rows();
    if (rows > maxr) rows = maxr;

    for (r = 0; r < rows; r++){
        const char* s = tcon_row(r);
        int len = tcon_row_len(r);
        int yy = y + r * cell;
        int over = (len > cols);
        if (over) len = cols;
        /* Blank cells need no work at all: the client was just filled with
         * the background, and most rows of a shell session are empty.  This
         * turns the common case from ~2200 cell fills into a few hundred. */
        for (c = 0; c < len; c++){
            char ch = s[c];
            if (ch <= ' ') continue;
            bui_cell(x + c * cell, yy, ch, g_desk.th.term_fg, g_desk.th.term_bg, CELL_SCALE);
        }
        /* A line the window is too narrow for is truncated, not wrapped -- so
         * mark it, instead of silently losing half a command's output. */
        if (over && cols > 0)
            bui_cell(x + (cols - 1) * cell, yy, '>', 0xE0C068, g_desk.th.term_bg, CELL_SCALE);
    }
    tcon_cursor(&crow, &ccol);
    if (crow < rows && ccol < cols)
        bui_fill(x + ccol * cell, y + crow * cell + cell - 3, cell - 1, 3,
                 g_desk.th.term_fg);
}

/* ---- desktop chrome ---------------------------------------------- */
static void draw_wall(void){
    int w = scr_w(), h = scr_h();
    bui_fill(0, 0, w, h, g_desk.th.wall);
    bui_fill(0, 0, w, 2, 0x1A2A3A);
    bui_text(w - bui_text_w("BerryOS", 4) - 44, h - TASKBAR_H - 110, 0x1C2636, "BerryOS", 4);
    bui_text(w - bui_text_w("Bui desktop", 1) - 46, h - TASKBAR_H - 66, 0x27313F, "Bui desktop", 1);
}

/* Icons fill a column first, then move right -- the familiar desktop
 * "auto arrange": a column of icons hugs the left edge instead of a single
 * row along the top, which also keeps them clear of the default window. */
static void icon_box(int i, int* ix, int* iy){
    int gx = g_desk.grid_x, gy = g_desk.grid_y;
    int dx = g_desk.grid_dx, dy = g_desk.grid_dy;
    int percol;
    if (dx < ICON_BOX + 8)  dx = ICON_BOX + 8;
    if (dy < ICON_BOX + 34) dy = ICON_BOX + 34;
    percol = (scr_h() - TASKBAR_H - gy) / dy;
    if (percol < 1) percol = 1;
    *ix = gx + (i / percol) * dx;
    *iy = gy + (i % percol) * dy;
}

static void draw_icons(void){
    int i;
    for (i = 0; i < g_napp; i++){
        int ix, iy, tx, ty;
        char cap[14];

        icon_box(i, &ix, &iy);
        if (iy + ICON_BOX + 24 > scr_h() - TASKBAR_H) break;   /* ran off the desk */
        if (!bui_hit(ix - 6, iy - 6, ICON_BOX + 12, ICON_BOX + 34)) continue;

        if (i == g_sel_icon){
            bui_fill(ix - 6, iy - 6, ICON_BOX + 12, ICON_BOX + 30, 0x1B2C44);
            bui_frame(ix - 6, iy - 6, ICON_BOX + 12, ICON_BOX + 30, g_desk.th.accent);
        }
        bui_fill(ix, iy, ICON_BOX, ICON_BOX, g_app[i].tint);
        bui_frame(ix, iy, ICON_BOX, ICON_BOX, 0x0A0F16);
        {
            char g[2];
            g[0] = g_app[i].glyph;
            g[1] = 0;
            bui_text_c(ix + ICON_BOX / 2, iy + (ICON_BOX - 24) / 2, 0x0A0F16, g, 3);
        }
        copy_n(cap, g_app[i].name, (int)sizeof(cap));
        tx = ix + ICON_BOX / 2 - bui_text_w(cap, 1) / 2;
        ty = iy + ICON_BOX + 8;
        bui_text(tx - 1, ty - 1, 0x080C12, cap, 1);          /* cheap shadow */
        bui_text(tx, ty, g_desk.th.ink, cap, 1);
    }
}

static void draw_taskbar(void){
    int w = scr_w(), h = scr_h();
    int y = h - TASKBAR_H;
    int bx, i;
    char up[32];

    bui_fill(0, y, w, TASKBAR_H, g_desk.th.panel);
    bui_fill(0, y, w, 1, 0x2C3A4E);
    bui_text(14, y + 9, g_desk.th.accent, "berry", TEXT_SCALE);

    bx = 132;
    for (i = 0; i < WIN_MAX; i++){
        struct win* win = &g_win[i];
        char cap[17];
        if (!win->used) continue;
        if (bui_hit(bx, y + 5, 156, TASKBAR_H - 10)){
            int focus = (i == win_focused());
            bui_fill(bx, y + 5, 156, TASKBAR_H - 10, focus ? 0x33456B : 0x222C3C);
            bui_frame(bx, y + 5, 156, TASKBAR_H - 10, focus ? g_desk.th.accent : 0x2C3A4E);
            copy_n(cap, win->title ? win->title : "window", 16);
            bui_text(bx + 8, y + 10, focus ? g_desk.th.ink : 0xA8B2C0, cap, 1);
        }
        bx += 166;
    }

    /* uptime, right aligned */
    {
        char num[24];
        uint64_t t = timer_ticks();
        int l = 0;
        up[0] = 'u'; up[1] = 'p'; up[2] = ' ';
        u64_str(num, sizeof(num), t / 100);
        copy_n(up + 3, num, 16);
        l = slen(up);
        up[l++] = '.'; up[l++] = (char)('0' + (int)((t % 100) / 10));
        up[l++] = (char)('0' + (int)(t % 10)); up[l] = 0;
    }
    bui_text(w - bui_text_w(up, TEXT_SCALE) - 16, y + 9, g_desk.th.ink, up, TEXT_SCALE);
}

static void draw_window(int i){
    struct win* w = &g_win[i];
    int focus = (i == win_focused());
    int cx, cy, cw, ch;

    if (!w->used) return;
    if (!bui_hit(w->x, w->y, w->w + 6, w->h + 6)) return;

    bui_fill(w->x + 5, w->y + 5, w->w, w->h, 0x0A0E14);          /* shadow */
    bui_fill(w->x, w->y, w->w, w->h, g_desk.th.panel);
    bui_frame(w->x, w->y, w->w, w->h, focus ? g_desk.th.accent : 0x2C3A4E);
    bui_fill(w->x + 1, w->y + 1, w->w - 2, TITLE_H - 2, focus ? 0x28354F : 0x1E2734);

    {
        char cap[40];
        int maxc, l;
        copy_n(cap, w->title ? w->title : "window", (int)sizeof(cap));
        maxc = (w->w - 76) / bui_cell_w(TEXT_SCALE);
        l = slen(cap);
        if (maxc >= 2 && l > maxc){ cap[maxc - 1] = '>'; cap[maxc] = 0; }
        if (maxc >= 2)
            bui_text(w->x + 12, w->y + (TITLE_H - 16) / 2,
                     focus ? g_desk.th.ink : 0x99A4B4, cap, TEXT_SCALE);
    }
    bui_fill(w->x + w->w - 26, w->y + 5, 20, 16, 0x8E3B5C);      /* close box */
    bui_text_c(w->x + w->w - 16, w->y + 7, 0xFFFFFF, "x", 1);

    client_of(w, &cx, &cy, &cw, &ch);

    /* A window's content never paints outside its client area.  The clip is
     * narrowed for the content and restored afterwards, so a widget that is
     * bigger than the window (or positioned to overhang its edge) is cut off
     * at the frame instead of drawing over the neighbouring window. */
    {
        int st[4];
        bui_clip_save(st);
        if (bui_clip_narrow(cx, cy, cw, ch)) switch (w->kind){
        case AK_TERM:
            bui_frame(cx - 1, cy - 1, cw + 2, ch + 2, 0x101820);
            paint_term(cx, cy, cw, ch);
            break;
        case AK_FILES: {
            char line[40];
            int n = 0, p = 0;
            bui_fill(cx, cy, cw, ch, g_desk.th.term_bg);
            while (w->list[p] && n < ch / 22){
                int l = 0;
                while (w->list[p] && w->list[p] != '\n' && w->list[p] != '\r' && l < 24)
                    line[l++] = w->list[p++];
                line[l] = 0;
                while (w->list[p] == '\n' || w->list[p] == '\r') p++;
                if (l) bui_text(cx + 14, cy + 12 + n * 22, g_desk.th.term_fg, line, TEXT_SCALE);
                n++;
            }
            break;
        }
        case AK_CANVAS: {
            /* Blit the offscreen surface.  Every pixel goes through
             * fb_put_pixel, so this is correct at 15/16/24/32bpp without a
             * single memcpy. */
            int j, i, cols = cw < CANVAS_W ? cw : CANVAS_W;
            int rows = ch < CANVAS_H ? ch : CANVAS_H;
            bui_frame(cx - 1, cy - 1, cw + 2, ch + 2, 0x101820);
            if (!g_canvas){
                bui_text(cx + 16, cy + 16, 0xE05C7A, "no canvas memory", TEXT_SCALE);
                break;
            }
            for (j = 0; j < rows; j++){
                if (!bui_hit(cx, cy + j, cols, 1)) continue;
                for (i = 0; i < cols; i++)
                    fb_put_pixel(cx + i, cy + j, g_canvas[j * CANVAS_W + i]);
            }
            break;
        }
        default: {
            const struct bui_node* tn;
            bui_fill(cx, cy, cw, ch, 0x161E2A);
            if (w->doc){
                bui_draw(w->doc, cx, cy, w->hot);
                tn = bui_first(w->doc, BUI_TERM);
                if (tn) paint_term(cx + tn->x, cy + tn->y, tn->w, tn->h);
            }
            break;
        }
        }
        bui_clip_restore(st);
    }
}

static void redraw(int x, int y, int w, int h){
    int i;
    if (w <= 0 || h <= 0) return;
    bui_clip(x, y, w, h);
    draw_wall();
    draw_icons();
    for (i = 0; i < WIN_MAX; i++) draw_window(i);
    draw_taskbar();
    bui_clip_reset();
}

/* ---- cursor ------------------------------------------------------ */
static void cursor_erase(void){
    int j, i;
    if (!g_cur_valid) return;
    for (j = 0; j < CURSOR_SZ; j++)
        for (i = 0; i < CURSOR_SZ; i++)
            fb_put_pixel(g_cur_x + i, g_cur_y + j, g_cback[j * CURSOR_SZ + i]);
    g_cur_valid = 0;
}

static void cursor_paint(int x, int y){
    int j, i;
    for (j = 0; j < CURSOR_SZ; j++)
        for (i = 0; i < CURSOR_SZ; i++)
            g_cback[j * CURSOR_SZ + i] = fb_get_pixel(x + i, y + j);
    g_cur_valid = 1;
    g_cur_x = x;
    g_cur_y = y;
    for (i = 0; i < 12; i++){
        fb_put_pixel(x + i, y, 0x000000);
        fb_put_pixel(x, y + i, 0x000000);
    }
    for (i = 0; i < 11; i++){
        fb_put_pixel(x + 1 + i, y + i, 0xFFFFFF);
        fb_put_pixel(x, y + i, 0xFFFFFF);
    }
}

/* ---- widget actions ---------------------------------------------- */
static void act_fill(struct win* w, const char* a){
    uint32_t v = 0;
    int k, i;
    if (!w->doc || *a != '#') return;
    for (k = 1; k <= 6; k++){
        int h = hexval(a[k]);
        if (h < 0) return;
        v = (v << 4) | (uint32_t)h;
    }
    /* Recolour the first rect of this interface.  Mutable nodes are what make
     * a Bui window interactive at all -- there is no widget object anywhere. */
    for (i = 0; i < w->doc->n; i++){
        if (w->doc->node[i].kind == BUI_RECT){
            w->doc->node[i].color = v & 0x00FFFFFF;
            break;
        }
    }
    g_full_dirty = 1;
}

static void act_run(struct win* w, const char* action){
    const char* a;
    if (!action || action[0] != ':') return;
    a = action + 1;
    while (*a && *a != ' ') a++;
    while (*a == ' ') a++;

    if (ieq(action, ":close") || action[1] == 'c' || action[1] == 'C'){
        int i;
        for (i = 0; i < WIN_MAX; i++) if (&g_win[i] == w){ win_close(i); return; }
        return;
    }
    if (ieq(action, ":fill") || action[1] == 'f' || action[1] == 'F'){ act_fill(w, a); return; }
    if (ieq(action, ":say") || action[1] == 's' || action[1] == 'S'){
        tcon_puts(a);
        tcon_putc('\n');
        return;
    }
    if (ieq(action, ":open") || action[1] == 'o' || action[1] == 'O'){ win_open_name(a); return; }
}

/* ---- input ------------------------------------------------------- */
/* Repaint just one icon's cell.  Selecting an icon used to force a full
 * screen repaint, which on a 1280x1024 framebuffer is ~1.3 M pixel writes --
 * long enough to swallow a 100 ms click, and this is a UI where clicks
 * matter. */
static void icon_repaint(int i){
    int ix, iy;
    if (i < 0 || i >= g_napp) return;
    icon_box(i, &ix, &iy);
    dirty_rect(ix - 8, iy - 8, ICON_BOX + 16, ICON_BOX + 40);
}

static void press_desktop(int mx, int my){
    int i;
    for (i = 0; i < g_napp; i++){
        int ix, iy;
        icon_box(i, &ix, &iy);
        if (my >= iy - 6 && my < iy + ICON_BOX + 28 &&
            mx >= ix - 6 && mx < ix + ICON_BOX + 6){
            uint64_t t = timer_ticks();
            /* Two presses on the same icon inside the window means "open".
             * 60 ticks == 0.6 s at the PIT's 100 Hz: generous enough that a
             * slow repaint cannot eat the second press. */
            if (g_last_icon == i && (t - g_last_click) < 60){
                g_last_icon = -1;
                win_open_app(i);
            } else {
                icon_repaint(g_sel_icon);
                g_sel_icon = i;
                g_last_icon = i;
                g_last_click = t;
                icon_repaint(i);
            }
            return;
        }
    }
    if (g_sel_icon >= 0){
        icon_repaint(g_sel_icon);
        g_sel_icon = -1;
    }
}

static void press_taskbar(int mx){
    int bx = 132, i;
    for (i = 0; i < WIN_MAX; i++){
        if (!g_win[i].used) continue;
        if (mx >= bx && mx < bx + 156){
            if (i != win_focused()){ win_raise(i); g_full_dirty = 1; }
            return;
        }
        bx += 166;
    }
}

static void press_window(int mx, int my){
    int i, top = -1;
    for (i = WIN_MAX - 1; i >= 0; i--){
        struct win* w = &g_win[i];
        if (!w->used) continue;
        if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h){
            top = i;
            break;
        }
    }
    if (top < 0){ press_desktop(mx, my); return; }

    if (top != win_focused()){ win_raise(top); g_full_dirty = 1; top = win_focused(); }
    {
        struct win* w = &g_win[top];
        int cx, cy, cw, ch;

        if (mx >= w->x + w->w - 28 && mx < w->x + w->w - 4 &&
            my >= w->y + 3 && my < w->y + TITLE_H - 2){
            win_close(top);
            return;
        }
        if (my < w->y + TITLE_H){                 /* title bar: start a drag */
            g_drag = top;
            g_drag_dx = mx - w->x;
            g_drag_dy = my - w->y;
            return;
        }
        client_of(w, &cx, &cy, &cw, &ch);
        if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch) return;

        if (w->doc){
            int hit = bui_pick(w->doc, cx, cy, mx, my);
            if (hit >= 0 && w->doc->node[hit].kind == BUI_BUTTON)
                act_run(w, w->doc->node[hit].action);
        } else if (w->kind == AK_FILES){
            files_snapshot(w);                    /* click to refresh listing */
            dirty_rect(w->x, w->y, w->w + 6, w->h + 6);
        }
    }
}

static void do_drag(int mx, int my){
    struct win* w;
    int ox, oy;
    if (g_drag < 0) return;
    if (g_drag >= WIN_MAX || !g_win[g_drag].used){ g_drag = -1; return; }
    w = &g_win[g_drag];
    ox = w->x; oy = w->y;
    w->x = mx - g_drag_dx;
    w->y = my - g_drag_dy;
    if (w->x < 0) w->x = 0;
    if (w->y < 0) w->y = 0;
    if (w->x > scr_w() - 60) w->x = scr_w() - 60;
    if (w->y > scr_h() - TASKBAR_H - TITLE_H) w->y = scr_h() - TASKBAR_H - TITLE_H;
    if (w->x == ox && w->y == oy) return;
    /* Repaint only what the move touched: the union of the old and new
     * rectangles.  A full repaint here would drop the drag to a crawl. */
    dirty_rect(ox - 1, oy - 1, w->w + 8, w->h + 8);
    dirty_rect(w->x - 1, w->y - 1, w->w + 8, w->h + 8);
}

/* ---- public entry points ---------------------------------------- */
int desktop_active(void){ return g_active; }

static void load_desktop_file(void){
    static char f[4096];
    long fd, n;
    bui_doc_init(&g_desk);
    if (!bfs_mounted()) return;
    fd = bfs_open("desktop.bui", BFS_O_RD);
    if (fd < 0) return;
    n = bfs_read(fd, f, sizeof(f) - 1);
    bfs_close(fd);
    if (n <= 0) return;
    f[n] = 0;
    if (bui_parse(&g_desk, f) != 0)
        serial_puts("[M5] desktop.bui: bad line (the rest was applied)\r\n");
    else
        serial_puts("[M5] desktop.bui applied\r\n");
}

static void apps_build(void){
    static char ls[2048];
    long n, i2 = 0;

    g_napp = 0;
    g_nbppg = 0;

    g_app[g_napp].kind = AK_TERM;  g_app[g_napp].name = "Terminal";
    g_app[g_napp].glyph = '>';     g_app[g_napp].tint = 0x2F4A6E; g_app[g_napp].bppgi = 0; g_napp++;

    g_app[g_napp].kind = AK_FILES; g_app[g_napp].name = "Basket";
    g_app[g_napp].glyph = '=';     g_app[g_napp].tint = 0x6E5A2F; g_app[g_napp].bppgi = 0; g_napp++;

    g_app[g_napp].kind = AK_BUI;   g_app[g_napp].name = "Bui Demo";
    g_app[g_napp].glyph = 'B';     g_app[g_napp].tint = 0x2F6E5A; g_app[g_napp].bppgi = 0; g_napp++;

    g_app[g_napp].kind = AK_ABOUT; g_app[g_napp].name = "About";
    g_app[g_napp].glyph = 'i';     g_app[g_napp].tint = 0x6E2F4A; g_app[g_napp].bppgi = 0; g_napp++;

    g_app[g_napp].kind = AK_CANVAS; g_app[g_napp].name = "Canvas";
    g_app[g_napp].glyph = '#';      g_app[g_napp].tint = 0x3E3E70; g_app[g_napp].bppgi = 0; g_napp++;

    /* Every *.bppg on BerryFS becomes an application.  The file is parsed once
     * here, so opening an icon never touches the disk. */
    n = bfs_ls(ls, sizeof(ls) - 1);
    if (n <= 0) return;
    ls[n] = 0;
    while (i2 < n && g_nbppg < BPPG_MAX && g_napp < APP_MAX){
        char name[32];
        int d = 0;
        while (i2 < n && ls[i2] != ' ' && ls[i2] != '\n' && ls[i2] != '\r' && d < 31)
            name[d++] = ls[i2++];
        name[d] = 0;
        while (i2 < n && ls[i2] != '\n' && ls[i2] != '\r') i2++;
        while (i2 < n && (ls[i2] == '\n' || ls[i2] == '\r')) i2++;

        if (d > 5 &&
            name[d - 5] == '.' &&
            (name[d - 4] == 'b' || name[d - 4] == 'B') &&
            (name[d - 3] == 'p' || name[d - 3] == 'P') &&
            (name[d - 2] == 'p' || name[d - 2] == 'P') &&
            (name[d - 1] == 'g' || name[d - 1] == 'G')){
            if (bppg_load(name, &g_bppg[g_nbppg]) >= 0){
                g_app[g_napp].kind  = AK_BPPG;
                g_app[g_napp].name  = g_bppg[g_nbppg].name[0] ? g_bppg[g_nbppg].name : name;
                g_app[g_napp].glyph = g_bppg[g_nbppg].icon;
                g_app[g_napp].tint  = g_bppg[g_nbppg].color;
                g_app[g_napp].bppgi = g_nbppg;
                g_nbppg++;
                g_napp++;
                serial_puts("[M5] app loaded: ");
                serial_puts(name);
                serial_puts("\r\n");
            } else {
                serial_puts("[M5] app REJECTED (not a valid bppg): ");
                serial_puts(name);
                serial_puts("\r\n");
            }
        }
    }
}

static void desktop_banner(void){
    tcon_puts("BerryOS 0.0.1 - Bui desktop\r\n");
    tcon_puts("the shell runs in this window; '?' lists the commands\r\n");
    tcon_puts("double-click a desktop icon to open an application\r\n");
    tcon_puts("\r\n");
}

void desktop_bind(void){
    int cell;
    if (!g_fb.vbase) return;
    g_active = 1;
    bui_clip_reset();
    tcon_reset();
    /* The terminal grid follows the terminal window, so the shell's screen
     * size is decided here instead of being hardcoded in the console driver. */
    cell = bui_cell_w(CELL_SCALE);
    tcon_resize((TERM_W - 2 * FRAME) / cell, (TERM_H - TITLE_H - FRAME) / cell);
    /* The user-space drawing surface.  Heap, not .bss -- see the comment on
     * g_canvas for why that is not negotiable. */
    g_canvas = (uint32_t*)kmalloc((size_t)CANVAS_W * CANVAS_H * sizeof(uint32_t));
    if (g_canvas){
        int i;
        for (i = 0; i < CANVAS_W * CANVAS_H; i++) g_canvas[i] = 0x0C1218;
    }
    desktop_banner();
    serial_puts("[M5] desktop bound, terminal grid ");
    serial_hex((uint64_t)tcon_cols());
    serial_puts("x");
    serial_hex((uint64_t)((TERM_H - TITLE_H - FRAME) / cell));
    serial_puts(", canvas ");
    serial_puts(g_canvas ? "ok\r\n" : "UNAVAILABLE (out of memory)\r\n");
}

void desktop_seed(void){
    static const char DESK_BUI[] =
"# BerryOS desktop - edit this file on BerryFS and reboot to change the look.\n"
"# Language: see bui.h (theme keys, icon grid, windows and widgets).\n"
"bui 1\n"
"theme wall #101820\n"
"theme panel #1B2430\n"
"theme ink #E6E6E6\n"
"theme accent #6CA8FF\n"
"theme termfg #C8D8C0\n"
"theme termbg #06090C\n"
"theme face #27324A\n"
"theme facehi #3A4A6E\n"
"grid 40 60 132 128\n";

    static const char HELLO_BPPG[] =
"bppg 1\n"
"name Hello\n"
"icon H\n"
"color #9BD770\n"
"ui\n"
"window \"Hello - a .bppg program\" 220 140 720 470\n"
"label 26 24 \"this window came from hello.bppg\" #9BD770\n"
"label 26 56 \"buttons are Bui, the run script is shell\" #6CA8FF\n"
"rect 26 90 668 2 #3A4A6E\n"
"button 26 116 200 46 \"say hi\" #27324A :say hi from hello.bppg\n"
"button 244 116 200 46 \"recolour\" #27324A :fill #3A6E5A\n"
"button 462 116 232 46 \"close\" #4A2530 :close\n"
"term 26 184 668 250\n"
"run\n"
"say hello.bppg ran its own run script\n"
"season\n";

    static const char SYSINFO_BPPG[] =
"bppg 1\n"
"name Sysinfo\n"
"icon S\n"
"color #6CA8FF\n"
"ui\n"
"window \"Sysinfo\" 260 170 760 560\n"
"label 26 24 \"kernel introspection, straight from syscalls\" #6CA8FF\n"
"rect 26 58 708 2 #3A4A6E\n"
"term 26 76 708 400\n"
"button 26 492 220 46 \"re-run\" #27324A :say (use the Terminal window)\n"
"button 512 492 222 46 \"close\" #4A2530 :close\n"
"run\n"
"roots\n"
"sap\n"
"grove\n"
"season\n";

    static const struct { const char* name; const char* body; unsigned long len; } seed[] = {
        { "desktop.bui",  DESK_BUI,     sizeof(DESK_BUI) - 1     },
        { "hello.bppg",   HELLO_BPPG,   sizeof(HELLO_BPPG) - 1   },
        { "sysinfo.bppg", SYSINFO_BPPG, sizeof(SYSINFO_BPPG) - 1 },
    };
    unsigned long i;

    if (!bfs_mounted()) return;
    for (i = 0; i < sizeof(seed) / sizeof(seed[0]); i++){
        long fd;
        if (bfs_open(seed[i].name, BFS_O_RD) >= 0) continue;   /* already there */
        fd = bfs_open(seed[i].name, BFS_O_WR | BFS_O_CREAT | BFS_O_TRUNC);
        if (fd < 0) continue;
        bfs_write(fd, seed[i].body, seed[i].len);
        bfs_close(fd);
        serial_puts("[M5] planted ");
        serial_puts(seed[i].name);
        serial_puts("\r\n");
    }
}

/* Repaint every place the terminal surface is visible: the Terminal window
 * itself, and the term widget of any Bui interface that declares one.  Those
 * are the only regions a line of shell output can change. */
static void redraw_term_areas(void){
    int i;
    for (i = 0; i < WIN_MAX; i++){
        const struct bui_node* tn;
        int cx, cy, cw, ch;
        if (!g_win[i].used) continue;
        client_of(&g_win[i], &cx, &cy, &cw, &ch);
        if (g_win[i].kind == AK_TERM){
            redraw(cx - 2, cy - 2, cw + 4, ch + 4);
            continue;
        }
        if (!g_win[i].doc) continue;
        tn = bui_first(g_win[i].doc, BUI_TERM);
        if (!tn) continue;
        redraw(cx + tn->x - 2, cy + tn->y - 2, tn->w + 4, tn->h + 4);
    }
}

int desktop_canvas_w(void){ return CANVAS_W; }
int desktop_canvas_h(void){ return CANVAS_H; }

/* Return the canvas window's client rect, but only when the focused window
 * actually IS the canvas.  Any other window has no canvas at all, and
 * SYS_GFX_* must stay silent rather than paint over the desktop. */
int desktop_canvas(int* x, int* y, int* w, int* h){
    int i;
    if (!g_active || !g_canvas) return 0;
    i = win_focused();
    if (i < 0 || g_win[i].kind != AK_CANVAS) return 0;
    client_of(&g_win[i], x, y, w, h);
    if (*w <= 0 || *h <= 0) return 0;
    return 1;
}

/* Clip a canvas-local rectangle and paint it into the offscreen surface.
 * Nothing is drawn on screen here: the desktop blits the surface, which is
 * what lets the drawing survive a repaint (otherwise the very next frame --
 * a drag, or one line of shell output -- would wipe it). */
void desktop_canvas_fill(int x, int y, int w, int h, uint32_t color){
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h, i, j;
    if (!g_canvas) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > CANVAS_W) x1 = CANVAS_W;
    if (y1 > CANVAS_H) y1 = CANVAS_H;
    if (x1 <= x0 || y1 <= y0) return;
    for (j = y0; j < y1; j++)
        for (i = x0; i < x1; i++)
            g_canvas[j * CANVAS_W + i] = color & 0x00FFFFFF;
    g_canvas_dirty = 1;
}

void desktop_canvas_text(int x, int y, uint32_t fg, uint32_t bg, const char* s){
    extern const uint8_t font8x8[95 * 8];
    int scale = 2, cx = x;
    if (!g_canvas) return;
    while (s && *s){
        char c = *s++;
        int gi, ry, rx;
        if (c < 32 || c > 126) c = '?';
        gi = c - 32;
        for (ry = 0; ry < 8; ry++){
            uint8_t row = font8x8[gi * 8 + ry];
            for (rx = 0; rx < 8; rx++){
                int on = (row & (0x80 >> rx)) != 0;
                int px = cx + rx * scale, py = y + ry * scale;
                int sx, sy;
                if (px < 0 || py < 0 || px + scale > CANVAS_W || py + scale > CANVAS_H)
                    continue;
                for (sy = 0; sy < scale; sy++)
                    for (sx = 0; sx < scale; sx++)
                        g_canvas[(py + sy) * CANVAS_W + (px + sx)] = on ? fg : bg;
            }
        }
        cx += 9 * scale;
    }
    g_canvas_dirty = 1;
}

void desktop_canvas_clear(uint32_t color){
    int i;
    if (!g_canvas) return;
    for (i = 0; i < CANVAS_W * CANVAS_H; i++) g_canvas[i] = color & 0x00FFFFFF;
    g_canvas_dirty = 1;
}

long desktop_ctl(int op, char* buf, unsigned long n){
    long r = -1;
    int k, open = 0;
    unsigned long p = 0;

    if (!g_active) return -1;
    /* Only OPEN needs the caller's buffer; CLOSE and LIST have their own. */
    if (op == DESK_CTL_OPEN && !buf) return -1;
    if (op == DESK_CTL_LIST && (!buf || n < 16)) return -1;

    cli();                     /* stop the desktop task from seeing half a change */
    if (op == DESK_CTL_OPEN){
        r = (long)win_open_name(buf);
    } else if (op == DESK_CTL_CLOSE){
        k = win_focused();
        if (k >= 0){ win_close(k); r = 0; }
    } else if (op == DESK_CTL_LIST){
        #define PUT(s) do { const char* _s = (s); while (*_s && p < n - 1) buf[p++] = *_s++; } while (0)
        #define PUTN(v) do { char _nb[16]; u64_str(_nb, sizeof(_nb), (uint64_t)(v)); PUT(_nb); } while (0)
        for (k = 0; k < WIN_MAX; k++) if (g_win[k].used) open++;
        if (!open) PUT("no windows open\r\n");
        else {
            PUT("windows (");
            PUTN(open);
            PUT(" open, topmost first):\r\n");
            for (k = WIN_MAX - 1; k >= 0; k--){
                if (!g_win[k].used) continue;
                PUT("  ");
                PUT(k == win_focused() ? "* " : "  ");
                PUT(g_win[k].title ? g_win[k].title : "window");
                PUT("  ");
                PUTN(g_win[k].x); PUT(",");
                PUTN(g_win[k].y); PUT("  ");
                PUTN(g_win[k].w); PUT("x");
                PUTN(g_win[k].h); PUT("\r\n");
            }
        }
        PUT("applications (pane open <name>):\r\n  ");
        for (k = 0; k < g_napp; k++){
            PUT(g_app[k].name);
            if (k + 1 < g_napp) PUT(", ");
        }
        PUT("\r\n");
        buf[p] = 0;
        r = (long)p;
        #undef PUT
        #undef PUTN
    }
    /* A listing changes nothing on screen, so only a window change repaints. */
    if (op == DESK_CTL_OPEN || op == DESK_CTL_CLOSE) g_full_dirty = 1;
    sti();
    return r;
}

void desktop_task(void* arg){
    int mx, my, but, down;
    (void)arg;
    if (!g_fb.vbase) return;

    load_desktop_file();
    bui_doc_init(&g_about);
    if (bui_parse(&g_about, BUI_ABOUT) != 0) serial_puts("[M5] builtin About: bad line\r\n");
    bui_doc_init(&g_demo);
    if (bui_parse(&g_demo, BUI_DEMO) != 0) serial_puts("[M5] builtin Bui demo: bad line\r\n");
    apps_build();

    /* Start with the shell visible: the Terminal is what makes this a desktop
     * rather than a picture. */
    win_open_app(0);

    mx = scr_w() / 2;
    my = scr_h() / 2;
    mouse_set_xy(mx, my);
    g_mx = mx; g_my = my;
    g_last_sec = timer_ticks() / 100;
    g_tcon_seen = tcon_seq();

    g_full_dirty = 1;
    redraw(0, 0, scr_w(), scr_h());
    cursor_paint(mx, my);
    g_full_dirty = 0;

    for (;;){
        int moved = 0;
        int full, term, bar, canvas, drw, drx = 0, dry = 0, drh = 0;

        mouse_get_xy(&mx, &my);
        but = mouse_get_buttons();
        down = but & 1;

        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (mx > scr_w() - CURSOR_SZ) mx = scr_w() - CURSOR_SZ;
        if (my > scr_h() - CURSOR_SZ) my = scr_h() - CURSOR_SZ;
        if (mx != g_mx || my != g_my){ g_mx = mx; g_my = my; moved = 1; }

        if (down && !g_down_prev){
            if (my >= scr_h() - TASKBAR_H) press_taskbar(mx);
            else                           press_window(mx, my);
        } else if (!down && g_down_prev){
            /* Button up.  A finished drag needs no repaint of its own: every
             * step of the move already repainted the union of the old and new
             * rectangles.  Repainting the whole screen here (as this used to)
             * costs ~40 ms per click and makes the desktop feel dead. */
            g_drag = -1;
        } else if (down && g_drag >= 0){
            do_drag(mx, my);
        }
        g_down_prev = down;

        /* Light up the button under the pointer. */
        {
            int f = win_focused();
            if (f >= 0 && g_win[f].doc){
                int cx, cy, cw, ch, hit;
                client_of(&g_win[f], &cx, &cy, &cw, &ch);
                hit = bui_pick(g_win[f].doc, cx, cy, mx, my);
                if (hit >= 0 && g_win[f].doc->node[hit].kind != BUI_BUTTON) hit = -1;
                if (hit != g_win[f].hot){ g_win[f].hot = hit; g_full_dirty = 1; }
            }
        }

        if (tcon_seq() != g_tcon_seen){ g_tcon_seen = tcon_seq(); g_term_dirty = 1; }
        {
            uint64_t sec = timer_ticks() / 100;
            if (sec != g_last_sec){ g_last_sec = sec; g_bar_dirty = 1; }
        }

        /* Claim the pending work ATOMICALLY, before painting anything.
         *
         * Clearing the flags after the repaints instead (the obvious way)
         * silently loses updates: the shell can be scheduled in the middle of
         * a long repaint, open a window, raise the "full repaint" flag -- and
         * then the desktop finishes and clears it.  The new window then only
         * gets painted where some other repaint happened to overlap it, which
         * is exactly how a window ends up on screen with no taskbar button.
         * Claiming first means any change made while we paint sets the flag
         * again and is honoured on the next pass. */
        cli();
        full   = g_full_dirty;   g_full_dirty   = 0;
        term   = g_term_dirty;   g_term_dirty   = 0;
        bar    = g_bar_dirty;    g_bar_dirty    = 0;
        canvas = g_canvas_dirty; g_canvas_dirty = 0;
        drw    = g_drag_rw;      g_drag_rw      = 0;
        drx    = g_drag_rx;      dry            = g_drag_ry; drh = g_drag_rh;
        sti();

        if (full || term || bar || canvas || drw || moved){
            cursor_erase();

            if (full){
                redraw(0, 0, scr_w(), scr_h());
            } else {
                if (canvas){
                    int f = win_focused();
                    if (f >= 0){
                        int cx, cy, cw, ch;
                        client_of(&g_win[f], &cx, &cy, &cw, &ch);
                        redraw(cx, cy, cw, ch);
                    } else {
                        redraw(0, 0, scr_w(), scr_h());
                    }
                }
                if (bar)
                    redraw(0, scr_h() - TASKBAR_H, scr_w(), TASKBAR_H);
                if (term)
                    redraw_term_areas();
                if (drw)
                    redraw(drx, dry, drw, drh);
            }

            cursor_paint(mx, my);
        }

        hlt();
    }
}
