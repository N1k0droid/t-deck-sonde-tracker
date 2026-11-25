// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Graw DFM-06/09/17 decoder: Manchester, deinterleaving, Hamming(8,4), data blocks.
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "dfm.h"
#include "bytes.h"
#include "timeutil.h"
#include <stdio.h>
#include <string.h>

// Header 0x45CF, Manchester "01" = 1, "10" = 0: 10 01 10 10 10 01 10 01 01 01 10 10 01 01 01 01
const uint8_t DFM_SYNC[4] = {0x9A, 0x99, 0x5A, 0x55};

// Parity rows of the Hamming(8,4) code: parity bit k = XOR of the data bits marked in ROW[k] (b3..b0 = d0..d3).
static const uint8_t ROW[4] = {0x7, 0xB, 0xD, 0xE};

static int parity4(uint8_t v)
{
    v &= 0xF;
    return (v ^ (v >> 1) ^ (v >> 2) ^ (v >> 3)) & 1;
}

#ifdef HOST_TEST
uint8_t dfm_hamming_encode(uint8_t nib)
{
    nib &= 0xF;
    uint8_t cw = (uint8_t)(nib << 4);
    for (int k = 0; k < 4; k++) cw |= (uint8_t)(parity4(nib & ROW[k]) << (3 - k));
    return cw;
}
#endif

// Syndrome of a codeword (8 bits, MSB = d0); 0 when valid.
static uint8_t syndrome(uint8_t cw)
{
    uint8_t s = 0;
    uint8_t d = cw >> 4;
    for (int k = 0; k < 4; k++) {
        int bit = parity4(d & ROW[k]) ^ ((cw >> (3 - k)) & 1);
        s |= (uint8_t)(bit << (3 - k));
    }
    return s;
}

// Corrects up to one bit; returns the data nibble, *err = 0 ok, 1 corrected, 2 not correctable.
static uint8_t hammingDecode(uint8_t cw, int *err)
{
    uint8_t s = syndrome(cw);
    *err = 0;
    if (s) {
        // the syndrome of a single error equals the column of the parity-check matrix of that bit
        static const uint8_t COL[8] = {0x7, 0xB, 0xD, 0xE, 0x8, 0x4, 0x2, 0x1};
        int pos = -1;
        for (int i = 0; i < 8; i++)
            if (COL[i] == s) pos = i;
        if (pos < 0) {
            *err = 2;
        } else {
            cw ^= (uint8_t)(0x80 >> pos);
            *err = 1;
        }
    }
    return cw >> 4;
}

static uint32_t bitsVal(const uint8_t *bits, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | bits[i];
    return v;
}

// L codewords interleaved by bit: transmitted bit L*j+i is bit j of codeword i.
static void decodeBlock(const uint8_t *bits, int L, uint8_t *nibbles, int *corrected, int *failed)
{
    for (int i = 0; i < L; i++) {
        uint8_t cw = 0;
        for (int j = 0; j < 8; j++) cw = (uint8_t)((cw << 1) | bits[L * j + i]);
        int e;
        nibbles[i] = hammingDecode(cw, &e);
        if (e == 1) (*corrected)++;
        if (e == 2) (*failed)++;
    }
}

static void nibblesToBits(const uint8_t *nib, int n, uint8_t *bits)
{
    for (int i = 0; i < n; i++)
        for (int b = 0; b < 4; b++) bits[4 * i + b] = (nib[i] >> (3 - b)) & 1;
}

void dfm_reset(DfmState *s)
{
    memset(s, 0, sizeof(*s));
    s->posMode = -1;
}

