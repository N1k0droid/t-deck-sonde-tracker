// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// InterMet iMet-54 (and iMet-50) decoder: 8N1 characters, 8x8 interleaving, Hamming(8,4), CRC32.
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "imet54.h"
#include "bytes.h"
#include "geo.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// "0000000001 0101010101 0001001001 0001001001": 8N1 characters 0x00 0xAA 0x24 0x24 (MSB first)
const uint8_t IMET54_SYNC[5] = {0x00, 0x55, 0x51, 0x24, 0x49};

// Codeword of each nibble; bit k of the byte is codeword bit k.
static const uint8_t HAM[16] = {0x00, 0x87, 0x99, 0x1E, 0xAA, 0x2D, 0x33, 0xB4,
                                0x4B, 0xCC, 0xD2, 0x55, 0xE1, 0x66, 0x78, 0xFF};

// Syndrome of a codeword (rows of the parity-check matrix as bit masks).
static int syndrome(uint8_t c)
{
    static const uint8_t ROW[4] = {0x55, 0x66, 0x78, 0xFF};
    int s = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t x = c & ROW[i];
        x ^= x >> 4;
        x ^= x >> 2;
        x ^= x >> 1;
        s |= (x & 1) << i;
    }
    return s;
}

// 0 clean, 1 corrected, -1 not correctable. Single-bit syndromes: 0x9..0xF for bits 0..6, 0x8 for bit 7.
static int hamming(uint8_t c, uint8_t *nib)
{
    int s = syndrome(c), r = 0;
    if (s) {
        if (!(s & 8)) return -1; // even number of errors
        int bit = s == 8 ? 7 : s - 9;
        c ^= (uint8_t)(1 << bit);
        r = 1;
    }
    for (int n = 0; n < 16; n++)
        if (HAM[n] == c) {
            *nib = (uint8_t)n;
            return r;
        }
    return -1;
}

// Check of the 108-byte frame: two 16-bit halves over bytes 0..105 (except 100, 101), stored at 100 and 106.
static void crcStdCalc(const uint8_t *f, uint32_t *r0, uint32_t *r1)
{
    uint32_t c0 = 0x48EB, c1 = 0x1ACA, crc0 = 0, crc1 = 0;
    int n = 104, b = 0;
    while (n >= 0) {
        if (n < 100 || (n > 101 && n < 106)) {
            if ((f[n] >> b) & 1) {
                crc0 ^= c0;
                crc1 ^= c1;
            }
        }
        uint32_t n0 = c0, n1 = c1;
        if (c1 & 0x8000) {
            n0 ^= 0x0EDB;
            n1 ^= 0x8260;
        }
        n0 <<= 1;
        n1 <<= 1;
        if (c1 & 0x8000) n0 |= 1;
        if ((c1 ^ c0) & 0x8000) n1 |= 1;
        c0 = n0 & 0xFFFF;
        c1 = n1; // not masked, as in the known decoders: only the low 16 bits are compared
        if (b < 7) {
            b++;
        } else {
            b = 0;
            n += n % 4 == 3 ? -7 : 1;
        }
    }
    *r0 = crc0;
    *r1 = crc1;
}

static bool imet54_crc_std(const uint8_t *f, int len)
{
    if (len < IMET54_FRAME) return false;
    uint32_t crc0, crc1;
    crcStdCalc(f, &crc0, &crc1);
    crc0 ^= (uint32_t)(f[100] << 8 | f[101]) ^ 0x5000;
    crc1 ^= (uint32_t)(f[106] << 8 | f[107]) ^ 0x1DAD;
    return (crc1 & 0xFFFF) == 0 && (crc0 & 0xF000) == 0 && !(crc1 >> 16);
}

// CRC-32 (IEEE, MSB first, final xor 0x63D60875) over bytes 0..0x33 taken as little-endian words, at 0x34.
static uint32_t crcContCalc(const uint8_t *f)
{
    uint32_t rem = 0;
    for (int i = 0; i < 0x34; i++) {
        rem ^= (uint32_t)f[(i & ~3) + 3 - (i & 3)] << 24;
        for (int j = 0; j < 8; j++) rem = rem & 0x80000000u ? (rem << 1) ^ 0x04C11DB7u : rem << 1;
    }
    return rem ^ 0x63D60875u;
}

static bool imet54_crc_cont(const uint8_t *f, int len)
{
    return len >= 0x38 && crcContCalc(f) == get_be32(f + 0x34);
}


