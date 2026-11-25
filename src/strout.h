// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Text built piece by piece into a fixed buffer (JSON for SondeHub and the apps). Once a piece does not
// fit, 'full' stays set and nothing more is written. Portable, header only.
struct StrOut {
    char *buf;
    size_t n, k;
    bool full;
    StrOut(char *b, size_t size) : buf(b), n(size), k(0), full(size == 0) {}
    void add(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
    {
        if (full) return;
        va_list ap;
        va_start(ap, fmt);
        int w = vsnprintf(buf + k, n - k, fmt, ap);
        va_end(ap);
        if (w < 0 || (size_t)w >= n - k) full = true;
        else k += (size_t)w;
    }
    // A JSON string in quotes: quotes, backslashes and control characters escaped (callsigns, names, serials).
    void str(const char *s)
    {
        add("\"");
        while (s && *s && !full) {
            // Plain characters in one copy (this used to be one printf per character).
            size_t run = 0;
            while (s[run] && s[run] != '"' && s[run] != '\\' && (unsigned char)s[run] >= 0x20) run++;
            if (run) {
                if (run >= n - k) {
                    full = true;
                    return;
                }
                memcpy(buf + k, s, run);
                k += run;
                buf[k] = 0;
                s += run;
                continue;
            }
            unsigned char c = (unsigned char)*s++;
            if (c == '"' || c == '\\') add("\\%c", c);
            else add("\\u%04x", c);
        }
        add("\"");
    }
    int result() const { return full ? -1 : (int)k; } // length written, -1 when it did not fit
};
