// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "scan.h"

int scan_median(const int8_t *rssi, int n)
{
    if (n <= 0) return -128;
    int hist[256] = {0};
    for (int i = 0; i < n; i++) hist[rssi[i] + 128]++;
    int acc = 0;
    for (int v = 0; v < 256; v++) {
        acc += hist[v];
        if (acc * 2 >= n) return v - 128;
    }
    return 127;
}

int scan_threshold(int median, int minAboveDb) { return (median > SCAN_NOISE_MIN ? median : SCAN_NOISE_MIN) + minAboveDb; }

#ifdef HOST_TEST
int scan_pick(const int8_t *rssi, int n, int minAboveDb, int minSepBins, int *outIdx, int maxOut)
{
    return scan_pick_thr(rssi, n, scan_median(rssi, n) + minAboveDb, minSepBins, outIdx, maxOut);
}
#endif

int scan_pick_thr(const int8_t *rssi, int n, int thr, int minSepBins, int *outIdx, int maxOut)
{
    int count = 0;
    for (int i = 0; i < n; i++) {
        int v = rssi[i];
        if (v < thr) continue;
        // Local maximum; on a plateau the first bin wins.
        if (i > 0 && rssi[i - 1] >= v) continue;
        if (i < n - 1 && rssi[i + 1] > v) continue;
        // Insert sorted by strength, enforcing the minimum separation.
        bool drop = false;
        for (int k = 0; k < count && !drop; k++) {
            int d = outIdx[k] - i;
            if (d < 0) d = -d;
            if (d < minSepBins) {
                if (rssi[outIdx[k]] >= v) drop = true;
                else {
                    for (int m = k; m < count - 1; m++) outIdx[m] = outIdx[m + 1];
                    count--;
                    k--;
                }
            }
        }
        if (drop) continue;
        int pos = count;
        while (pos > 0 && rssi[outIdx[pos - 1]] < v) pos--;
        if (pos >= maxOut) continue;
        if (count < maxOut) count++;
        for (int m = count - 1; m > pos; m--) outIdx[m] = outIdx[m - 1];
        outIdx[pos] = i;
    }
    return count;
}

void scan_skip_reset(ScanSkip *s) { s->n = 0; }

static bool skipActive(const ScanSkip *s, int i, uint32_t nowMs) { return (int32_t)(s->untilMs[i] - nowMs) > 0; }

void scan_skip_add(ScanSkip *s, uint32_t khz, uint32_t nowMs, uint32_t forMs)
{
    int slot = -1;
    for (int i = 0; i < s->n; i++)
        if (s->khz[i] == khz || !skipActive(s, i, nowMs)) {
            slot = i;
            break;
        }
    if (slot < 0 && s->n < SCAN_SKIP_MAX) slot = s->n++;
    if (slot < 0) { // full: the one that expires first
        slot = 0;
        for (int i = 1; i < s->n; i++)
            if ((int32_t)(s->untilMs[i] - s->untilMs[slot]) < 0) slot = i;
    }
    s->khz[slot] = khz;
    s->untilMs[slot] = nowMs + forMs;
}

bool scan_skip_has(const ScanSkip *s, uint32_t khz, uint32_t nowMs, uint32_t tolKhz)
{
    for (int i = 0; i < s->n; i++) {
        uint32_t d = khz > s->khz[i] ? khz - s->khz[i] : s->khz[i] - khz;
        if (d <= tolKhz && skipActive(s, i, nowMs)) return true;
    }
    return false;
}

int scan_skip_count(const ScanSkip *s, uint32_t nowMs)
{
    int c = 0;
    for (int i = 0; i < s->n; i++) c += skipActive(s, i, nowMs);
    return c;
}
