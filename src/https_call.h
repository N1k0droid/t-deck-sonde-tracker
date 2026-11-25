// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

// One HTTP(S) request for every module (SondeHub reads and uploads, prediction server): one TLS session at a
// time (TlsSession), the client that never closes descriptor 0 (TlsClient), net_mark() on every answer.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>

#define HTTPS_BUSY -3   // the other TLS session did not end within waitMs: nothing was sent
#define HTTPS_NO_URL -100 // the URL could not be used
#define HTTPS_STREAM -101 // the answer could not be written to 'body'

struct HttpsReq {
    const char *method = "GET";
    const char *url = nullptr;     // https://... (http://... only for the bench mock: no TLS session)
    const uint8_t *data = nullptr; // request body
    size_t len = 0;
    const char *ua = nullptr;      // User-Agent
    const char *ca = nullptr;      // root certificates (PEM); nullptr: not checked (public data only)
    const char *auth = nullptr;    // Authorization value
    const char *date = nullptr;    // Date value
    bool json = false;             // Content-Type: application/json
    const char *keep = nullptr;    // one answer header to read (into 'kept')
    String *kept = nullptr;
    uint32_t timeoutMs = 8000;
    uint32_t connectMs = 0;        // 0: the library's own
    uint32_t waitMs = portMAX_DELAY; // for the other TLS session (the Wi-Fi task never waits long)
    Stream *body = nullptr;        // status 200: the answer is written here
    String *reply = nullptr;       // any answer: its text, cut to replyMax
    size_t replyMax = 0;
};

// HTTP status, or a negative value: connection error (HTTPClient's), HTTPS_BUSY, HTTPS_NO_URL, HTTPS_STREAM.
int https_call(const HttpsReq &r);
