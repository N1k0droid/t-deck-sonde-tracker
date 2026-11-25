// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stddef.h>

// Saved Wi-Fi networks in a text file on the SD card (/wifi.txt). Portable, tested on host.
//
//   # comment
//   HomeNet,my password          name, comma, password (split at the first comma)
//   Phone hotspot,               an open network: nothing after the comma (or no comma)
//
// Spaces around the name and the password are dropped.

#define WIFI_SAVED_MAX 24
#define WIFI_FILE_MAX 4096

struct SavedNet {
    char ssid[33];
    char pass[64];
};

// Parses the whole file; returns the number of networks (lines that cannot be read are skipped).
int wifilist_parse(const char *text, SavedNet *out, int max);
// True when the name and password can be written in the file and read back the same.
bool wifilist_ok(const char *ssid, const char *pass);
// Writes ssid,pass in text (capacity cap): the line of the same name is replaced, otherwise one is added.
// Returns 0 replaced, 1 added, -1 refused (not wifilist_ok, list or file full).
int wifilist_put(char *text, size_t cap, const char *ssid, const char *pass);
// Removes the line of network number index (as counted by wifilist_parse); false if not found.
bool wifilist_remove(char *text, int index);
// Index of the network with this name, -1 if none.
int wifilist_find(const SavedNet *n, int count, const char *ssid);
// The instructions written when the file does not exist yet.
extern const char *const WIFI_FILE_TEMPLATE;
