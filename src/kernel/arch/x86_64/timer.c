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

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_FREQ  1193182

static volatile uint64_t ticks;

void timer_tick(void){
    ticks++;
}

uint64_t timer_ticks(void){
    return ticks;
}

void timer_init(uint32_t hz){
    uint32_t divisor;
    if (hz == 0) hz = 100;
    divisor = PIT_FREQ / hz;
    if (divisor > 0xFFFF) divisor = 0xFFFF;

    outb(PIT_CMD, 0x36);          /* channel 0, lobyte/hibyte, mode 3, binary */
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, (divisor >> 8) & 0xFF);
}