static void confBlock(DfmState *s, const uint8_t *nib)
{
    uint8_t bits[28];
    nibblesToBits(nib, 7, bits);
    uint8_t confId = nib[0];
    uint8_t ch2 = (uint8_t)(nib[0] << 4 | nib[1]);
    if (confId > 4 && bitsVal(bits + 8, 20) == 0) s->nulCh = ch2;
    // DFM-06: 6 hex digits on the channel after the empty one.
    if ((s->nulCh & 0x58) == 0x58 && confId == (s->nulCh >> 4) + 1) {
        uint32_t sn6 = bitsVal(bits + 4, 24);
        if (sn6 && sn6 == s->sn6Prev) {
            s->sn6 = sn6;
            s->type = 6;
        }
        s->sn6Prev = sn6;
        return;
    }
    // DFM-09/17: "sC aaaa h" (or "s0..."), h = which half of the decimal serial number.
    // Only on the serial channel (0xA-0xD, or right after the empty one), not on measurement channels.
    if (confId > 5 && (confId >= 0xA || confId == (s->nulCh >> 4) + 1) && ((ch2 & 0xF) == 0xC || (ch2 & 0xF) == 0x0)) {
        uint32_t val = bitsVal(bits + 8, 20);
        uint8_t half = val & 0xF;
        if (half < 2) {
            if (s->snCh != confId) {
                s->snBits = 0;
                s->snCh = confId;
            }
            s->snHalf[half] = (val >> 4) & 0xFFFF;
            s->snBits |= (uint8_t)(1 << half);
            if (s->snBits == 3) {
                uint32_t sn = s->snHalf[0] << 16 | s->snHalf[1];
                if (sn == s->snPrev) {
                    s->sn = sn;
                    s->type = confId;
                }
                s->snPrev = sn;
                s->snBits = 0;
            }
        }
        return;
    }
    // DFM-09/17 status: battery voltage on channel 5 (mV); used once the model is known.
    if (confId == 5) s->battV = bitsVal(bits + 8, 16) / 1000.0f;
}

static int16_t s16(uint32_t v) { return (int16_t)(uint16_t)v; }

static void dataBlock(DfmState *s, const uint8_t *nib)
{
    uint8_t b[52];
    nibblesToBits(nib, 13, b);
    int id = (int)bitsVal(b + 48, 4);
    if (id == 0) {
        int mode = (int)bitsVal(b + 16, 8);
        if (mode >= 2 && mode <= 4) s->posMode = mode;
        s->frameCount = (uint8_t)bitsVal(b + 24, 8); // seconds counter of the sonde
    }
    if (id == 8) {
        s->year = (int)bitsVal(b, 12);
        s->month = (int)bitsVal(b + 12, 4);
        s->day = (int)bitsVal(b + 16, 5);
        s->hour = (int)bitsVal(b + 21, 5);
        s->minute = (int)bitsVal(b + 26, 6);
        s->sats = (uint8_t)bitsVal(b + 32, 8);
        return;
    }
    if (s->posMode == 2) {
        switch (id) {
        case 1: s->sec = bitsVal(b + 32, 16) / 1000.0f; break;
        case 2: s->lat = (int32_t)bitsVal(b, 32) / 1e7; s->vH = s16(bitsVal(b + 32, 16)) / 100.0f; s->got |= 1; break;
        case 3: s->lon = (int32_t)bitsVal(b, 32) / 1e7; s->dir = bitsVal(b + 32, 16) / 100.0f; s->got |= 2; break;
        case 4: s->alt = (int32_t)bitsVal(b, 32) / 100.0; s->vV = s16(bitsVal(b + 32, 16)) / 100.0f; s->got |= 4; break;
        }
    } else if (s->posMode == 3 || s->posMode == 4) {
        switch (id) {
        case 0: s->sec = bitsVal(b, 16) / 1000.0f; s->vH = s16(bitsVal(b + 32, 16)) / 100.0f; break;
        case 1: s->lat = (int32_t)bitsVal(b, 32) / 1e7; s->dir = bitsVal(b + 32, 16) / 100.0f; s->got |= 1; break;
        case 2: s->lon = (int32_t)bitsVal(b, 32) / 1e7; s->vV = s16(bitsVal(b + 32, 16)) / 100.0f; s->got |= 2; break;
        case 3: s->alt = (int32_t)bitsVal(b, 32) / 100.0; s->got |= 4; break;
        }
    }
}

