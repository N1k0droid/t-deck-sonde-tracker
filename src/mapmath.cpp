// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "mapmath.h"
#include "geo.h"
#include <math.h>

static const double PI_D = GEO_PI;

void map_to_pixel(double lat, double lon, int z, double *px, double *py)
{
    if (lat > MAP_MAX_LAT) lat = MAP_MAX_LAT;
    if (lat < -MAP_MAX_LAT) lat = -MAP_MAX_LAT;
    double world = (double)MAP_TILE_PX * (double)(1UL << z);
    double phi = lat * PI_D / 180.0;
    *px = (lon + 180.0) / 360.0 * world;
    *py = (1.0 - log(tan(phi) + 1.0 / cos(phi)) / PI_D) / 2.0 * world;
}

#ifdef HOST_TEST
void map_from_pixel(double px, double py, int z, double *lat, double *lon)
{
    double world = (double)MAP_TILE_PX * (double)(1UL << z);
    *lon = px / world * 360.0 - 180.0;
    double n = PI_D * (1.0 - 2.0 * py / world);
    *lat = atan(sinh(n)) * 180.0 / PI_D;
}
#endif

double map_metres_per_pixel(double lat, int z)
{
    return 156543.03392 * cos(lat * PI_D / 180.0) / (double)(1UL << z);
}

int map_fit_zoom(double lat1, double lon1, double lat2, double lon2, int w, int h, int margin, int zMin, int zMax)
{
    for (int z = zMax; z > zMin; z--) {
        double x1, y1, x2, y2;
        map_to_pixel(lat1, lon1, z, &x1, &y1);
        map_to_pixel(lat2, lon2, z, &x2, &y2);
        if (fabs(x1 - x2) <= w - 2 * margin && fabs(y1 - y2) <= h - 2 * margin) return z;
    }
    return zMin;
}

int map_scale_bar(double metresPerPixel, int maxPx, double *metres)
{
    static const double STEP[3] = {5.0, 2.0, 1.0};
    double maxM = metresPerPixel * maxPx;
    double mag = pow(10.0, floor(log10(maxM)));
    double best = mag;
    for (int i = 0; i < 3; i++) {
        if (STEP[i] * mag <= maxM) {
            best = STEP[i] * mag;
            break;
        }
    }
    *metres = best;
    return (int)(best / metresPerPixel + 0.5);
}
