// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stddef.h>
#include <stdint.h>

// Reading SondeHub (read only): latest positions of sondes in an area and the landing prediction of
// one sonde. Parses the answers of https://api.v2.sondehub.org. Portable, tested on the PC.

struct ShSonde {
    char serial[12];
    char type[12];
    double lat, lon;
    float alt, velV, velH, heading;
    float freqMhz;   // 0 when not reported
    uint32_t time;   // Unix time of the frame (sonde GPS time), 0 when unknown
};

#define SH_TRAJ_MAX 48

struct ShPrediction {
    bool ok;
    char vehicle[12];
    double landLat, landLon;
    uint32_t landTime;
    bool descending, landed;
    int n;                        // trajectory points kept (evenly spaced, last = landing)
    float lat[SH_TRAJ_MAX], lon[SH_TRAJ_MAX];
};

// GET /sondes?lat=..&lon=..&distance=..&last=..  ->  {"SERIAL": {telemetry}, ...}. Returns how many were read.
int sh_parse_sondes(const char *body, size_t len, ShSonde *out, int max);
// GET /predictions?vehicles=SERIAL  ->  [{"vehicle":..., "data": "[{time,lat,lon,alt}, ...]"}].
// scratch receives the unescaped trajectory (size it like the answer).
bool sh_parse_prediction(const char *body, size_t len, const char *serial, ShPrediction *out, char *scratch,
                         size_t scratchLen);
// GET /sondes/summary?serial=SER: launch, burst and last frame, predicted landing, recovery.
struct ShSummary {
    bool ok;
    char serial[12];
    char site[12];          // launch site ID, "" unknown
    bool haveLaunch, haveBurst, haveLast, havePredLand;
    double launchLat, launchLon, burstAlt, lastLat, lastLon, landLat, landLon;
    float lastAlt, lastVelV, freqMhz;
    uint32_t launchTime, lastTime;
    bool recovered;         // someone reported it recovered
    char type[12];
};
bool sh_parse_summary(const char *body, size_t len, const char *serial, ShSummary *out);

// GET /sites: launch sites with their schedule. Only sites within maxKm of (lat, lon) are kept.
#define SH_SITE_TIMES 8
struct ShSite {
    char id[12];
    char name[32];
    double lat, lon;
    float alt, ascent, burst, descent; // flight profile (SondeHub statistics or defaults)
    float freqMhz;                      // first frequency listed, 0 unknown
    float distKm;
    int nTimes;
    uint8_t day[SH_SITE_TIMES];         // 0 every day, 1..7 = Monday..Sunday
    uint16_t minute[SH_SITE_TIMES];     // UTC minute of the day
};
int sh_parse_sites(const char *body, size_t len, double lat, double lon, double maxKm, ShSite *out, int max);

// Next launches of the given sites in [from, to) (Unix times), soonest first.
struct ShLaunch {
    int site;           // index in the sites array
    uint32_t time;
};
int sh_next_launches(const ShSite *sites, int nSites, uint32_t from, uint32_t to, ShLaunch *out, int max);

// Tawhiri predictor answer: landing point (last point of the last stage) and burst point.
struct ShFlightPred {
    bool ok;
    double landLat, landLon, burstLat, burstLon;
    uint32_t landTime;
};
bool sh_parse_tawhiri(const char *body, size_t len, ShFlightPred *out);

// gzip member (SondeHub sends /predictions gzip-compressed even unasked): where the raw deflate data
// starts, how long it is and the uncompressed size from the trailer. False when it is not gzip.
bool sh_gzip_payload(const uint8_t *in, size_t len, size_t *off, size_t *deflateLen, uint32_t *isize);
// "2026-11-17T14:05:09.123Z" -> Unix seconds, 0 when malformed.
uint32_t sh_iso_time(const char *s);

// The followed sonde seen on SondeHub: "stale" when its position has not moved more than SH_STALE_MOVE_M for
// SH_STALE_MS, when its last frame there is older than SH_STALE_FRAME_S, or when it is not listed any more
// (listKnown: the sonde list was received at least once). Then there is no point asking often: landed, or
// nobody hears it. A new serial starts the watch again.
#define SH_STALE_MS (10UL * 60 * 1000)
#define SH_STALE_MOVE_M 50.0
#define SH_STALE_FRAME_S 600
struct ShFollowWatch {
    char serial[12];
    double lat, lon;
    uint32_t movedMs;
    bool have;
};
bool sh_follow_stale(ShFollowWatch *w, const char *serial, const ShSonde *s, bool listKnown, uint32_t nowMs,
                     uint32_t nowUnix);
