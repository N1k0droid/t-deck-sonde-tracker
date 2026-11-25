// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <Arduino.h>

// Bluetooth LE link for the hunting apps (MySondyGO, Hunter Tracker, TrovaLaSonda): the T-Deck shows up
// as "MySondyGO-XXXX" with the MySondy service and, for apps that look for it, the Nordic UART service.
// Lines are sent from ble_poll() in MTU-sized chunks; bytes written by the app are queued for ble_read().

void ble_start();
void ble_stop();
bool ble_running();
// Queues one protocol line (without line end) for all subscribed apps; replaces a line not yet sent.
void ble_send_line(const char *line);
// Sends pending chunks; call often from the main loop.
void ble_poll();
// Next byte written by an app, -1 when none.
int ble_read();

struct BleStatus {
    bool running;
    bool connected;
    bool subscribed;
    uint16_t mtu;
    uint32_t linesSent;
    uint32_t bytesIn;
    char name[24];
};
void ble_status(BleStatus *s);
