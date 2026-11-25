// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "json_lite.h"
#include <stdlib.h>
#include <string.h>

static const char *ws(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
    return p;
}

// p at the opening quote; returns one past the closing quote.
static const char *skipString(const char *p, const char *e)
{
    for (p++; p < e; p++) {
        if (*p == '\\') {
            p++;
            continue;
        }
        if (*p == '"') return p + 1;
    }
    return nullptr;
}

// Skips one value; returns one past its end.
static const char *skipValue(const char *p, const char *e)
{
    p = ws(p, e);
    if (p >= e) return nullptr;
    if (*p == '"') return skipString(p, e);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (p < e) {
            char c = *p;
            if (c == '"') {
                p = skipString(p, e);
                if (!p) return nullptr;
                continue;
            }
            if (c == '{' || c == '[') depth++;
            else if (c == '}' || c == ']') {
                if (--depth == 0) return p + 1;
            }
            p++;
        }
        return nullptr;
    }
    const char *s = p;
    while (p < e && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
    return p > s ? p : nullptr;
}

JSpan json_span(const char *text, size_t len)
{
    return {text, text + len};
}

bool json_begin(JIter &it, JSpan v)
{
    const char *p = ws(v.p, v.e);
    if (p >= v.e || (*p != '{' && *p != '[')) return false;
    it.close = *p == '{' ? '}' : ']';
    it.p = p + 1;
    it.e = v.e;
    return true;
}

bool json_next(JIter &it, JSpan *key, JSpan &val)
{
    const char *p = ws(it.p, it.e);
    if (p < it.e && *p == ',') p = ws(p + 1, it.e);
    if (p >= it.e || *p == it.close) return false;
    if (it.close == '}') {
        if (*p != '"') return false;
        const char *k = p;
        p = skipString(p, it.e);
        if (!p) return false;
        if (key) {
            key->p = k + 1;
            key->e = p - 1;
        }
        p = ws(p, it.e);
        if (p >= it.e || *p != ':') return false;
        p = ws(p + 1, it.e);
    }
    const char *v = p;
    p = skipValue(p, it.e);
    if (!p) return false;
    val.p = v;
    val.e = p;
    it.p = p;
    return true;
}

static bool json_key_is(JSpan key, const char *s)
{
    size_t n = strlen(s);
    return (size_t)(key.e - key.p) == n && !memcmp(key.p, s, n);
}

bool json_get(JSpan obj, const char *key, JSpan &val)
{
    JIter it;
    if (!json_begin(it, obj) || it.close != '}') return false;
    JSpan k, v;
    while (json_next(it, &k, v)) {
        if (json_key_is(k, key)) {
            val = v;
            return true;
        }
    }
    return false;
}

double json_num(JSpan v, double def)
{
    const char *p = v.p;
    if (p < v.e && *p == '"') p++;
    char buf[32];
    size_t n = 0;
    while (p < v.e && n < sizeof(buf) - 1 && *p != '"') buf[n++] = *p++;
    buf[n] = 0;
    if (!n || !strcmp(buf, "null")) return def;
    char *end;
    double d = strtod(buf, &end);
    return end == buf ? def : d;
}

int json_str(JSpan v, char *out, size_t n)
{
    if (v.e - v.p < 2 || *v.p != '"') return -1;
    size_t k = 0;
    for (const char *p = v.p + 1; p < v.e - 1 && k + 1 < n; p++) {
        char c = *p;
        if (c == '\\' && p + 1 < v.e - 1) {
            c = *++p;
            switch (c) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': p += 4; c = '?'; break;
            default: break; // \" \\ \/
            }
        }
        out[k++] = c;
    }
    if (n) out[k] = 0;
    return (int)k;
}

double json_get_num(JSpan obj, const char *key, double def)
{
    JSpan v;
    return json_get(obj, key, v) ? json_num(v, def) : def;
}

void json_get_str(JSpan obj, const char *key, char *out, size_t n)
{
    JSpan v;
    out[0] = 0;
    if (json_get(obj, key, v) && json_str(v, out, n) < 0) out[0] = 0;
}
