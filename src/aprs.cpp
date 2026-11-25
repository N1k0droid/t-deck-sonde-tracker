// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "aprs.h"
#include "sondetype.h"
#include "strout.h"
#include "timeutil.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int aprs_passcode(const char *call)
{
    int h = 0x73E2;
    for (int i = 0; call[i] && call[i] != '-'; i += 2) {
        h ^= toupper((unsigned char)call[i]) << 8;
        if (!call[i + 1] || call[i + 1] == '-') break;
        h ^= toupper((unsigned char)call[i + 1]);
    }
    return h & 0x7FFF;
}

bool aprs_call_ok(const char *call)
{
    const char *dash = strchr(call, '-');
    size_t base = dash ? (size_t)(dash - call) : strlen(call);
    if (base < 3 || base > 6) return false;
    bool digit = false;
    for (size_t i = 0; i < base; i++) {
        char c = call[i];
        if (c >= '0' && c <= '9') digit = true;
        else if (!(c >= 'A' && c <= 'Z')) return false; // upper case only: what the servers expect
    }
    if (!digit) return false;
    if (!dash) return true;
    const char *s = dash + 1;
    size_t n = strlen(s);
    if (n < 1 || n > 2) return false;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'Z'))) return false;
    return true;
}

size_t aprs_login(char *buf, size_t n, const char *call, int pass, const char *ver)
{
    StrOut o(buf, n);
    o.add("user %s pass %d vers T-Deck-SondeTracker %s\r\n", call, pass, ver);
    return o.full ? 0 : o.k;
}

bool aprs_object_name(uint8_t type, const char *serial, char out[10])
{
    char d[16];
    size_t k = 0;
    for (const char *s = serial; *s && k + 1 < sizeof(d); s++) // the digits of the serial
        if (*s >= '0' && *s <= '9') d[k++] = *s;
    d[k] = 0;
    char name[16];
    if (!sh_serial_ok(type, serial)) return false;
    switch (type) {
    case ST_RS41: snprintf(name, sizeof(name), "%s", serial); break;
    case ST_DFM: // the number without leading zeros; an old DFM-06 keeps its hex serial
        if (strlen(serial) == 8) snprintf(name, sizeof(name), "D%lu", strtoul(serial, nullptr, 10));
        else snprintf(name, sizeof(name), "D%s", serial);
        break;
    case ST_M10: snprintf(name, sizeof(name), "ME%s", k > 7 ? d + k - 7 : d); break;
    case ST_IMET54: snprintf(name, sizeof(name), "IMET%s", k > 5 ? d + k - 5 : d); break;
    default: return false;
    }
    snprintf(out, 10, "%-9.9s", name);
    return true;
}

// "DDMM.mmN" / "DDDMM.mmE", and the DAO letter of the next two digits of the minutes (WGS84, base 91).
static void coord(double v, bool lat, char *out, size_t n, char *dao)
{
    double a = fabs(v);
    int deg = (int)a;
    long m = lround((a - deg) * 600000.0); // 1/10000 of a minute
    if (m >= 600000) {
        deg++;
        m -= 600000;
    }
    long hund = m / 100;
    *dao = (char)(33 + (int)((m % 100) / 1.1 + 0.5));
    if (lat) snprintf(out, n, "%02d%02ld.%02ld%c", deg, hund / 100, hund % 100, v < 0 ? 'S' : 'N');
    else snprintf(out, n, "%03d%02ld.%02ld%c", deg, hund / 100, hund % 100, v < 0 ? 'W' : 'E');
}

size_t aprs_object(char *buf, size_t n, const char *call, const ShFrame &f)
{
    char name[10], la[12], lo[12], daoLa, daoLo, type[12];
    if (!aprs_object_name(f.type, f.serial, name) || !f.timeUnix || !sh_type_label(f, type)) return 0;
    if (fabs(f.lat) > 90 || fabs(f.lon) > 180) return 0;
    uint32_t t = f.timeUnix - (f.timeGps ? GPS_UTC_LEAP_S : 0); // object time in UTC
    coord(f.lat, true, la, sizeof(la), &daoLa);
    coord(f.lon, false, lo, sizeof(lo), &daoLo);
    int kn = (int)lround(f.velH * 1.943844);
    int crs = (int)lround(f.heading) % 360;
    if (crs <= 0) crs += 360; // 000 means unknown
    if (kn < 1) crs = kn = 0;
    if (kn > 999) kn = 999;
    long ft = lround(f.alt / 0.3048);
    if (ft < 0) ft = 0;
    if (ft > 999999) ft = 999999;
    StrOut o(buf, n);
    o.add("%s>" APRS_TOCALL ",TCPIP*:;%s*%02lu%02lu%02luh%s/%sO%03d/%03d/A=%06ld Clb=%.1fm/s", call, name,
          (unsigned long)(t / 3600 % 24), (unsigned long)(t / 60 % 60), (unsigned long)(t % 60), la, lo, crs, kn, ft,
          f.velV);
    if (f.hasTemp) o.add(" t=%.1fC", f.temp);
    o.add(" %.3fMHz Type=%s", f.freqMhz, type);
    if (f.type != ST_RS41) o.add(" ser=%s", f.serial);
    o.add(" !w%c%c!\r\n", daoLa, daoLo);
    return o.full ? 0 : o.k;
}

