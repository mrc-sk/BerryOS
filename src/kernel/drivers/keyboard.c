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
#include "tcon.h"
#include "desktop.h"

/* =====================================================================
 * PS/2 keyboard driver (M3).
 *
 * The IRQ1 handler reads the scan code from port 0x60 and hands it to
 * kbd_handle_scan(), which decodes scan-code set 1 into ASCII, keeps a
 * currently-typed line with local editing (backspace), and commits it to
 * a ring buffer on Enter.  Ctrl+letter yields the control code instead of
 * the printable character, and Ctrl+X / Ctrl+C additionally commit the
 * line -- see the comment on that branch.  A user process blocked in
 * sys_read() is woken when a full line becomes available.
 *
 * Adapted from the earlier 32-bit BerryOS keyboard driver, reworked for
 * the 64-bit kernel: single keyboard owner, ring buffer + line editor,
 * and an explicit wake of the blocked reader.  The new scheduler only
 * runs TASK_READY tasks, so a blocked reader must be re-enqueued (just
 * like a parent waiting in sched_wait).
 *
 * kbd_handle_scan() is shared by the real IRQ path and the boot-time
 * self-test, so the decode/echo/ring logic is exercised even when no
 * real key is pressed.
 * =================================================================== */

#define K_RING_SZ 256
#define K_LINE_SZ 256

static char  k_ring[K_RING_SZ];
static int   k_rd, k_wr, k_count;

static char  k_line[K_LINE_SZ];
static int   k_line_len;

static int   k_shift = 0;     /* shift currently held */
static int   k_ctrl  = 0;     /* ctrl currently held */
static int   k_caps  = 0;     /* capslock toggled */
static int   k_ext   = 0;     /* mid 0xE0/0xE1 extended sequence */
static int   k_wait  = 0;     /* tid blocked in sys_read (0 = none) */

