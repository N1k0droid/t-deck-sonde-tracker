// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "geo.h"
#include <math.h>

static const double DEG = GEO_DEG;
static const double R_EARTH = 6371000.0;

double geo_norm360(double deg)
{
    deg = fmod(deg, 360.0);
    return deg < 0 ? deg + 360.0 : deg;
}

double geo_dir_deg(double vE, double vN) { return geo_norm360(atan2(vE, vN) / DEG); }

void geo_enu_m(double lat1, double lon1, double lat2, double lon2, double *dE, double *dN)
{
    *dN = (lat2 - lat1) * GEO_M_PER_DEG;
    *dE = (lon2 - lon1) * GEO_M_PER_DEG * cos(lat1 * DEG);
}

void geo_move_m(double lat, double lon, double dE, double dN, double *lat2, double *lon2)
{
    *lat2 = lat + dN / GEO_M_PER_DEG;
    double c = cos(lat * DEG);
    *lon2 = fabs(c) > 1e-6 ? lon + dE / (GEO_M_PER_DEG * c) : lon;
}

double geo_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    double dLat = (lat2 - lat1) * DEG, dLon = (lon2 - lon1) * DEG;
    double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * DEG) * cos(lat2 * DEG) * sin(dLon / 2) * sin(dLon / 2);
    return 2.0 * R_EARTH * atan2(sqrt(a), sqrt(1.0 - a));
}

double geo_bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    double dLon = (lon2 - lon1) * DEG;
    double y = sin(dLon) * cos(lat2 * DEG);
    double x = cos(lat1 * DEG) * sin(lat2 * DEG) - sin(lat1 * DEG) * cos(lat2 * DEG) * cos(dLon);
    return geo_norm360(atan2(y, x) / DEG);
}

void geo_dist_brg(double lat1, double lon1, double lat2, double lon2, double *distM, double *brgDeg)
{
    *distM = geo_distance_m(lat1, lon1, lat2, lon2);
    *brgDeg = geo_bearing_deg(lat1, lon1, lat2, lon2);
}

double geo_elevation_deg(double distM, double dAltM)
{
    if (distM < 1.0 && fabs(dAltM) < 1.0) return 0.0;
    return atan2(dAltM, distM) / DEG;
}

const char *geo_cardinal(double deg)
{
    static const char *N[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return N[(int)(geo_norm360(deg + 22.5) / 45.0) & 7];
}
