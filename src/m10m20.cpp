// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Meteomodem M10, M10+ and M20 decoder: Manchester + differential coding, checksum, GPS fields.
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "m10m20.h"
#include "bytes.h"
#include "geo.h"
#include "timeutil.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// Header "10011001100110010100110010011001" (32 symbols, MSB first)
const uint8_t M10_SYNC[4] = {0x99, 0x99, 0x4C, 0x99};

// The frame check: a 16-bit linear checksum updated byte by byte.
static uint16_t checkStep(uint16_t c, uint8_t b)
{
    int c1 = c & 0xFF;
    b = (uint8_t)((b >> 1) | ((b & 1) << 7));
    b ^= (b >> 2) & 0xFF;
    int t6 = (c & 1) ^ ((c >> 2) & 1) ^ ((c >> 4) & 1);
    int t7 = ((c >> 1) & 1) ^ ((c >> 3) & 1) ^ ((c >> 5) & 1);
    int t = (c & 0x3F) | (t6 << 6) | (t7 << 7);
    int s = (c >> 7) & 0xFF;
    s ^= (s >> 2) & 0xFF;
    int c0 = b ^ t ^ s;
    return (uint16_t)(((c1 << 8) | c0) & 0xFFFF);
}

static uint16_t m10_check(const uint8_t *msg, int n)
{
    uint16_t c = 0;
    for (int i = 0; i < n; i++) c = checkStep(c, msg[i]);
    return c;
}

static inline int symAt(const uint8_t *raw, int s) { return get_bit(raw, s); }

// Manchester pair "01" = 1, "10" = 0 (m = the second symbol); the data bit is 1 when the Manchester bit
// repeats the previous one. The reference before the first data bit is 0 (1 with inverted polarity).
static void bitsToFrame(const uint8_t *m, int nBits, bool invert, uint8_t *frame, int maxBytes)
{
    int prev = invert ? 1 : 0;
    memset(frame, 0, maxBytes);
    for (int i = 0; i < nBits; i++) {
        if (m[i] == prev) frame[i / 8] |= (uint8_t)(0x80 >> (i % 8));
        prev = m[i];
    }
}

#define SLIP_BAND 4 // up to 4 symbols lost or added by the receiver in one frame
#define SLIP_COST 2
#define SLIP_VARIANTS 2 // slips whose kind (lost / added) is also tried the other way

enum { STEP_PAIR = 1, STEP_LOST = 2, STEP_ADDED = 3 };

// Pairs the symbols into Manchester bits along the cheapest path: steps[i] says how pair i was read. A pair
// costs 1 when its two symbols are equal (one of them is wrong); a symbol lost or added by the receiver's
// clock (a slip: the following pairs are out of step) costs SLIP_COST. False when no path fits.
static bool alignPairs(const uint8_t *raw, int nSym, int nPairs, uint8_t *steps)
{
    const int W = 2 * SLIP_BAND + 1, INF = 1 << 28;
    static uint8_t back[M10_MAXFRAME * 8 + 1][2 * SLIP_BAND + 1];
    int cost[W], next[W];
    for (int o = 0; o < W; o++) cost[o] = INF;
    cost[SLIP_BAND] = 0; // symbol position of pair i: 2 * i + o - SLIP_BAND
    for (int i = 0; i < nPairs; i++) {
        for (int o = 0; o < W; o++) next[o] = INF;
        for (int o = 0; o < W; o++) { // plain pairs first: on equal cost they win, so a slip goes as early as it can
            int pos = 2 * i + o - SLIP_BAND;
            if (cost[o] >= INF || pos < 0 || pos + 1 >= nSym) continue;
            next[o] = cost[o] + (symAt(raw, pos) == symAt(raw, pos + 1));
            back[i + 1][o] = STEP_PAIR;
        }
        for (int o = 0; o < W; o++) {
            int pos = 2 * i + o - SLIP_BAND;
            if (cost[o] >= INF || pos < 0) continue;
            if (o > 0 && pos < nSym && cost[o] + SLIP_COST < next[o - 1])
                next[o - 1] = cost[o] + SLIP_COST, back[i + 1][o - 1] = STEP_LOST;
            if (o + 1 < W && pos + 2 < nSym && cost[o] + SLIP_COST < next[o + 1])
                next[o + 1] = cost[o] + SLIP_COST, back[i + 1][o + 1] = STEP_ADDED;
        }
        memcpy(cost, next, sizeof(cost));
    }
    int o = 0;
    for (int k = 1; k < W; k++)
        if (cost[k] < cost[o]) o = k;
    if (cost[o] >= INF) return false;
    for (int i = nPairs; i > 0; i--) {
        steps[i - 1] = back[i][o];
        o = steps[i - 1] == STEP_PAIR ? o : steps[i - 1] == STEP_LOST ? o + 1 : o - 1;
    }
    return true;
}

