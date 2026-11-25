// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "https_call.h"
#include "tls_client.h"
#include "uploader.h"
#include <HTTPClient.h>
#include <WiFi.h>

int https_call(const HttpsReq &r)
{
    bool tls = !strncmp(r.url, "https://", 8);
    TlsSession one(tls, r.waitMs); // before the clients: they are gone before the session is let go
    if (one.busy) return HTTPS_BUSY;
    TlsClient secure;
    WiFiClient plain;
    if (tls) {
        if (r.ca) secure.setCACert(r.ca);
        else secure.setInsecure(); // public data; no certificate store on the device
    }
    HTTPClient http;
    http.setTimeout(r.timeoutMs);
    if (r.connectMs) http.setConnectTimeout(r.connectMs);
    if (!(tls ? http.begin(secure, r.url) : http.begin(plain, r.url))) return HTTPS_NO_URL;
    if (r.keep) {
        const char *keys[] = {r.keep};
        http.collectHeaders(keys, 1);
    }
    if (r.ua) http.setUserAgent(r.ua);
    if (r.json) http.addHeader("Content-Type", "application/json");
    if (r.date) http.addHeader("Date", r.date);
    if (r.auth) http.addHeader("Authorization", r.auth);
    int code = http.sendRequest(r.method, (uint8_t *)r.data, r.len);
    net_mark(code > 0);
    if (code == 200 && r.body && http.writeToStream(r.body) < 0) code = HTTPS_STREAM;
    else if (code > 0 && r.reply) {
        *r.reply = http.getString();
        if (r.replyMax && r.reply->length() > r.replyMax) *r.reply = r.reply->substring(0, r.replyMax);
    }
    if (r.keep && r.kept) *r.kept = http.header(r.keep);
    http.end();
    return code;
}
