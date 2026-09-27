/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Copyright (C) mrc-sk and imjumping
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version. See LICENSE for the full license text and the additional
 * non-commercial restriction terms that apply to this software.
 */
/* BerryOS init process (pid 1) + the BerryOS shell.
 *
 * M2 one-time demo forks/execs/waits the worker program; afterwards init
 * becomes an interactive shell reading from the keyboard via the blocking
 * sys_read() (canonical mode, echoed live by the keyboard driver).
 *
 * M5: this shell now lives INSIDE A WINDOW of the Bui desktop.  Nothing here
 * changed to make that happen -- sys_write() is routed to the terminal surface
 * the desktop paints, so the shell became movable and closable for free.
 *
 * The command vocabulary is deliberately NOT Unix/Windows.  It follows the
 * OS's own orchard metaphor and is resolved by MINIMUM UNIQUE PREFIX
 * (DCL/VMS style): "bas" is enough for "basket", "roo" for "roots".  An
 * ambiguous prefix lists its candidates instead of guessing.
 *
 *   basket / taste / plant / uproot / till    BerryFS
 *   say / sprout / wipe / roots / bloom       everyday
 *   sap / grove / season                      machine introspection
 *   weave / unweave                           command packages
 *   pane                                      the desktop's windows and apps
 *   /<name>                                   run a command package
 *
 * Command packages: `weave` opens a small line editor (Ctrl+X saves, Ctrl+C
 * discards), asks for a name, and stores the script on BerryFS as "bp.<name>".
 * "/<name>" replays it line by line through the very same parser the prompt
 * uses.  A package name may not collide with a builtin command name.
 *
 * A .bppg program (see bppg.h) is the desktop-visible form of the same idea:
 * a text program with its own Bui window, whose run script the desktop feeds
 * back into this shell.
 */
#include "syscall.h"
#include "string.h"

static const char crlf[]   = "\r\n";
static const char prompt[] = "berry> ";
static const char pk_pfx[] = "bp.";          /* BerryFS name for a package */

#define LINE_MAX  256
#define ARGV_MAX  16
#define PKG_MAX   2048      /* BerryFS caps a file at BFS_DIRECT*512 = 4096 */
#define PKG_NAME  20        /* BFS_NAME is 24; "bp." eats 3, +1 for NUL */
#define PKG_LINES 96
#define PKG_DEPTH 4         /* nested package calls */

/* Package names harvested off BerryFS (files named "bp.<name>"). */
struct pkglist { char names[24][PKG_NAME + 1]; int n; };
static void pkg_collect(struct pkglist* L);
static void pkg_list(void);

/* ---------------------------------------------------------------- helpers */
static void out(const char* s){ sys_write(1, s, strlen(s)); }
static void outn(const char* s, unsigned long n){ sys_write(1, s, n); }

/* case-insensitive compare */
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

/* does `s` prefix `full`? (case-insensitive) */
static int is_pref(const char* s, const char* full){
    while (*s && *full){
        char ca = *s, cb = *full;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        s++; full++;
    }
    return (*s == 0);
}

static void u64_str(char* buf, unsigned long cap, unsigned long v){
    char tmp[24];
    int i = 23, len = 0;
    tmp[i--] = 0;
    if (v == 0) tmp[i--] = '0';
    while (v){ tmp[i--] = (char)('0' + (v % 10)); v /= 10; }
    while (tmp[++i] && len < (int)cap - 1) buf[len++] = tmp[i];
    buf[len] = 0;
}

static void out_u64(unsigned long v){
    char b[24];
    u64_str(b, sizeof(b), v);
    out(b);
}

/* Split line into argv on spaces/tabs (modifies line in place). */
static int tokenize(char* line, char* argv[], int max){
    int argc = 0;
    char* p = line;
    while (*p && argc < max){
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p){ *p = 0; p++; }
    }
    return argc;
}

