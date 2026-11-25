// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once

// Web Mercator ("slippy map") math for 256 px tiles. Portable, tested on host.

#define MAP_TILE_PX 256
#define MAP_MAX_LAT 85.05112878

// World pixel coordinates at zoom z: x grows east, y grows south, origin at 180 W / 85.05 N.
void map_to_pixel(double lat, double lon, int z, double *px, double *py);
#ifdef HOST_TEST
void map_from_pixel(double px, double py, int z, double *lat, double *lon); // the inverse, to test map_to_pixel
#endif
double map_metres_per_pixel(double lat, int z);
// Highest zoom in [zMin, zMax] at which both points fit in a w x h view with a margin.
int map_fit_zoom(double lat1, double lon1, double lat2, double lon2, int w, int h, int margin, int zMin, int zMax);
// Round length (1, 2 or 5 x 10^n metres) that spans at most maxPx pixels; returns its pixel length.
int map_scale_bar(double metresPerPixel, int maxPx, double *metres);
