// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <Arduino.h>
#include "sondehub.h"

// Where the uploads go. Only a test build changes them, from the command line (never in a file), both
// towards the PC running tools/bench/mock_uploads.py:
//   PLATFORMIO_BUILD_FLAGS='-DSH_API_BASE=\"http://<PC>:8080\" -DAPRS_HOST=\"<PC>\"' pio run
#ifndef SH_API_BASE
#define SH_API_BASE "https://api.v2.sondehub.org"
#endif
#define SH_API_REAL "https://api.v2.sondehub.org"
// Test build towards the real SondeHub with every frame marked "dev": checked and answered, never stored. Only
// from the command line, for one real flight: PLATFORMIO_BUILD_FLAGS='-DSH_DEV=1' pio run
#ifndef SH_DEV
#define SH_DEV 0
#endif
#ifndef APRS_HOST
#define APRS_HOST "radiosondy.info"
#endif
#define APRS_HOST_REAL "radiosondy.info"
#define APRS_PORT 14580
// aprs.fi: the main APRS-IS network through the European servers. A test build pointing APRS_HOST at the PC
// sends there too (uploader.cpp, fiHost).
#define APRSFI_HOST_REAL "euro.aprs2.net"
#define APRSFI_MIN_MS 60000 // a mobile station at most once a minute on the main network

// Wi-Fi + SondeHub upload, in its own task so that TLS never stalls frame reception.
// Wi-Fi stays off unless upload, the debug server, the apps or the internet services need it.

struct UploaderCfg {
    bool enabled;   // SondeHub upload
    bool wifiDebug; // keep Wi-Fi on for the debug server, without uploading
    bool aprs;      // radiosondy.info upload (APRS-IS)
    bool aprsFi;    // station position on aprs.fi (main APRS-IS network)
    bool predSrv;   // prediction server link (optional build): keeps Wi-Fi on
    bool wifiSleep; // Wi-Fi modem sleep (power save)
    char aprsFiText[61]; // comment of that position, "" = none (chosen by main for the situation)
    int aprsPass;   // APRS-IS passcode of the callsign
    bool shPos, rsPos, fiPos; // station position shared with SondeHub, radiosondy.info, aprs.fi
    bool mobile;    // chase car or fixed receiver
    char mobileSym; // APRS icon of the mobile station (APRS_MOBILE_SYM)
    uint32_t mobileMs, fixedMs; // how often the station position is sent
    uint16_t minMoveM;  // mobile: no new position until moved this far from the last one sent (0 = always)
    char ssid[33];
    char pass[64];
    char callsign[16];
    char swVersion[8];
};

struct UploaderStatus {
    bool wifiOn;
    bool wifiConnected;
    char ip[16];
    uint32_t sent;    // frames accepted by the server
    uint32_t failed;  // frames in failed requests
    uint32_t dropped; // queue full
    int lastHttp;     // last HTTP status, <= 0 = none / connection error
    uint32_t stationSent; // station position updates accepted
    int stationHttp;      // last HTTP status of those
    bool aprsConnected;   // logged in to the APRS-IS server
    bool aprsVerified;    // the server accepted the passcode
    uint32_t aprsSent;    // sonde objects sent
    uint32_t aprsBeacons; // station beacons sent
    bool fiConnected;     // logged in to the aprs.fi server
    bool fiVerified;
    uint32_t fiBeacons;   // positions sent to aprs.fi
    char shReply[160];    // SondeHub's answer when it was not a plain 200: its first error message, "" = none
    int shReplyCode;      // HTTP status of that answer
    uint32_t shReplySeq;  // grows when shReply changes (main logs it once)
    int netOkS;           // seconds since a server last answered over Wi-Fi, -1 = not since connecting
    uint32_t netFails;    // requests without an answer since then
    uint32_t netResets;   // Wi-Fi reconnections forced because nothing answered
    uint32_t wifiDrops;   // times the Wi-Fi link was lost (main logs each one)
    int wifiReason;       // the chip's reason for the last loss (802.11 / ESP-IDF code)
    int wifiRssi;         // signal of the network at the last loss, dBm (0 = unknown)
    uint32_t wifiJoins;   // join attempts since the link was lost
};

// Wi-Fi networks around, asked from the settings (the scan runs in the Wi-Fi task, which owns the radio).
#define WIFI_SCAN_MAX 16
struct WifiNet {
    char ssid[33];
    int8_t rssi;
    bool secure;
};

void uploader_begin();
void uploader_scan_start();
// Networks found, strongest first, one row per name: -1 still scanning, -2 failed or never asked.
int uploader_scan_result(WifiNet *out, int max);
void uploader_configure(const UploaderCfg &cfg);
// Own GPS position, for the station updates (valid false: none is sent).
void uploader_position(bool valid, double lat, double lon, double alt, float speedKmh = 0, float courseDeg = 0);
// Last batch sent to SondeHub and its full answer (debug page /upload_last). False when nothing was sent yet.
bool uploader_last_upload(String &body, String &reply, int &code);
// "real"; "mock" in a test build where both SondeHub and APRS point elsewhere; "dev" with SH_DEV.
const char *uploader_target();
bool uploader_push(const ShFrame &f);
// Latest frame of a sonde for radiosondy.info: one object per sonde at most every 30 s.
void uploader_aprs_push(const ShFrame &f);
// Every request over Wi-Fi reports here whether a server answered (any HTTP code, an APRS login): "online"
// means a recent answer, and the Wi-Fi task reconnects when nothing answers for minutes while it says connected.
void net_mark(bool answered);
// Lowest free stack of the Wi-Fi task so far, bytes (for /status).
uint32_t uploader_stack_free();
void uploader_status(UploaderStatus *st);
