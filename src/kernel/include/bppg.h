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
#ifndef BERRYOS_BPPG_H
#define BERRYOS_BPPG_H

#include "berryos.h"
#include "bui.h"

/* =====================================================================
 * .bppg -- a BerryOS program.
 *
 * A Bppg is a text program, not a binary: BerryFS caps a file at 8*512 = 4096
 * bytes, which is far too small for anything an ELF loader would accept, but
 * plenty for a declarative program.  The file has a small header, an optional
 * Bui interface (the window the program brings up) and a run script written in
 * the ordinary BerryOS shell language:
 *
 *   bppg 1                    magic + version, first line
 *   name  Hello               display name (<= 19 chars, icon caption)
 *   icon  H                   one-glyph icon
 *   color #9BD770             icon tint
 *   ui                        -- everything below is Bui (see bui.h)
 *     window "Hello" 260 180 560 340
 *     label  20 20  "hello from a .bppg" #9BD770
 *     button 20 100 200 44 "say hi" #2A3548 :say hi from Hello
 *     button 240 100 200 44 "close"  #4A2530 :close
 *   run                       -- everything below is shell script
 *     say Hello.bppg is running
 *     season
 *
 * `run` lines are handed to the real shell, unchanged: the desktop writes them
 * into the keyboard queue, so a program has exactly the privileges and the
 * vocabulary of someone sitting at the prompt.  No private ABI, no new
 * syscall -- which is the whole point of doing it as a text program.
 * =================================================================== */

#define BPPG_NAME_MAX 19              /* 24-char BFS name minus ".bppg" */
#define BPPG_RUN_MAX  512
#define BPPG_SRC_MAX  4096            /* == BerryFS max file size */

struct bppg {
    char  name[BPPG_NAME_MAX + 1];
    char  icon;
    uint32_t color;
    int   has_ui;
    int   has_run;
    struct bui_doc ui;
    char  run[BPPG_RUN_MAX];
};

/* Parse a whole .bppg body.  Returns 0 on success, -1 when the header is not
 * recognisable (a program that cannot be identified is not run). */
int  bppg_parse(struct bppg* p, const char* src, unsigned long len);

/* Load and parse file `fname` from BerryFS.  Returns the number of bytes read,
 * or -1 when the file is missing / BerryFS is unavailable. */
long bppg_load(const char* fname, struct bppg* p);

/* Render a program back to text (used to plant the sample apps on a fresh
 * disk, so what ships on the disk is literally a text file you can edit). */
unsigned long bppg_serialize(const struct bppg* p, char* out, unsigned long cap);

#endif /* BERRYOS_BPPG_H */
