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
#ifndef BERRYOS_ATA_H
#define BERRYOS_ATA_H

#include "berryos.h"

/* ATA/IDE PIO driver (M4 framework).
 * Primary bus: cmd 0x1F7 / ctrl 0x3F6, data 0x1F0.  LBA28 PIO only.
 * ata_init() probes the primary master; read/write sectors back the BerryFS
 * layer (M4 day-2 feature) and the boot self-test. */

#define ATA_MODEL_LEN 41

int        ata_present(void);
const char* ata_model(void);
void       ata_init(void);
int        ata_read_sectors(uint32_t lba, uint32_t count, void* buf);
int        ata_write_sectors(uint32_t lba, uint32_t count, const void* buf);

#endif /* BERRYOS_ATA_H */
