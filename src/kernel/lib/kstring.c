/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Copyright (C) mrc-sk and imjumping
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, version 3 of the License, or (at your option) any later
 * version. See the LICENSE file for the full license text and the additional
 * non-commercial restriction terms that apply to this software.
 */
#include "berryos.h"

/* =====================================================================
 * The handful of memory/string routines the compiler and the kernel expect.
 *
 * These live in a real translation unit rather than as macros because clang
 * emits CALLS to them on its own: a struct assignment large enough to be
 * worth it (e.g. shuffling a window between z-order slots in the desktop)
 * compiles to a `memcpy`, and with -nostdlib there is nothing to link
 * against -- the build fails with "undefined symbol: memcpy".  Providing
 * them here is what makes ordinary C (struct copies, array sweeps) usable in
 * kernel code.
 *
 * Hand written: no SSE, no vector intrinsics, byte/word loops only.
 * =================================================================== */

void* memcpy(void* dst, const void* src, size_t n){
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    size_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void* memmove(void* dst, const void* src, size_t n){
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    size_t i;
    if (d == s || n == 0) return dst;
    if (d < s){
        for (i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

void* memset(void* dst, int c, size_t n){
    unsigned char* d = (unsigned char*)dst;
    size_t i;
    for (i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}

int memcmp(const void* a, const void* b, size_t n){
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    size_t i;
    for (i = 0; i < n; i++){
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}
