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
#include "bppg.h"

/* See bppg.h for the file format.  The parser is intentionally forgiving about
 * sections (unknown ones are skipped) and strict about the magic line, because
 * running a file whose format we guessed wrong is worse than refusing it. */

enum { SEC_NONE = 0, SEC_UI, SEC_RUN, SEC_SKIP };

static void copy_str(char* dst, const char* src, int cap){
    int i = 0;
    while (src && src[i] && i < cap - 1){ dst[i] = src[i]; i++; }
    dst[i] = 0;
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

/* First whitespace-delimited word of `line` into `w`. */
static void first_word(char* w, int cap, const char* line){
    int i = 0;
    while (*line == ' ' || *line == '\t') line++;
    while (*line && *line != ' ' && *line != '\t' && i < cap - 1) w[i++] = *line++;
    w[i] = 0;
}

/* Everything after the first whitespace run of `line`. */
static const char* rest_of(const char* line){
    while (*line == ' ' || *line == '\t') line++;
    while (*line && *line != ' ' && *line != '\t') line++;
    while (*line == ' ' || *line == '\t') line++;
    return line;
}

static uint32_t parse_color(const char* t, uint32_t dflt){
    uint32_t v = 0;
    int i;
    while (*t == ' ' || *t == '\t') t++;
    if (t[0] != '#') return dflt;
    for (i = 1; i <= 6; i++){
        char c = t[i];
        if (!c) return dflt;
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return dflt;
    }
    return v & 0x00FFFFFF;
}

int bppg_parse(struct bppg* p, const char* src, unsigned long len){
    static char line[BPPG_SRC_MAX];      /* static: this is a 4 KiB buffer */
    int  sec = SEC_NONE;
    int  seen_magic = 0;
    unsigned long i = 0;
    unsigned long rlen = 0;

    if (!src || !p) return -1;
    if (len > BPPG_SRC_MAX) len = BPPG_SRC_MAX;
    p->name[0] = 0;
    p->icon = '*';
    p->color = 0x6CA8FF;
    p->has_ui = 0;
    p->has_run = 0;
    p->run[0] = 0;
    bui_doc_init(&p->ui);

    /* One pass, one line at a time.  A file whose last line has no trailing
     * newline is still processed (the inner loop simply hits the end), which
     * matters because hand-edited files often lose that newline. */
    while (i < len){
        int n = 0;
        char word[32];
        while (i < len && src[i] != '\n'){
            if (n < (int)sizeof(line) - 1) line[n++] = src[i];
            i++;
        }
        if (i < len) i++;                          /* consume the newline */
        line[n] = 0;
        while (n > 0 && line[n - 1] == '\r') line[--n] = 0;

        first_word(word, sizeof(word), line);
        if (!word[0]) continue;                    /* blank line */
        if (word[0] == '#') continue;              /* comment */

        if (ieq(word, "bppg")){ seen_magic = 1; continue; }
        if (ieq(word, "ui")  || ieq(word, "ui:")){ sec = SEC_UI; continue; }
        if (ieq(word, "run") || ieq(word, "run:")){ sec = SEC_RUN; continue; }

        if (sec == SEC_UI){
            /* Bui has no magic line of its own inside a program; anything the
             * Bui parser rejects makes the whole program suspect. */
            if (bui_parse_line(&p->ui, line) != 0) return -1;
            p->has_ui = 1;
            continue;
        }
        if (sec == SEC_RUN){
            const char* body = line;
            int k;
            while (*body == ' ' || *body == '\t') body++;   /* de-indent */
            for (k = 0; body[k] && rlen + 2 < BPPG_RUN_MAX; k++) p->run[rlen++] = body[k];
            if (rlen + 2 < BPPG_RUN_MAX) p->run[rlen++] = '\n';
            p->has_run = 1;
            continue;
        }
        if (sec == SEC_NONE){
            if (ieq(word, "name"))  copy_str(p->name, rest_of(line), BPPG_NAME_MAX + 1);
            else if (ieq(word, "icon")){ const char* r = rest_of(line); p->icon = r[0] ? r[0] : '*'; }
            else if (ieq(word, "color")) p->color = parse_color(rest_of(line), p->color);
            else sec = SEC_SKIP;                   /* unknown header key: skip its block */
        }
    }

    if (!seen_magic) return -1;
    p->run[rlen] = 0;
    return 0;
}

long bppg_load(const char* fname, struct bppg* p){
    static char buf[BPPG_SRC_MAX + 1];
    long fd, n;
    fd = bfs_open(fname, BFS_O_RD);
    if (fd < 0) return -1;
    n = bfs_read(fd, buf, BPPG_SRC_MAX);
    bfs_close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    if (bppg_parse(p, buf, (unsigned long)n) != 0) return -1;
    return n;
}

unsigned long bppg_serialize(const struct bppg* p, char* out, unsigned long cap){
    static const char hex[] = "0123456789abcdef";
    unsigned long len = 0;
    int i;

    #define PUTS(s) do { const char* _s = (s); while (*_s && len < cap - 1) out[len++] = *_s++; } while (0)
    #define PUTC(c) do { if (len < cap - 1) out[len++] = (char)(c); } while (0)

    PUTS("bppg 1\nname ");
    PUTS(p->name);
    PUTS("\nicon ");
    PUTC(p->icon);
    PUTS("\ncolor #");
    for (i = 5; i >= 0; i--) PUTC(hex[(p->color >> (i * 4)) & 0xF]);
    PUTC('\n');
    if (p->has_ui){
        PUTS("ui\n");
        i = 0;
        while (p->ui.title[i] && len < cap - 2){ PUTC(p->ui.title[i]); i++; }
        PUTC('\n');
        for (i = 0; i < p->ui.n && len < cap - 2; i++){
            const struct bui_node* nd = &p->ui.node[i];
            int k;
            char nb[16];
            #define NUM(v) do { int _v = (v), _j = 15; char _b[16]; _b[_j] = 0; \
                if (_v == 0) _b[--_j] = '0'; \
                while (_v > 0){ _b[--_j] = (char)('0' + _v % 10); _v /= 10; } \
                for (k = 0; _b[_j + k]; k++) nb[k] = _b[_j + k]; nb[k] = 0; } while (0)
            switch (nd->kind){
            case BUI_RECT:
                PUTS("rect "); NUM(nd->x); PUTS(nb); PUTC(' '); NUM(nd->y); PUTS(nb);
                PUTC(' '); NUM(nd->w); PUTS(nb); PUTC(' '); NUM(nd->h); PUTS(nb);
                PUTS(" #"); for (k = 5; k >= 0; k--) PUTC(hex[(nd->color >> (k * 4)) & 0xF]);
                PUTC('\n');
                break;
            case BUI_LABEL:
                PUTS("label "); NUM(nd->x); PUTS(nb); PUTC(' '); NUM(nd->y); PUTS(nb);
                PUTS(" \""); PUTS(nd->text); PUTS("\" #");
                for (k = 5; k >= 0; k--) PUTC(hex[(nd->color >> (k * 4)) & 0xF]);
                PUTC('\n');
                break;
            case BUI_BUTTON:
                PUTS("button "); NUM(nd->x); PUTS(nb); PUTC(' '); NUM(nd->y); PUTS(nb);
                PUTC(' '); NUM(nd->w); PUTS(nb); PUTC(' '); NUM(nd->h); PUTS(nb);
                PUTS(" \""); PUTS(nd->text); PUTS("\" #");
                for (k = 5; k >= 0; k--) PUTC(hex[(nd->color >> (k * 4)) & 0xF]);
                PUTC(' '); PUTS(nd->action); PUTC('\n');
                break;
            case BUI_TERM:
                PUTS("term "); NUM(nd->x); PUTS(nb); PUTC(' '); NUM(nd->y); PUTS(nb);
                PUTC(' '); NUM(nd->w); PUTS(nb); PUTC(' '); NUM(nd->h); PUTS(nb);
                PUTC('\n');
                break;
            default:
                break;
            }
            #undef NUM
        }
    }
    if (p->has_run){
        const char* r = p->run;
        PUTS("run\n");
        while (*r && len < cap - 2){ PUTC(*r++); }
    }
    out[len] = 0;
    #undef PUTS
    #undef PUTC
    return len;
}
