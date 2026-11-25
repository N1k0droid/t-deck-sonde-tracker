// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Offline map: PNG tiles from the SD card, cached in PSRAM.
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "mapview.h"
#include "mapmath.h"
#include <SD.h>
#include <PNGdec.h>

#define TILE_CACHE 12
#define PENDING_MAX 8
#define MISSING_MAX 96
#define TILE_BYTES (MAP_TILE_PX * MAP_TILE_PX * 2)
#define OVERZOOM_MAX 3 // zoom up to 3 levels past the deepest one on the card (8x enlargement)
#define NO_TILE_COLOR 0x4208

struct TileKey {
    uint8_t z;
    uint32_t x, y;
    bool operator==(const TileKey &o) const { return z == o.z && x == o.x && y == o.y; }
};

struct TileSlot {
    bool used;
    TileKey key;
    uint16_t *px;
    uint32_t stamp;
};

static TFT_eSPI *tft = nullptr;
static TFT_eSprite *spr = nullptr;
static TileSlot cache[TILE_CACHE];
static TileKey pending[PENDING_MAX];
static int nPending = 0;
static TileKey missing[MISSING_MAX];
static int nMissing = 0, missingNext = 0;
static uint32_t stampCounter = 0;
static const char *error = "map not started";
static bool ready = false;
static int zoom = 13;
static bool userZoom = false; // manual zoom overrides the automatic fit
static uint8_t centerMode = MAPC_FIT;
static int tileZoomMax = MAP_ZOOM_MAX; // deepest zoom level present on the SD card
static bool levelPresent[MAP_ZOOM_MAX + 1]; // /maps/<z> exists
static bool haveDefault = false;       // centre of the tile coverage, used when no position is known
static double defLat = 0, defLon = 0;
static int defZoom = 0;
static uint32_t tilesOk = 0, tilesNotFound = 0, tilesBad = 0;
static int lastPngError = 0;
static PNG png;
static File pngFile;

// ---- PNG decoding into a tile buffer ----

static void *pngOpen(const char *name, int32_t *size)
{
    pngFile = SD.open(name, FILE_READ);
    if (!pngFile) return nullptr;
    *size = (int32_t)pngFile.size();
    return &pngFile;
}

static void pngClose(void *)
{
    if (pngFile) pngFile.close();
}

// A tile that takes longer than this is given up (marked bad) instead of holding the T-Deck.
#define TILE_TIMEOUT_MS 2000
static uint32_t decodeStartMs = 0;
static bool decodeTimedOut = false;
static uint32_t tilesTimedOut = 0;
static char lastBadPath[48] = "";

static bool decodeLate()
{
    if (!decodeTimedOut && millis() - decodeStartMs > TILE_TIMEOUT_MS) decodeTimedOut = true;
    return decodeTimedOut;
}

static int32_t pngRead(PNGFILE *, uint8_t *buf, int32_t len)
{
    if (decodeLate()) return 0; // the decoder then stops with an error
    return pngFile ? (int32_t)pngFile.read(buf, (size_t)len) : 0;
}

static int32_t pngSeek(PNGFILE *, int32_t pos)
{
    return pngFile && pngFile.seek((uint32_t)pos) ? pos : -1;
}

static int pngDraw(PNGDRAW *d)
{
    if (decodeLate()) return 0; // stop decoding
    uint16_t *dst = (uint16_t *)d->pUser;
    if (d->y < MAP_TILE_PX && d->iWidth <= MAP_TILE_PX) {
        png.getLineAsRGB565(d, dst + d->y * MAP_TILE_PX, PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);
    }
    return 1;
}

// Time spent loading tiles (SD read + PNG decode), for the debug status.
static uint32_t tileLastMs = 0, tileTotalMs = 0, tileCount = 0;

void mapview_timing(uint32_t *lastMs, uint32_t *avgMs, uint32_t *count)
{
    *lastMs = tileLastMs;
    *avgMs = tileCount ? tileTotalMs / tileCount : 0;
    *count = tileCount;
}

void (*mapview_crumb)(const char *what) = nullptr;

static void tilePath(const TileKey &k, char *path, size_t n)
{
    snprintf(path, n, "/maps/%u/%lu/%lu.png", (unsigned)k.z, (unsigned long)k.x, (unsigned long)k.y);
}

