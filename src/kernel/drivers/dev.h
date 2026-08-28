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
#ifndef BERRYOS_DEV_H
#define BERRYOS_DEV_H

#include "berryos.h"

/* Device/driver registry (M4 framework).
 *
 * Every kernel subsystem that owns hardware registers (ata, fb, future
 * network/audio) registers a struct driver here. drivers_init() walks the
 * table in order, calling each init and logging the result, so bringing up
 * a new device is a one-line addition to the table below -- no changes to
 * kmain. */

struct driver {
    const char* name;
    void (*init)(void);    /* bring the device up; never fails fatally */
    void (*probe)(void);   /* optional, may be NULL */
};

void drivers_init(void);

#endif /* BERRYOS_DEV_H */
