// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <Arduino.h>

// rdz_ttgo_sonde compatible JSON server over Wi-Fi, for apps that support "TTGO rdz" receivers:
// TCP port 14570, advertised over mDNS as _jsonrdz._tcp (host name "sondetracker").
// /live.json is served by the HTTP server in main.cpp.

// Starts or stops the server following 'want' (true only while Wi-Fi is connected); accepts clients.
void rdz_link_poll(bool want);
bool rdz_link_client();
// Sends one JSON line (with '\n') to the connected client, if any.
void rdz_link_send(const char *line, size_t len);
uint32_t rdz_link_lines();
