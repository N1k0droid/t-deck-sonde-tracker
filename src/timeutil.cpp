// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "timeutil.h"

// Howard Hinnant's civil calendar algorithms.
int32_t tu_days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void tu_civil_from_days(int32_t days, int *y, int *m, int *d)
{
    int32_t z = days + 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

uint32_t tu_unix(int y, int mo, int d, int h, int mi, int s)
{
    if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    return (uint32_t)tu_days_from_civil(y, mo, d) * 86400u + (uint32_t)(h * 3600 + mi * 60 + s);
}

int tu_weekday(uint32_t unixTime) { return (int)((unixTime / 86400u + 4) % 7); } // 1970-01-01 was a Thursday
