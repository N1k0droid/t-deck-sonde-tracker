// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// InterMet iMet-54 (and iMet-50) decoder: 8N1 characters, 8x8 interleaving, Hamming(8,4), CRC32.
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stddef.h>
#include <stdint.h>

// On air: 4798 bit/s GFSK, 8N1 characters (start 0, 8 bits, stop 1). Preamble 0x00 0xAA repeated,
// then 0x24 x4 and 0x42; the receiver syncs on "00 AA 24 24" (40 bits) and the decoder skips the
// next 3 characters. 216 data characters follow: 27 blocks of 64 bits, each an 8x8 transpose;
// every 8 bits is a Hamming(8,4) codeword carrying a nibble; 108 frame bytes.
// No velocities and no date in the frame: speeds come from consecutive positions.

#define IMET54_BAUD 4798
#define IMET54_SKIP_BITS 30      // 0x24 0x24 0x42 after the sync word
#define IMET54_CHARS 216
#define IMET54_CAPTURE 274       // bytes after the sync: (30 + 216 * 10) bits
#define IMET54_FRAME 108
extern const uint8_t IMET54_SYNC[5];

struct Imet54State {
    bool have;
    uint32_t sn;
    int32_t secOfDay;
    double lat, lon, alt;
};

struct Imet54Frame {
    bool ok;                     // CRC good (or a clean frame, see imet54.cpp)
    bool crcStd, crcCont;
    int corrected;               // Hamming codewords corrected
    int bytes;                   // frame bytes rebuilt
    uint32_t sn;
    char id[16];                 // serial number, decimal
    char model[10];              // "iMet-54" or "iMet-50"
    bool gpsValid;
    double lat, lon, alt;
    bool velValid;               // speeds from the previous frame of the same sonde
    float vH, dir, vV;
    int32_t secOfDay;            // GPS time of day (s), -1 unknown
    float tempC, rh;             // -273.15 / -1 when missing
    uint16_t status;
};

void imet54_reset(Imet54State *st);
// raw: bytes after the sync word, len of them (255 still holds the GPS block). invert: radio polarity.
bool imet54_decode(Imet54State *st, const uint8_t *raw, int len, bool invert, Imet54Frame *out);
