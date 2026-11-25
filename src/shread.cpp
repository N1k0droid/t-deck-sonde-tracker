// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "shread.h"
#include "json_lite.h"
#include "geo.h"
#include "timeutil.h"
#include <stdio.h>
#include <string.h>

bool sh_follow_stale(ShFollowWatch *w, const char *serial, const ShSonde *s, bool listKnown, uint32_t nowMs,
                     uint32_t nowUnix)
{
    if (strcmp(w->serial, serial)) { // another sonde: watch from now
        snprintf(w->serial, sizeof(w->serial), "%s", serial);
        w->have = false;
        w->movedMs = nowMs;
    }
    if (!s) return listKnown;
    if (!w->have || geo_distance_m(w->lat, w->lon, s->lat, s->lon) > SH_STALE_MOVE_M) {
        w->have = true;
        w->lat = s->lat;
        w->lon = s->lon;
        w->movedMs = nowMs;
    }
    if (nowUnix && s->time && (int32_t)(nowUnix - s->time) > SH_STALE_FRAME_S) return true;
    return nowMs - w->movedMs > SH_STALE_MS;
}

uint32_t sh_iso_time(const char *s)
{
    int y, mo, d, h, mi, se;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
    if (h > 23 || mi > 59 || se > 60) return 0;
    return tu_unix(y, mo, d, h, mi, se);
}

bool sh_parse_summary(const char *body, size_t len, const char *serial, ShSummary *out)
{
    memset(out, 0, sizeof(*out));
    JSpan all = json_span(body, len), sondes, s;
    if (!json_get(all, "radiosondes", sondes) || !json_get(sondes, serial, s)) return false;
    snprintf(out->serial, sizeof(out->serial), "%s", serial);
    json_get_str(s, "launchsite", out->site, sizeof(out->site));
    JSpan v;
    if (json_get(s, "recovery", v) && *v.p == '{') {
        JSpan r;
        out->recovered = json_get(v, "recovered", r) && *r.p == 't';
    }
    if (json_get(s, "predicted_landing", v) && *v.p == '{') {
        out->landLat = json_get_num(v, "lat", 0);
        out->landLon = json_get_num(v, "lon", 0);
        out->havePredLand = out->landLat != 0 || out->landLon != 0;
    }
    // summary: [launch, highest point, last frame] (fewer entries for short tracks).
    JSpan sum, el;
    JIter it;
    if (json_get(s, "summary", sum) && json_begin(it, sum)) {
        int n = 0;
        JSpan first = {}, mid = {}, lastEl = {};
        while (json_next(it, nullptr, el)) {
            if (n == 0) first = el;
            if (n == 1) mid = el;
            lastEl = el;
            n++;
        }
        char dt[40];
        if (n >= 1) {
            out->haveLaunch = true;
            out->launchLat = json_get_num(first, "lat", 0);
            out->launchLon = json_get_num(first, "lon", 0);
            json_get_str(first, "datetime", dt, sizeof(dt));
            out->launchTime = sh_iso_time(dt);
            json_get_str(first, "type", out->type, sizeof(out->type));
        }
        if (n >= 3) {
            out->haveBurst = true;
            out->burstAlt = json_get_num(mid, "alt", 0);
        }
        if (n >= 1) {
            out->haveLast = true;
            out->lastLat = json_get_num(lastEl, "lat", 0);
            out->lastLon = json_get_num(lastEl, "lon", 0);
            out->lastAlt = (float)json_get_num(lastEl, "alt", 0);
            out->lastVelV = (float)json_get_num(lastEl, "vel_v", 0);
            out->freqMhz = (float)json_get_num(lastEl, "frequency", 0);
            json_get_str(lastEl, "datetime", dt, sizeof(dt));
            out->lastTime = sh_iso_time(dt);
        }
    }
    out->ok = out->haveLast || out->havePredLand;
    return out->ok;
}

