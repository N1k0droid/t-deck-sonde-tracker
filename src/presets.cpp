// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "presets.h"
#include "sondetype.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *skipSpace(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// Copies s (up to the end of the line) without trailing blanks.
static void copyTrim(char *dst, size_t n, const char *s, const char *end)
{
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
    size_t len = (size_t)(end - s);
    if (len >= n) len = n - 1;
    memcpy(dst, s, len);
    dst[len] = 0;
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static bool sameText(const char *a, const char *b) { return strcasecmp(a, b) == 0; }

static bool contains(const char *s, const char *q)
{
    for (; *s; s++) {
        size_t i = 0;
        while (q[i] && lower(s[i]) == lower(q[i])) i++;
        if (!q[i]) return true;
    }
    return false;
}

// Site key of a name: its first word when at least 4 letters long ("Trapani Birgi" -> "trapani"),
// otherwise the whole name ("San Pietro Capofiume").
static void siteKey(char *out, size_t n, const char *name)
{
    size_t w = strcspn(name, " /(");
    size_t len = w >= 4 ? w : strlen(name);
    if (len >= n) len = n - 1;
    for (size_t i = 0; i < len; i++) out[i] = lower(name[i]);
    out[len] = 0;
}

// Parses one line (no newline needed); true when it is a preset. Group headers update group.
static bool parseLine(const char *s, const char *eol, char *group, Preset &q)
{
    s = skipSpace(s);
    if (*s == '[') {
        const char *close = (const char *)memchr(s, ']', (size_t)(eol - s));
        if (close) copyTrim(group, 16, skipSpace(s + 1), close);
        return false;
    }
    if (!*s || *s == '#' || *s == '\r' || s >= eol) return false;
    char *after;
    double mhz = strtod(s, &after);
    if (after == s || after > eol || mhz < 400.0 || mhz > 406.0) return false;
    memset(&q, 0, sizeof(q));
    q.khz = (uint32_t)(mhz * 1000.0 + 0.5);
    q.type = ST_AUTO;
    const char *t = skipSpace(after);
    int ty = t < eol ? sonde_type_parse(t) : -1;
    if (ty >= 0) {
        q.type = (uint8_t)ty;
        while (t < eol && *t != ' ' && *t != '\t' && *t != '\r') t++;
        t = skipSpace(t);
    }
    if (t < eol) copyTrim(q.name, sizeof(q.name), t, eol);
    memcpy(q.group, group, sizeof(q.group));
    return true;
}

int presets_parse(const char *text, Preset *out, int max)
{
    int n = 0;
    char group[16] = "";
    const char *p = text;
    while (*p && n < max) {
        const char *eol = strchr(p, '\n');
        if (!eol) eol = p + strlen(p);
        if (parseLine(p, eol, group, out[n])) n++;
        p = *eol ? eol + 1 : eol;
    }
    return n;
}

int presets_line(char *buf, size_t n, const Preset &p)
{
    return snprintf(buf, n, "%lu.%03lu  %-4s  %s", (unsigned long)(p.khz / 1000), (unsigned long)(p.khz % 1000),
                    sonde_type_name(p.type), p.name);
}

// Launch sites in Europe with the frequency and family of the sondes heard from them on SondeHub
// (16-20 November 2026). Frequencies can change: the user edits /presets.txt.
const char *const PRESETS_DEFAULT_TEXT =
    "[Sicily]\n"
    "404.500  RS41  Trapani Birgi\n"
    "403.500  RS41  Decimomannu / Galatina\n"
    "402.000  DFM   Tunis (DFM-17)\n"
    "[Italy]\n"
    "405.800  RS41  Pratica di Mare\n"
    "404.600  RS41  San Pietro Capofiume\n"
    "403.700  RS41  Cuneo Levaldigi\n"
    "404.900  RS41  Novara Cameri\n"
    "404.000  RS41  Udine Rivolto\n"
    "405.000  RS41  L'Aquila (ozone)\n"
    "405.500  RS41  Bracciano (ozone)\n"
    "403.900  RS41  Bolzano (campaigns)\n"
    "402.100  RS41  Potenza CNR\n"
    "403.000  M20   Ajaccio (lands in Lazio)\n"
    "[Austria]\n"
    "403.500  M20   Wien Hohe Warte\n"
    "405.800  M20   Wien Hohe Warte\n"
    "404.700  RS41  Graz\n"
    "404.100  RS41  Innsbruck\n"
    "403.400  RS41  Linz\n"
    "[Belgium]\n"
    "403.500  RS41  Uccle\n"
    "[Bulgaria]\n"
    "405.600  RS41  Sofia\n"
    "[Croatia]\n"
    "405.300  RS41  Zagreb\n"
    "[Cyprus]\n"
    "403.000  RS41  Athalassa\n"
    "[Czech Republic]\n"
    "401.100  RS41  Praha-Libus\n"
    "404.200  RS41  Praha-Libus\n"
    "402.100  RS41  Prostejov\n"
    "[Denmark]\n"
    "405.300  RS41  Karup\n"
    "[Estonia]\n"
    "404.000  RS41  Tallinn\n"
    "[Finland]\n"
    "403.000  RS41  Jokioinen\n"
    "404.000  RS41  Sodankyla / Lohtaja\n"
    "[France]\n"
    "403.000  M10   Trappes\n"
    "401.000  M10   Trappes\n"
    "403.000  M20   Ajaccio\n"
    "402.800  M20   Bordeaux\n"
    "404.000  M20   Brest\n"
    "405.000  M20   Nimes\n"
    "403.000  RS41  Canjuers\n"
    "402.000  DFM   Toulon / Levant (DFM-17)\n"
    "[Germany]\n"
    "402.500  RS41  Altenstadt/Schleswig/Hohenp.\n"
    "402.300  RS41  Greifswald/Meiningen/Muenchen\n"
    "402.700  RS41  Kuemmersbruck / Idar-Ob.\n"
    "405.700  RS41  Bergen-Hohne\n"
    "405.300  RS41  Essen\n"
    "404.100  RS41  Norderney\n"
    "405.100  RS41  Lindenberg\n"
    "404.500  RS41  Meppen / Stuttgart\n"
    "[Greece]\n"
    "405.200  M20   Athens\n"
    "404.200  M20   Thessaloniki\n"
    "400.600  M20   Heraklion\n"
    "[Hungary]\n"
    "403.700  RS41  Budapest\n"
    "402.300  RS41  Szeged\n"
    "[Iceland]\n"
    "403.000  RS41  Keflavik\n"
    "[Ireland]\n"
    "401.000  RS41  Valentia\n"
    "[Latvia]\n"
    "403.200  RS41  Skriveri\n"
    "[Lithuania]\n"
    "401.000  RS41  Kaunas\n"
    "[Netherlands]\n"
    "403.900  RS41  De Bilt\n"
    "[Norway]\n"
    "403.000  RS41  Andoya / Ekofisk\n"
    "402.000  RS41  Stavanger\n"
    "405.400  M20   Orland\n"
    "[Poland]\n"
    "404.500  RS41  Legionowo\n"
    "403.600  M20   Leba\n"
    "405.800  M20   Tarnow\n"
    "403.000  M20   Wroclaw\n"
    "[Portugal]\n"
    "403.400  RS41  Lisboa\n"
    "[Romania]\n"
    "403.920  RS41  Bucuresti\n"
    "[Serbia]\n"
    "402.800  M10   Beograd\n"
    "404.200  M10   Negotin\n"
    "402.400  M10   Nis\n"
    "403.200  M10   Sjenica\n"
    "402.000  DFM   Nikinci (DFM-17)\n"
    "[Slovakia]\n"
    "401.600  RS41  Poprad\n"
    "400.600  RS41  Poprad\n"
    "[Slovenia]\n"
    "404.600  M20   Ljubljana\n"
    "[Spain]\n"
    "403.000  RS41  Madrid\n"
    "404.000  RS41  La Coruna\n"
    "402.500  RS41  Huelva\n"
    "405.500  RS41  Mallorca\n"
    "401.500  RS41  Santander\n"
    "404.000  M20   Barcelona\n"
    "403.400  M10   Murcia\n"
    "401.500  M20   Alcantarilla\n"
    "[Sweden]\n"
    "400.500  RS41  Karlskoga\n"
    "403.000  RS41  Lulea\n"
    "[Switzerland]\n"
    "403.500  RS41  Payerne\n"
    "[United Kingdom]\n"
    "405.100  RS41  Albemarle / Eskmeals\n"
    "401.600  RS41  Eskmeals\n"
    "405.700  RS41  Camborne\n"
    "403.400  RS41  Castor Bay\n"
    "405.800  RS41  Herstmonceux\n"
    "403.100  RS41  Lerwick\n"
    "405.500  RS41  Watnall\n";

int presets_template(char *buf, size_t n, const Preset *p, int count)
{
    int k = snprintf(buf, n,
                     "# T-Deck SondeTracker - frequency presets\n"
                     "#\n"
                     "# One preset per line: frequency (MHz), sonde type, name.\n"
                     "#   type: AUTO (the receiver finds it) or the sonde family:");
    for (int t = 1; t < ST_COUNT && k > 0 && (size_t)k < n; t++) k += snprintf(buf + k, n - k, " %s", sonde_type_name(t));
    if (k < 0 || (size_t)k >= n) return k; // buffer full: no write past its end
    k += snprintf(buf + k, n - k,
                  " (M10 and M20 also work)\n"
                  "#   the name is the rest of the line and may be left out\n"
                  "# [Name] on its own line starts a group (country, region...).\n"
                  "# Lines starting with # are comments. Up to %d presets.\n"
                  "# Key 'a' on the T-Deck adds the current frequency and type, Alt+D in the list (key 'j')\n"
                  "# deletes a preset: only then the T-Deck rewrites this file, keeping your edits.\n"
                  "#\n"
                  "# European launch sites with the frequency and type of the sondes heard from them on\n"
                  "# SondeHub (November 2026). Frequencies can change: check them before a hunt and\n"
                  "# correct the line here. Not listed: sites flying on changing frequencies (Idar-Oberstein\n"
                  "# DFM-17, Zahorie, test sites) and types not received by the T-Deck (iMet-4 Beauvechain\n"
                  "# 404.000, MTS01 Istanbul 403.960, MRZ in Russia).\n"
                  "\n%s",
                  PRESET_MAX, PRESETS_DEFAULT_TEXT);
    if (k < 0 || (size_t)k >= n) return k;
    bool header = false;
    Preset *def = (Preset *)malloc(sizeof(Preset) * PRESET_MAX);
    int nDef = def ? presets_parse(PRESETS_DEFAULT_TEXT, def, PRESET_MAX) : 0;
    for (int i = 0; i < count && k > 0 && (size_t)k < n; i++) {
        if (presets_find(def, nDef, p[i].khz) >= 0) continue;
        if (!header) {
            k += snprintf(buf + k, n - k, "[" PRESET_SAVED_GROUP "]\n");
            header = true;
        }
        k += presets_line(buf + k, n - k, p[i]);
        if ((size_t)k + 1 < n) buf[k++] = '\n', buf[k] = 0;
    }
    free(def);
    return k;
}

int presets_merge_defaults(char *text, size_t cap)
{
    Preset *have = (Preset *)malloc(sizeof(Preset) * PRESET_MAX * 2);
    if (!have) return 0;
    Preset *def = have + PRESET_MAX;
    int nHave = presets_parse(text, have, PRESET_MAX);
    int nDef = presets_parse(PRESETS_DEFAULT_TEXT, def, PRESET_MAX);
    size_t k = strlen(text);
    int added = 0;
    char group[16] = "";
    for (int i = 0; i < nDef && nHave + added < PRESET_MAX; i++) {
        char key[32], other[32];
        siteKey(key, sizeof(key), def[i].name);
        bool there = false;
        for (int j = 0; j < nHave && !there; j++) {
            siteKey(other, sizeof(other), have[j].name);
            there = (key[0] && !strcmp(key, other)) || (have[j].khz == def[i].khz && sameText(have[j].group, def[i].group));
        }
        if (there) continue;
        if (k && text[k - 1] != '\n' && k + 1 < cap) text[k++] = '\n';
        if (strcmp(group, def[i].group) && k + 20 < cap) {
            snprintf(group, sizeof(group), "%s", def[i].group);
            k += snprintf(text + k, cap - k, "[%s]\n", group);
        }
        if (k + 56 >= cap) break;
        k += presets_line(text + k, cap - k, def[i]);
        text[k++] = '\n';
        text[k] = 0;
        added++;
    }
    free(have);
    return added;
}

int presets_find(const Preset *p, int count, uint32_t khz)
{
    for (int i = 0; i < count; i++)
        if (p[i].khz == khz) return i;
    return -1;
}

int presets_find_named(const Preset *p, int count, const char *group, const char *name, uint32_t khz)
{
    int found = -1;
    for (int i = 0; i < count; i++) {
        if (!sameText(p[i].group, group) || !sameText(p[i].name, name)) continue;
        if (p[i].khz == khz) return i;
        if (found < 0) found = i;
    }
    return found;
}

bool presets_remove(char *text, int index)
{
    char group[16] = "";
    int n = 0;
    char *p = text;
    while (*p) {
        char *eol = strchr(p, '\n');
        char *next = eol ? eol + 1 : p + strlen(p);
        Preset q;
        if (parseLine(p, eol ? eol : next, group, q)) {
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

int presets_append(char *text, size_t cap, const Preset &q)
{
    char group[16] = "";
    int n = 0;
    const char *p = text;
    while (*p) { // count presets and find the last group
        const char *eol = strchr(p, '\n');
        if (!eol) eol = p + strlen(p);
        Preset tmp;
        if (parseLine(p, eol, group, tmp)) n++;
        p = *eol ? eol + 1 : eol;
    }
    if (n >= PRESET_MAX) return -1;
    size_t k = strlen(text);
    if (k + 80 >= cap) return -1;
    if (k && text[k - 1] != '\n') text[k++] = '\n';
    if (!sameText(group, PRESET_SAVED_GROUP)) k += snprintf(text + k, cap - k, "[" PRESET_SAVED_GROUP "]\n");
    k += presets_line(text + k, cap - k, q);
    text[k++] = '\n';
    text[k] = 0;
    return n;
}

int presets_next_in_group(const Preset *p, int count, int cur)
{
    if (count <= 0) return -1;
    if (cur < 0 || cur >= count) return 0;
    for (int s = 1; s <= count; s++) {
        int i = (cur + s) % count;
        if (!strcmp(p[i].group, p[cur].group)) return i;
    }
    return cur;
}

int presets_group_jump(const Preset *p, int count, int from, int dir)
{
    if (count <= 0) return -1;
    if (from < 0 || from >= count) from = 0;
    // Start of the group of from.
    int start = from;
    while (start > 0 && !strcmp(p[start - 1].group, p[from].group)) start--;
    if (dir > 0) {
        int i = from;
        while (i < count && !strcmp(p[i].group, p[from].group)) i++;
        return i < count ? i : 0;
    }
    if (from != start) return start; // first back to the top of this group
    int i = start > 0 ? start - 1 : count - 1;
    while (i > 0 && !strcmp(p[i - 1].group, p[i].group)) i--;
    return i;
}

bool presets_match(const Preset &p, const char *query)
{
    if (!query[0]) return true;
    if (query[0] >= '0' && query[0] <= '9') {
        char f[12];
        snprintf(f, sizeof(f), "%lu.%03lu", (unsigned long)(p.khz / 1000), (unsigned long)(p.khz % 1000));
        return !strncmp(f, query, strlen(query));
    }
    return contains(p.name, query) || contains(p.group, query) || contains(sonde_type_name(p.type), query);
}

bool presets_unique_name(const Preset *p, int count, const char *want, char *out, size_t n)
{
    char base[sizeof(((Preset *)0)->name)];
    while (*want == ' ') want++;
    snprintf(base, sizeof(base), "%s", want);
    for (size_t k = strlen(base); k && base[k - 1] == ' '; k--) base[k - 1] = 0;
    if (!base[0] || n < 2) return false;
    for (int suffix = 1; suffix < 1000; suffix++) {
        char cand[sizeof(base)];
        if (suffix == 1) {
            snprintf(cand, sizeof(cand), "%s", base);
        } else {
            char sfx[8];
            int ls = snprintf(sfx, sizeof(sfx), " %d", suffix);
            int room = (int)sizeof(cand) - 1 - ls; // the suffix always fits: the base is cut if needed
            snprintf(cand, sizeof(cand), "%.*s%s", room, base, sfx);
        }
        bool taken = false;
        for (int i = 0; i < count && !taken; i++) taken = sameText(p[i].name, cand);
        if (!taken) {
            snprintf(out, n, "%s", cand);
            return true;
        }
    }
    return false;
}