// Reads the pairs along the steps. A symbol added and a symbol lost then a whole bit more leave the same
// valid pairs after them: only the frame check tells them apart, so the first slips can be read the other
// way (bit k of flip: slip k). m: the bit of every pair (a guess when doubtful); doubt: a wrong pair or a
// slip. Returns the number of doubtful bits, -1 when the symbols run out.
static int readPairs(const uint8_t *raw, int nSym, const uint8_t *steps, int nPairs, int flip, uint8_t *m,
                     uint8_t *doubt)
{
    int pos = 0, nd = 0, slip = 0;
    for (int i = 0; i < nPairs; i++) {
        int st = steps[i];
        if (st != STEP_PAIR && slip < SLIP_VARIANTS && (flip >> slip++ & 1)) st = st == STEP_LOST ? STEP_ADDED : STEP_LOST;
        int need = st == STEP_LOST ? 1 : st == STEP_PAIR ? 2 : 3;
        if (pos + need > nSym) return -1;
        int a = symAt(raw, pos), b = need > 1 ? symAt(raw, pos + 1) : a;
        if (st == STEP_PAIR) {
            m[i] = (uint8_t)b;
            doubt[i] = a == b;
        } else if (st == STEP_LOST) {
            m[i] = (uint8_t)a;
            doubt[i] = 1;
        } else { // one of the three symbols is too many
            m[i] = (uint8_t)(a != b ? b : symAt(raw, pos + 2));
            doubt[i] = 1;
        }
        nd += doubt[i];
        pos += need;
    }
    return nd;
}

static int countSlips(const uint8_t *steps, int nPairs)
{
    int n = 0;
    for (int i = 0; i < nPairs; i++) n += steps[i] != STEP_PAIR;
    return n;
}

#ifdef HOST_TEST
int m10_doubtful(const uint8_t *raw, int len, int nPairs, int *plain, uint8_t *mOut, uint8_t *dOut)
{
    static uint8_t steps[M10_MAXFRAME * 8], m[M10_MAXFRAME * 8], d[M10_MAXFRAME * 8];
    if (nPairs > M10_MAXFRAME * 8) nPairs = M10_MAXFRAME * 8;
    int nd = alignPairs(raw, len * 8, nPairs, steps) ? readPairs(raw, len * 8, steps, nPairs, 0, m, d) : -1;
    if (mOut) memcpy(mOut, m, nPairs);
    if (dOut) memcpy(dOut, d, nPairs);
    int bad = 0;
    for (int i = 0; i < nPairs; i++) bad += symAt(raw, 2 * i) == symAt(raw, 2 * i + 1);
    if (plain) *plain = bad; // doubtful bits without realignment, for comparison
    return nd;
}
#endif

static bool checkOk(const uint8_t *f)
{
    int flen = f[0];
    return (uint16_t)(f[flen - 1] << 8 | f[flen]) == m10_check(f, flen - 1);
}

