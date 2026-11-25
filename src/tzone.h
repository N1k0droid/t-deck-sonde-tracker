// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stddef.h>
#include <stdint.h>

// Local time: a UTC offset plus daylight saving time. Portable, tested on host.
#define TZ_MIN_MIN (-720) // UTC-12
#define TZ_MAX_MIN 840    // UTC+14
enum { TZ_DST_OFF = 0, TZ_DST_ON = 1, TZ_DST_EU = 2, TZ_DST_COUNT = 3 };

// Standard (winter) offsets for the picker, west to east, with a few places that use them.
struct TzZone {
    int16_t min;
    const char *places;
};
extern const TzZone TZ_ZONES[];
extern const int TZ_ZONE_COUNT;
// Index of the zone with this offset, or of the nearest one.
int tz_zone_index(int offsetMin);

// European summer time: last Sunday of March 01:00 UTC to last Sunday of October 01:00 UTC.
bool tz_eu_summer(uint32_t unixTime);
// Seconds to add to UTC for the local time.
int tz_offset_s(uint32_t unixTime, int offsetMin, int dst);
// "UTC+1", "UTC-3:30", "UTC" for 0.
void tz_offset_name(int offsetMin, char *b, size_t n);
