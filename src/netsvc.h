// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <Arduino.h>
#include "shread.h"

// Internet services through the Wi-Fi (phone hotspot), in their own task so that HTTPS never blocks
// the radio: SondeHub is only READ. Nothing is ever sent to SondeHub from here.

#define NET_MAX_SONDES 16  // "near now" list (last 3 hours)
#define NET_MAX_LANDED 48  // landed/seen list for a longer period
#define NET_MAX_SITES 8
#define NET_MAX_LAUNCH 28  // 7 days of launches

struct NetRequest {
    bool enabled;
    bool haveCentre;
    double lat, lon;      // centre of the area queries (rounded to 0.1 degree before sending)
    char follow[12];      // serial whose landing prediction is wanted, "" = none
    uint32_t landedLast;  // seconds of history for the landed list, 0 = not wanted
    char summary[12];     // serial to look up (search / details), "" = none
    bool launches;        // Launches tab open: its Tawhiri predictions may be asked
    uint32_t launchGen;   // grows each time the Launches tab is opened
    uint32_t launchHorizonS; // how far ahead the next launches are looked for
    bool sondesFast;      // nearby sondes every 60 s instead of every 5 min (Near list on screen, or the radio
                          // does not hear the sonde being followed)
    bool followStale;     // the followed sonde does not move on SondeHub (landed, or nobody hears it): no
                          // more predictions for it
};

enum { NET_Q_SONDES, NET_Q_PRED, NET_Q_LANDED, NET_Q_SUMMARY, NET_Q_SITES, NET_Q_TAWHIRI, NET_Q_N };

struct NetLaunch {
    int site;             // index in sites[]
    uint32_t time;        // launch, Unix UTC
    bool tried;
    ShFlightPred pred;
};

struct NetResult {
    uint32_t version;     // changes when anything below changes
    int nSondes;
    ShSonde sondes[NET_MAX_SONDES];
    uint32_t sondesMs;    // millis() of the last good answer, 0 = never
    ShPrediction pred;
    uint32_t predMs;
    int nLanded;
    ShSonde landed[NET_MAX_LANDED];
    uint32_t landedLast;  // period these entries cover (seconds)
    uint32_t landedMs;
    ShSummary summary;    // summary.ok false when not found
    char summaryFor[12];  // serial the summary answer belongs to ("" = none yet)
    uint32_t summaryMs;
    int nSites;
    ShSite sites[NET_MAX_SITES];
    int nLaunch;
    NetLaunch launch[NET_MAX_LAUNCH];
    uint32_t launchMs;    // last time a launch prediction arrived
    int lastHttp;         // last HTTP status (negative: connection error)
    uint32_t requests, failures;
    uint32_t perKind[NET_Q_N]; // requests by kind (NET_Q_*), for /status
};

void netsvc_begin();
// Lowest free stack of the internet task so far, bytes (for /status).
uint32_t netsvc_stack_free();
void netsvc_request(const NetRequest &r);
// Copies the results when their version differs from 'seen'; true when copied.
bool netsvc_result(NetResult *out, uint32_t seen);