/* Read one line, strip CR/LF; returns its length (0 = nothing usable). */
static long read_line(char* buf, unsigned long cap){
    long n = sys_read(0, buf, cap);
    if (n <= 0) return 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
    buf[n] = 0;
    return n;
}

/* ------------------------------------------------------- command handlers */
static void run_worker(void){
    long pid = sys_fork();
    if (pid == 0){
        sys_exec(1);           /* replace image with the worker program */
        sys_exit(127);         /* reached only if exec failed */
    }
    sys_wait(pid);             /* parent waits (blocks) */
}

/* List plain files, then the woven packages separately: a package lives on
 * BerryFS as "bp.<name>", which is a storage detail the user need not read. */
static void cmd_basket(char* argv[], int argc){
    static char buf[2048];
    struct pkglist L;
    unsigned long i = 0, n;
    int shown = 0, k;
    (void)argv; (void)argc;

    n = (unsigned long)sys_ls(buf, sizeof(buf) - 1);
    if ((long)n < 0){ out("basket: BerryFS not readable\r\n"); return; }
    buf[n] = 0;

    while (i < n){
        unsigned long start = i;
        int is_pkg = 1, q;
        while (i < n && buf[i] != '\n' && buf[i] != '\r') i++;
        for (q = 0; q < 3; q++)
            if (start + (unsigned long)q >= i || buf[start + q] != pk_pfx[q]) is_pkg = 0;
        if (i > start && !is_pkg){ outn(buf + start, i - start); out(crlf); shown++; }
        while (i < n && (buf[i] == '\n' || buf[i] == '\r')) i++;
    }

    pkg_collect(&L);
    if (L.n){
        out("\r\nwoven packages (run with /<name>):\r\n");
        for (k = 0; k < L.n; k++){ out("  /"); out(L.names[k]); out(crlf); }
    }
    if (!shown && !L.n) out("(the basket is empty)\r\n");
}

static void cmd_taste(char* argv[], int argc){
    static char buf[512];
    long fd, n;
    if (argc < 2){ out("usage: taste <file>\r\n"); return; }
    fd = sys_open(argv[1], O_RDONLY);
    if (fd < 0){ out("taste: no such file\r\n"); return; }
    while ((n = sys_fread(fd, buf, sizeof(buf))) > 0)
        outn(buf, (unsigned long)n);
    sys_close(fd);
}

/* Rebuild a single space-joined string from argv[first..argc-1] into outb. */
static void join_args(char* outb, int first, char* argv[], int argc, int max){
    int i, p = 0;
    for (i = first; i < argc && p < max - 1; i++){
        const char* s = argv[i];
        while (*s && p < max - 1) outb[p++] = *s++;
        if (i + 1 < argc && p < max - 1) outb[p++] = ' ';
    }
    outb[p] = 0;
}

static void cmd_plant(char* argv[], int argc){
    static char text[256];
    long fd;
    if (argc < 3){ out("usage: plant <file> <text>\r\n"); return; }
    join_args(text, 2, argv, argc, (int)sizeof(text));
    fd = sys_open(argv[1], O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0){
        out("plant: open failed - BerryFS unavailable (no disk) or full\r\n");
        return;
    }
    sys_fwrite(fd, text, strlen(text));
    sys_close(fd);
    out("planted "); out(argv[1]); out(crlf);
}

static void cmd_uproot(char* argv[], int argc){
    if (argc < 2){ out("usage: uproot <file>\r\n"); return; }
    if (sys_unlink(argv[1]) < 0){ out("uproot: no such file\r\n"); return; }
    out("uprooted "); out(argv[1]); out(crlf);
}

static void cmd_till(char* argv[], int argc){
    (void)argv; (void)argc;
    if (sys_mkfs() != 0){ out("till: no disk to turn (BerryFS is unavailable)\r\n"); return; }
    out("soil turned - BerryFS formatted\r\n");
}