int sh_parse_sites(const char *body, size_t len, double lat, double lon, double maxKm, ShSite *out, int max)
{
    JIter it;
    if (!json_begin(it, json_span(body, len)) || it.close != '}') return 0;
    JSpan key, site;
    int n = 0;
    if (max <= 0) return 0;
    while (json_next(it, &key, site)) { // keeps the 'max' nearest sites, nearest first
        JSpan pos, p0, p1;
        JIter pi;
        if (!json_get(site, "position", pos) || !json_begin(pi, pos) || !json_next(pi, nullptr, p0) ||
            !json_next(pi, nullptr, p1))
            continue;
        ShSite s;
        memset(&s, 0, sizeof(s));
        s.lon = json_num(p0, 0); // [lon, lat]
        s.lat = json_num(p1, 0);
        s.distKm = (float)(geo_distance_m(lat, lon, s.lat, s.lon) / 1000.0);
        if (s.distKm > maxKm) continue;
        JSpan times, t;
        JIter ti;
        if (!json_get(site, "times", times) || !json_begin(ti, times)) continue;
        while (s.nTimes < SH_SITE_TIMES && json_next(ti, nullptr, t)) {
            char b[16];
            int d, h, m;
            if (json_str(t, b, sizeof(b)) > 0 && sscanf(b, "%d:%d:%d", &d, &h, &m) == 3 && d >= 0 && d <= 7 &&
                h < 24 && m < 60) {
                s.day[s.nTimes] = (uint8_t)d;
                s.minute[s.nTimes] = (uint16_t)(h * 60 + m);
                s.nTimes++;
            }
        }
        if (!s.nTimes) continue; // no schedule: nothing to predict
        size_t kl = key.e - key.p < (long)sizeof(s.id) - 1 ? key.e - key.p : sizeof(s.id) - 1;
        memcpy(s.id, key.p, kl);
        json_get_str(site, "station_name", s.name, sizeof(s.name));
        s.alt = (float)json_get_num(site, "alt", 0);
        s.ascent = (float)json_get_num(site, "ascent_rate", 5.0);
        s.burst = (float)json_get_num(site, "burst_altitude", 26000);
        s.descent = (float)json_get_num(site, "descent_rate", 6.0);
        // rs_types: ["41", ...] or [["41", "404.5"], ...]: the second form gives the frequency.
        JSpan rt, r0, rf;
        JIter ri, rj;
        if (json_get(site, "rs_types", rt) && json_begin(ri, rt) && json_next(ri, nullptr, r0) && *r0.p == '[' &&
            json_begin(rj, r0) && json_next(rj, nullptr, rf) && json_next(rj, nullptr, rf))
            s.freqMhz = (float)json_num(rf, 0);
        // out is kept sorted by distance: when full, a new site replaces the farthest one if it is nearer.
        if (n == max && out[max - 1].distKm <= s.distKm) continue;
        int i = n < max ? n++ : max - 1;
        while (i > 0 && out[i - 1].distKm > s.distKm) {
            out[i] = out[i - 1];
            i--;
        }
        out[i] = s;
    }
    return n;
}

int sh_next_launches(const ShSite *sites, int nSites, uint32_t from, uint32_t to, ShLaunch *out, int max)
{
    int n = 0;
    for (uint32_t day = from / 86400; day * 86400 < to; day++) {
        int wd = tu_weekday(day * 86400u);
        int isoDow = wd ? wd : 7; // Monday = 1 ... Sunday = 7
        for (int s = 0; s < nSites; s++) {
            for (int k = 0; k < sites[s].nTimes; k++) {
                if (sites[s].day[k] && sites[s].day[k] != isoDow) continue;
                uint32_t t = day * 86400 + sites[s].minute[k] * 60u;
                if (t < from || t >= to) continue;
                // Insert sorted by time; drop the latest when full.
                int pos = n;
                while (pos > 0 && out[pos - 1].time > t) pos--;
                if (pos >= max) continue;
                int last = n < max ? n : max - 1;
                for (int j = last; j > pos; j--) out[j] = out[j - 1];
                out[pos] = {s, t};
                if (n < max) n++;
            }
        }
    }
    return n;
}

bool sh_parse_tawhiri(const char *body, size_t len, ShFlightPred *out)
{
    memset(out, 0, sizeof(*out));
    JSpan stages, st, traj, pt, lastPt = {};
    JIter si, ti;
    if (!json_get(json_span(body, len), "prediction", stages) || !json_begin(si, stages)) return false;
    bool any = false;
    while (json_next(si, nullptr, st)) {
        char name[16];
        json_get_str(st, "stage", name, sizeof(name));
        if (!json_get(st, "trajectory", traj) || !json_begin(ti, traj)) continue;
        while (json_next(ti, nullptr, pt)) {
            lastPt = pt;
            any = true;
        }
        if (!strcmp(name, "ascent") && any) {
            out->burstLat = json_get_num(lastPt, "latitude", 0);
            out->burstLon = json_get_num(lastPt, "longitude", 0);
            if (out->burstLon > 180) out->burstLon -= 360;
        }
    }
    if (!any) return false;
    out->landLat = json_get_num(lastPt, "latitude", 0);
    out->landLon = json_get_num(lastPt, "longitude", 0);
    if (out->landLon > 180) out->landLon -= 360; // Tawhiri gives 0..360
    char dt[40];
    json_get_str(lastPt, "datetime", dt, sizeof(dt));
    out->landTime = sh_iso_time(dt);
    out->ok = true;
    return true;
}

