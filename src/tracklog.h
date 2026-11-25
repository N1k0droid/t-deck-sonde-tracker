// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stdint.h>
#include <stddef.h>

// Per-sonde track log: CSV rows (one per decoded frame) and CSV -> KML export.
// Portable, tested on host.

struct TrackRow {
    uint32_t utc;      // Unix time from the sonde GPS, 0 = unknown
    uint16_t frame;
    bool hasPos;
    double lat, lon, alt;
    float vH, vDir, vV;
    uint8_t sats;
    float rssi;
    float battV;
    bool hasTemp;
    float tempC;
    bool hasMe;        // own GPS fix
    double myLat, myLon;
    float myAlt;
    double dist, brg;  // only with hasPos && hasMe
    bool hasPred;      // predicted landing point
    double predLat, predLon;
    bool hasO3;        // ozone sensor (extended frames)
    float o3Mpa;
};

struct TrackPoint {
    char utc[24];
    unsigned frame;
    double lat, lon, alt;
};

typedef void (*TrackPut)(void *ctx, const char *s);

extern const char *const TRACK_CSV_HEADER;

// "YYYY-MM-DDThh:mm:ssZ", empty string when unix is 0.
void track_iso_time(uint32_t unix, char out[24]);
// One CSV line without newline. Missing values are left empty.
int track_csv_row(char *buf, size_t n, const TrackRow &r);
// False for the header and for rows without a position.
bool track_csv_parse(const char *line, TrackPoint *p);
// Sonde IDs become file names: letters, digits and '-' (not first).
bool track_id_ok(const char *id);

void track_kml_begin(TrackPut put, void *ctx, const char *id);
void track_kml_point(TrackPut put, void *ctx, const TrackPoint &p);
// last may be null when the track has no points.
void track_kml_end(TrackPut put, void *ctx, const char *id, const TrackPoint *last, unsigned nPoints);