static float f32be(const uint8_t *p)
{
    uint32_t v = get_be32(p);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

// Water vapour saturation pressure (Hyland and Wexler), for the humidity sensor temperature correction.
static double vaporSat(double tc)
{
    double t = tc + 273.15;
    return exp(-5800.2206 / t + 1.3914993 + 6.5459673 * log(t) - 4.8640239e-2 * t + 4.1764768e-5 * t * t -
               1.4452093e-8 * t * t * t);
}

// ddmm.mmmm x 1e4 (degrees x 1e6 + minutes x 1e4) -> degrees
static double ddmm(int32_t v)
{
    int32_t deg = v / 1000000;
    return deg + (v - deg * 1000000) / 1e4 / 60.0;
}

void imet54_reset(Imet54State *st) { memset(st, 0, sizeof(*st)); }

bool imet54_decode(Imet54State *st, const uint8_t *raw, int len, bool invert, Imet54Frame *out)
{
    memset(out, 0, sizeof(*out));
    out->secOfDay = -1;
    out->tempC = -273.15f;
    out->rh = -1;
    int chars = (len * 8 - IMET54_SKIP_BITS) / 10;
    if (chars > IMET54_CHARS) chars = IMET54_CHARS;
    int blocks = chars * 8 / 64;
    if (blocks < 7) return false; // fewer than 56 bytes: no check possible

    static uint8_t data[IMET54_CHARS * 8];
    for (int c = 0; c < blocks * 8; c++)
        for (int i = 0; i < 8; i++) data[c * 8 + i] = (uint8_t)(get_bit(raw, IMET54_SKIP_BITS + c * 10 + 1 + i) ^ invert);

    uint8_t f[IMET54_FRAME];
    memset(f, 0, sizeof(f));
    int badHead = 0;
    for (int b = 0; b < blocks; b++) {
        const uint8_t *in = data + b * 64;
        for (int j = 0; j < 8; j++) { // codeword j of the block: bits in[8i + j], i = 0..7
            uint8_t cw = 0, nib = 0;
            for (int i = 0; i < 8; i++) cw |= (uint8_t)(in[8 * i + j] << i);
            int r = hamming(cw, &nib);
            int k = b * 8 + j; // nibble index
            if (r < 0) {
                if (k < 2 * 0x34) badHead++;
            } else if (r > 0) {
                out->corrected++;
            }
            if (k < 2 * IMET54_FRAME) f[k / 2] |= (uint8_t)(k & 1 ? nib : nib << 4);
        }
    }
    int nBytes = blocks * 4;
    out->bytes = nBytes;
    out->crcStd = imet54_crc_std(f, nBytes);
    out->crcCont = imet54_crc_cont(f, nBytes);
    bool clean = badHead == 0 && out->corrected == 0 && nBytes >= 0x34; // no error at all in the GPS/PTU block

    out->sn = get_be32(f);
    snprintf(out->id, sizeof(out->id), "%lu", (unsigned long)out->sn);
    int32_t tm = (int32_t)get_be32(f + 0x04); // hhmmssmmm
    out->lat = ddmm((int32_t)get_be32(f + 0x08));
    out->lon = ddmm((int32_t)get_be32(f + 0x0C));
    out->alt = (int32_t)get_be32(f + 0x10) / 10.0;
    bool plausible = tm >= 0 && tm <= 235959999 && fabs(out->lat) <= 90 && fabs(out->lon) <= 180 &&
                     out->alt > -400 && out->alt < 60000;
    out->ok = (out->crcStd || out->crcCont || clean) && plausible;
    if (!out->ok) return false;

    out->secOfDay = (tm / 10000000) * 3600 + (tm / 100000 % 100) * 60 + tm / 1000 % 100;
    out->gpsValid = out->lat != 0 || out->lon != 0;
    out->status = (uint16_t)(f[0x2A] << 8 | f[0x2B]);
    int missing = 0;
    float t = f32be(f + 0x1C), rhRaw = f32be(f + 0x20), trh = f32be(f + 0x24);
    missing += get_be32(f + 0x1C) == 0x4E6E6B28;
    missing += get_be32(f + 0x20) == 0x4E6E6B28;
    missing += get_be32(f + 0x24) == 0x4E6E6B28;
    if (t > -120 && t < 80 && get_be32(f + 0x1C) != 0x4E6E6B28) out->tempC = t;
    if (trh > -120 && trh < 80 && rhRaw >= 0 && rhRaw <= 100 && out->tempC > -273) {
        double rh = rhRaw * vaporSat(trh) / vaporSat(out->tempC);
        out->rh = (float)(rh < 0 ? 0 : rh > 100 ? 100 : rh);
    }
    int sum = 0;
    for (int i = 0x2C; i < 0x52 && i < nBytes; i++) sum += f[i];
    bool imet50 = nBytes > 0x52 && sum == 0 && (out->status & 0xF0F) == 0 && missing == 3;
    snprintf(out->model, sizeof(out->model), imet50 ? "iMet-50" : "iMet-54");

    // Speeds from the previous frame of the same sonde (1 to 10 s before).
    if (st) {
        if (st->have && st->sn == out->sn && out->gpsValid) {
            int dt = out->secOfDay - st->secOfDay;
            if (dt < 0) dt += 86400;
            if (dt >= 1 && dt <= 10) {
                double dE, dN;
                geo_enu_m(st->lat, st->lon, out->lat, out->lon, &dE, &dN);
                out->vH = (float)(sqrt(dN * dN + dE * dE) / dt);
                out->dir = (float)geo_dir_deg(dE, dN);
                out->vV = (float)((out->alt - st->alt) / dt);
                out->velValid = true;
            }
        }
        if (out->gpsValid) {
            st->have = true;
            st->sn = out->sn;
            st->secOfDay = out->secOfDay;
            st->lat = out->lat;
            st->lon = out->lon;
            st->alt = out->alt;
        }
    }
    return true;
}
