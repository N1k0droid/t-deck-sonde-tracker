// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "rdz_link.h"
#include <ESPmDNS.h>
#include <WiFi.h>

#define RDZ_PORT 14570

static WiFiServer server(RDZ_PORT);
static WiFiClient client;
static bool running = false;
static uint32_t lines = 0;

void rdz_link_poll(bool want)
{
    if (want && !running) {
        server.begin();
        if (MDNS.begin("sondetracker")) MDNS.addService("jsonrdz", "tcp", RDZ_PORT);
        running = true;
    } else if (!want && running) {
        if (client) client.stop();
        server.end();
        MDNS.end();
        running = false;
    }
    if (!running) return;
    WiFiClient c = server.available();
    if (c) {
        if (client) client.stop(); // one app at a time, the newest wins (as rdz does)
        client = c;
    }
    // The app may send its own GPS position; it is not used: drain it.
    while (client && client.connected() && client.available()) client.read();
}

bool rdz_link_client()
{
    return running && client && client.connected();
}

void rdz_link_send(const char *line, size_t len)
{
    if (!rdz_link_client()) return;
    if (client.write((const uint8_t *)line, len) != len) {
        client.stop();
        return;
    }
    lines++;
}

uint32_t rdz_link_lines()
{
    return lines;
}
