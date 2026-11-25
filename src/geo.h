// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once

#define GEO_PI 3.14159265358979323846
#define GEO_DEG (GEO_PI / 180.0) // radians per degree
#define GEO_M_PER_DEG 111320.0 // metres per degree of latitude (and of longitude at the equator)

// Direction of a velocity from its east and north components, 0..360, 0 = north.
double geo_dir_deg(double vE, double vN);
// Flat-earth offset in metres from point 1 to point 2 (east, north): short distances only.
void geo_enu_m(double lat1, double lon1, double lat2, double lon2, double *dE, double *dN);
// Point 1 moved by dE, dN metres (flat earth); the longitude is kept near the poles.
void geo_move_m(double lat, double lon, double dE, double dN, double *lat2, double *lon2);

// Great-circle helpers (spherical earth). Portable, tested on host.
double geo_distance_m(double lat1, double lon1, double lat2, double lon2);
// Initial bearing from point 1 to point 2, 0..360, 0 = north.
double geo_bearing_deg(double lat1, double lon1, double lat2, double lon2);
// Both at once: distance (m) and initial bearing (degrees) from point 1 to point 2.
void geo_dist_brg(double lat1, double lon1, double lat2, double lon2, double *distM, double *brgDeg);
// Elevation angle of a target dAlt metres above, at ground distance distM.
double geo_elevation_deg(double distM, double dAltM);
// 8-point compass name.
const char *geo_cardinal(double deg);
// Normalizes to 0..360.
double geo_norm360(double deg);
