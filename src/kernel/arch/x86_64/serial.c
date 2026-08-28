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

#define COM1 0x3F8

void serial_init(void){
    outb(COM1 + 1, 0x00);   /* disable interrupts */
    outb(COM1 + 3, 0x80);   /* DLAB on */
    outb(COM1 + 0, 0x01);   /* divisor low  (115200 baud) */
    outb(COM1 + 1, 0x00);   /* divisor high */
    outb(COM1 + 3, 0x03);   /* 8 data bits, no parity, 1 stop (8N1) */
    outb(COM1 + 2, 0xC7);   /* enable FIFO, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B);   /* IRQs enabled, OUT2 asserted */
}

void serial_putc(char c){
    while ((inb(COM1 + 5) & 0x20) == 0)  /* wait for THR empty */
        ;
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char* s){
    while (*s){
        serial_putc(*s);
        s++;
    }
}

void serial_hex(uint64_t v){
    static const char hexd[] = "0123456789abcdef";
    char buf[17];
    int i;
    for (i = 15; i >= 0; i--){
        buf[i] = hexd[v & 0xF];
        v >>= 4;
    }
    buf[16] = 0;
    serial_puts(buf);
}
