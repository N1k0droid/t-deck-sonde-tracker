// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stddef.h>
#include <stdint.h>

// Minimal JSON reader: walks the text in place, no allocation. Enough to pick fields out of SondeHub
// answers. Portable, tested on the PC.

struct JSpan {
    const char *p; // first character of the value (or of the key, without quotes)
    const char *e; // one past the last character
};

struct JIter {
    const char *p;
    const char *e;
    char close; // '}' or ']'
};

// Starts iterating the members of an object or the elements of an array.
bool json_begin(JIter &it, JSpan v);
// Next member (key filled for objects, may be null) or element. False at the end or on bad input.
bool json_next(JIter &it, JSpan *key, JSpan &val);
// Value of a member of an object, by key.
bool json_get(JSpan obj, const char *key, JSpan &val);
// Number (also a number written as a string), or def.
double json_num(JSpan v, double def);
// String value unescaped into out (\uXXXX becomes '?'). Returns the length, -1 if v is not a string.
int json_str(JSpan v, char *out, size_t n);
JSpan json_span(const char *text, size_t len);
// Member 'key' of an object as a number (def when missing) or as a string (empty when missing or not a string).
double json_get_num(JSpan obj, const char *key, double def);
void json_get_str(JSpan obj, const char *key, char *out, size_t n);
