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

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_ICW4      0x01
#define ICW1_INIT      0x10
#define ICW4_8086      0x01
#define PIC_EOI        0x20

void pic_init(void){
    /* remap IRQ0..15 to vectors 32..47 */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC1_DATA, 0x20);        /* vector base 32 */
    io_wait();
    outb(PIC2_DATA, 0x28);        /* vector base 40 */
    io_wait();
    outb(PIC1_DATA, 4);           /* cascade: slave on IRQ2 */
    io_wait();
    outb(PIC2_DATA, 2);
    io_wait();
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* mask all IRQs except timer (0) */
    outb(PIC1_DATA, ~(1 << 0));
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(uint8_t irq){
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

/* Unmask a single IRQ line (re-enable its interrupt). */
void pic_unmask(uint8_t irq){
    if (irq < 8){
        uint8_t m = inb(PIC1_DATA);
        m &= (uint8_t)~(1u << irq);
        outb(PIC1_DATA, m);
    } else {
        uint8_t m = inb(PIC2_DATA);
        m &= (uint8_t)~(1u << (irq - 8));
        outb(PIC2_DATA, m);
    }
}
