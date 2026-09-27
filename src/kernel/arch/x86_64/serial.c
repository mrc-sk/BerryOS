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

/* Use the QEMU debugcon port (0xE9) which is reliable and needs no init.
 * This avoids the COM1 THR-ready spin that can hang under -serial file. */
#define COM1 0xE9

void serial_init(void){
    /* debugcon needs no UART configuration */
}

void serial_putc(char c){
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