const char APRS_MOBILE_SYM[APRS_MOBILE_ICONS] = {'[', '>', '('};
const char *const APRS_MOBILE_NAME[APRS_MOBILE_ICONS] = {"person", "car", "chase /("};

size_t aprs_beacon(char *buf, size_t n, const char *call, double lat, double lon, char symbol, const char *ver,
                   const char *comment, const AprsMotion *m)
{
    char la[12], lo[12], d1, d2;
    if (fabs(lat) > 90 || fabs(lon) > 180) return 0;
    coord(lat, true, la, sizeof(la), &d1);
    coord(lon, false, lo, sizeof(lo), &d2);
    StrOut o(buf, n);
    o.add("%s>" APRS_TOCALL ",TCPIP*:!%s/%s%c", call, la, lo, symbol);
    if (m && m->moving) { // CSE/SPD: course 001-360 (000 unknown), knots
        int kn = (int)lround(m->speedKmh / 1.852);
        int cse = (int)lround(m->courseDeg) % 360;
        if (cse <= 0) cse += 360;
        if (m->speedKmh < 3 || kn < 0) cse = kn = 0;
        o.add("%03d/%03d", cse, kn > 999 ? 999 : kn);
    }
    if (m && m->hasAlt) {
        long ft = lround(m->altM / 0.3048);
        if (ft < -99999) ft = -99999;
        if (ft > 999999) ft = 999999;
        o.add(ft < 0 ? "/A=-%05ld" : "/A=%06ld", ft < 0 ? -ft : ft);
    }
    if (!comment) o.add("%sT-Deck SondeTracker %s", m && m->hasAlt ? " " : "", ver);
    else o.add("%s%s", m && m->hasAlt && comment[0] ? " " : "", comment);
    o.add("\r\n");
    return o.full ? 0 : o.k;
}

const char *const APRS_COMMENT_TEMPLATE =
    "# T-Deck SondeTracker - comment sent to aprs.fi with your station position.\n"
    "# One line per situation: key = text. {serial} is the sonde, {site} the launch site\n"
    "# (the name of the frequency preset in use). The _nosite lines are used without a preset.\n"
    "# Up to 59 characters; | and ~ are not allowed by APRS. Read at start-up.\n"
    "#\n"
    "# chase: a mobile station hears a sonde by radio\n"
    "chase = Chasing radiosonde {serial} launched from {site}\n"
    "chase_nosite = Chasing radiosonde {serial}\n"
    "# receive: a fixed station hears a sonde by radio\n"
    "receive = Receiving radiosonde {serial} launched from {site}\n"
    "receive_nosite = Receiving radiosonde {serial}\n"
    "# idle: no sonde heard by radio (only a remembered one, or SondeHub data)\n"
    "idle = Looking for radiosondes\n";

// Value of "key = text" in the file; false when missing. Spaces around key and text are dropped.
static bool commentLine(const char *file, const char *key, char *out, size_t n)
{
    size_t kl = strlen(key);
    for (const char *p = file; p && *p;) {
        const char *e = strchr(p, '\n');
        const char *end = e ? e : p + strlen(p);
        const char *a = p;
        if (!strncmp(a, "\xEF\xBB\xBF", 3)) a += 3; // Windows editors
        while (a < end && (*a == ' ' || *a == '\t')) a++;
        if (*a != '#' && (size_t)(end - a) > kl && !strncmp(a, key, kl)) {
            const char *b = a + kl;
            while (b < end && (*b == ' ' || *b == '\t')) b++;
            if (b < end && *b == '=') {
                b++;
                while (b < end && (*b == ' ' || *b == '\t')) b++;
                const char *z = end;
                while (z > b && (z[-1] == ' ' || z[-1] == '\t' || z[-1] == '\r')) z--;
                size_t len = (size_t)(z - b) < n - 1 ? (size_t)(z - b) : n - 1;
                memcpy(out, b, len);
                out[len] = 0;
                return true;
            }
        }
        p = e ? e + 1 : nullptr;
    }
    return false;
}

void aprs_comment(const char *file, AprsSituation s, const char *serial, const char *site, char *out, size_t max)
{
    static const char *const KEY[3][2] = {{"chase", "chase_nosite"}, {"receive", "receive_nosite"}, {"idle", "idle"}};
    if (!max) return;
    bool noSite = !site || !site[0];
    const char *key = KEY[s][noSite ? 1 : 0];
    char tpl[160];
    if (!file || !commentLine(file, key, tpl, sizeof(tpl)))
        if (!commentLine(APRS_COMMENT_TEMPLATE, key, tpl, sizeof(tpl))) tpl[0] = 0;
    size_t k = 0;
    auto put = [&](char c) {
        if (k + 1 >= max || c < 0x20 || c > 0x7E || c == '|' || c == '~') return;
        out[k++] = c;
    };
    for (const char *p = tpl; *p; p++) {
        if (!strncmp(p, "{serial}", 8)) {
            for (const char *q = serial ? serial : ""; *q; q++) put(*q);
            p += 7;
        } else if (!strncmp(p, "{site}", 6)) {
            for (const char *q = site ? site : ""; *q; q++) put(*q);
            p += 5;
        } else {
            put(*p);
        }
    }
    out[k] = 0;
}
