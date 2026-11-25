// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stdint.h>

// Picks candidate channels from an RSSI sweep. Portable, tested on host.
// rssi: dBm per bin. A candidate is a local maximum at least minAboveDb over the median (noise floor);
// candidates closer than minSepBins to a stronger one are dropped. Result is sorted strongest first.
// Returns the number of candidates written to outIdx (at most maxOut).
#ifdef HOST_TEST
int scan_pick(const int8_t *rssi, int n, int minAboveDb, int minSepBins, int *outIdx, int maxOut);
#endif
// Same with the threshold given (dBm): what the scan uses, with scan_threshold.
int scan_pick_thr(const int8_t *rssi, int n, int thr, int minSepBins, int *outIdx, int maxOut);
// Threshold for a noise floor: the T-Deck's real floor (-125..-130 dBm) is mostly read as -128, so the median
// would put the threshold among the noise peaks (-118): the floor is taken as at least -124 dBm.
#define SCAN_NOISE_MIN -124
int scan_threshold(int median, int minAboveDb);
// Median of the sweep (noise floor estimate).
int scan_median(const int8_t *rssi, int n);

// Frequencies the continuous scan leaves alone for a while: tried without a decode, or a sonde the user did not
// want. Times in ms (millis), compared wrap-safe; a full list replaces the entry that expires first.
#define SCAN_SKIP_MAX 16
struct ScanSkip {
    uint32_t khz[SCAN_SKIP_MAX];
    uint32_t untilMs[SCAN_SKIP_MAX];
    int n;
};
void scan_skip_reset(ScanSkip *s);
void scan_skip_add(ScanSkip *s, uint32_t khz, uint32_t nowMs, uint32_t forMs);
// True when khz is within tolKhz of a frequency still skipped at nowMs.
bool scan_skip_has(const ScanSkip *s, uint32_t khz, uint32_t nowMs, uint32_t tolKhz);
// Entries still active at nowMs.
int scan_skip_count(const ScanSkip *s, uint32_t nowMs);
