/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * PS/2 mouse driver (M4 GUI).  The mouse is the PS/2 "auxiliary" device on the
 * slave PIC (IRQ 12).  We enable it, turn on data reporting, and feed 3-byte
 * packets from the controller into a tiny state machine.  gui.c reads the
 * resulting pointer position / button state. */
#include "berryos.h"

static int     m_x = 0;        /* current pointer position */
static int     m_y = 0;
static int     m_buttons = 0;  /* bit0 L, bit1 R, bit2 M */
static int     m_phase = 0;    /* packet assembly phase (0,1,2) */
static uint8_t m_pkt[3];

void mouse_init(void){
    /* Enable the auxiliary (mouse) device on the PS/2 controller. */
    while (inb(0x64) & 0x02);
    outb(0x64, 0xA8);

    /* Read the controller configuration byte (command 0x20). */
    while (inb(0x64) & 0x02);
    outb(0x64, 0x20);
    uint8_t cfg = inb(0x60);
    cfg |= 0x02;     /* enable IRQ12 (mouse interrupts) */
    cfg &= ~0x20;    /* do NOT disable the mouse clock */
    while (inb(0x64) & 0x02);
    outb(0x64, 0x60);
    while (inb(0x64) & 0x02);
    outb(0x60, cfg);

    /* Enable mouse data reporting (command 0xD4 -> 0xF4). */
    while (inb(0x64) & 0x02);
    outb(0x64, 0xD4);
    while (inb(0x64) & 0x02);
    outb(0x60, 0xF4);
    (void)inb(0x60);   /* drain the 0xFA acknowledgement */

    m_phase = 0;
    pic_unmask(12);
}

/* Called from the IRQ12 ISR (isr.c).  The mouse asserts one IRQ per packet,
 * but we drain everything that is pending to be safe. */
void mouse_isr(void){
    while (inb(0x64) & 0x20){
        uint8_t d = inb(0x60);
        if (m_phase == 0){
            if ((d & 0x08) == 0) continue;   /* not the start of a packet */
            m_pkt[0] = d; m_phase = 1;
        } else if (m_phase == 1){
            m_pkt[1] = d; m_phase = 2;
        } else {
            m_pkt[2] = d; m_phase = 0;
            m_buttons = m_pkt[0] & 0x07;
            m_x += (int8_t)m_pkt[1];
            m_y -= (int8_t)m_pkt[2];   /* screen y grows downward */
        }
    }
}

void mouse_get_xy(int* x, int* y){ *x = m_x; *y = m_y; }
int  mouse_get_buttons(void){ return m_buttons; }
void mouse_set_xy(int x, int y){ m_x = x; m_y = y; }