// The caller has checked that the tile exists.
static bool decodeTile(const TileKey &k, uint16_t *px)
{
    char path[48];
    tilePath(k, path, sizeof(path));
    if (mapview_crumb) mapview_crumb(path); // if decoding ever hangs, the next start says which tile
    decodeStartMs = millis();
    decodeTimedOut = false;
    int rc = png.open(path, pngOpen, pngClose, pngRead, pngSeek, pngDraw);
    if (rc != PNG_SUCCESS) {
        lastPngError = rc;
        tilesBad++;
        return false;
    }
    bool ok = png.getWidth() == MAP_TILE_PX && png.getHeight() == MAP_TILE_PX;
    if (ok) {
        memset(px, 0xFF, TILE_BYTES);
        rc = png.decode(px, 0);
        ok = rc == PNG_SUCCESS;
    }
    png.close();
    if (decodeTimedOut) ok = false;
    if (ok) {
        tilesOk++;
    } else {
        lastPngError = decodeTimedOut ? -99 : rc ? rc : -1;
        if (decodeTimedOut) tilesTimedOut++;
        tilesBad++;
        snprintf(lastBadPath, sizeof(lastBadPath), "%s", path);
    }
    return ok;
}

// ---- Cache ----

static bool isMissing(const TileKey &k)
{
    for (int i = 0; i < nMissing; i++)
        if (missing[i] == k) return true;
    return false;
}

static void addMissing(const TileKey &k)
{
    missing[missingNext] = k;
    missingNext = (missingNext + 1) % MISSING_MAX;
    if (nMissing < MISSING_MAX) nMissing++;
}

static const uint16_t *cacheFind(const TileKey &k)
{
    for (int i = 0; i < TILE_CACHE; i++) {
        if (cache[i].used && cache[i].key == k) {
            cache[i].stamp = ++stampCounter;
            return cache[i].px;
        }
    }
    return nullptr;
}

static void queueTile(const TileKey &k)
{
    for (int i = 0; i < nPending; i++)
        if (pending[i] == k) return;
    if (nPending < PENDING_MAX) pending[nPending++] = k;
}

// Most detailed tile available for view tile (tx, ty) at zoom z: the tile itself or an ancestor,
// *up = levels climbed (enlargement 2^up). The most detailed tile not yet tried is queued for loading;
// coarser ones are queued only once it turns out to be missing, so loads stay few.
static const uint16_t *resolveTile(int z, long tx, long ty, int *up)
{
    bool queued = false;
    for (int d = 0; d <= z; d++) {
        int lz = z - d;
        if (!levelPresent[lz]) continue;
        TileKey k = {(uint8_t)lz, (uint32_t)(tx >> d), (uint32_t)(ty >> d)};
        const uint16_t *px = cacheFind(k);
        if (px) {
            *up = d;
            return px;
        }
        if (isMissing(k)) continue;
        if (!queued) {
            queueTile(k);
            queued = true;
        }
    }
    *up = 0;
    return nullptr;
}

bool mapview_service()
{
    if (!ready || nPending == 0) return false;
    TileKey k = pending[0];
    for (int i = 1; i < nPending; i++) pending[i - 1] = pending[i];
    nPending--;
    char path[48];
    tilePath(k, path, sizeof(path));
    if (!SD.exists(path)) { // missing: no cached tile is thrown away for it
        tilesNotFound++;
        addMissing(k);
        return true;
    }
    int slot = 0;
    for (int i = 0; i < TILE_CACHE; i++) {
        if (!cache[i].used) {
            slot = i;
            break;
        }
        if (cache[i].stamp < cache[slot].stamp) slot = i;
    }
    cache[slot].used = false;
    uint32_t t0 = millis();
    bool decoded = decodeTile(k, cache[slot].px);
    tileLastMs = millis() - t0;
    tileTotalMs += tileLastMs;
    tileCount++;
    if (decoded) {
        cache[slot].used = true;
        cache[slot].key = k;
        cache[slot].stamp = ++stampCounter;
    } else {
        addMissing(k);
    }
    return true;
}

// ---- Setup ----

// Where to look when no position is known: "lat lon zoom" in /maps/center.txt (written by
// tools/mappack/make_maps.py), else the whole world (listing the tile folders is too slow).
static void findDefaultCentre()
{
    defLat = defLon = 0;
    defZoom = 0;
    File f = SD.open("/maps/center.txt", FILE_READ);
    if (f) {
        char b[64] = {0};
        f.read((uint8_t *)b, sizeof(b) - 1);
        f.close();
        double lat, lon;
        int z;
        if (sscanf(b, "%lf %lf %d", &lat, &lon, &z) == 3 && lat > -85 && lat < 85 && lon >= -180 && lon <= 180) {
            defLat = lat;
            defLon = lon;
            defZoom = z < MAP_ZOOM_MIN ? MAP_ZOOM_MIN : z > MAP_ZOOM_MAX ? MAP_ZOOM_MAX : z;
        }
    }
    haveDefault = true;
}