// (check of the data XOR the stored check) << 8 | length byte, for a frame of flen + 1 bytes: it equals
// flen exactly when the length byte is flen and checkOk() holds. XOR-linear in the frame bits.
static uint32_t syndrome(const uint8_t *f, int flen)
{
    uint16_t c = m10_check(f, flen - 1) ^ (uint16_t)(f[flen - 1] << 8 | f[flen]);
    return (uint32_t)c << 8 | f[0];
}

// Syndrome change when Manchester bit j flips: frame bits j and j + 1 change (each frame bit compares a
// Manchester bit with the one before it).
static uint32_t flipSyndrome(int j, int nBits, int flen)
{
    uint8_t d[M10_MAXFRAME + 1] = {0};
    d[j / 8] ^= (uint8_t)(0x80 >> (j % 8));
    if (j + 1 < nBits) d[(j + 1) / 8] ^= (uint8_t)(0x80 >> ((j + 1) % 8));
    return syndrome(d, flen);
}


static uint32_t gpsToUnix(uint32_t week, double towS, int leap)
{
    if (week < 1304) week += 1024; // week number rollover of old receivers
    if (week > 4000) return 0;
    return (uint32_t)(GPS_EPOCH_UNIX + week * 604800.0 + towS - leap);
}

static void velocity(M10Frame *o, double vE, double vN, double vU)
{
    o->vH = (float)sqrt(vE * vE + vN * vN);
    o->dir = (float)geo_dir_deg(vE, vN);
    o->vV = (float)vU;
}

// A 16-bit check alone lets a damaged frame through now and then: the values must also make sense.
static bool plausible(const M10Frame *o)
{
    return fabs(o->lat) <= 90 && fabs(o->lon) <= 180 && o->alt > -500 && o->alt < 60000 && fabs(o->vV) < 200 &&
           o->vH < 300 && o->sats <= 32;
}

