// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// RS41 frame descrambling, error correction and decoding.
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "rs41.h"
#include "rs_ecc.h"
#include "bytes.h"
#include "geo.h"
#include "timeutil.h"
#include <string.h>
#include <math.h>

const uint8_t RS41_SYNC_MSB[8] = {0x08, 0x6D, 0x53, 0x88, 0x44, 0x69, 0x48, 0x1F};

static const uint8_t HDR[8] = {0x86, 0x35, 0xF4, 0x40, 0x93, 0xDF, 0x1A, 0x60};

static const uint8_t MASK[64] = {
    0x96, 0x83, 0x3E, 0x51, 0xB1, 0x49, 0x08, 0x98, 0x32, 0x05, 0x59, 0x0E, 0xF9, 0x44, 0xC6, 0x26,
    0x21, 0x60, 0xC2, 0xEA, 0x79, 0x5D, 0x6D, 0xA1, 0x54, 0x69, 0x47, 0x0C, 0xDC, 0xE8, 0x5C, 0xF1,
    0xF7, 0x76, 0x82, 0x7F, 0x07, 0x99, 0xA2, 0x2C, 0x93, 0x7C, 0x30, 0x63, 0xF5, 0x10, 0x2E, 0x61,
    0xD0, 0xBC, 0xB4, 0xB6, 0x06, 0xAA, 0xF4, 0x23, 0x78, 0x6E, 0x3B, 0xAE, 0xBF, 0x7B, 0x4C, 0xC1};

#define POS_ECC 0x08
#define POS_MSG 0x38
#define POS_FRAMETYPE 0x38
#define POS_BLK_FRAME 0x039
#define POS_BLK_PTU 0x065
#define POS_BLK_GPS1 0x093
#define POS_BLK_GPS2 0x0B5
#define POS_BLK_GPS3 0x112
#define POS_ZERO_STD 0x12B

