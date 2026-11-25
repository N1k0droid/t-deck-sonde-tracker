// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once

// Landing prediction from the wind profile measured by the sonde itself. Portable, tested on host.

#define PRED_BIN_M 250
#define PRED_BINS 160 // up to 40 km

struct WindProfile {
    float vN[PRED_BINS];
    float vE[PRED_BINS];
    bool have[PRED_BINS];
};

struct Prediction {
    bool ok;
    double lat, lon;
    float timeS;       // time to ground
    float driftM;      // horizontal distance still to travel
    int coveragePct;   // share of the remaining descent covered by measured wind
};

void pred_reset(WindProfile *w);
// Horizontal velocity of the sonde = wind at that altitude. dirDeg is the direction of travel.
void pred_add(WindProfile *w, double altM, float vH, float dirDeg);
// ISA air density, kg/m3.
double pred_air_density(double altM);
// descentRate: positive m/s, measured now at altM. Under a parachute it scales with 1/sqrt(density).
// curVN/curVE: current horizontal velocity, used where no wind was measured.
// Own prediction or SondeHub's? Own when the measured wind covers at least half of the descent left, or below
// 2500 m above the ground: higher up, with the low winds unknown (ascent not heard), SondeHub's was far better
// (21/11/2026: 6.4 km against 1.3 km off at 6.3 km), lower down the own one won and is fresher.
#define PRED_OWN_COVERAGE_PCT 50
#define PRED_OWN_BELOW_M 2500.0
bool pred_prefer_own(int coveragePct, double heightAboveGroundM);

Prediction pred_landing(const WindProfile *w, double lat, double lon, double altM, float descentRate, float curVN,
                        float curVE, double groundAltM);
