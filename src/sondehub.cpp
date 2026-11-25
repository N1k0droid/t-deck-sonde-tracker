// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "sondehub.h"
#include "sondetype.h"
#include "strout.h"
#include "timeutil.h"
#include "tracklog.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void sh_iso_time(uint32_t unix, uint16_t ms, char out[32])
{
    char t[24];
    track_iso_time(unix ? unix : 1, t); // "YYYY-MM-DDTHH:MM:SSZ"
    t[19] = 0;
    snprintf(out, 32, "%s.%03u000Z", t, (unsigned)(ms % 1000));
}

void sh_http_date(uint32_t unix, char out[32])
{
    static const char *const DAY[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *const MON[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int y, mo, d;
    uint32_t s = unix % 86400u;
    tu_civil_from_days((int32_t)(unix / 86400u), &y, &mo, &d);
    snprintf(out, 32, "%s, %02d %s %04d %02u:%02u:%02u GMT", DAY[tu_weekday(unix)], d, MON[mo - 1], y, (unsigned)(s / 3600),
             (unsigned)(s / 60 % 60), (unsigned)(s % 60));
}

bool sh_subtype_ok(const char *subtype)
{
    return strcmp(subtype, "RS41-SG") == 0 || strcmp(subtype, "RS41-SGP") == 0 || strcmp(subtype, "RS41-SGM") == 0;
}

static bool digits(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (s[i] < '0' || s[i] > '9') return false;
    return strlen(s) == n;
}

static bool alnum(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'); }

bool sh_serial_ok(uint8_t type, const char *s)
{
    size_t n = strlen(s);
    switch (type) {
    case ST_RS41: return n == 8 && s[0] >= 'A' && s[0] <= 'Z' && digits(s + 1, 7);
    case ST_DFM: // 8 digits; the old DFM-06 its 6 hex digits
        if (n == 6) {
            for (size_t i = 0; i < 6; i++)
                if (!alnum(s[i]) || s[i] > 'F') return false;
            return true;
        }
        return digits(s, 8);
    case ST_M10: // "909-2-12345"
        if (n != 11 || s[3] != '-' || s[5] != '-') return false;
        for (size_t i = 0; i < n; i++)
            if (i != 3 && i != 5 && !alnum(s[i])) return false;
        return true;
    case ST_IMET54: return n >= 1 && n <= 10 && digits(s, n);
    default: return false;
    }
}

bool sh_serial_blocked(const char *serial)
{
    static const char *const SIM[] = {"Z9999991", "21099991", "909-2-12345", "810-2-07777", "54012345"};
    for (const char *s : SIM)
        if (!strcmp(serial, s)) return true;
    return false;
}

uint32_t sh_day_time(uint32_t now, int32_t secOfDay)
{
    if (!now || secOfDay < 0 || secOfDay >= 86400) return 0;
    int64_t t = (int64_t)(now / 86400u) * 86400 + secOfDay;
    if (t - (int64_t)now > 43200) t -= 86400; // received just after midnight, sent just before
    else if ((int64_t)now - t > 43200) t += 86400;
    return t > 0 ? (uint32_t)t : 0;
}

bool sh_callsign_ok(const char *call)
{
    size_t n = strlen(call);
    if (n < 3 || n > 15) return false;
    for (const char *c = call; *c; c++) {
        bool ok = (*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || *c == '-' ||
                  *c == '_';
        if (!ok) return false;
    }
    return true;
}

// Manufacturer, type and subtype as SondeHub names them; false for a type it does not take from us.
static bool shNames(const ShFrame &f, const char **manuf, const char **type, char sub[12])
{
    sub[0] = 0;
    switch (f.type) {
    case ST_RS41:
        *manuf = "Vaisala";
        *type = "RS41";
        if (sh_subtype_ok(f.subtype)) snprintf(sub, 12, "%s", f.subtype);
        return true;
    case ST_DFM: // "DFM-09" -> "DFM09"; "DFM-17P" is a DFM17
        *manuf = "Graw";
        *type = "DFM";
        if (!strncmp(f.model, "DFM-", 4) && digits(f.model + 4, 2)) snprintf(sub, 12, "DFM%.2s", f.model + 4);
        else if (!strncmp(f.model, "DFM-17", 6)) snprintf(sub, 12, "DFM17");
        return true;
    case ST_M10:
        *manuf = "Meteomodem";
        *type = !strcmp(f.model, "M20") ? "M20" : "M10";
        return true;
    case ST_IMET54:
        *manuf = "Intermet Systems";
        *type = !strcmp(f.model, "iMet-50") ? "iMet-50" : "iMet-54";
        return true;
    default: return false;
    }
}

bool sh_type_label(const ShFrame &f, char out[12])
{
    const char *manuf, *type;
    char sub[12];
    if (!shNames(f, &manuf, &type, sub)) return false;
    snprintf(out, 12, "%s", sub[0] ? sub : type);
    return true;
}

size_t sh_frame_json(char *buf, size_t n, const ShFrame &f, const char *callsign, const char *swVersion, bool dev)
{
    const char *manuf, *type;
    char sub[12];
    if (!shNames(f, &manuf, &type, sub) || !sh_serial_ok(f.type, f.serial) || !f.timeUnix) return 0;
    bool rs41 = f.type == ST_RS41;
    char rx[32], when[32];
    sh_iso_time(f.rxUnix, 0, rx);
    sh_iso_time(f.timeUnix, f.timeMs, when);
    StrOut o(buf, n);
    // The callsign goes also into the JSON log on the SD, typed before any check: escaped like the serial.
    o.add("{\"software_name\":\"t-deck-sonde-tracker\",\"software_version\":");
    o.str(swVersion);
    o.add(",\"uploader_callsign\":");
    o.str(callsign);
    o.add(",\"time_received\":\"%s\",\"manufacturer\":\"%s\",\"type\":\"%s\",\"serial\":", rx, manuf, type);
    o.str(f.serial);
    o.add(",\"frame\":%lu,\"datetime\":\"%s\",\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f", (unsigned long)f.frame, when,
          f.lat, f.lon, f.alt);
    if (sub[0]) o.add(",\"subtype\":\"%s\"", sub);
    if (f.hasTemp) o.add(",\"temp\":%.1f", f.temp);
    o.add(",\"vel_h\":%.1f,\"vel_v\":%.1f,\"heading\":%.1f", f.velH, f.velV, f.heading);
    if (rs41 || f.sats) o.add(",\"sats\":%u", (unsigned)f.sats); // the other types: only when the frame has them
    if (rs41 || f.batt > 0) o.add(",\"batt\":%.1f", f.batt);
    if (f.burstTimer >= 0) o.add(",\"burst_timer\":%d", f.burstTimer);
    if (f.txKhz) o.add(",\"tx_frequency\":%.3f", f.txKhz / 1000.0);
    o.add(",\"frequency\":%.3f,\"rssi\":%.1f", f.freqMhz, f.rssi);
    if (rs41) o.add(",\"ref_position\":\"GPS\""); // the others: height reference not known for sure
    o.add(",\"ref_datetime\":\"%s\"", f.timeGps ? "GPS" : "UTC");
    if (f.hasUploaderPos) o.add(",\"uploader_position\":[%.5f,%.5f,%.0f]", f.upLat, f.upLon, f.upAlt);
    if (dev) o.add(",\"dev\":\"t-deck test\"");
    o.add("}");
    return o.full ? 0 : o.k;
}

size_t sh_listener_json(char *buf, size_t n, const char *callsign, const char *swVersion, double lat, double lon,
                        double alt, bool mobile)
{
    StrOut o(buf, n);
    o.add("{\"software_name\":\"t-deck-sonde-tracker\",\"software_version\":");
    o.str(swVersion);
    o.add(",\"uploader_callsign\":");
    o.str(callsign);
    o.add(",\"uploader_position\":[%.5f,%.5f,%.0f],\"uploader_radio\":\"LilyGO T-Deck Plus (SX1262)\",\"mobile\":%s}", lat,
          lon, alt, mobile ? "true" : "false");
    return o.full ? 0 : o.k;
}