/* scan-code set 1 -> ASCII (unshifted / shifted) */
static const char k_norm[128] = {
    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',[0x07]='6',
    [0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',[0x0C]='-',[0x0D]='=',
    [0x0F]='\t',
    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',[0x15]='y',
    [0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',
    [0x1A]='[',[0x1B]=']',
    [0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',
    [0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',[0x29]='`',
    [0x2B]='\\',
    [0x2C]='z',[0x2D]='x',[0x2E]='c',[0x2F]='v',[0x30]='b',[0x31]='n',
    [0x32]='m',[0x33]=',',[0x34]='.',[0x35]='/',
    [0x39]=' ',
};

static const char k_shifted[128] = {
    [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',[0x07]='^',
    [0x08]='&',[0x09]='*',[0x0A]='(',[0x0B]=')',[0x0C]='_',[0x0D]='+',
    [0x0F]='\t',
    [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',[0x15]='Y',
    [0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',
    [0x1A]='{',[0x1B]='}',
    [0x1E]='A',[0x1F]='S',[0x20]='D',[0x21]='F',[0x22]='G',[0x23]='H',
    [0x24]='J',[0x25]='K',[0x26]='L',[0x27]=':',[0x28]='"',[0x29]='~',
    [0x2B]='|',
    [0x2C]='Z',[0x2D]='X',[0x2E]='C',[0x2F]='V',[0x30]='B',[0x31]='N',
    [0x32]='M',[0x33]='<',[0x34]='>',[0x35]='?',
    [0x39]=' ',
};

/* ---- ring buffer of completed lines (each ending in '\n') ---- */
static void ring_put(char c){
    if (k_count >= K_RING_SZ) return;
    k_ring[k_wr] = c;
    k_wr = (k_wr + 1) % K_RING_SZ;
    k_count++;
}

static int ring_get(void){
    int c;
    if (k_count == 0) return -1;
    c = k_ring[k_rd];
    k_rd = (k_rd + 1) % K_RING_SZ;
    k_count--;
    return c;
}

static void ring_reset(void){
    k_rd = k_wr = k_count = 0;
}

/* Echo a character: to the serial log always, and on screen either into the
 * desktop's terminal surface (when the desktop owns the screen) or to the VGA
 * text console.  Without the tcon branch, typing would be invisible: the shell
 * only writes its OUTPUT through sys_write, and sys_write feeds tcon. */
static void kbd_echo(char c){
    serial_putc(c);
    if (desktop_active()) tcon_putc(c);
    else                  vga_putc(c);
}

/* Map a make scan-code to ASCII, applying shift + capslock. Returns 0
 * for keys that are not directly printable (handled separately). */
static char map_char(uint8_t sc){
    char c = k_shift ? k_shifted[sc] : k_norm[sc];
    if (!c) return 0;
    if (k_caps){
        if (c >= 'a' && c <= 'z')      c = (char)(c - 'a' + 'A');
        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return c;
}

/* Decode one scan code (make/break) and update the line/ring buffer. */
static void kbd_handle_scan(uint8_t sc){
    if (k_ext){ k_ext = 0; return; }         /* 2nd byte of extended */
    if (sc == 0xE0 || sc == 0xE1){ k_ext = 1; return; }

    if (sc & 0x80){
        /* break code (key released) */
        uint8_t c = (uint8_t)(sc & 0x7F);
        if (c == 0x2A || c == 0x36) k_shift = 0;   /* shift up */
        if (c == 0x1D) k_ctrl = 0;                 /* ctrl up */
        return;
    }

    /* make code (key pressed) */
    switch (sc){
        case 0x2A: case 0x36: k_shift = 1; return;       /* shift down */
        case 0x1D: k_ctrl = 1; return;                   /* ctrl down */
        case 0x3A: k_caps ^= 1; return;                  /* capslock */
        case 0x0E:                                        /* backspace */
            if (k_line_len > 0){
                k_line_len--;
                kbd_echo('\b'); kbd_echo(' '); kbd_echo('\b');
            }
            return;
        case 0x1C: {                                      /* enter */
            int i;
            for (i = 0; i < k_line_len; i++) ring_put(k_line[i]);
            ring_put('\n');
            k_line_len = 0;
            kbd_echo('\r'); kbd_echo('\n');
            if (k_wait) sched_wake_task(k_wait);
            return;
        }
        default: break;
    }

    /* Ctrl+letter -> the classic control code (a=0x01 .. z=0x1A) instead of
     * the printable character, which is what makes editor key bindings such
     * as Ctrl+X (save) / Ctrl+C (cancel) possible at all -- without this the
     * driver simply typed a literal 'x'. */
    if (k_ctrl){
        char base = k_norm[sc];
        if (base >= 'a' && base <= 'z'){
            char ctl = (char)(base - 'a' + 1);
            if (k_line_len < K_LINE_SZ - 1) k_line[k_line_len++] = ctl;
            /* Ctrl+X and Ctrl+C also COMMIT the line.  sys_read() only
             * returns on a newline, so without this the reader would sit
             * blocked until the user also pressed Enter. */
            if (ctl == 0x18 || ctl == 0x03){
                int i;
                for (i = 0; i < k_line_len; i++) ring_put(k_line[i]);
                ring_put('\n');
                k_line_len = 0;
                kbd_echo('\r'); kbd_echo('\n');
                if (k_wait) sched_wake_task(k_wait);
            }
        }
        return;                     /* Ctrl + anything else: ignore */
    }

    if (k_line_len < K_LINE_SZ - 1){
        char c = map_char(sc);
        if (c){
            k_line[k_line_len++] = c;
            kbd_echo(c);
        }
    }
}

void keyboard_isr(void){
    uint8_t st = inb(0x64);
    if (!(st & 0x01)) return;          /* no data pending */
    kbd_handle_scan(inb(0x60));
}

void keyboard_init(void){
    k_rd = k_wr = k_count = 0;
    k_line_len = 0;
    k_shift = k_ctrl = k_caps = k_ext = k_wait = 0;
    pic_unmask(1);            /* enable IRQ1 */

    /* keyboard_selftest();   -- disabled: debug */
}

void keyboard_set_waiter(int id){
    k_wait = id;
}

int keyboard_available(void){
    return k_count;
}

int keyboard_dequeue(void){
    return ring_get();
}

/* Push text into the keyboard queue as if it had been typed, and wake a reader
 * blocked in sys_read().  This is how a .bppg program's `run` script reaches
 * the shell: the program is not given a private API, it is given the prompt.
 * The ring holds 256 bytes, so keep injected scripts short. */
void keyboard_inject(const char* s){
    int nl = 0;
    if (!s) return;
    while (*s){
        ring_put(*s);
        if (*s == '\n') nl = 1;
        s++;
    }
    if (nl && k_wait) sched_wake_task(k_wait);
}

/* Boot-time self-test: feed synthetic scan codes for "hi\n" through the
 * exact same decode path the IRQ uses, then verify the ring buffer. */
void keyboard_selftest(void){
    /* make/break for h(0x23), i(0x17), Enter(0x1C) */
    static const uint8_t seq[] = {
        0x23, 0xA3, 0x17, 0x97, 0x1C, 0x9C
    };
    static const char expect[3] = { 'h', 'i', '\n' };
    int i, ok = 1;

    for (i = 0; i < (int)(sizeof(seq) / sizeof(seq[0])); i++)
        kbd_handle_scan(seq[i]);

    if (k_count != 3) ok = 0;
    for (i = 0; i < 3; i++){
        int c = ring_get();
        if (c != (unsigned char)expect[i]) ok = 0;
    }

    serial_puts("[M1] keyboard self-test: ");
    serial_puts(ok ? "PASS (decoded 'hi')\r\n" : "FAIL\r\n");

    ring_reset();   /* don't let the test line leak into the demo */
    k_line_len = 0;
}
