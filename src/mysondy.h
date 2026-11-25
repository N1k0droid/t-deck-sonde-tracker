// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stddef.h>
#include <stdint.h>

// MySondy GO app protocol (API 4.1), spoken over BLE so that MySondyGO, Hunter Tracker and TrovaLaSonda
// can use the T-Deck as their receiver. Portable: no Arduino code, tested on the PC.
//
// Device -> app, one line per second, fields separated by '/', ending "/o":
//   0/TYPE/FREQ/SIGN/BATPCT/BATV/MUTE/VER/o                       no signal
//   2/TYPE/FREQ/SER/SIGN/BATPCT/AFC/BATV/MUTE/VER/o               decoding, no position
//   1/TYPE/FREQ/SER/LAT/LON/ALT/HVEL/VVEL/SIGN/BATPCT/AFC/BK/BKTIME/BATV/MUTE/DIR/0/0/VER/o
//   3/...                                                          settings, answer to "?"
// SIGN is the signal level as a positive number (-dBm). HVEL is km/h, VVEL m/s.
// App -> device: frames "o{key=value/key/...}o", e.g. "o{f=404.50/tipo=1}o".

#define MYSONDY_VER "3.10"

struct MySondyData {
    const char *type;   // "RS41"
    double freqMhz;
    bool decoding;      // frames are arriving now
    const char *ser;    // serial, "" when unknown
    bool havePos;
    double lat, lon;
    float altM, hvelMs, vvelMs, dirDeg;
    float rssiDbm;      // negative
    int batPct;         // receiver battery
    int batMv;
    bool killKnown;     // burst-kill timer read from the calibration data
    int killSec;
};

// Status line (type 0, 1 or 2) with the trailing "/o" and no line end. Returns the length.
int mysondy_line(char *buf, size_t n, const MySondyData &d);
// Settings line (type 3). bwIndex is a protocol bandwidth index (mysondy_bw_index).
int mysondy_settings(char *buf, size_t n, const char *type, double freqMhz, int bwIndex, const char *call);

// Protocol bandwidth table: index 0..19 -> Hz.
#define MYSONDY_BW_COUNT 20
#ifdef HOST_TEST
uint32_t mysondy_bw_hz(int index); // app bandwidths are only logged since v0.91
#endif
// Nearest protocol index for a bandwidth in Hz.
int mysondy_bw_index(uint32_t hz);

// One command frame, as parsed. Only the fields with has* set were present.
struct MySondyCmd {
    bool hasFreq;
    double freqMhz;
    bool hasType;
    int type;           // 1 RS41, 2 M20, 3 M10, 4 PILOT, 5 DFM
    bool query;         // "?": send the settings line
    bool hasCall;
    char call[9];
    bool hasBw;
    int bwIndex;
};

class MySondyParser {
public:
    // Feeds one received byte; true when a complete frame was parsed into cmd().
    bool feed(char c);
    const MySondyCmd &cmd() const { return out; }

private:
    void parse();
    char buf[160];
    int len = 0;
    bool inFrame = false;
    char prev = 0;
    MySondyCmd out = {};
};