bool mapview_begin(TFT_eSPI *t, bool sdOk)
{
    tft = t;
    if (!sdOk) {
        error = "no SD card";
        return false;
    }
    if (!SD.exists("/maps")) {
        error = "no /maps folder on the SD card";
        return false;
    }
    if (!psramFound()) {
        error = "no PSRAM";
        return false;
    }
    for (int i = 0; i < TILE_CACHE; i++) {
        cache[i].px = (uint16_t *)ps_malloc(TILE_BYTES);
        if (!cache[i].px) {
            error = "not enough memory for the tile cache";
            return false;
        }
        cache[i].used = false;
    }
    spr = new TFT_eSprite(tft);
    spr->setColorDepth(16);
    if (!spr->createSprite(MAPVIEW_W, MAPVIEW_H)) {
        error = "not enough memory for the map";
        return false;
    }
    spr->setSwapBytes(true);
    tileZoomMax = -1;
    for (int z = 0; z <= MAP_ZOOM_MAX; z++) {
        char dir[16];
        snprintf(dir, sizeof(dir), "/maps/%d", z);
        levelPresent[z] = SD.exists(dir);
        if (levelPresent[z]) tileZoomMax = z;
    }
    if (tileZoomMax < 0) {
        error = "/maps has no zoom folders (0..17)";
        return false;
    }
    if (zoom > tileZoomMax) zoom = tileZoomMax;
    findDefaultCentre();
    error = "";
    ready = true;
    return true;
}

const char *mapview_error()
{
    return error;
}

// Offset of the view from the chosen centre, in world pixels at the current zoom (trackball panning).
static double panX = 0, panY = 0;

void mapview_zoom(int delta)
{
    int old = zoom;
    zoom += delta;
    if (zoom < MAP_ZOOM_MIN) zoom = MAP_ZOOM_MIN;
    if (zoom > MAP_ZOOM_MAX) zoom = MAP_ZOOM_MAX;
    if (zoom > tileZoomMax + OVERZOOM_MAX) zoom = tileZoomMax + OVERZOOM_MAX;
    double f = ldexp(1.0, zoom - old); // keep the same spot in the middle
    panX *= f;
    panY *= f;
    userZoom = true;
    nPending = 0;
}

void mapview_cycle_center()
{
    centerMode = (centerMode + 1) % MAPC_COUNT;
    userZoom = false;
    panX = panY = 0;
    nPending = 0;
}

void mapview_pan(int dx, int dy)
{
    panX += dx;
    panY += dy;
    userZoom = true; // a moved view keeps its zoom
    nPending = 0;
}

bool mapview_panned()
{
    return panX != 0 || panY != 0;
}

int mapview_pending()
{
    return nPending;
}

int mapview_zoom_level()
{
    return zoom;
}

const char *mapview_center_name()
{
    static const char *const N[MAPC_COUNT] = {"fit", "me", "sonde"};
    return N[centerMode];
}

int mapview_tile_zoom_max()
{
    return tileZoomMax;
}

void mapview_stats(char *buf, size_t n)
{
    int k = snprintf(buf, n, "tiles %lu ok", (unsigned long)tilesOk);
    if (tilesNotFound) k += snprintf(buf + k, n - k, ", %lu absent", (unsigned long)tilesNotFound);
    if (tilesBad) snprintf(buf + k, n - k, ", %lu bad (err %d)", (unsigned long)tilesBad, lastPngError);
}

const char *mapview_last_bad(uint32_t *timedOut)
{
    *timedOut = tilesTimedOut;
    return lastBadPath;
}

// ---- Drawing ----

static void drawCross(int x, int y, uint16_t col)
{
    for (int d = -1; d <= 1; d++) {
        spr->drawLine(x - 6, y - 6 + d, x + 6, y + 6 + d, col);
        spr->drawLine(x - 6, y + 6 + d, x + 6, y - 6 + d, col);
    }
}