static uint8_t rs41_bitrev(uint8_t b)
{
    b = (uint8_t)((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = (uint8_t)((b & 0xCC) >> 2 | (b & 0x33) << 2);
    b = (uint8_t)((b & 0xAA) >> 1 | (b & 0x55) << 1);
    return b;
}

int rs41_frame_len_from_raw(uint8_t raw48, bool invert)
{
    uint8_t b = rs41_bitrev(invert ? (uint8_t)~raw48 : raw48) ^ MASK[POS_FRAMETYPE % 64];
    int ft = 0; // +4 for 0x0F, -4 for 0xF0, bit errors move it towards 0
    for (int i = 0; i < 4; i++) ft += ((b >> i) & 1) - ((b >> (i + 4)) & 1);
    return ft < 0 ? RS41_EXT_LEN : RS41_STD_LEN;
}

static uint16_t rs41_crc16(const uint8_t *data, int len)
{
    uint16_t rem = 0xFFFF;
    for (int i = 0; i < len; i++) {
        rem ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            rem = (rem & 0x8000) ? (uint16_t)((rem << 1) ^ 0x1021) : (uint16_t)(rem << 1);
        }
    }
    return rem;
}

void rs41_raw_to_frame(const uint8_t *raw, int nRaw, bool invert, uint8_t *frame)
{
    memcpy(frame, HDR, RS41_HDR_LEN);
    for (int i = 0; i < nRaw && RS41_HDR_LEN + i < RS41_EXT_LEN; i++) {
        int p = RS41_HDR_LEN + i;
        uint8_t r = invert ? (uint8_t)~raw[i] : raw[i];
        frame[p] = rs41_bitrev(r) ^ MASK[p % 64];
    }
}

#ifdef HOST_TEST
void rs41_frame_to_raw(const uint8_t *frame, int len, uint8_t *raw)
{
    for (int p = 0; p < len; p++) raw[p] = rs41_bitrev(frame[p] ^ MASK[p % 64]);
}
#endif

// Maps frame index to (codeword, symbol position); returns false for header bytes.
static bool frameToCw(int i, int *cw, int *pos)
{
    if (i < POS_ECC) return false;
    if (i < POS_MSG) {
        *cw = (i - POS_ECC) / RS_R;
        *pos = (i - POS_ECC) % RS_R;
        return true;
    }
    int k = i - POS_MSG;
    *cw = k & 1;
    *pos = RS_R + (k >> 1);
    return *pos < RS_N;
}

static void setKnownStd(uint8_t *w, bool *known)
{
    w[POS_ZERO_STD] = 0x76;
    w[POS_ZERO_STD + 1] = 0x11;
    for (int i = POS_ZERO_STD + 2; i < RS41_STD_LEN - 2; i++) w[i] = 0;
    w[RS41_STD_LEN - 2] = 0xEC;
    w[RS41_STD_LEN - 1] = 0xC7;
    for (int i = POS_ZERO_STD; i < RS41_STD_LEN; i++) known[i] = true;
    w[POS_BLK_GPS3] = 0x7B;
    w[POS_BLK_GPS3 + 1] = 0x15;
    known[POS_BLK_GPS3] = known[POS_BLK_GPS3 + 1] = true;
}

static void setKnownHeaders(uint8_t *w)
{
    w[POS_BLK_FRAME] = 0x79; w[POS_BLK_FRAME + 1] = 0x28;
    w[POS_BLK_PTU] = 0x7A;   w[POS_BLK_PTU + 1] = 0x2A;
    w[POS_BLK_GPS1] = 0x7C;  w[POS_BLK_GPS1 + 1] = 0x1E;
    w[POS_BLK_GPS2] = 0x7D;  w[POS_BLK_GPS2 + 1] = 0x59;
    w[POS_BLK_GPS3] = 0x7B;  w[POS_BLK_GPS3 + 1] = 0x15;
}

static bool tryEcc(const uint8_t *frame, int rxLen, int frameLen, bool useKnownStd,
                   bool forceHeaders, uint8_t *w, Rs41Result *res)
{
    bool known[RS41_EXT_LEN];
    memcpy(w, frame, RS41_EXT_LEN);
    for (int i = 0; i < RS41_EXT_LEN; i++) known[i] = i < rxLen;
    for (int i = frameLen; i < RS41_EXT_LEN; i++) {
        w[i] = 0;
        known[i] = true;
    }
    if (useKnownStd && frameLen == RS41_STD_LEN) {
        for (int i = 0; i < RS41_EXT_LEN; i++) {
            if (!known[i] && i >= POS_ZERO_STD && i < RS41_STD_LEN) {
                setKnownStd(w, known);
                break;
            }
        }
        if (!known[POS_BLK_GPS3]) {
            w[POS_BLK_GPS3] = 0x7B;
            w[POS_BLK_GPS3 + 1] = 0x15;
            known[POS_BLK_GPS3] = known[POS_BLK_GPS3 + 1] = true;
        }
    }
    if (forceHeaders && frameLen == RS41_STD_LEN) setKnownHeaders(w);

    uint8_t cw[2][RS_N];
    uint8_t era[2][RS_R + 1];
    int nEra[2] = {0, 0};
    memset(cw, 0, sizeof(cw));
    for (int i = POS_ECC; i < RS41_EXT_LEN; i++) {
        int c, p;
        if (!frameToCw(i, &c, &p)) continue;
        cw[c][p] = w[i];
        if (!known[i]) {
            if (nEra[c] >= RS_R) return false;
            era[c][nEra[c]++] = (uint8_t)p;
        }
    }
    res->nEra = nEra[0] + nEra[1];
    res->ecc1 = rs_decode(cw[0], era[0], nEra[0]);
    res->ecc2 = rs_decode(cw[1], era[1], nEra[1]);
    if (res->ecc1 < 0 || res->ecc2 < 0) return false;

    for (int i = POS_ECC; i < frameLen; i++) {
        int c, p;
        if (frameToCw(i, &c, &p)) w[i] = cw[c][p];
    }
    // Shortened code: padding beyond frameLen must stay zero.
    for (int i = frameLen; i < RS41_EXT_LEN; i++) {
        int c, p;
        if (frameToCw(i, &c, &p) && cw[c][p] != 0) return false;
    }
    return true;
}

static void ecefToGeo(double x, double y, double z, double *lat, double *lon, double *alt)
{
    const double a = 6378137.0;
    const double f = 1.0 / 298.257223563;
    const double b = a * (1.0 - f);
    const double e2 = (a * a - b * b) / (a * a);
    const double ee2 = (a * a - b * b) / (b * b);
    double lam = atan2(y, x);
    double p = sqrt(x * x + y * y);
    double t = atan2(z * a, p * b);
    double st = sin(t), ct = cos(t);
    double phi = atan2(z + ee2 * b * st * st * st, p - e2 * a * ct * ct * ct);
    double R = a / sqrt(1.0 - e2 * sin(phi) * sin(phi));
    *alt = p / cos(phi) - R;
    *lat = phi * 180.0 / GEO_PI;
    *lon = lam * 180.0 / GEO_PI;
}

static inline int32_t rd32(const uint8_t *p) { return (int32_t)get_le32(p); }
static inline int16_t rd16(const uint8_t *p) { return (int16_t)get_le16(p); }

static void parseEcef(const uint8_t *d, Rs41Result *res)
{
    double X = rd32(d) / 100.0, Y = rd32(d + 4) / 100.0, Z = rd32(d + 8) / 100.0;
    if (X == 0 && Y == 0 && Z == 0) return;
    ecefToGeo(X, Y, Z, &res->lat, &res->lon, &res->alt);
    double vx = rd16(d + 12) / 100.0, vy = rd16(d + 14) / 100.0, vz = rd16(d + 16) / 100.0;
    double phi = res->lat * GEO_DEG, lam = res->lon * GEO_DEG;
    double vN = -vx * sin(phi) * cos(lam) - vy * sin(phi) * sin(lam) + vz * cos(phi);
    double vE = -vx * sin(lam) + vy * cos(lam);
    double vU = vx * cos(phi) * cos(lam) + vy * cos(phi) * sin(lam) + vz * sin(phi);
    res->vH = sqrt(vN * vN + vE * vE);
    res->vDir = geo_dir_deg(vE, vN);
    res->vV = vU;
    res->gpsValid = res->alt > -1000.0 && res->alt < 60000.0;
}

static int hexVal(const char *s, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        int d = hex_digit(s[i]);
        if (d < 0) return -1;
        v = v * 16 + d;
    }
    return v;
}

