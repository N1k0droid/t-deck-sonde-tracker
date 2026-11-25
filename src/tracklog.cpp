// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "tracklog.h"
#include "timeutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const TRACK_CSV_HEADER =
    "utc,frame,lat,lon,alt_m,vh_ms,dir_deg,vv_ms,sats,rssi_dbm,batt_v,temp_c,my_lat,my_lon,my_alt_m,dist_m,brg_deg,pred_lat,pred_lon,o3_mpa";

void track_iso_time(uint32_t unix, char out[24])
{
    out[0] = 0;
    if (unix == 0) return;
    uint32_t rem = unix % 86400u;
    int y, m, d;
    tu_civil_from_days((int32_t)(unix / 86400u), &y, &m, &d);
    char b[48];
    snprintf(b, sizeof(b), "%04d-%02d-%02dT%02u:%02u:%02uZ", y, m, d, (unsigned)(rem / 3600), (unsigned)(rem % 3600 / 60),
             (unsigned)(rem % 60));
    memcpy(out, b, 23);
    out[23] = 0;
}

int track_csv_row(char *buf, size_t n, const TrackRow &r)
{
    char t[24];
    track_iso_time(r.utc, t);
    int k = snprintf(buf, n, "%s,%u", t, (unsigned)r.frame);
    // Every row has the same number of columns as the header.
    auto add = [&](const char *fmt, double v, bool present) {
        if (k < 0 || (size_t)k + 1 >= n) return;
        buf[k++] = ',';
        buf[k] = 0;
        if (present) k += snprintf(buf + k, n - k, fmt, v);
    };
    add("%.6f", r.lat, r.hasPos);
    add("%.6f", r.lon, r.hasPos);
    add("%.1f", r.alt, r.hasPos);
    add("%.1f", r.vH, r.hasPos);
    add("%.0f", r.vDir, r.hasPos);
    add("%.1f", r.vV, r.hasPos);
    add("%.0f", r.sats, true);
    add("%.1f", r.rssi, true);
    add("%.1f", r.battV, true);
    add("%.1f", r.tempC, r.hasTemp);
    add("%.6f", r.myLat, r.hasMe);
    add("%.6f", r.myLon, r.hasMe);
    add("%.0f", r.myAlt, r.hasMe);
    add("%.0f", r.dist, r.hasPos && r.hasMe);
    add("%.0f", r.brg, r.hasPos && r.hasMe);
    add("%.6f", r.predLat, r.hasPred);
    add("%.6f", r.predLon, r.hasPred);
    add("%.3f", r.o3Mpa, r.hasO3);
    return k;
}

// Copies CSV field idx (0-based) into out; returns false when missing or empty.
static bool csvField(const char *line, int idx, char *out, size_t n)
{
    const char *p = line;
    for (int i = 0; i < idx; i++) {
        p = strchr(p, ',');
        if (!p) return false;
        p++;
    }
    size_t len = strcspn(p, ",\r\n");
    if (len == 0 || len >= n) return false;
    memcpy(out, p, len);
    out[len] = 0;
    return true;
}

bool track_csv_parse(const char *line, TrackPoint *p)
{
    char f[32];
    if (!csvField(line, 2, f, sizeof(f))) return false;
    char *end;
    p->lat = strtod(f, &end);
    if (end == f) return false; // header
    if (!csvField(line, 3, f, sizeof(f))) return false;
    p->lon = strtod(f, nullptr);
    if (!csvField(line, 4, f, sizeof(f))) return false;
    p->alt = strtod(f, nullptr);
    p->frame = csvField(line, 1, f, sizeof(f)) ? (unsigned)strtoul(f, nullptr, 10) : 0;
    if (!csvField(line, 0, p->utc, sizeof(p->utc))) p->utc[0] = 0;
    return true;
}

bool track_id_ok(const char *id)
{
    if (!id || !id[0]) return false;
    for (const char *c = id; *c; c++) {
        bool ok = (*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
                  (*c == '-' && c != id); // M10/M20 serials: 501-2-10550
        if (!ok) return false;
    }
    return true;
}

void track_kml_begin(TrackPut put, void *ctx, const char *id)
{
    put(ctx, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n<Document>\n<name>Sonde "); // every sonde type, not only RS41
    put(ctx, id);
    put(ctx, "</name>\n<Style id=\"trk\"><LineStyle><color>ff0000ff</color><width>3</width></LineStyle></Style>\n"
             "<Placemark>\n<name>Track ");
    put(ctx, id);
    put(ctx, "</name>\n<styleUrl>#trk</styleUrl>\n<LineString>\n<altitudeMode>absolute</altitudeMode>\n<coordinates>\n");
}

void track_kml_point(TrackPut put, void *ctx, const TrackPoint &p)
{
    char b[64];
    snprintf(b, sizeof(b), "%.6f,%.6f,%.1f\n", p.lon, p.lat, p.alt);
    put(ctx, b);
}

void track_kml_end(TrackPut put, void *ctx, const char *id, const TrackPoint *last, unsigned nPoints)
{
    put(ctx, "</coordinates>\n</LineString>\n</Placemark>\n");
    if (last) {
        char b[200];
        put(ctx, "<Placemark>\n<name>");
        put(ctx, id);
        put(ctx, " last position</name>\n<description>");
        snprintf(b, sizeof(b), "%s frame %u, %.0f m, %u points", last->utc, last->frame, last->alt, nPoints);
        put(ctx, b);
        put(ctx, "</description>\n<Point>\n<altitudeMode>absolute</altitudeMode>\n<coordinates>");
        snprintf(b, sizeof(b), "%.6f,%.6f,%.1f", last->lon, last->lat, last->alt);
        put(ctx, b);
        put(ctx, "</coordinates>\n</Point>\n</Placemark>\n");
    }
    put(ctx, "</Document>\n</kml>\n");
}