static bool decodeOne(const uint8_t *raw, int len, bool invert, M10Frame *out, int maxDoubt)
{
    memset(out, 0, sizeof(*out));
    uint8_t f[M10_MAXFRAME + 1];
    static uint8_t m[M10_MAXFRAME * 8]; // static: the main task has a small stack (one decoder at a time)
    int nBits = len * 8 / 2;
    if (nBits > M10_MAXFRAME * 8) nBits = M10_MAXFRAME * 8;
    for (int i = 0; i < nBits; i++) m[i] = (uint8_t)symAt(raw, 2 * i + 1);
    bitsToFrame(m, nBits, invert, f, M10_MAXFRAME);
    int flen = f[0]; // index of the last byte
    if (flen < 0x20 || flen + 1 > nBits / 8) return false;
    out->len = flen + 1;
    out->type = f[1];
    out->ok = checkOk(f);
    if (out->ok) { // the check holds: a pair with two equal symbols still makes the frame doubtful
        int bad = 0;
        for (int i = 0; i < (flen + 1) * 8; i++) bad += symAt(raw, 2 * i) == symAt(raw, 2 * i + 1);
        out->fixed = (uint8_t)(bad > 255 ? 255 : bad);
    } else {
        // A pair with two equal symbols had one of them wrong, and the receiver may have lost or added a
        // symbol (the pairs after it are out of step): the cheapest pairing marks those bits as doubtful and
        // trusts the others. With few doubtful bits every combination is tried against the check.
        static uint8_t d[M10_MAXFRAME * 8], steps[M10_MAXFRAME * 8];
        int nPairs = (flen + 1) * 8;
        if (!alignPairs(raw, len * 8, nPairs, steps)) return false;
        int slips = countSlips(steps, nPairs);
        int variants = 1 << (slips < SLIP_VARIANTS ? slips : SLIP_VARIANTS);
        for (int v = 0; v < variants && !out->ok; v++) {
            int nd = readPairs(raw, len * 8, steps, nPairs, v, m, d);
            if (nd < 1 || nd > maxDoubt) continue;
            int doubt[M10_MAX_DOUBTFUL], k = 0;
            for (int i = 0; i < nPairs; i++)
                if (d[i]) doubt[k++] = i;
            // The check is linear: a frame is good when its syndrome equals flen, and flipping a Manchester
            // bit changes the syndrome by a fixed amount. Same order of combinations as trying each one,
            // one XOR per combination instead of rebuilding and checking the whole frame.
            bitsToFrame(m, nPairs, invert, f, M10_MAXFRAME);
            uint32_t syn = syndrome(f, flen), acc[M10_MAX_DOUBTFUL], x = 0;
            for (int j = 0; j < nd; j++) acc[j] = x ^= flipSyndrome(doubt[j], nPairs, flen);
            int found = syn == (uint32_t)flen ? 0 : -1;
            for (int combo = 1; combo < (1 << nd) && found < 0; combo++) {
                syn ^= acc[__builtin_ctz(combo)]; // combo - 1 -> combo flips bits 0..ctz(combo)
                if (syn == (uint32_t)flen) found = combo;
            }
            if (found >= 0) {
                for (int j = 0; j < nd; j++) m[doubt[j]] ^= (uint8_t)((found >> j) & 1);
                bitsToFrame(m, nPairs, invert, f, M10_MAXFRAME);
                out->ok = f[0] == flen && checkOk(f);
            }
            if (out->ok) out->fixed = (uint8_t)(nd + (v ? 1 : 0)); // a slip read the other way counts one more
        }
        if (!out->ok) return false;
    }

    if (f[1] == 0x20) { // M20
        snprintf(out->model, sizeof(out->model), "M20");
        if (flen < 0x43) return false;
        out->lat = (int32_t)get_be(f + 0x1C, 4) / 1e6;
        out->lon = (int32_t)get_be(f + 0x20, 4) / 1e6;
        out->alt = get_be(f + 0x08, 3) / 100.0;
        velocity(out, (int16_t)get_be(f + 0x0B, 2) / 100.0, (int16_t)get_be(f + 0x0D, 2) / 100.0, (int16_t)get_be(f + 0x18, 2) / 100.0);
        out->unixTime = gpsToUnix(get_be(f + 0x1A, 2), get_be(f + 0x0F, 3), GPS_UTC_LEAP_S);
        out->counter = f[0x15];
        out->battV = f[0x26] * 3.3f / 255.0f;
        uint32_t sn = (uint32_t)f[0x14] << 16 | (uint32_t)f[0x13] << 8 | f[0x12];
        int ym = sn & 0x7F;
        snprintf(out->id, sizeof(out->id), "%u%02u-%u-%u%04u", ym / 12, ym % 12 + 1, (unsigned)((sn >> 7) & 7) + 1,
                 (unsigned)((sn >> 23) & 1), (unsigned)((sn >> 10) & 0x1FFF));
        out->gpsValid = (out->lat != 0 || out->lon != 0) && out->alt > -500 && out->alt < 60000;
        return plausible(out);
    }
    if (f[1] == 0x9F || f[1] == 0x8F || f[1] == 0xAF) { // M10 (Trimble GPS) or M10+ (Gtop GPS)
        if (flen < 0x64) return false;
        snprintf(out->model, sizeof(out->model), f[1] == 0xAF ? "M10+" : "M10");
        if (f[1] == 0xAF) {
            out->lat = (int32_t)get_be(f + 0x04, 4) / 1e6;
            out->lon = (int32_t)get_be(f + 0x08, 4) / 1e6;
            int32_t a = (int32_t)get_be(f + 0x0C, 3);
            if (a & 0x800000) a -= 0x1000000;
            out->alt = a / 100.0;
            velocity(out, (int16_t)get_be(f + 0x0F, 2) / 100.0, (int16_t)get_be(f + 0x11, 2) / 100.0, (int16_t)get_be(f + 0x13, 2) / 100.0);
            uint32_t t = get_be(f + 0x15, 3), d = get_be(f + 0x18, 3);
            out->unixTime = tu_unix(2000 + d % 100, (d % 10000) / 100, d / 10000, t / 10000, (t % 10000) / 100, t % 100);
            out->gpsValid = out->lat != 0 || out->lon != 0;
        } else {
            const double scale = (1 << 30) / 90.0; // 2^32 / 360
            out->lat = (int32_t)get_be(f + 0x0E, 4) / scale;
            out->lon = (int32_t)get_be(f + 0x12, 4) / scale;
            out->alt = (int32_t)get_be(f + 0x16, 4) / 1000.0;
            velocity(out, (int16_t)get_be(f + 0x04, 2) / 200.0, (int16_t)get_be(f + 0x06, 2) / 200.0, (int16_t)get_be(f + 0x08, 2) / 200.0);
            out->sats = f[0x1E];
            out->unixTime = gpsToUnix(get_be(f + 0x20, 2), get_be(f + 0x0A, 4) / 1000.0, f[0x1F]);
            out->gpsValid = !(f[0x1D] & 1) && out->sats >= 4;
        }
        out->counter = f[0x62];
        uint32_t adc = (uint32_t)f[0x46] << 8 | f[0x45];
        out->battV = adc ? 2.709f * adc * 2.5f / 1023.0f : 0;
        uint8_t b2 = f[0x5F];
        uint16_t b34 = (uint16_t)(f[0x60] | f[0x61] << 8);
        snprintf(out->id, sizeof(out->id), "%1X%02u-%1X-%1u%04u", (b2 >> 4) & 0xF, b2 & 0xF, f[0x5D] & 0xF,
                 (unsigned)((b34 >> 13) & 7), (unsigned)(b34 & 0x1FFF));
        return plausible(out);
    }
    return false; // satellite-status or unknown frame
}

