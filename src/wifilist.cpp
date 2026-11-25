// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "wifilist.h"
#include <stdio.h>
#include <string.h>

const char *const WIFI_FILE_TEMPLATE =
    "# Saved Wi-Fi networks of T-Deck SondeTracker: one per line, name,password\n"
    "# (the first comma splits them, so the name cannot have one; the password can).\n"
    "# An open network: the name and a comma. Lines starting with # are ignored.\n"
    "# Example:\n"
    "# HomeNet,my password\n";

static bool blank(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// Skips a UTF-8 byte order mark (files saved by some Windows editors).
static const char *body(const char *text)
{
    return (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF
               ? text + 3
               : text;
}

// Copies [a, b) without the blanks at the ends; false when it does not fit in n - 1 chars.
static bool field(const char *a, const char *b, char *out, size_t n)
{
    while (a < b && blank(*a)) a++;
    while (b > a && blank(b[-1])) b--;
    if ((size_t)(b - a) >= n) return false;
    memcpy(out, a, b - a);
    out[b - a] = 0;
    return true;
}

static bool parseLine(const char *p, const char *eol, SavedNet &n)
{
    while (p < eol && blank(*p)) p++;
    if (p == eol || *p == '#') return false;
    const char *comma = (const char *)memchr(p, ',', eol - p);
    if (!field(p, comma ? comma : eol, n.ssid, sizeof(n.ssid)) || !n.ssid[0]) return false;
    if (!comma) n.pass[0] = 0;
    else if (!field(comma + 1, eol, n.pass, sizeof(n.pass))) return false;
    return true;
}

int wifilist_parse(const char *text, SavedNet *out, int max)
{
    int n = 0;
    const char *p = body(text);
    while (*p && n < max) {
        const char *eol = strchr(p, '\n');
        if (!eol) eol = p + strlen(p);
        if (parseLine(p, eol, out[n])) n++;
        p = *eol ? eol + 1 : eol;
    }
    return n;
}

bool wifilist_ok(const char *ssid, const char *pass)
{
    size_t ls = strlen(ssid), lp = strlen(pass);
    if (!ls || ls >= sizeof(((SavedNet *)0)->ssid) || lp >= sizeof(((SavedNet *)0)->pass)) return false;
    if (ssid[0] == '#' || strchr(ssid, ',') || strpbrk(ssid, "\r\n") || strpbrk(pass, "\r\n")) return false;
    if (blank(ssid[0]) || blank(ssid[ls - 1]) || (lp && (blank(pass[0]) || blank(pass[lp - 1])))) return false;
    return true;
}

int wifilist_find(const SavedNet *n, int count, const char *ssid)
{
    for (int i = 0; i < count; i++)
        if (!strcmp(n[i].ssid, ssid)) return i;
    return -1;
}

int wifilist_put(char *text, size_t cap, const char *ssid, const char *pass)
{
    if (!wifilist_ok(ssid, pass)) return -1;
    char line[100];
    int ll = snprintf(line, sizeof(line), "%s,%s", ssid, pass);
    int count = 0;
    char *p = (char *)body(text);
    while (*p) {
        char *eol = strchr(p, '\n');
        char *end = eol ? eol : p + strlen(p);
        SavedNet q;
        if (parseLine(p, end, q)) {
            if (!strcmp(q.ssid, ssid)) { // same name: the line is rewritten
                size_t old = end - p, k = strlen(text);
                if (k - old + ll + 1 > cap) return -1;
                memmove(p + ll, end, strlen(end) + 1);
                memcpy(p, line, ll);
                return 0;
            }
            count++;
        }
        p = *end ? end + 1 : end;
    }
    if (count >= WIFI_SAVED_MAX) return -1;
    size_t k = strlen(text);
    if (k + ll + 3 > cap) return -1;
    if (k && text[k - 1] != '\n') text[k++] = '\n';
    memcpy(text + k, line, ll);
    k += ll;
    text[k++] = '\n';
    text[k] = 0;
    return 1;
}

bool wifilist_remove(char *text, int index)
{
    int n = 0;
    char *p = (char *)body(text);
    while (*p) {
        char *eol = strchr(p, '\n');
        char *next = eol ? eol + 1 : p + strlen(p);
        SavedNet q;
        if (parseLine(p, eol ? eol : next, q)) {
            if (n == index) {
                memmove(p, next, strlen(next) + 1);
                return true;
            }
            n++;
        }
        p = next;
    }
    return false;
}
