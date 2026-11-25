// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "predict.h"
#include "geo.h"
#include <math.h>
#include <string.h>

static const double DEG = GEO_DEG;

void pred_reset(WindProfile *w)
{
    memset(w, 0, sizeof(*w));
}

static int binOf(double altM)
{
    int b = (int)floor(altM / PRED_BIN_M);
    return b < 0 ? 0 : b >= PRED_BINS ? PRED_BINS - 1 : b;
}

void pred_add(WindProfile *w, double altM, float vH, float dirDeg)
{
    int b = binOf(altM);
    float vN = vH * (float)cos(dirDeg * DEG), vE = vH * (float)sin(dirDeg * DEG);
    if (w->have[b]) {
        // Light smoothing inside a bin; the latest pass (descent) dominates.
        w->vN[b] = 0.7f * w->vN[b] + 0.3f * vN;
        w->vE[b] = 0.7f * w->vE[b] + 0.3f * vE;
    } else {
        w->vN[b] = vN;
        w->vE[b] = vE;
        w->have[b] = true;
    }
}

double pred_air_density(double altM)
{
    if (altM < 0) altM = 0;
    if (altM < 11000.0) return 1.225 * pow(1.0 - 2.25577e-5 * altM, 4.25588);
    return 0.36391 * exp(-(altM - 11000.0) / 6341.62);
}

// Wind for a bin: measured value, else the nearest measured bin, else the current velocity.
static bool windAt(const WindProfile *w, int b, float curVN, float curVE, float *vN, float *vE)
{
    if (w->have[b]) {
        *vN = w->vN[b];
        *vE = w->vE[b];
        return true;
    }
    for (int d = 1; d < PRED_BINS; d++) {
        int lo = b - d, hi = b + d;
        if (hi < PRED_BINS && w->have[hi]) {
            *vN = w->vN[hi];
            *vE = w->vE[hi];
            return false;
        }
        if (lo >= 0 && w->have[lo]) {
            *vN = w->vN[lo];
            *vE = w->vE[lo];
            return false;
        }
    }
    *vN = curVN;
    *vE = curVE;
    return false;
}

Prediction pred_landing(const WindProfile *w, double lat, double lon, double altM, float descentRate, float curVN,
                        float curVE, double groundAltM)
{
    Prediction p = {};
    p.lat = lat;
    p.lon = lon;
    if (descentRate < 0.5f || altM <= groundAltM) return p;
    double rhoNow = pred_air_density(altM);
    double h = altM, dN = 0, dE = 0, t = 0, covered = 0;
    while (h > groundAltM) {
        double next = (ceil(h / PRED_BIN_M) - 1.0) * PRED_BIN_M;
        if (next < groundAltM) next = groundAltM;
        double mid = 0.5 * (h + next);
        double v = descentRate * sqrt(rhoNow / pred_air_density(mid));
        double dt = (h - next) / v;
        float vN, vE;
        if (windAt(w, binOf(mid), curVN, curVE, &vN, &vE)) covered += h - next;
        dN += vN * dt;
        dE += vE * dt;
        t += dt;
        h = next;
    }
    p.ok = true;
    p.timeS = (float)t;
    p.driftM = (float)sqrt(dN * dN + dE * dE);
    p.coveragePct = (int)(100.0 * covered / (altM - groundAltM) + 0.5);
    geo_move_m(lat, lon, dE, dN, &p.lat, &p.lon);
    return p;
}

bool pred_prefer_own(int coveragePct, double heightAboveGroundM)
{
    return coveragePct >= PRED_OWN_COVERAGE_PCT || heightAboveGroundM < PRED_OWN_BELOW_M;
}
