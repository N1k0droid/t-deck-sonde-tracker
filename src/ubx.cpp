// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "ubx.h"
#include "bytes.h"
#include "timeutil.h"
#include <math.h>
#include <string.h>


void ubx_checksum(const uint8_t *msg, size_t len, uint8_t *ckA, uint8_t *ckB)
{
    uint8_t a = 0, b = 0;
    for (size_t i = 2; i < len - 2; i++) {
        a += msg[i];
        b += a;
    }
    *ckA = a;
    *ckB = b;
}

// Header (sync, class MGA 0x13, id INI 0x40, length), payload already in place, checksum.
static size_t finish(uint8_t *out, uint16_t payloadLen)
{
    out[0] = 0xB5;
    out[1] = 0x62;
    out[2] = 0x13;
    out[3] = 0x40;
    put_le16(out + 4, payloadLen);
    size_t len = 8 + payloadLen;
    ubx_checksum(out, len, &out[len - 2], &out[len - 1]);
    return len;
}

size_t ubx_mga_ini_time_utc(uint8_t *out, uint32_t unixTime, uint16_t accSec)
{
    uint8_t *p = out + 6;
    memset(p, 0, 24);
    int y, mo, d;
    tu_civil_from_days((int32_t)(unixTime / 86400), &y, &mo, &d);
    uint32_t s = unixTime % 86400;
    p[0] = 0x10;           // type: TIME_UTC
    p[1] = 0x00;           // version
    p[2] = 0x00;           // ref: none, time valid on receipt
    p[3] = (uint8_t)-128;  // leap seconds unknown
    put_le16(p + 4, (uint16_t)y);
    p[6] = (uint8_t)mo;
    p[7] = (uint8_t)d;
    p[8] = (uint8_t)(s / 3600);
    p[9] = (uint8_t)(s / 60 % 60);
    p[10] = (uint8_t)(s % 60);
    put_le32(p + 12, 0);      // ns
    put_le16(p + 16, accSec); // tAccS
    put_le32(p + 20, 0);      // tAccNs
    return finish(out, 24);
}

size_t ubx_mga_ini_pos_llh(uint8_t *out, double lat, double lon, double heightM, uint32_t accM)
{
    uint8_t *p = out + 6;
    memset(p, 0, 20);
    p[0] = 0x01; // type: POS_LLH
    p[1] = 0x00; // version
    put_le32(p + 4, (uint32_t)(int32_t)lround(lat * 1e7));
    put_le32(p + 8, (uint32_t)(int32_t)lround(lon * 1e7));
    put_le32(p + 12, (uint32_t)(int32_t)lround(heightM * 100.0));
    put_le32(p + 16, accM > 40000000u ? 4000000000u : accM * 100u);
    return finish(out, 20);
}

size_t ubx_mga_dbd_poll(uint8_t *out)
{
    out[0] = 0xB5;
    out[1] = 0x62;
    out[2] = 0x13;
    out[3] = 0x80;
    out[4] = 0;
    out[5] = 0;
    ubx_checksum(out, UBX_MGA_DBD_POLL_LEN, &out[6], &out[7]);
    return UBX_MGA_DBD_POLL_LEN;
}

bool UbxReader::feed(uint8_t c)
{
    if (n == 0) {
        if (c == 0xB5) buf[n++] = c;
        return false;
    }
    if (n == 1) {
        if (c == 0x62) buf[n++] = c;
        else n = c == 0xB5 ? 1 : 0;
        return false;
    }
    buf[n++] = c;
    if (n == 6) {
        size_t payload = buf[4] | (buf[5] << 8);
        need = 6 + payload + 2;
        if (need > sizeof(buf)) { // too long for us: resync
            n = 0;
            return false;
        }
    }
    if (n < 6 || n < need) return false;
    uint8_t a, b;
    ubx_checksum(buf, n, &a, &b);
    bool ok = a == buf[n - 2] && b == buf[n - 1];
    flen = ok ? n : 0;
    n = 0; // the frame stays in buf until the next sync byte arrives: copy it right away
    return ok;
}
