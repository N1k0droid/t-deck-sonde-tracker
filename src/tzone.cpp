// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "tzone.h"
#include "timeutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const TzZone TZ_ZONES[] = {
    {-720, "Baker Island"},
    {-660, "American Samoa, Niue"},
    {-600, "Hawaii, Tahiti"},
    {-570, "Marquesas Islands"},
    {-540, "Alaska"},
    {-480, "Los Angeles, Vancouver"},
    {-420, "Denver, Phoenix"},
    {-360, "Chicago, Mexico City"},
    {-300, "New York, Toronto, Lima"},
    {-240, "Halifax, Caracas, La Paz"},
    {-210, "Newfoundland"},
    {-180, "Buenos Aires, Sao Paulo"},
    {-120, "South Georgia"},
    {-60, "Azores, Cape Verde"},
    {0, "London, Lisbon, Reykjavik"},
    {60, "Rome, Paris, Berlin, Tunis"},
    {120, "Athens, Helsinki, Cairo"},
    {180, "Moscow, Istanbul, Riyadh"},
    {210, "Tehran"},
    {240, "Dubai, Baku"},
    {270, "Kabul"},
    {300, "Karachi, Tashkent"},
    {330, "India, Sri Lanka"},
    {345, "Nepal"},
    {360, "Dhaka, Almaty"},
    {390, "Myanmar"},
    {420, "Bangkok, Jakarta"},
    {480, "Beijing, Singapore, Perth"},
    {540, "Tokyo, Seoul"},
    {570, "Adelaide, Darwin"},
    {600, "Sydney, Guam"},
    {660, "Solomon Islands"},
    {720, "Auckland, Fiji"},
    {765, "Chatham Islands"},
    {780, "Tonga, Samoa"},
    {840, "Kiribati (Line Islands)"},
};
const int TZ_ZONE_COUNT = sizeof(TZ_ZONES) / sizeof(TZ_ZONES[0]);

int tz_zone_index(int offsetMin)
{
    int best = 0;
    for (int i = 1; i < TZ_ZONE_COUNT; i++)
        if (abs(TZ_ZONES[i].min - offsetMin) < abs(TZ_ZONES[best].min - offsetMin)) best = i;
    return best;
}

// Unix time of the last Sunday of a month (1-12) at 01:00 UTC.
static uint32_t lastSunday(int y, int m)
{
    uint32_t last = (uint32_t)(m == 12 ? tu_days_from_civil(y + 1, 1, 1) : tu_days_from_civil(y, m + 1, 1)) - 1;
    return (last - tu_weekday(last * 86400u)) * 86400u + 3600;
}

bool tz_eu_summer(uint32_t t)
{
    int y, m, d;
    tu_civil_from_days((int32_t)(t / 86400), &y, &m, &d);
    return t >= lastSunday(y, 3) && t < lastSunday(y, 10);
}

int tz_offset_s(uint32_t t, int offsetMin, int dst)
{
    int s = offsetMin * 60;
    if (dst == TZ_DST_ON || (dst == TZ_DST_EU && tz_eu_summer(t))) s += 3600;
    return s;
}

void tz_offset_name(int offsetMin, char *b, size_t n)
{
    if (!offsetMin) {
        snprintf(b, n, "UTC");
        return;
    }
    int a = abs(offsetMin);
    if (a % 60) snprintf(b, n, "UTC%c%d:%02d", offsetMin < 0 ? '-' : '+', a / 60, a % 60);
    else snprintf(b, n, "UTC%c%d", offsetMin < 0 ? '-' : '+', a / 60);
}
