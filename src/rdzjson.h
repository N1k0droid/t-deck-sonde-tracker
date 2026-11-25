// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stddef.h>
#include <stdint.h>

// JSON in the format of rdz_ttgo_sonde ("TTGO rdz" receivers), for apps that read it over Wi-Fi:
// HTTP GET /live.json  -> {"sonde": {...}, "gps": {...}}
// TCP port 14570 (mDNS _jsonrdz._tcp) -> one {...} object per line.
// Portable: no Arduino code, tested on the PC.

struct RdzSonde {
    bool valid;          // a frame is being received
    bool havePos;
    double lat, lon;
    float altM, vsMs, hsMs, dirDeg;
    const char *type;    // "RS41"
    const char *ser;     // serial, "" when unknown
    uint32_t frame;      // frame number from the sonde
    uint32_t time;       // Unix time from the sonde GPS, 0 when unknown
    int sats;
    double freqMhz;
    float rssiDbm;
    int burstKT, countKT; // seconds, -1 when unknown
    float battV;          // 0 when unknown
};

struct RdzGps {
    bool valid;
    double lat, lon;
    int altM, sats, dirDeg, hdop;
    float speedKmh;
};

// Whole /live.json document.
int rdz_live_json(char *buf, size_t n, const RdzSonde &s, const RdzGps &g);
// One TCP line, ending in '\n'.
int rdz_tcp_line(char *buf, size_t n, const RdzSonde &s, const RdzGps &g);
