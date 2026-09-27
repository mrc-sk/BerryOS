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
#ifndef BERRYOS_SPLASH_H
#define BERRYOS_SPLASH_H

#include "berryos.h"

/* Boot splash: the "BerrOS" wordmark centred on black plus an indeterminate
 * (chasing) progress bar underneath, mimicking the M4 boot animation.
 *
 * splash_show() is called from kmain() right after the VBE linear framebuffer
 * has been bound and before the framebuffer console is brought up, so it owns
 * the whole screen.  It is a no-op when no framebuffer is bound, and it never
 * blocks the boot path for longer than total_ms (any key skips it early).
 *
 * It runs with interrupts still masked -- the existing boot order enables IRQs
 * only after the scheduler is initialised -- so timing comes from a
 * self-contained PIT channel-2 one-shot delay rather than timer_ticks(). */
void splash_show(uint32_t total_ms);

#endif /* BERRYOS_SPLASH_H */
