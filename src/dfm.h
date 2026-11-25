// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Graw DFM-06/09/17 decoder: Manchester, deinterleaving, Hamming(8,4), data blocks.
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stddef.h>
#include <stdint.h>

// On air: 2500 symbols/s, Manchester (1250 data bits/s). A frame is a 16-bit header (sync, 32 symbols) followed
// by 264 data bits = 528 symbols = 66 bytes: config block (7 Hamming codewords) and two data blocks
// (13 codewords each). Position, speed, time and serial number are spread over several frames.
// Portable, tested on the PC.

#define DFM_BAUD 2500            // symbol rate seen by the radio
#define DFM_PAYLOAD 66           // bytes after the sync word
#define DFM_FRAME_BYTES 70       // a whole frame on air (header + payload): frames follow without gaps
#define DFM_CAPTURE (DFM_PAYLOAD + 2 * DFM_FRAME_BYTES) // three frames per reception (206 bytes)
extern const uint8_t DFM_SYNC[4]; // header 0x45CF in Manchester symbols, MSB first

struct DfmState {
    int posMode;                 // 2: DFM-06/older layout, 3: DFM-09/17 layout, -1 unknown
    double lat, lon, alt;
    float vH, dir, vV;
    uint8_t got;                 // bit 0 lat, 1 lon, 2 alt (current cycle)
    uint8_t sats;
    float sec;                   // seconds of the minute
    int year, month, day, hour, minute;
    uint32_t snHalf[2];
    uint8_t snBits, snCh;
    uint32_t sn, snPrev;         // DFM-09/17 serial (decimal)
    uint32_t sn6, sn6Prev;       // DFM-06 serial (hex)
    uint8_t nulCh;
    uint8_t type;                // channel of the serial: 6 DFM-06, 0xA DFM-09, 0xB/0xC DFM-17, 0xD DFM-17P
    float battV;
    uint16_t frames;
    uint8_t frameCount;          // counter sent in data frame 0 (0 until seen)
};

struct DfmFrame {
    bool ok;                     // at least one data block passed Hamming
    int corrected;               // bits corrected by Hamming
    int failed;                  // codewords with 2+ errors
    bool newPos;                 // a whole position (lat, lon, alt) was completed by this frame
    char id[9];                  // serial number, "" until known
    char model[10];              // "DFM-09", "DFM-17"... "DFM" until known
    double lat, lon, alt;
    float vH, dir, vV;
    uint8_t sats;
    uint32_t unixTime;           // 0 until date and time are known
    float battV;                 // 0 when not sent
    uint16_t frameNb;            // the sonde's counter (data frame 0), or frames received until it is seen
};

void dfm_reset(DfmState *s);
// raw: DFM_PAYLOAD bytes as received after the sync. Updates the state; false when nothing was usable.
bool dfm_decode(DfmState *s, const uint8_t *raw, bool invert, DfmFrame *out);

// Several consecutive frames as received after one sync (len bytes): each following frame is checked by
// its header. out gets the last decoded frame, with newPos set if any frame completed a position.
// Returns the number of frames decoded.
int dfm_decode_stream(DfmState *s, const uint8_t *raw, int len, bool invert, DfmFrame *out);

#ifdef HOST_TEST
// For tests: Hamming(8,4) codeword of a nibble (systematic: data bits first).
uint8_t dfm_hamming_encode(uint8_t nibble);
#endif