static void cmd_say(char* argv[], int argc){
    int i;
    if (argc <= 1){ out(crlf); return; }
    for (i = 1; i < argc; i++){
        out(argv[i]);
        if (i + 1 < argc) out(" ");
    }
    out(crlf);
}

static void cmd_sprout(char* argv[], int argc){
    (void)argv; (void)argc;
    run_worker();
}

static void cmd_wipe(char* argv[], int argc){
    (void)argv; (void)argc;
    sys_clear();
}

static void cmd_roots(char* argv[], int argc){
    (void)argv; (void)argc;
    out("BerryOS 0.0.2 - hybrid x86_64 kernel\r\n");
    out("M1 boot mm paging sched   M2 multiprocess   M3 keyboard\r\n");
    out("M4 device framework + BerryFS persistent filesystem\r\n");
    out("M5 Bui desktop: window manager + .bppg text programs\r\n");
    out("shell: orchard vocabulary, minimum-unique-prefix matching\r\n");
}

/* The graphics demo draws into the Canvas window.  With the desktop up,
 * SYS_GFX_* has a surface only when the canvas is the focused window -- so ask
 * for it first, instead of painting over whatever the user is looking at. */
static void cmd_bloom(char* argv[], int argc){
    static const unsigned int colors[] = {
        0x8E3B5C, 0x2BB7B3, 0x6CA8FF, 0xE0C068, 0x9BD770, 0xD76B6B
    };
    int i;
    (void)argv; (void)argc;
    if (sys_desktop(DESK_OPEN, "Canvas", 0) != 0){
        out("bloom: no desktop on this screen, nothing to draw on\r\n");
        return;
    }
    gfx_clear(0x0C1218);
    gfx_fill(0, 0, 760, 42, 0x2A1A2E);
    gfx_text(16, 11, 0xB5567A, 0x2A1A2E, "BerryOS - graphics in a window");
    for (i = 0; i < 6; i++)
        gfx_fill(20 + i * 122, 80, 100, 120, colors[i]);
    gfx_text(20, 226, 0xD8DCE0, 0x0C1218, "SYS_GFX_* into the Canvas surface");
    gfx_text(20, 258, 0x7A6B72, 0x0C1218, "drawn from ring 3, kept across repaints");
    out("bloomed into the Canvas window\r\n");
}

static void cmd_sap(char* argv[], int argc){
    unsigned long total = 0, freeb = 0;
    (void)argv; (void)argc;
    if (sys_meminfo(&total, &freeb) != 0){ out("sap: unavailable\r\n"); return; }
    out("sap: ");
    out_u64(freeb >> 20); out(" / "); out_u64(total >> 20); out(" MiB free");
    if (total){
        /* permille, so one decimal place without needing floating point */
        unsigned long pm = (unsigned long)((total - freeb) / (total >> 10));
        out(" ("); out_u64(pm / 10); out("."); out_u64(pm % 10); out("% drawn)");
    }
    out(crlf);
}

static void cmd_grove(char* argv[], int argc){
    static char buf[1536];
    long n;
    (void)argv; (void)argc;
    n = sys_tasks(buf, sizeof(buf));
    if (n < 0){ out("grove: unavailable\r\n"); return; }
    if (n == 0){ out("(nothing growing)\r\n"); return; }
    outn(buf, (unsigned long)n);
}

static void cmd_season(char* argv[], int argc){
    unsigned long t = sys_uptime();      /* PIT runs at 100 Hz */
    (void)argv; (void)argc;
    out("season: up ");
    out_u64(t / 100); out(".");
    if ((t % 100) < 10) out("0");
    out_u64(t % 100);
    out(" s ("); out_u64(t); out(" ticks)\r\n");
}

static void cmd_dormant(char* argv[], int argc){
    (void)argv; (void)argc;
    out("BerryOS going dormant.\r\n");
    sys_exit(0);
}

/* `pane` -- the desktop's windows and programs.  The shell asks the window
 * manager through SYS_DESKTOP; the kernel knows nothing about this vocabulary,
 * which is why adding it cost one syscall and one table entry. */
