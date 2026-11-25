// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "mysondy.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t BW_HZ[MYSONDY_BW_COUNT] = {2600,  3100,  3900,  5200,   6300,   7800,   10400,
                                                 12500, 15600, 20800, 25000,  31300,  41700,  50000,
                                                 62500, 83300, 100000, 125000, 166700, 200000};

#ifdef HOST_TEST
uint32_t mysondy_bw_hz(int index)
{
    if (index < 0 || index >= MYSONDY_BW_COUNT) return 0;
    return BW_HZ[index];
}
#endif

int mysondy_bw_index(uint32_t hz)
{
    int best = 0;
    for (int i = 1; i < MYSONDY_BW_COUNT; i++) {
        if (labs((long)BW_HZ[i] - (long)hz) < labs((long)BW_HZ[best] - (long)hz)) best = i;
    }
    return best;
}

static float sign(float rssiDbm)
{
    float s = -rssiDbm;
    return s < 0 ? 0 : s;
}

int mysondy_line(char *buf, size_t n, const MySondyData &d)
{
    const char *ser = d.ser && d.ser[0] ? d.ser : "";
    if (!d.decoding) {
        return snprintf(buf, n, "0/%s/%.3f/%.1f/%d/%d/%d/%s/o", d.type, d.freqMhz, sign(d.rssiDbm), d.batPct,
                        d.batMv, -1, MYSONDY_VER);
    }
    if (!d.havePos) {
        return snprintf(buf, n, "2/%s/%.3f/%s/%.1f/%d/%d/%d/%d/%s/o", d.type, d.freqMhz, ser, sign(d.rssiDbm),
                        d.batPct, 0, d.batMv, -1, MYSONDY_VER);
    }
    int bk = d.killKnown && d.killSec > 0 ? 1 : 0;
    return snprintf(buf, n, "1/%s/%.3f/%s/%.5f/%.5f/%.1f/%.1f/%.1f/%.1f/%d/%d/%d/%d/%d/%d/%.0f/0/0/%s/o", d.type,
                    d.freqMhz, ser, d.lat, d.lon, d.altM, d.hvelMs * 3.6f, d.vvelMs, sign(d.rssiDbm), d.batPct, 0, bk,
                    bk ? d.killSec : 0, d.batMv, -1, d.dirDeg, MYSONDY_VER);
}

int mysondy_settings(char *buf, size_t n, const char *type, double freqMhz, int bwIndex, const char *call)
{
    // Pins are 0 ("not installed"): the app shows them, the T-Deck does not use them.
    // Fields: type, freq, oled sda/scl/rst, led, rs41/m10/m20/pilot/dfm bandwidth, call, freq offset,
    // battery pin, battery min/max mV, 1, lcd type, name type, buzzer pin, version.
    return snprintf(buf, n, "3/%s/%.3f/%d/%d/%d/%d/%d/%d/%d/%d/%d/%s/%d/%d/%d/%d/%d/%d/%d/%d/%s/o", type, freqMhz, 0,
                    0, 0, 0, bwIndex, 7, 7, 7, 7, call && call[0] ? call : "MYCALL", 0, 0, 3300, 4200, 1, 1, 0, 0,
                    MYSONDY_VER);
}

bool MySondyParser::feed(char c)
{
    bool done = false;
    if (!inFrame) {
        if (prev == 'o' && c == '{') {
            inFrame = true;
            len = 0;
        }
    } else if (prev == '}' && c == 'o') {
        if (len > 0) len--; // drop the '}'
        buf[len] = 0;
        parse();
        inFrame = false;
        done = true;
    } else if (len < (int)sizeof(buf) - 1) {
        buf[len++] = c;
    } else {
        inFrame = false; // too long: wait for the next "o{"
    }
    prev = done ? 0 : c;
    return done;
}

void MySondyParser::parse()
{
    out = MySondyCmd();
    char *save = nullptr;
    for (char *tok = strtok_r(buf, "/", &save); tok; tok = strtok_r(nullptr, "/", &save)) {
        char *eq = strchr(tok, '=');
        if (!eq) {
            if (!strcmp(tok, "?")) out.query = true;
            continue;
        }
        *eq = 0;
        const char *k = tok, *v = eq + 1;
        if (!strcmp(k, "f")) {
            out.hasFreq = true;
            out.freqMhz = atof(v);
        } else if (!strcmp(k, "tipo")) {
            out.hasType = true;
            out.type = atoi(v);
        } else if (!strcmp(k, "myCall")) {
            out.hasCall = true;
            snprintf(out.call, sizeof(out.call), "%s", v);
        } else if (!strcmp(k, "rs41.rxbw")) {
            out.hasBw = true;
            out.bwIndex = atoi(v);
        }
        // Other keys (pins, lcd, buzzer, sleep, reboot...) are hardware settings of the TTGO boards: ignored.
    }
}
