// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stddef.h>
#include <stdint.h>

// u-blox UBX messages to help the T-Deck GPS (u-blox M10) get its first fix sooner: approximate time
// and position (MGA-INI). Portable, tested on the PC.

#define UBX_MGA_INI_TIME_LEN (8 + 24)
#define UBX_MGA_INI_POS_LEN (8 + 20)

// UTC time, accuracy in seconds. Returns the message length.
size_t ubx_mga_ini_time_utc(uint8_t *out, uint32_t unixTime, uint16_t accSec);
// Position (degrees, height above the ellipsoid in metres), accuracy in metres.
size_t ubx_mga_ini_pos_llh(uint8_t *out, double lat, double lon, double heightM, uint32_t accM);
// Fletcher checksum over class, id, length and payload (bytes 2 .. len-3 of a message).
void ubx_checksum(const uint8_t *msg, size_t len, uint8_t *ckA, uint8_t *ckB);

// Navigation database: UBX-MGA-DBD (class 0x13, id 0x80). An empty one asks the receiver to dump its
// database (ephemeris, almanac...) as a series of MGA-DBD messages; sent back unchanged after a power-off
// they let it skip most of the cold start.
#define UBX_MGA_DBD_POLL_LEN 8
size_t ubx_mga_dbd_poll(uint8_t *out);

// Picks UBX frames out of the receiver's output (NMEA and UBX mixed on the same line).
class UbxReader {
public:
    // Feeds one byte; true when a complete frame with a valid checksum ends here: see frame()/len().
    bool feed(uint8_t c);
    const uint8_t *frame() const { return buf; }
    size_t len() const { return flen; }
    uint8_t cls() const { return buf[2]; }
    uint8_t id() const { return buf[3]; }

private:
    uint8_t buf[1100]; // largest MGA-DBD entries are well below 1 KB
    size_t n = 0, need = 0, flen = 0;
};
