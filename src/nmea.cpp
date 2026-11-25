// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "nmea.h"
#include "bytes.h"
#include "timeutil.h"
#include <stdlib.h>
#include <string.h>

static inline int hexVal(char c) { return hex_digit(c); }

// ddmm.mmmm / dddmm.mmmm -> degrees
static double degMin(const char *s, const char *hemi)
{
    double v = atof(s);
    int d = (int)(v / 100.0);
    double deg = d + (v - d * 100.0) / 60.0;
    return (*hemi == 'S' || *hemi == 'W') ? -deg : deg;
}

bool NmeaParser::feed(char c)
{
    if (c == '$') {
        len = 0;
        return false;
    }
    if (len < 0) return false;
    if (c != '\r' && c != '\n') {
        if (len < (int)sizeof(buf) - 1) buf[len++] = c;
        else len = -1;
        return false;
    }
    buf[len] = 0;
    int n = len;
    len = -1;
    char *star = strrchr(buf, '*');
    if (!star || n - (int)(star - buf) < 3) {
        if (n > 0) bad++;
        return false;
    }
    uint8_t sum = 0;
    for (char *p = buf; p < star; p++) sum ^= (uint8_t)*p;
    int hi = hexVal(star[1]), lo = hexVal(star[2]);
    if (hi < 0 || lo < 0 || sum != (uint8_t)(hi << 4 | lo)) {
        bad++;
        return false;
    }
    *star = 0;
    good++;
    parse();
    return true;
}

uint32_t nmea_unix_time(const char *ddmmyy, const char *hhmmss)
{
    if (strlen(ddmmyy) < 6 || strlen(hhmmss) < 6) return 0;
    for (int i = 0; i < 6; i++)
        if (ddmmyy[i] < '0' || ddmmyy[i] > '9' || hhmmss[i] < '0' || hhmmss[i] > '9') return 0;
    auto two = [](const char *s) { return (s[0] - '0') * 10 + (s[1] - '0'); };
    int d = two(ddmmyy), mo = two(ddmmyy + 2), y = 2000 + two(ddmmyy + 4);
    int h = two(hhmmss), mi = two(hhmmss + 2), s = two(hhmmss + 4);
    if (h > 23 || mi > 59 || s > 60) return 0;
    return tu_unix(y, mo, d, h, mi, s);
}

static int talkerSys(const char *talker)
{
    if (!strncmp(talker, "GP", 2)) return GNSS_GPS;
    if (!strncmp(talker, "GL", 2)) return GNSS_GLONASS;
    if (!strncmp(talker, "GA", 2)) return GNSS_GALILEO;
    if (!strncmp(talker, "GB", 2) || !strncmp(talker, "BD", 2)) return GNSS_BEIDOU;
    if (!strncmp(talker, "GQ", 2)) return GNSS_QZSS;
    return -1;
}

// $xxGSV,total,num,inView,{prn,elev,az,snr}*[,signalId]: one group spreads over 'total' sentences.
void NmeaParser::parseGsv(int sys, char **f, int nf)
{
    if (nf < 4) return;
    int total = atoi(f[1]), num = atoi(f[2]);
    if (total < 1 || num < 1 || num > total) return;
    if (num == 1) nTmp[sys] = 0;
    // Satellite blocks start at field 4; NMEA 4.1 adds a signal ID after the last block.
    for (int i = 4; i + 3 < nf; i += 4) {
        if (!*f[i]) continue;
        if (nTmp[sys] >= GNSS_MAX_SV) break;
        SatInView &s = tmp[sys][nTmp[sys]++];
        s.prn = (uint8_t)atoi(f[i]);
        s.elev = *f[i + 1] ? (int8_t)atoi(f[i + 1]) : -1;
        s.az = (uint16_t)atoi(f[i + 2]);
        s.snr = (uint8_t)atoi(f[i + 3]);
    }
    if (num == total) {
        // NMEA 4.11: an odd field after the last block is the signal ID; without it, signal 0.
        int id = (nf - 4) % 4 == 1 ? (int)strtol(f[nf - 1], nullptr, 16) : 0;
        int k = 0;
        while (k < GNSS_SIGS && sigId[sys][k] != id && sigId[sys][k] >= 0) k++;
        if (k == GNSS_SIGS) k = GNSS_SIGS - 1; // more signals than slots: the last slot is shared
        sigId[sys][k] = (int8_t)id;
        memcpy(sig[sys][k], tmp[sys], sizeof(sig[sys][k]));
        nSig[sys][k] = nTmp[sys];
        mergeSigs(sys);
        gsvGroups++;
    }
}

// One row per satellite across the signals: its best C/N0, and its position from any group that has it.
void NmeaParser::mergeSigs(int sys)
{
    int n = 0;
    for (int k = 0; k < GNSS_SIGS; k++) {
        for (int i = 0; i < nSig[sys][k]; i++) {
            const SatInView &s = sig[sys][k][i];
            int j = 0;
            while (j < n && sv[sys][j].prn != s.prn) j++;
            if (j == n) {
                if (n >= GNSS_MAX_SV) continue;
                sv[sys][n++] = s;
                continue;
            }
            SatInView &d = sv[sys][j];
            if (s.snr > d.snr) d.snr = s.snr;
            if (d.elev < 0 && s.elev >= 0) {
                d.elev = s.elev;
                d.az = s.az;
            }
        }
    }
    nSv[sys] = (uint8_t)n;
}

void NmeaParser::parse()
{
    char *f[24];
    int nf = 0;
    char *p = buf;
    f[nf++] = p;
    while (*p && nf < 24) {
        if (*p == ',') {
            *p = 0;
            f[nf++] = p + 1;
        }
        p++;
    }
    if (strlen(f[0]) < 5) return;
    const char *type = f[0] + 2;
    if (strcmp(type, "GGA") == 0 && nf >= 10) {
        int q = atoi(f[6]);
        fix.sats = (uint8_t)atoi(f[7]);
        if (q > 0 && *f[2] && *f[4]) {
            fix.lat = degMin(f[2], f[3]);
            fix.lon = degMin(f[4], f[5]);
            fix.hdop = (float)atof(f[8]);
            fix.altM = (float)atof(f[9]);
            fix.geoidSepM = nf >= 12 ? (float)atof(f[11]) : 0.0f;
            fix.valid = true;
        } else {
            fix.valid = false;
        }
    } else if (strcmp(type, "GSA") == 0 && nf >= 18) {
        // Several GSA sentences per epoch (one per constellation) carry the same fix type and DOPs.
        fix.fixType = (uint8_t)atoi(f[2]);
        fix.pdop = (float)atof(f[15]);
    } else if (strcmp(type, "GSV") == 0) {
        int sys = talkerSys(f[0]);
        if (sys >= 0) parseGsv(sys, f, nf);
    } else if (strcmp(type, "RMC") == 0 && nf >= 9) {
        // Time and date are present as soon as the receiver knows them, even without a fix.
        if (nf >= 10) {
            uint32_t t = nmea_unix_time(f[9], f[1]);
            if (t) fix.utc = t;
        }
        if (*f[2] == 'A' && *f[3] && *f[5]) {
            fix.lat = degMin(f[3], f[4]);
            fix.lon = degMin(f[5], f[6]);
            fix.speedKmh = (float)(atof(f[7]) * 1.852);
            if (*f[8]) fix.courseDeg = (float)atof(f[8]);
            fix.valid = true;
        } else {
            fix.valid = false;
            fix.speedKmh = 0;
        }
    }
}
