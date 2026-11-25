// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <Arduino.h>

// Trackball and touch screen as one stream of events (the keyboard keeps its own path in main.cpp).
// Trackball: four Hall-sensor lines counted by interrupts, click on the BOOT pin.
// Touch: GT911 on the shared I2C bus, read directly (registers), polled from the loop.

enum InputType : uint8_t { IN_NONE, IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_SELECT, IN_TAP, IN_LONG, IN_HOLD };

struct InputEvent {
    InputType type;
    int16_t x, y; // screen coordinates for IN_TAP / IN_LONG
};

// Wire must already be started. Returns false when no touch controller answers (trackball still works).
bool input_begin();
bool input_touch_ok();
// Before input_begin: does the touch controller answer on the I2C bus (Wire already started)?
bool input_touch_answers();
void input_note_power_cycle(); // for /status
void input_set_touch(bool on);
// Trackball edges for one step: 2 fast (default), 4 medium, 6 slow.
void input_set_trackball_step(uint8_t edges);
// Next event, false when none. Call often from the loop.
bool input_poll(InputEvent &e);
// Debug: queue an event as if it came from the hardware.
void input_inject(const InputEvent &e);

struct InputStats {
    uint32_t steps[4]; // up, down, left, right
    uint32_t clicks, taps, longs, holds; // holds: trackball held 3 s
    int16_t lastX, lastY, lastRawX, lastRawY;
    uint8_t touchAddr;
    uint32_t touchProbes; // attempts to find the touch controller (first 30 s only)
    uint32_t probeMs;     // duration of the last attempt
    bool powerCycled;     // stuck after a warm restart: peripherals switched off and on at start-up
    uint8_t tbStep;       // trackball edges per step
};
// Debug: find the touch controller again now; true when found.
bool input_probe_now();
void input_stats(InputStats *s);