// Same sonde, 1-30 s later; position within 20 m + 3 m/s of the one predicted from the other frame (its
// position moved by its speed and heading), altitude likewise, speeds close.
static bool agrees(const M10Fix *st, const M10Frame *o)
{
    if (!st->have || !o->gpsValid || !o->unixTime || !st->unixTime || strcmp(st->id, o->id)) return false;
    double dt = (double)o->unixTime - (double)st->unixTime;
    if (dt < 1 || dt > 30) return false;
    double h = st->dir * GEO_DEG, de, dn;
    geo_enu_m(st->lat, st->lon, o->lat, o->lon, &de, &dn);
    dn -= st->vH * cos(h) * dt; // minus the move predicted from its speed and heading, m
    de -= st->vH * sin(h) * dt;
    return sqrt(dn * dn + de * de) <= 20 + 3 * dt && fabs(o->alt - (st->alt + st->vV * dt)) <= 15 + 3 * dt &&
           fabs(o->vV - st->vV) <= 2 + dt && fabs(o->vH - st->vH) <= 3 + dt;
}

static void keep(M10Fix *k, const M10Frame *o)
{
    k->have = o->gpsValid && o->unixTime;
    snprintf(k->id, sizeof(k->id), "%s", o->id);
    k->lat = o->lat;
    k->lon = o->lon;
    k->alt = o->alt;
    k->vH = o->vH;
    k->dir = o->dir;
    k->vV = o->vV;
    k->unixTime = o->unixTime;
}

bool m10_decode(const uint8_t *raw, int len, bool invert, M10Frame *out, M10State *st)
{
    if (!decodeOne(raw, len, invert, out, st ? M10_MAX_DOUBTFUL : M10_MAX_DOUBTFUL_ALONE)) return false;
    if (!st) return true;
    if (out->fixed && !agrees(&st->last, out) && !agrees(&st->waiting, out)) {
        keep(&st->waiting, out); // not shown: it may confirm the next one
        out->ok = false;
        return false;
    }
    keep(&st->last, out);
    st->waiting.have = false;
    return true;
}