// OIF411 measurement string "05NNTTTTIIIIIVVPPPEE" -> o3* fields (ozone partial pressure estimated
// with RS41_O3_T100, no background current). False for other instruments or the ID message.
static bool rs41_parse_oif411(const char *s, Rs41Result *res)
{
    // Layout from rs41mod.c (rs1729/RS) and Vaisala M211486EN: hex nibbles, MSB first.
    if (strlen(s) < 20 || strncmp(s, "05", 2) != 0 || s[20] == 'I') return false;
    int n = hexVal(s + 2, 2), t = hexVal(s + 4, 4), i = hexVal(s + 8, 5), vb = hexVal(s + 13, 2);
    int ip = hexVal(s + 15, 3), ve = hexVal(s + 18, 2);
    if (n < 0 || t < 0 || i < 0 || vb < 0 || ip < 0 || ve < 0) return false;
    res->o3Instr = (uint8_t)n;
    res->o3TpumpC = (int16_t)t / 100.0f;
    res->o3IcellUa = i / 10000.0f;
    res->o3VbatV = vb / 10.0f;
    res->o3IpumpMa = (uint16_t)ip;
    res->o3VextV = ve / 10.0f;
    // Partial pressure (mPa) = 4.307e-4 * I (uA) * T pump (K) * t100 (s)
    res->o3Mpa = 4.307e-4f * res->o3IcellUa * (res->o3TpumpC + 273.15f) * RS41_O3_T100;
    res->o3Valid = true;
    return true;
}

