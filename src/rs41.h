// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stdint.h>
#include <stddef.h>

#define RS41_STD_LEN 320
#define RS41_EXT_LEN 518
#define RS41_HDR_LEN 8

// RS41 sync word as seen by an MSB-first GFSK receiver (scrambled header, bit order on air).
extern const uint8_t RS41_SYNC_MSB[8];

struct Rs41Result {
    uint8_t sondeType;  // SondeType: RS41, or DFM frames converted to this record
    char model[10];     // "RS41", "DFM-09"...
    bool eccOk;
    int ecc1;           // corrected symbols codeword 1, -1 = fail
    int ecc2;           // corrected symbols codeword 2, -1 = fail
    int frameLen;       // 320 or 518
    bool frameCrcOk;
    uint16_t frameNb;
    char id[16];        // serial: 8 characters for RS41, up to 15 for other families
    bool gpsCrcOk;
    bool gpsValid;
    double lat, lon, alt;
    double vH, vDir, vV;
    uint8_t numSV;      // < 4 means no fix: gpsValid is false, lat/lon are not usable
    uint8_t sAcc, pDOP;
    int nEra;
    float battV;
    uint8_t calIdx;     // calibration subframe index, 0xFF = none
    uint8_t calData[16];
    bool ptuCrcOk;
    uint32_t meas[12];  // raw PTU counters (0..2 temperature: main, ref1, ref2)
    bool timeCrcOk;
    uint16_t gpsWeek;   // RS41 only
    uint32_t gpsTowMs;  // RS41 only (its milliseconds go to SondeHub)
    uint32_t unixTime;  // sonde GPS time as Unix UTC, every type; 0 = unknown
    // XDATA (extended frames, 0x7E): instrument data as an ASCII hex string, first block.
    bool xdata;
    char xdataStr[48];
    // OIF411 ozone sensor (XDATA instrument 0x05), measurement message.
    bool o3Valid;
    uint8_t o3Instr;
    float o3TpumpC, o3IcellUa, o3VbatV, o3VextV, o3Mpa;
    uint16_t o3IpumpMa;
};

// Nominal pump time for 100 ml (s): the real one is on the sensor's calibration sheet.
#define RS41_O3_T100 28.5f

#define RS41_CAL_FRAMES 51

// Calibration/config data, sent 16 bytes per frame, complete after 51 frames.
struct Rs41Calib {
    char id[9];
    uint8_t data[RS41_CAL_FRAMES * 16];
    bool have[RS41_CAL_FRAMES];
    int nHave;
};

void rs41_calib_reset(Rs41Calib *c);
// Stores the subframe carried by res; restarts collection when the sonde ID changes.
void rs41_calib_update(Rs41Calib *c, const Rs41Result *res);
// Each getter returns false until the subframes it needs have been received.
bool rs41_calib_freq_khz(const Rs41Calib *c, uint32_t *khz);
bool rs41_calib_type(const Rs41Calib *c, char out[10]);
#ifdef HOST_TEST
bool rs41_calib_firmware(const Rs41Calib *c, uint16_t *fw);
#endif
// Seconds; -1 = timer not set.
bool rs41_calib_kill_timer(const Rs41Calib *c, int *sec);
bool rs41_calib_burst_timer(const Rs41Calib *c, int *sec);
bool rs41_calib_countdown(const Rs41Calib *c, int *sec);
bool rs41_temperature(const Rs41Calib *c, const Rs41Result *res, float *degC);
// GPS week/TOW -> Unix time (UTC, GPS_UTC_LEAP_S). Returns 0 when week is 0.
uint32_t rs41_gps_unix(uint16_t week, uint32_t towMs);

// Frame length (320 or 518) from the raw received byte 48 after the sync (frame type 0x0F / 0xF0).
int rs41_frame_len_from_raw(uint8_t raw48, bool invert);

// Converts raw bytes received MSB-first (after the sync word) into descrambled frame bytes.
// frame[0..7] receives the known header; raw[i] maps to frame[8+i].
void rs41_raw_to_frame(const uint8_t *raw, int nRaw, bool invert, uint8_t *frame);

#ifdef HOST_TEST
// Descrambled frame -> raw MSB-first bytes (host tests synthesize input with it).
void rs41_frame_to_raw(const uint8_t *frame, int len, uint8_t *raw);
#endif

// frame: RS41_EXT_LEN buffer, bytes [0, rxLen) valid, rest unknown (erased, or set from the known
// standard-frame contents when useKnownStd). Applies erasure-aware RS correction and parses the frame.
bool rs41_decode(uint8_t *frame, int rxLen, Rs41Result *res, bool useKnownStd);

