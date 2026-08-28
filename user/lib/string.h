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
#ifndef BERRYOS_USER_STRING_H
#define BERRYOS_USER_STRING_H

typedef unsigned long size_t;

size_t strlen(const char* s);
void*  memcpy(void* dst, const void* src, size_t n);
void*  memset(void* dst, int c, size_t n);
int    itoa(long value, char* buf);   /* decimal, returns digit count */

#endif /* BERRYOS_USER_STRING_H */