bool sh_gzip_payload(const uint8_t *in, size_t len, size_t *off, size_t *deflateLen, uint32_t *isize)
{
    if (len < 18 || in[0] != 0x1F || in[1] != 0x8B || in[2] != 8) return false;
    uint8_t flg = in[3];
    size_t p = 10;
    if (flg & 0x04) { // FEXTRA
        if (p + 2 > len) return false;
        p += 2 + (in[p] | in[p + 1] << 8);
    }
    if (flg & 0x08) { // FNAME
        while (p < len && in[p]) p++;
        p++;
    }
    if (flg & 0x10) { // FCOMMENT
        while (p < len && in[p]) p++;
        p++;
    }
    if (flg & 0x02) p += 2; // FHCRC
    if (p + 8 > len) return false;
    *off = p;
    *deflateLen = len - 8 - p;
    *isize = in[len - 4] | in[len - 3] << 8 | in[len - 2] << 16 | (uint32_t)in[len - 1] << 24;
    return true;
}

int sh_parse_sondes(const char *body, size_t len, ShSonde *out, int max)
{
    JIter it;
    if (!json_begin(it, json_span(body, len)) || it.close != '}') return 0;
    JSpan key, val;
    int n = 0;
    while (n < max && json_next(it, &key, val)) {
        ShSonde &s = out[n];
        memset(&s, 0, sizeof(s));
        size_t kl = key.e - key.p < (long)sizeof(s.serial) - 1 ? key.e - key.p : sizeof(s.serial) - 1;
        memcpy(s.serial, key.p, kl);
        s.serial[kl] = 0;
        JSpan lat, lon;
        if (!json_get(val, "lat", lat) || !json_get(val, "lon", lon)) continue;
        s.lat = json_num(lat, 0);
        s.lon = json_num(lon, 0);
        s.alt = (float)json_get_num(val, "alt", 0);
        s.velV = (float)json_get_num(val, "vel_v", 0);
        s.velH = (float)json_get_num(val, "vel_h", 0);
        s.heading = (float)json_get_num(val, "heading", 0);
        s.freqMhz = (float)json_get_num(val, "frequency", 0);
        json_get_str(val, "type", s.type, sizeof(s.type));
        char dt[40];
        json_get_str(val, "datetime", dt, sizeof(dt));
        s.time = sh_iso_time(dt);
        n++;
    }
    return n;
}

bool sh_parse_prediction(const char *body, size_t len, const char *serial, ShPrediction *out, char *scratch,
                         size_t scratchLen)
{
    memset(out, 0, sizeof(*out));
    JIter it;
    if (!json_begin(it, json_span(body, len)) || it.close != ']') return false;
    JSpan el;
    while (json_next(it, nullptr, el)) {
        char veh[12];
        json_get_str(el, "vehicle", veh, sizeof(veh));
        if (serial && serial[0] && strcmp(veh, serial)) continue;
        snprintf(out->vehicle, sizeof(out->vehicle), "%s", veh);
        out->descending = json_get_num(el, "descending", 0) != 0;
        out->landed = json_get_num(el, "landed", 0) != 0;
        JSpan data;
        if (!json_get(el, "data", data)) return false;
        int dl = json_str(data, scratch, scratchLen);
        // A cut trajectory would make a point in mid flight the landing: no prediction rather than a wrong one.
        if (dl <= 0 || dl >= (int)scratchLen - 1) return false;
        // Trajectory: count the points, then keep SH_TRAJ_MAX evenly spaced ones (always the last).
        JSpan traj = json_span(scratch, dl), pt;
        JIter ti;
        int total = 0;
        if (!json_begin(ti, traj)) return false;
        while (json_next(ti, nullptr, pt)) total++;
        if (!total) return false;
        json_begin(ti, traj);
        int idx = 0, kept = 0;
        int slots = total < SH_TRAJ_MAX ? total : SH_TRAJ_MAX;
        // Point j of the kept ones is the input point j * (total - 1) / (slots - 1): first and last included.
        auto target = [&](int j) { return slots > 1 ? (int)((long)j * (total - 1) / (slots - 1)) : 0; };
        while (json_next(ti, nullptr, pt)) {
            bool last = idx == total - 1;
            if (kept < slots && idx == target(kept)) {
                out->lat[kept] = (float)json_get_num(pt, "lat", 0);
                out->lon[kept] = (float)json_get_num(pt, "lon", 0);
                kept++;
            }
            if (last) {
                out->landLat = json_get_num(pt, "lat", 0);
                out->landLon = json_get_num(pt, "lon", 0);
                out->landTime = (uint32_t)json_get_num(pt, "time", 0);
            }
            idx++;
        }
        out->n = kept;
        out->ok = true;
        return true;
    }
    return false;
}
