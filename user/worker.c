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
/* BerryOS worker program (M2).  Reached via sys_exec() from the init
 * child; runs in its own isolated process (separate page tables) and
 * exits with a distinct code so init can observe reaping. */
#include "syscall.h"
#include "string.h"

int main(void){
    sys_write(1, "[worker] exec'd by init, running in my own process!\r\n",
              sizeof("[worker] exec'd by init, running in my own process!\r\n") - 1);
    sys_exit(99);
}