static void cmd_pane(char* argv[], int argc){
    static char buf[1024];
    static char name[64];
    long n;

    if (argc >= 2){
        if (is_pref(argv[1], "open")){
            if (argc < 3){ out("usage: pane open <application>\r\n"); return; }
            /* Join the rest, so a name may contain spaces ("Bui Demo"). */
            join_args(name, 2, argv, argc, (int)sizeof(name));
            if (sys_desktop(DESK_OPEN, name, 0) != 0){
                out("pane: no such application: "); out(name); out(crlf);
                out("      try: pane\r\n");
            } else {
                out("raised "); out(name); out(crlf);
            }
            return;
        }
        if (is_pref(argv[1], "close")){
            if (sys_desktop(DESK_CLOSE, 0, 0) != 0) out("pane: nothing to close\r\n");
            else out("closed the topmost window\r\n");
            return;
        }
        if (is_pref(argv[1], "list")){ /* fall through to the listing */ }
        else { out("usage: pane [open <app> | close | list]\r\n"); return; }
    }

    n = sys_desktop(DESK_LIST, buf, sizeof(buf));
    if (n < 0){ out("pane: no desktop on this screen (no VBE mode)\r\n"); return; }
    outn(buf, (unsigned long)n);
}

/* --------------------------------------------------------- command table */
struct cmd {
    const char* name;
    void (*fn)(char* argv[], int argc);
    const char* help;
    int alias;               /* 1 = kept working, hidden from the listing */
};

static void cmd_help(char* argv[], int argc);
static void cmd_weave(char* argv[], int argc);
static void cmd_unweave(char* argv[], int argc);

static const struct cmd CMDS[] = {
    { "basket",  cmd_basket,  "basket                list the basket (BerryFS)", 0 },
    { "taste",   cmd_taste,   "taste <file>          read a file", 0 },
    { "plant",   cmd_plant,   "plant <file> <text>   write a file", 0 },
    { "uproot",  cmd_uproot,  "uproot <file>         delete a file", 0 },
    { "till",    cmd_till,    "till                  turn the soil (format)", 0 },
    { "say",     cmd_say,     "say <text>            print text", 0 },
    { "sprout",  cmd_sprout,  "sprout                fork+exec+wait the worker", 0 },
    { "wipe",    cmd_wipe,    "wipe                  clear the screen", 0 },
    { "roots",   cmd_roots,   "roots                 about this system", 0 },
    { "bloom",   cmd_bloom,   "bloom                 draw into the Canvas window", 0 },
    { "dormant", cmd_dormant, "dormant               halt BerryOS", 0 },
    { "sap",     cmd_sap,     "sap                   memory in use", 0 },
    { "grove",   cmd_grove,   "grove                 list tasks", 0 },
    { "season",  cmd_season,  "season                uptime", 0 },
    { "weave",   cmd_weave,   "weave                 write a command package", 0 },
    { "unweave", cmd_unweave, "unweave <name>        drop a command package", 0 },
    { "pane",    cmd_pane,    "pane [open|close]     desktop windows and apps", 0 },
    { "?",       cmd_help,    "?                     this help", 0 },
    /* aliases: accepted, but not advertised */
    { "help",    cmd_help,    "", 1 },
    { "bintp",   cmd_weave,   "", 1 },
};
#define NCMDS ((int)(sizeof(CMDS) / sizeof(CMDS[0])))

static void cmd_help(char* argv[], int argc){
    int i;
    (void)argv; (void)argc;
    out("BerryOS shell - commands take any unique prefix\r\n");
    for (i = 0; i < NCMDS; i++){
        if (CMDS[i].alias) continue;
        out("  "); out(CMDS[i].help); out(crlf);
    }
    out("\r\n  /<name>   run a woven command package (see: weave)\r\n");
    out("            a package name may not clash with a command\r\n");
    out("  .bppg     a text program; it appears as an icon\r\n");
    out("            double-click it, or: pane open <name>\r\n");
}

