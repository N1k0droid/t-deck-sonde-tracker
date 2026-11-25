// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stdint.h>
#include <stddef.h>

// SondeHub telemetry JSON (github.com/projecthorus/sondehub-infra/wiki/SondeHub-Telemetry-Format).
// Portable, tested on host. Networking is in uploader.cpp.

struct ShFrame {
    uint8_t type;       // SondeType (sondetype.h)
    char serial[16];    // as the decoder writes it: RS41 "S1234567", DFM 8 digits, M10/M20 "909-2-12345"
    char model[10];     // decoder model: "DFM-09", "M10+", "M20", "iMet-54"...
    uint32_t frame;     // RS41 frame counter; the other types the Unix time of the sonde (SondeHub rule)
    uint32_t timeUnix;  // sonde time as Unix seconds: GPS time (no leap seconds) when timeGps, else UTC
    uint16_t timeMs;
    bool timeGps;
    uint32_t rxUnix;    // UTC time of reception
    double lat, lon, alt;
    float velH, velV, heading;
    uint8_t sats;
    float batt;
    bool hasTemp;
    float temp;
    char subtype[10];   // "" = unknown
    int burstTimer;     // seconds, < 0 = unknown
    uint32_t txKhz;     // from the sonde, 0 = unknown
    float freqMhz;      // receiver frequency
    float rssi;
    bool hasUploaderPos;
    double upLat, upLon;
    float upAlt;
};

// "YYYY-MM-DDTHH:MM:SS.mmm000Z"
void sh_iso_time(uint32_t unix, uint16_t ms, char out[32]);
// "Mon, 16 Nov 2026 20:30:36 GMT"
void sh_http_date(uint32_t unix, char out[32]);
// Only the subtypes SondeHub accepts for the RS41.
bool sh_subtype_ok(const char *subtype);
// Serial in the form SondeHub wants for the type (a DFM whose serial is not known yet fails).
bool sh_serial_ok(uint8_t type, const char *serial);
// Serials of the simulators in tools/sonde_sim: never uploaded.
bool sh_serial_blocked(const char *serial);
// Sonde time from a time of day (iMet-54 sends no date): the day closest to 'now' (UTC). 0 when now is 0.
uint32_t sh_day_time(uint32_t now, int32_t secOfDay);
// Callsign: 3..15 chars, letters, digits, '-' or '_'.
bool sh_callsign_ok(const char *call);
// One JSON object, no trailing separator. Returns the length, or 0 when it does not fit.
// Type for people: the subtype when known ("RS41-SG", "DFM17"), else the type ("M20", "iMet-54").
bool sh_type_label(const ShFrame &f, char out[12]);
// Types SondeHub does not know, or a bad serial: 0 too. 'dev' adds "dev" (SondeHub then stores nothing).
size_t sh_frame_json(char *buf, size_t n, const ShFrame &f, const char *callsign, const char *swVersion, bool dev = false);
// Station position for PUT /listeners; mobile shows it as a chase car on the map.
size_t sh_listener_json(char *buf, size_t n, const char *callsign, const char *swVersion, double lat, double lon,
                        double alt, bool mobile);
