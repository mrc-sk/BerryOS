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
#include "dev.h"
#include "ata.h"
#include "fb.h"

/* =====================================================================
 * Device/driver registry (M4 framework).
 *
 * The table is the single source of truth for boot-time device bring-up.
 * Add a new driver by appending one line -- drivers_init() does the rest.
 * =================================================================== */

static const struct driver g_drivers[] = {
    { "ata", ata_init,  NULL },
    { "fb",  fb_init,   NULL },
};

void drivers_init(void){
    int i;
    int n = (int)(sizeof(g_drivers) / sizeof(g_drivers[0]));

    serial_puts("[M4] device framework init (");
    serial_hex((uint64_t)n);
    serial_puts(" drivers)\r\n");

    for (i = 0; i < n; i++){
        serial_puts("  init ");
        serial_puts(g_drivers[i].name);
        serial_puts(" ... ");
        if (g_drivers[i].init) g_drivers[i].init();
        serial_puts("ok\r\n");
    }
}
