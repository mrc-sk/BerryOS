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
#include "string.h"

size_t strlen(const char* s){
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

void* memcpy(void* dst, const void* src, size_t n){
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void* memset(void* dst, int c, size_t n){
    char* d = (char*)dst;
    for (size_t i = 0; i < n; i++) d[i] = (char)c;
    return dst;
}

int itoa(long value, char* buf){
    if (value == 0){
        buf[0] = '0';
        return 1;
    }
    char tmp[24];
    int i = 0;
    long v = value;
    while (v > 0){
        tmp[i++] = '0' + (int)(v % 10);
        v /= 10;
    }
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    return i;
}