static void parseBlocks(const uint8_t *w, int len, Rs41Result *res)
{
    int pos = POS_BLK_FRAME;
    while (pos + 4 <= len) {
        uint8_t id = w[pos];
        int l = w[pos + 1];
        if (pos + 4 + l > len) break;
        const uint8_t *d = w + pos + 2;
        bool crcOk = rs41_crc16(d, l) == (uint16_t)(d[l] | d[l + 1] << 8);
        if (id == 0x79 && l >= 10) {
            res->frameCrcOk = crcOk;
            if (crcOk) {
                res->frameNb = (uint16_t)(d[0] | d[1] << 8);
                memcpy(res->id, d + 2, 8);
                res->id[8] = 0;
                res->battV = d[10] / 10.0f;
                if (l >= 40) {
                    res->calIdx = d[23];
                    memcpy(res->calData, d + 24, 16);
                }
            }
        } else if (id == 0x7A && l >= 36) {
            res->ptuCrcOk = crcOk;
            if (crcOk) {
                for (int i = 0; i < 12; i++)
                    res->meas[i] = (uint32_t)d[3 * i] | (uint32_t)d[3 * i + 1] << 8 | (uint32_t)d[3 * i + 2] << 16;
            }
        } else if (id == 0x7C && l >= 6) {
            res->timeCrcOk = crcOk;
            if (crcOk) {
                res->gpsWeek = get_le16(d);
                res->gpsTowMs = get_le32(d + 2);
                res->unixTime = rs41_gps_unix(res->gpsWeek, res->gpsTowMs);
            }
        } else if (id == 0x7B && l >= 19) {
            res->gpsCrcOk = crcOk;
            if (crcOk) {
                parseEcef(d, res);
                res->numSV = d[18];
                if (l >= 21) {
                    res->sAcc = d[19];
                    res->pDOP = d[20];
                }
                // Before the first fix the sonde sends wandering positions with 0 sats.
                if (res->numSV < 4) res->gpsValid = false;
            }
        } else if (id == 0x7E && l >= 2) {
            if (crcOk && !res->xdata) {
                // First byte: position in the instrument chain; then ASCII hex.
                int k = 0;
                for (int i = 1; i < l && k < (int)sizeof(res->xdataStr) - 1; i++)
                    if (d[i] > 0x1E && d[i] < 0x7F) res->xdataStr[k++] = (char)d[i];
                res->xdataStr[k] = 0;
                res->xdata = true;
                rs41_parse_oif411(res->xdataStr, res);
            }
        } else if (id == 0x82 && l >= 18) {
            if (crcOk && !res->gpsValid) {
                res->gpsCrcOk = true;
                parseEcef(d, res);
            }
        }
        pos += l + 4;
    }
}

bool rs41_decode(uint8_t *frame, int rxLen, Rs41Result *res, bool useKnownStd)
{
    rs_init();
    memset(res, 0, sizeof(*res));
    res->sondeType = 1; // ST_RS41
    memcpy(res->model, "RS41", 5);
    res->ecc1 = res->ecc2 = -1;
    res->calIdx = 0xFF;
    if (rxLen > RS41_EXT_LEN) rxLen = RS41_EXT_LEN;

    static uint8_t w[RS41_EXT_LEN];
    int order[2] = {RS41_STD_LEN, RS41_EXT_LEN};
    if (rxLen > POS_FRAMETYPE && frame[POS_FRAMETYPE] == 0xF0) {
        order[0] = RS41_EXT_LEN;
        order[1] = RS41_STD_LEN;
    }
    bool ok = false;
    for (int pass = 0; pass < 2 && !ok; pass++) {
        for (int k = 0; k < 2 && !ok; k++) {
            int flen = order[k];
            if (pass == 1 && flen != RS41_STD_LEN) continue; // pass 1 adds the known headers: standard frames only
            ok = tryEcc(frame, rxLen, flen, useKnownStd, pass == 1, w, res);
            if (ok) res->frameLen = flen;
        }
    }
    res->eccOk = ok;
    int parseLen = ok ? res->frameLen : (rxLen < RS41_STD_LEN ? rxLen : RS41_STD_LEN);
    if (ok) memcpy(frame, w, RS41_EXT_LEN);
    parseBlocks(frame, parseLen, res);
    return ok;
}

