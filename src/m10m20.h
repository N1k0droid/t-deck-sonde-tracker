// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Meteomodem M10, M10+ and M20 decoder: Manchester + differential coding, checksum, GPS fields.
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stddef.h>
#include <stdint.h>

// On air: 9600 symbols/s Manchester (4800 bit/s), then differential: a data bit is 1 when two consecutive
// Manchester bits are equal. A 32-symbol header is the sync word; the frame follows: [0] length-1 (0x64 M10,
// 0x45 M20, more with auxiliary data), [1] type (0x9F M10, 0xAF M10+, 0x20 M20), data, 16-bit check.
// One frame per second. Portable, tested on the PC.

#define M10_BAUD 9600
#define M10_CAPTURE 238          // bytes after the sync: the longest frame (0x76 + 1 bytes) in symbols
#define M10_MAXFRAME 119
extern const uint8_t M10_SYNC[4];

struct M10Frame {
    bool ok;                     // checksum good
    uint8_t type;                // frame[1]
    char model[10];              // "M10", "M10+", "M20"
    char id[16];                 // serial as written on the sonde, e.g. "501-2-10550"
    bool gpsValid;
    double lat, lon, alt;
    float vH, dir, vV;
    uint8_t sats;
    uint32_t unixTime;           // 0 when unknown
    float battV;                 // 0 when unknown
    uint8_t counter;
    int len;                     // frame bytes (frame[0] + 1)
    uint8_t fixed;               // doubtful Manchester bits (a wrong symbol, a slip) when the check held (0: clean)
};

// Doubtful Manchester bits tried against the 16-bit check (2^n candidates). Serial and position are what
// matter for a hunt, so a frame fixed by trial (up to 10 doubtful bits) counts only when it agrees with
// another frame of the same sonde: the previous good one, or, with none, the previous fixed one (kept
// waiting). Agree: same serial, 1-30 s later, position within 20 m + 3 m/s of the one predicted from the
// other frame's speed and heading, altitude and speeds likewise. Without a state (tests, tools) a frame
// with up to 3 doubtful bits (8 candidates) counts alone. Every frame must have plausible values.
#define M10_MAX_DOUBTFUL_ALONE 3
#ifndef M10_MAX_DOUBTFUL
#define M10_MAX_DOUBTFUL 10
#endif

struct M10Fix {
    bool have;
    char id[16];
    double lat, lon, alt;
    float vH, dir, vV;
    uint32_t unixTime;
};
// Kept between frames: the last good frame and a fixed frame waiting for a confirmation.
struct M10State {
    M10Fix last, waiting;
};

// raw: bytes after the sync word, len of them. invert: the radio polarity used. st (optional): kept between
// calls; with it, frames with more doubtful bits are accepted when they agree with the previous one.
bool m10_decode(const uint8_t *raw, int len, bool invert, M10Frame *out, M10State *st = nullptr);
#ifdef HOST_TEST
// Doubtful bits of the first nPairs Manchester bits with realignment (and, in *plain, without).
// PC tools and tests only (tools/bench/m10cap.cpp).
int m10_doubtful(const uint8_t *raw, int len, int nPairs, int *plain, uint8_t *m = nullptr, uint8_t *doubt = nullptr);
#endif
