// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stdint.h>

// Minimal NMEA parser (GGA, RMC, GSA, GSV, any talker). Portable, tested on host.
struct GpsFix {
    bool valid;        // position fix available
    double lat, lon;
    float altM;        // above mean sea level
    float geoidSepM;   // altM + geoidSepM = height above the WGS84 ellipsoid (what the sonde reports)
    uint8_t sats;      // satellites used in the fix
    float hdop;
    float speedKmh;
    float courseDeg;   // course over ground, only meaningful while moving
    uint8_t fixType;   // from GSA: 0 unknown, 1 none, 2 = 2D, 3 = 3D
    float pdop;
    uint32_t utc;      // Unix time from RMC (date + time), 0 until known
};

// Constellations reported in GSV sentences.
enum GnssSys : uint8_t { GNSS_GPS, GNSS_GLONASS, GNSS_GALILEO, GNSS_BEIDOU, GNSS_QZSS, GNSS_COUNT };
#define GNSS_MAX_SV 24 // per constellation
#define GNSS_SIGS 4    // GSV groups per constellation, one per signal (u-blox M10: L1C/A, B1I, B1C, E1 ...)

struct SatInView {
    uint8_t prn;
    int8_t elev;    // degrees, -1 unknown
    uint16_t az;    // degrees
    uint8_t snr;    // C/N0 in dB-Hz, 0 = not tracked
};

class NmeaParser {
public:
    // Returns true when a sentence with a valid checksum has just been completed.
    bool feed(char c);
    GpsFix fix = {};
    uint32_t good = 0;
    uint32_t bad = 0;
    // Satellites in view of each constellation: the last group of every signal, merged by satellite.
    SatInView sv[GNSS_COUNT][GNSS_MAX_SV] = {};
    uint8_t nSv[GNSS_COUNT] = {};
    uint32_t gsvGroups = 0; // complete GSV groups seen (debug)

private:
    void parse();
    void parseGsv(int sys, char **f, int nf);
    char buf[100];
    int len = -1; // -1 = waiting for '$'
    void mergeSigs(int sys);
    SatInView tmp[GNSS_COUNT][GNSS_MAX_SV] = {};
    uint8_t nTmp[GNSS_COUNT] = {};
    // Last complete group of each signal: one group no longer replaces the others (the bars came and went).
    SatInView sig[GNSS_COUNT][GNSS_SIGS][GNSS_MAX_SV] = {};
    uint8_t nSig[GNSS_COUNT][GNSS_SIGS] = {};
    int8_t sigId[GNSS_COUNT][GNSS_SIGS] = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1},
                                           {-1, -1, -1, -1}};
};

// Unix time from NMEA "ddmmyy" and "hhmmss.ss"; 0 when either is malformed. Portable.
uint32_t nmea_unix_time(const char *ddmmyy, const char *hhmmss);
