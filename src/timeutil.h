// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stdint.h>

// Calendar dates and Unix time (UTC, proleptic Gregorian). Portable, tested on host.

// GPS time: its epoch (1980-01-06) in Unix time, and GPS minus UTC (leap seconds, 18 since 2017).
#define GPS_EPOCH_UNIX 315964800u
#define GPS_UTC_LEAP_S 18

// Days since 1970-01-01 of y-m-d (m 1-12).
int32_t tu_days_from_civil(int y, int m, int d);
// Date of a day number (days since 1970-01-01).
void tu_civil_from_days(int32_t days, int *y, int *m, int *d);
// Unix time of a date and time; 0 when the date is not a plausible GPS date (year < 2000, month or day out of range).
uint32_t tu_unix(int y, int mo, int d, int h, int mi, int s);
// Day of the week of a Unix time: 0 Sunday ... 6 Saturday.
int tu_weekday(uint32_t unixTime);
