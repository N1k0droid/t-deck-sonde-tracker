// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stdint.h>

// Reed-Solomon RS(255,231) over GF(2^8), poly 0x11D, alpha=2, first root alpha^0 (Vaisala RS41).
// Codeword layout: cw[0..23] parity, cw[24..254] message; cw[i] is the coefficient of x^i.

#define RS_N 255
#define RS_K 231
#define RS_R 24

void rs_init();
#ifdef HOST_TEST
void rs_encode(uint8_t cw[RS_N]); // host tests build codewords with it
#endif
// Returns number of corrected symbols (errors + erasures), or -1 if uncorrectable.
int rs_decode(uint8_t cw[RS_N], const uint8_t *eraPos, int nEra);