/* Minimum-unique-prefix lookup.  Returns the match or NULL; *ambig is set
 * when the prefix fits more than one command. */
static const struct cmd* find_cmd(const char* s, int* ambig){
    int hits = 0, i;
    const struct cmd* found = 0;
    *ambig = 0;
    for (i = 0; i < NCMDS; i++){
        if (is_pref(s, CMDS[i].name)){ hits++; found = &CMDS[i]; }
    }
    if (hits == 1) return found;
    if (hits > 1) *ambig = 1;
    return 0;
}

static void list_candidates(const char* s){
    int i;
    out("ambiguous: '"); out(s); out("' could be");
    for (i = 0; i < NCMDS; i++){
        if (CMDS[i].alias) continue;
        if (is_pref(s, CMDS[i].name)){ out(" "); out(CMDS[i].name); }
    }
    out(crlf);
}

/* A package name must not shadow a builtin command (or one of its aliases). */
static int name_clashes(const char* name){
    int i;
    for (i = 0; i < NCMDS; i++)
        if (ieq(name, CMDS[i].name)) return 1;
    return 0;
}

/* ------------------------------------------------------ command packages */
/* BerryFS is flat, so a package is just a file whose name starts with "bp.".
 * BFS_NAME is 24, hence the 20-character limit on the visible name. */
static void pkg_path(char* outb, const char* name){
    int i = 0, j = 0;
    while (pk_pfx[i]){ outb[i] = pk_pfx[i]; i++; }
    while (name[j] && i < 23){ outb[i] = name[j]; i++; j++; }
    outb[i] = 0;
}