void mapview_draw(const MapScene &s, TFT_eSprite *dst)
{
    if (!ready) return;

    // Centre and zoom.
    double cLat = 0, cLon = 0;
    bool haveCentre = true;
    if (centerMode == MAPC_ME && s.haveMe) {
        cLat = s.meLat;
        cLon = s.meLon;
    } else if (centerMode == MAPC_TARGET && s.haveTarget) {
        cLat = s.tLat;
        cLon = s.tLon;
    } else if (s.haveMe && s.haveTarget) {
        cLat = 0.5 * (s.meLat + s.tLat);
        cLon = 0.5 * (s.meLon + s.tLon);
        if (centerMode == MAPC_FIT && !userZoom) {
            int zTop = tileZoomMax + OVERZOOM_MAX < MAP_ZOOM_MAX ? tileZoomMax + OVERZOOM_MAX : MAP_ZOOM_MAX;
            int z = map_fit_zoom(s.meLat, s.meLon, s.tLat, s.tLon, MAPVIEW_W, MAPVIEW_H, 24, MAP_ZOOM_MIN, zTop);
            if (z != zoom) nPending = 0; // tiles queued for the old zoom are not decoded any more
            zoom = z;
        }
    } else if (s.haveMe) {
        cLat = s.meLat;
        cLon = s.meLon;
    } else if (s.haveTarget) {
        cLat = s.tLat;
        cLon = s.tLon;
    } else if (haveDefault) {
        // Nothing to track yet: show the mapped area as a whole.
        cLat = defLat;
        cLon = defLon;
        if (!userZoom) zoom = defZoom;
    } else {
        haveCentre = false;
    }
    if (!haveCentre) {
        spr->fillSprite(TFT_BLACK);
        spr->setTextColor(TFT_ORANGE, TFT_BLACK);
        spr->drawString("no position yet: nothing to centre on", 10, 90, 2);
        spr->pushToSprite(dst, 0, 0);
        return;
    }

    double cx, cy;
    map_to_pixel(cLat, cLon, zoom, &cx, &cy);
    cx += panX;
    cy += panY;
    double left = cx - MAPVIEW_W / 2.0, top = cy - MAPVIEW_H / 2.0;

    // Tiles: each visible tile at the view zoom shows itself or its nearest available ancestor.
    long worldPx = (long)MAP_TILE_PX << zoom;
    long nTiles = 1L << zoom;
    long wl = (long)floor(left), wt = (long)floor(top); // view origin in world pixels at 'zoom'
    long tx0 = (long)floor(left / MAP_TILE_PX), ty0 = (long)floor(top / MAP_TILE_PX);
    const uint16_t *cellPx[2][3] = {};
    int cellUp[2][3] = {};
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 3; c++) {
            long ty = ty0 + r, tx = ((tx0 + c) % nTiles + nTiles) % nTiles;
            if (ty < 0 || ty >= nTiles) continue;
            if ((ty0 + r) * MAP_TILE_PX > wt + MAPVIEW_H - 1 || (tx0 + c) * MAP_TILE_PX > wl + MAPVIEW_W - 1) continue;
            cellPx[r][c] = resolveTile(zoom, tx, ty, &cellUp[r][c]);
        }
    }
    // Integer only (no double math per pixel: the ESP32-S3 has no double-precision unit). '>> 8' is a floor
    // division by the tile size, also for negative X (arithmetic shift).
    static_assert(MAP_TILE_PX == 256, "the pixel loop shifts by 8");
    static uint16_t line[MAPVIEW_W];
    const long xw0 = (wl % worldPx + worldPx) % worldPx; // first column wrapped around the antimeridian
    for (int sy = 0; sy < MAPVIEW_H; sy++) {
        long Y = wt + sy;
        int r = (int)((Y >> 8) - ty0);
        bool rowOk = Y >= 0 && Y < worldPx && r >= 0 && r < 2;
        long Xw = xw0;
        for (int sx = 0; sx < MAPVIEW_W; sx++, Xw = Xw + 1 == worldPx ? 0 : Xw + 1) {
            int c = (int)(((wl + sx) >> 8) - tx0);
            const uint16_t *t = rowOk && c >= 0 && c < 3 ? cellPx[r][c] : nullptr;
            if (!t) {
                line[sx] = NO_TILE_COLOR;
                continue;
            }
            int d = cellUp[r][c];
            line[sx] = t[((Y >> d) & 255) * MAP_TILE_PX + ((Xw >> d) & 255)];
        }
        spr->pushImage(0, sy, MAPVIEW_W, 1, line);
    }

    auto toScreen = [&](double lat, double lon, int *x, int *y) {
        double px, py;
        map_to_pixel(lat, lon, zoom, &px, &py);
        *x = (int)lround(px - floor(left));
        *y = (int)lround(py - floor(top));
    };
    int x, y, x2, y2;

    // Sonde track (each point projected once).
    if (s.nTrk > 0) toScreen(s.trkLat[0], s.trkLon[0], &x, &y);
    for (int i = 1; i < s.nTrk; i++) {
        toScreen(s.trkLat[i], s.trkLon[i], &x2, &y2);
        spr->drawLine(x, y, x2, y2, TFT_RED);
        spr->drawLine(x + 1, y, x2 + 1, y2, TFT_RED);
        x = x2;
        y = y2;
    }
    // Line from me to the target.
    if (s.haveMe && s.haveTarget) {
        toScreen(s.meLat, s.meLon, &x, &y);
        toScreen(s.tLat, s.tLon, &x2, &y2);
        spr->drawLine(x, y, x2, y2, TFT_BLUE);
    }
    // SondeHub prediction (from the internet): dashed path to the landing point, cyan cross.
    if (s.haveShPred) {
        if (s.nShPred > 0) toScreen(s.shLat[0], s.shLon[0], &x, &y);
        for (int i = 1; i < s.nShPred; i++) {
            toScreen(s.shLat[i], s.shLon[i], &x2, &y2);
            if (i % 2) spr->drawLine(x, y, x2, y2, TFT_CYAN);
            x = x2;
            y = y2;
        }
        toScreen(s.shpLat, s.shpLon, &x, &y);
        drawCross(x, y, TFT_CYAN);
    }
    if (s.haveSrvPred) {
        toScreen(s.srvLat, s.srvLon, &x, &y);
        drawCross(x, y, TFT_ORANGE);
    }
    if (s.havePred) {
        toScreen(s.pLat, s.pLon, &x, &y);
        drawCross(x, y, TFT_MAGENTA);
    }
    if (s.haveTarget) {
        toScreen(s.tLat, s.tLon, &x, &y);
        uint16_t col = s.targetManual ? TFT_YELLOW : s.targetStale ? TFT_ORANGE : TFT_RED;
        spr->fillCircle(x, y, 6, TFT_WHITE);
        spr->fillCircle(x, y, 4, col);
    }
    if (s.haveMe) {
        toScreen(s.meLat, s.meLon, &x, &y);
        if (s.meMoving) {
            float a = s.meCourse * DEG_TO_RAD;
            auto px = [&](float ang, float r) { return x + (int)lroundf(sinf(ang) * r); };
            auto py = [&](float ang, float r) { return y - (int)lroundf(cosf(ang) * r); };
            spr->fillTriangle(px(a, 11), py(a, 11), px(a + 2.5f, 8), py(a + 2.5f, 8), px(a - 2.5f, 8), py(a - 2.5f, 8),
                              TFT_WHITE);
            spr->fillTriangle(px(a, 8), py(a, 8), px(a + 2.5f, 6), py(a + 2.5f, 6), px(a - 2.5f, 6), py(a - 2.5f, 6),
                              TFT_BLUE);
        } else {
            spr->fillCircle(x, y, 6, TFT_WHITE);
            spr->fillCircle(x, y, 4, TFT_BLUE);
        }
    }

    // Scale bar.
    double metres;
    int barPx = map_scale_bar(map_metres_per_pixel(cLat, zoom), 90, &metres);
    spr->fillRect(6, MAPVIEW_H - 20, barPx + 8, 16, TFT_WHITE);
    spr->fillRect(10, MAPVIEW_H - 8, barPx, 2, TFT_BLACK);
    char b[16];
    if (metres >= 1000) snprintf(b, sizeof(b), "%.0f km", metres / 1000.0);
    else snprintf(b, sizeof(b), "%.0f m", metres);
    spr->setTextColor(TFT_BLACK, TFT_WHITE);
    spr->drawString(b, 10, MAPVIEW_H - 19, 1);

    // Attribution required by the OpenStreetMap licence (ODbL).
    static const char *const ATTR = "(c) OpenStreetMap";
    int aw = spr->textWidth(ATTR, 1) + 4;
    spr->fillRect(MAPVIEW_W - aw, MAPVIEW_H - 10, aw, 10, TFT_WHITE);
    spr->drawString(ATTR, MAPVIEW_W - aw + 2, MAPVIEW_H - 9, 1);

    spr->pushToSprite(dst, 0, 0);
}
