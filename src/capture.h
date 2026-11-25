// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <Arduino.h>
#include "sx1262.h"
#include "sondetype.h"

// Frame capture after the sync word of the sonde type in use: one 255-byte GFSK packet (DFM, M10/M20, and
// RS41 / iMet-54 with full frames off), or the whole 320/518-byte frame by moving the packet end while it
// arrives (full frames). A cut RS41 frame is completed by erasure Reed-Solomon decoding (see rs41.cpp).

#define CAP_P1_LEN 255  // one hardware packet
#define CAP_MAX_LEN 510 // whole extended frame after the 8-byte header

struct CaptureResult {
    uint8_t type; // SondeType the radio was set for
    bool invert;
    uint8_t p1[CAP_MAX_LEN];
    int n1; // bytes received after the sync word: 255, or 312 / 510 with full frames
    float rssiSync;
    float rssiAvg;
    uint16_t devErrors;
    uint32_t tSync; // millis at sync
};

class Capture {
public:
    explicit Capture(Sx1262 &r) : radio(r) {}
    // type: a SondeType other than AUTO (bit rate, sync word and packet length differ).
    void configure(uint8_t type, double freqMhz, uint8_t bwCode, bool invert);
    uint8_t type() const { return ctype; }
    // Non-blocking while waiting for sync; blocks for the rest of the packet once sync is seen.
    bool poll(CaptureResult &out);
    bool busy() const { return inFrame; }
    uint32_t lastSyncMs() const { return lastSync; }
    float rssiNow();
    // Scan helper: retunes and returns the RSSI after settleMs. Call configure() afterwards to resume reception.
    float measureRssi(double freqMhz, uint8_t settleMs);
    // Full frames: the packet end is moved forward while receiving (Semtech AN1200.53), so whole
    // 320- and 518-byte frames arrive; falls back to 255 bytes when that does not work.
    void setFullFrames(bool on) { full = on; }
    bool fullFrames() const { return full; }
    // Full-frame counters: whole 320, whole 518, cut at 255 (sync seen too late), end not reached.
    uint32_t nFull320 = 0, nFull518 = 0, nCut = 0, nShort = 0;

private:
    void arm();
    bool readPacket(CaptureResult &out);
    bool readLong(CaptureResult &out);
    Sx1262 &radio;
    bool inv = false;
    bool inFrame = false;
    uint32_t lastSync = 0;
    bool syncSeen = false;
    bool full = false;
    uint8_t ctype = ST_RS41;
    int pktLen() const;
    float byteUs() const;
};