static int valid_name(const char* n){
    int i = 0;
    if (!n[0]) return 0;
    while (n[i]){
        char c = n[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
        i++;
    }
    return i <= PKG_NAME;
}

static void lower_copy(char* dst, const char* src){
    int i = 0;
    while (src[i] && i < PKG_NAME){
        char c = src[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        dst[i++] = c;
    }
    dst[i] = 0;
}

static void pkg_collect(struct pkglist* L){
    static char buf[2048];
    long n = sys_ls(buf, sizeof(buf) - 1);
    unsigned long i = 0;
    L->n = 0;
    if (n <= 0) return;
    buf[n] = 0;
    while (i < (unsigned long)n && L->n < 24){
        unsigned long start = i, k;
        char name[32];
        int d = 0, q, ok = 1;
        while (i < (unsigned long)n && buf[i] != '\n' && buf[i] != '\r') i++;
        for (k = start; k < i && buf[k] != ' ' && d < 31; k++) name[d++] = buf[k];
        name[d] = 0;
        while (i < (unsigned long)n && (buf[i] == '\n' || buf[i] == '\r')) i++;
        for (q = 0; q < 3; q++) if (name[q] != pk_pfx[q]) ok = 0;
        if (ok && name[3]){
            int t = 0, dup = 0, j;
            while (name[3 + t] && t < PKG_NAME){ L->names[L->n][t] = name[3 + t]; t++; }
            L->names[L->n][t] = 0;
            /* A damaged/duplicated inode table must not turn one package into
             * "ambiguous", so collapse repeats by name. */
            for (j = 0; j < L->n; j++)
                if (ieq(L->names[j], L->names[L->n])){ dup = 1; break; }
            if (!dup) L->n++;
        }
    }
}

/* Prefix lookup over package names; writes the resolved name into outname. */
static int pkg_find(struct pkglist* L, const char* s, char* outname, int* ambig){
    int hits = 0, i;
    *ambig = 0;
    outname[0] = 0;
    for (i = 0; i < L->n; i++){
        if (is_pref(s, L->names[i])){
            int t = 0;
            while (L->names[i][t]){ outname[t] = L->names[i][t]; t++; }
            outname[t] = 0;
            hits++;
        }
    }
    if (hits == 1) return 1;
    if (hits > 1) *ambig = 1;
    return 0;
}

static void pkg_list(void){
    struct pkglist L;
    int i;
    pkg_collect(&L);
    if (L.n == 0){ out("(no packages woven yet - try: weave)\r\n"); return; }
    for (i = 0; i < L.n; i++){ out("  /"); out(L.names[i]); out(crlf); }
}

/* Replay a stored package.  The depth/self-reference guards stop a package
 * from calling itself -- directly or through another package -- forever.
 * The body buffer is per-depth: a package may invoke another one. */
static int  g_depth = 0;
static char g_stack[PKG_DEPTH][PKG_NAME + 1];
static char g_body[PKG_DEPTH][PKG_MAX + 1];

static void exec_line(char* line);

static void pkg_run(const char* name){
    char path[32];
    char line[LINE_MAX];
    long fd, n, i, start;
    int k, t;

    if (g_depth >= PKG_DEPTH){
        out("/"); out(name); out(": nesting too deep (max ");
        out_u64(PKG_DEPTH); out(")\r\n");
        return;
    }
    for (k = 0; k < g_depth; k++){
        if (ieq(g_stack[k], name)){
            out("/"); out(name); out(": recursive package, refused\r\n");
            return;
        }
    }

    pkg_path(path, name);
    fd = sys_open(path, O_RDONLY);
    if (fd < 0){ out("/"); out(name); out(": no such package\r\n"); return; }
    n = sys_fread(fd, g_body[g_depth], PKG_MAX);
    sys_close(fd);
    if (n <= 0){ out("/"); out(name); out(": empty package\r\n"); return; }
    g_body[g_depth][n] = 0;

    for (t = 0; name[t] && t < PKG_NAME; t++) g_stack[g_depth][t] = name[t];
    g_stack[g_depth][t] = 0;
    g_depth++;

    i = 0; start = 0;
    while (i <= n){
        if (i == n || g_body[g_depth - 1][i] == '\n'){
            long len = i - start;
            if (len > 0 && len < LINE_MAX){
                int q;
                for (q = 0; q < (int)len; q++)
                    line[q] = g_body[g_depth - 1][start + q];
                line[len] = 0;
                exec_line(line);
            }
            if (i == n) break;
            start = i + 1;
        }
        i++;
    }
    g_depth--;
}

/* The weave editor: collect command lines until Ctrl+X (save) or Ctrl+C
 * (discard), then ask for a name and store the script on BerryFS. */
static void cmd_weave(char* argv[], int argc){
    static char body[PKG_MAX + 1];
    static char lbuf[LINE_MAX];
    static char nbuf[LINE_MAX];
    char path[32];
    char name[PKG_NAME + 1];
    unsigned long len = 0;
    int lineno = 1, named = 0, tries;
    long fd, n;
    (void)argv; (void)argc;

    out("weaving a command package - one command per line\r\n");
    out("  ctrl+X save    ctrl+C discard\r\n");

    while (lineno <= PKG_LINES){
        int cancel = 0, save = 0, w = 0, t;
        char num[8];
        u64_str(num, sizeof(num), (unsigned long)lineno);
        out(num); out("| ");
        n = sys_read(0, lbuf, sizeof(lbuf) - 1);
        if (n <= 0) continue;
        /* Pull the control byte out, drop CR/LF, keep the rest of the line. */
        for (t = 0; t < (int)n; t++){
            unsigned char c = (unsigned char)lbuf[t];
            if (c == 0x18){ save = 1; continue; }        /* Ctrl+X */
            if (c == 0x03){ cancel = 1; continue; }      /* Ctrl+C */
            if (c == '\n' || c == '\r') continue;
            if (w < LINE_MAX - 1) lbuf[w++] = (char)c;
        }
        lbuf[w] = 0;
        if (cancel){ out("discarded\r\n"); return; }
        if (w > 0){
            if (len + (unsigned long)w + 2 > PKG_MAX){
                out("(package full - saving what fits)\r\n");
                save = 1;
            } else {
                for (t = 0; t < w; t++) body[len++] = lbuf[t];
                body[len++] = '\n';
                lineno++;
            }
        }
        if (save) break;
    }

    if (len == 0){ out("nothing woven - empty package\r\n"); return; }

    for (tries = 0; tries < 3; tries++){
        out("package name: ");
        if (read_line(nbuf, sizeof(nbuf) - 1) <= 0){ out(crlf); return; }
        lower_copy(name, nbuf);
        if (!valid_name(name)){
            out("  letters/digits/_/- only, at most ");
            out_u64(PKG_NAME); out(" chars\r\n");
            continue;
        }
        if (name_clashes(name)){
            out("  '"); out(name);
            out("' is a builtin command - pick another name\r\n");
            continue;
        }
        named = 1;
        break;
    }
    if (!named){ out("weave: no usable name, discarded\r\n"); return; }

    pkg_path(path, name);
    fd = sys_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0){ out("weave: BerryFS full or unwritable\r\n"); return; }
    sys_fwrite(fd, body, len);
    sys_close(fd);
    out("woven '/"); out(name); out("' ("); out_u64(len);
    out(" bytes) - run it with /"); out(name); out(crlf);
}

static void cmd_unweave(char* argv[], int argc){
    struct pkglist L;
    char full[PKG_NAME + 1];
    char path[32];
    int ambig;
    if (argc < 2){ out("usage: unweave <name>\r\n"); pkg_list(); return; }
    pkg_collect(&L);
    if (L.n == 0){ out("unweave: nothing woven yet\r\n"); return; }
    if (!pkg_find(&L, argv[1], full, &ambig)){
        if (ambig) out("unweave: ambiguous package name\r\n");
        else       out("unweave: no such package\r\n");
        return;
    }
    pkg_path(path, full);
    if (sys_unlink(path) < 0){ out("unweave: could not remove\r\n"); return; }
    out("unwoven '/"); out(full); out("'\r\n");
}

/* ------------------------------------------------------------- dispatcher */
/* Parse and run one line.  Shared by the prompt and by package replay, so a
 * package may contain exactly what you would type at the prompt. */
static void exec_line(char* line){
    char* argv[ARGV_MAX];
    int argc, ambig;

    argc = tokenize(line, argv, ARGV_MAX);
    if (argc == 0) return;

    if (argv[0][0] == '/'){
        struct pkglist L;
        char full[PKG_NAME + 1];
        pkg_collect(&L);
        if (argv[0][1] == 0){ pkg_list(); return; }
        if (pkg_find(&L, argv[0] + 1, full, &ambig)){
            pkg_run(full);
        } else if (ambig){
            out("ambiguous package '"); out(argv[0] + 1); out("'\r\n");
        } else {
            out("no such package '"); out(argv[0] + 1); out("'\r\n");
        }
        return;
    }

    {
        const struct cmd* c = find_cmd(argv[0], &ambig);
        if (c){ c->fn(argv, argc); return; }
        if (ambig){ list_candidates(argv[0]); return; }
    }
    out("unknown: "); out(argv[0]); out("  (try '?')\r\n");
}

int main(void){
    static const char banner[]    = "BerryOS init (pid 1)\r\n";
    static const char shellmsg[]  =
        "BerryOS shell, in a window of the Bui desktop.\r\n"
        "'?' lists commands, 'pane' drives the windows.\r\n";

    sys_write(1, banner, sizeof(banner) - 1);

    {
        static const char m[] = "[init] M2 demo: fork+exec+wait worker\r\n";
        sys_write(1, m, sizeof(m) - 1);
        run_worker();
    }

    sys_write(1, shellmsg, sizeof(shellmsg) - 1);

    for (;;){
        static char line[LINE_MAX];
        out(prompt);
        if (read_line(line, sizeof(line) - 1) <= 0){ out(crlf); continue; }
        exec_line(line);
    }
}