// ---- Calibration / config subframes ----

void rs41_calib_reset(Rs41Calib *c)
{
    memset(c, 0, sizeof(*c));
}

void rs41_calib_update(Rs41Calib *c, const Rs41Result *res)
{
    if (!res->frameCrcOk || res->calIdx >= RS41_CAL_FRAMES) return;
    if (memcmp(c->id, res->id, 8) != 0) {
        rs41_calib_reset(c);
        memcpy(c->id, res->id, 8);
    }
    memcpy(c->data + res->calIdx * 16, res->calData, 16);
    if (!c->have[res->calIdx]) {
        c->have[res->calIdx] = true;
        c->nHave++;
    }
}

// True when all subframes covering data[from, from+len) are present.
static bool calHas(const Rs41Calib *c, int from, int len)
{
    for (int i = from / 16; i <= (from + len - 1) / 16; i++)
        if (!c->have[i]) return false;
    return true;
}

static int calU16(const Rs41Calib *c, int ofs) { return get_le16(c->data + ofs); }

static float calF32(const Rs41Calib *c, int ofs)
{
    float f;
    memcpy(&f, c->data + ofs, 4); // little-endian hosts only
    return f;
}

bool rs41_calib_freq_khz(const Rs41Calib *c, uint32_t *khz)
{
    if (!calHas(c, 0x02, 2)) return false;
    *khz = 400000u + 40u * c->data[0x03] + (uint32_t)(c->data[0x02] & 0xC0) * 10u / 64u;
    return true;
}

bool rs41_calib_type(const Rs41Calib *c, char out[10])
{
    if (!calHas(c, 0x218, 8)) return false;
    int n = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t ch = c->data[0x218 + i];
        if (ch < 0x20 || ch > 0x7E) break;
        out[n++] = (char)ch;
    }
    out[n] = 0;
    return n > 0;
}

#ifdef HOST_TEST
bool rs41_calib_firmware(const Rs41Calib *c, uint16_t *fw)
{
    if (!calHas(c, 0x15, 2)) return false;
    *fw = (uint16_t)calU16(c, 0x15);
    return true;
}
#endif

static bool calTimer(const Rs41Calib *c, int ofs, int *sec)
{
    if (!calHas(c, ofs, 2)) return false;
    int v = calU16(c, ofs);
    *sec = v == 0xFFFF ? -1 : v;
    return true;
}

bool rs41_calib_kill_timer(const Rs41Calib *c, int *sec) { return calTimer(c, 0x27, sec); }
bool rs41_calib_burst_timer(const Rs41Calib *c, int *sec) { return calTimer(c, 0x316, sec); }
bool rs41_calib_countdown(const Rs41Calib *c, int *sec) { return calTimer(c, 0x320, sec); }

bool rs41_temperature(const Rs41Calib *c, const Rs41Result *res, float *degC)
{
    if (!res->ptuCrcOk || !calHas(c, 61, 40)) return false;
    float Rf1 = calF32(c, 61), Rf2 = calF32(c, 65);
    float p0 = calF32(c, 77), p1 = calF32(c, 81), p2 = calF32(c, 85);
    float c0 = calF32(c, 89), c1 = calF32(c, 93), c2 = calF32(c, 97);
    float f = (float)res->meas[0], f1 = (float)res->meas[1], f2 = (float)res->meas[2];
    if (f2 == f1 || Rf2 == Rf1) return false;
    float g = (f2 - f1) / (Rf2 - Rf1);
    float Rb = (f1 * Rf2 - f2 * Rf1) / (f2 - f1);
    float R = (f / g - Rb) * c0;
    float T = (p0 + p1 * R + p2 * R * R + c1) * (1.0f + c2);
    if (!(T > -120.0f && T < 80.0f)) return false;
    *degC = T;
    return true;
}

uint32_t rs41_gps_unix(uint16_t week, uint32_t towMs)
{
    if (week == 0) return 0;
    return GPS_EPOCH_UNIX + (uint32_t)week * 604800u + towMs / 1000u - GPS_UTC_LEAP_S;
}