bool dfm_decode(DfmState *s, const uint8_t *raw, bool invert, DfmFrame *out)
{
    memset(out, 0, sizeof(*out));
    // Manchester: the second symbol of each pair is the data bit ("01" = 1, "10" = 0).
    uint8_t bits[264];
    for (int i = 0; i < 264; i++) bits[i] = (uint8_t)(get_bit(raw, 2 * i + 1) ^ invert);
    uint8_t conf[7], d1[13], d2[13];
    int corr = 0, failConf = 0, fail1 = 0, fail2 = 0;
    decodeBlock(bits, 7, conf, &corr, &failConf);
    decodeBlock(bits + 56, 13, d1, &corr, &fail1);
    decodeBlock(bits + 160, 13, d2, &corr, &fail2);
    out->corrected = corr;
    out->failed = failConf + fail1 + fail2;
    if (!failConf) confBlock(s, conf);
    if (!fail1) dataBlock(s, d1);
    if (!fail2) dataBlock(s, d2);
    out->ok = !fail1 || !fail2;
    if (!out->ok) return false;
    s->frames++;

    if ((s->got & 7) == 7) {
        out->newPos = s->lat > -90 && s->lat < 90 && s->lon > -180 && s->lon <= 180 && s->alt > -1000 && s->alt < 60000;
        s->got = 0;
    }
    if (s->type == 6 && s->sn6) {
        snprintf(out->id, sizeof(out->id), "%06lX", (unsigned long)s->sn6);
        snprintf(out->model, sizeof(out->model), "DFM-06");
    } else if (s->sn) {
        snprintf(out->id, sizeof(out->id), "%08lu", (unsigned long)(s->sn % 100000000UL));
        const char *m = s->type == 0xA ? "DFM-09" : s->type == 0xB || s->type == 0xC ? "DFM-17" : s->type == 0xD ? "DFM-17P" : "DFM";
        snprintf(out->model, sizeof(out->model), "%s", m);
    } else {
        snprintf(out->model, sizeof(out->model), "DFM");
    }
    out->lat = s->lat;
    out->lon = s->lon;
    out->alt = s->alt;
    out->vH = s->vH;
    out->dir = s->dir;
    out->vV = s->vV;
    out->sats = s->sats;
    out->battV = s->type >= 0xA ? s->battV : 0;
    out->frameNb = s->frameCount ? s->frameCount : s->frames;
    out->unixTime = tu_unix(s->year, s->month, s->day, s->hour, s->minute, (int)s->sec);
    return true;
}

static int bitDiff(uint8_t a, uint8_t b)
{
    uint8_t x = a ^ b;
    int n = 0;
    while (x) {
        n += x & 1;
        x >>= 1;
    }
    return n;
}

int dfm_decode_stream(DfmState *s, const uint8_t *raw, int len, bool invert, DfmFrame *out)
{
    int n = 0;
    bool newPos = false;
    DfmFrame f;
    memset(out, 0, sizeof(*out));
    for (int ofs = 0; ofs + DFM_PAYLOAD <= len; ofs += DFM_FRAME_BYTES) {
        if (ofs) { // the header of this frame, just before its payload
            int diff = 0;
            for (int i = 0; i < 4; i++) diff += bitDiff(raw[ofs - 4 + i], invert ? (uint8_t)~DFM_SYNC[i] : DFM_SYNC[i]);
            if (diff > 4) break; // lost the frame rhythm
        }
        if (dfm_decode(s, raw + ofs, invert, &f)) {
            n++;
            newPos |= f.newPos;
            if (f.newPos) { // keep the completed position even if a later frame adds nothing
                *out = f;
            } else {
                double la = out->lat, lo = out->lon, al = out->alt;
                bool had = out->newPos;
                *out = f;
                if (had) {
                    out->lat = la;
                    out->lon = lo;
                    out->alt = al;
                }
            }
            out->newPos = newPos;
        }
    }
    return n;
}
