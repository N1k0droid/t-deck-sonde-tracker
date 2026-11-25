// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "rdzjson.h"
#include "strout.h"
#include <stdio.h>

// Body of the sonde object, without braces (shared by both formats).
static void sondeFields(StrOut &o, const RdzSonde &s)
{
    if (s.havePos) {
        o.add("\"lat\": %.5f,\"lon\": %.5f,\"alt\": %.1f,", s.lat, s.lon, s.altM);
        o.add("\"vs\": %.1f,\"hs\": %.1f,\"climb\": %.1f,\"speed\": %.1f,\"dir\": %.1f,", s.vsMs, s.hsMs, s.vsMs, s.hsMs,
              s.dirDeg);
    }
    const char *ser = s.ser ? s.ser : "";
    // rssi as rdz reports it: the SX127x register value, i.e. -2 x dBm.
    int rssi = (int)(-2.0f * s.rssiDbm + 0.5f);
    o.add("\"type\":");
    o.str(s.type);
    o.add(",\"id\": ");
    o.str(ser);
    o.add(",\"ser\": ");
    o.str(ser);
    o.add(",\"frame\": %u,\"vframe\": %u,\"time\": %u,\"sats\": %d,", (unsigned)s.frame, (unsigned)s.frame,
          (unsigned)s.time, s.sats);
    o.add("\"freq\": %.2f,\"rssi\": %d,\"afc\": 0,\"launchKT\": -1,\"burstKT\": %d,\"countKT\": %d,\"crefKT\": -1,",
          s.freqMhz, rssi, s.burstKT, s.countKT);
    o.add("\"launchsite\": \"\",\"res\": %d", s.valid && s.havePos ? 0 : 1);
    if (s.battV > 0) o.add(",\"batt\": %.1f", s.battV);
}

int rdz_live_json(char *buf, size_t n, const RdzSonde &s, const RdzGps &g)
{
    StrOut o(buf, n);
    o.add("{\"sonde\": {");
    sondeFields(o, s);
    o.add(" }");
    if (g.valid) {
        o.add(", \"gps\": {\"lat\": %g, \"lon\": %g, \"alt\": %d, \"sat\": %d, \"speed\": %g, \"dir\": %d, \"hdop\": %d }",
              g.lat, g.lon, g.altM, g.sats, (double)g.speedKmh, g.dirDeg, g.hdop);
    }
    o.add("}");
    return o.result();
}

int rdz_tcp_line(char *buf, size_t n, const RdzSonde &s, const RdzGps &g)
{
    StrOut o(buf, n);
    o.add("{");
    sondeFields(o, s);
    o.add(",\"active\":%d,\"validId\":%d,\"validPos\":%d", s.valid ? 1 : 0, s.ser && s.ser[0] ? 1 : 0, s.havePos ? 1 : 0);
    if (g.valid) {
        o.add(", \"gpslat\": %f,\"gpslon\": %f,\"gpsalt\": %d,\"gpsacc\": %d,\"gpsdir\": %d", g.lat, g.lon, g.altM, g.hdop,
              g.dirDeg);
    }
    o.add("}\n");
    return o.result();
}
