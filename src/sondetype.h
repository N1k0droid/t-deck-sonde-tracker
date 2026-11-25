// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stdint.h>
#include <string.h>

// Sonde families the receiver can decode. AUTO: the decoders take turns while listening.
// M10 covers the Meteomodem M10 and M20 (same header and decoder); IMET54 also the iMet-50.
enum SondeType : uint8_t { ST_AUTO = 0, ST_RS41 = 1, ST_DFM = 2, ST_M10 = 3, ST_IMET54 = 4, ST_COUNT };

static inline const char *sonde_type_name(uint8_t t)
{
    static const char *const N[ST_COUNT] = {"AUTO", "RS41", "DFM", "M10/M20", "iMet54"};
    return t < ST_COUNT ? N[t] : "?";
}

// Short name for narrow columns (preset list): "M10/20" instead of "M10/M20".
static inline const char *sonde_type_short(uint8_t t) { return t == ST_M10 ? "M10/20" : sonde_type_name(t); }

// Next family in the AUTO rotation (never AUTO itself).
static inline uint8_t sonde_type_next(uint8_t t) { return t + 1 < ST_COUNT ? t + 1 : ST_RS41; }

// Case-insensitive prefix match: true when s starts with p and the word ends there.
static inline bool sonde_word_is(const char *s, const char *p)
{
    size_t i = 0;
    for (; p[i]; i++) {
        char c = s[i], d = p[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (d >= 'a' && d <= 'z') d -= 32;
        if (c != d) return false;
    }
    return !s[i] || s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n';
}

// A type word in the presets file: "RS41", "dfm", "M10", "M20", "M10/M20", "iMet54", "auto"...; -1 when unknown.
static inline int sonde_type_parse(const char *s)
{
    for (int t = 0; t < ST_COUNT; t++)
        if (sonde_word_is(s, sonde_type_name(t))) return t;
    if (sonde_word_is(s, "M10") || sonde_word_is(s, "M20") || sonde_word_is(s, "M10/20")) return ST_M10;
    if (sonde_word_is(s, "IMET-54") || sonde_word_is(s, "IMET5")) return ST_IMET54;
    return -1;
}

// Family from a SondeHub type or serial ("RS41", "DFM", "M10", "M20", "iMet-54"...); ST_AUTO when not decoded here.
static inline uint8_t sonde_type_from_sondehub(const char *type)
{
    if (!strncmp(type, "RS41", 4)) return ST_RS41;
    if (!strncmp(type, "DFM", 3)) return ST_DFM;
    if (!strncmp(type, "M10", 3) || !strncmp(type, "M20", 3)) return ST_M10;
    if (!strncmp(type, "iMet-5", 6) || !strncmp(type, "IMET5", 5)) return ST_IMET54; // not iMet-1/4
    return ST_AUTO;
}
