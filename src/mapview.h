// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// Offline map screen: PNG tiles from /maps/<z>/<x>/<y>.png on the SD card, cached in PSRAM.
// Where a tile is missing, the nearest lower zoom level that has one is shown enlarged:
// the card can hold the world at low zoom and detail only where it is needed.

#define MAPVIEW_W 320
#define MAPVIEW_H 200
#define MAP_ZOOM_MIN 0 // whole world in one tile
#define MAP_ZOOM_MAX 17

enum MapCenter : uint8_t { MAPC_FIT, MAPC_ME, MAPC_TARGET, MAPC_COUNT };

struct MapScene {
    bool haveMe;
    double meLat, meLon;
    bool meMoving;
    float meCourse;
    bool haveTarget;      // sonde (or manual target)
    double tLat, tLon;
    bool targetManual;
    bool targetStale;     // no recent frames
    bool havePred;        // predicted landing
    double pLat, pLon;
    const float *trkLat;  // sonde track, oldest first
    const float *trkLon;
    int nTrk;
    bool haveSrvPred;     // prediction server (optional build)
    double srvLat, srvLon;
    int srvEta;
    bool haveShPred;      // SondeHub prediction (internet)
    double shpLat, shpLon;
    const float *shLat;   // predicted path, last point = landing
    const float *shLon;
    int nShPred;
};

// Allocates sprite and tile cache; false when PSRAM or the /maps folder is missing (see mapview_error).
bool mapview_begin(TFT_eSPI *tft, bool sdOk);
const char *mapview_error();
// Composes the map and copies it to the top of dst (the full-screen canvas).
void mapview_draw(const MapScene &s, TFT_eSprite *dst);
// Decodes at most one missing tile from SD (blocking, a few hundred ms). True when a redraw is useful.
bool mapview_service();
void mapview_zoom(int delta);
// Called with the tile path before each decode (debug breadcrumb), may stay null.
extern void (*mapview_crumb)(const char *what);
void mapview_cycle_center();
// Moves the view by screen pixels (trackball); the next centre change resets it.
void mapview_pan(int dx, int dy);
bool mapview_panned();
int mapview_zoom_level();
// Tiles of the current view still waiting to be decoded.
int mapview_pending();
const char *mapview_center_name();
// Deepest zoom level found on the SD card; beyond it the map is enlarged digitally.
int mapview_tile_zoom_max();
// "tiles 12 ok, 3 absent, 1 bad (err 4)": tells a missing file from one that cannot be decoded.
void mapview_stats(char *buf, size_t n);
// Path of the last tile that could not be decoded ("" none); timedOut = tiles given up for taking too long.
const char *mapview_last_bad(uint32_t *timedOut);
// Tile load time (SD read + PNG decode): last, average, number of tiles loaded.
void mapview_timing(uint32_t *lastMs, uint32_t *avgMs, uint32_t *count);
