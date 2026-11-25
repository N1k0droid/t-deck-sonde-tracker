// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Reed-Solomon decoder with erasures for RS41 frames.
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "rs_ecc.h"
#include <string.h>

static uint8_t gfExp[512];
static uint8_t gfLog[256];
static uint8_t gen[RS_R + 1];
static bool initialized = false;

static inline uint8_t gfMul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return gfExp[gfLog[a] + gfLog[b]];
}

static inline uint8_t gfDiv(uint8_t a, uint8_t b)
{
    if (a == 0) return 0;
    return gfExp[(gfLog[a] + 255 - gfLog[b]) % 255];
}

static inline uint8_t gfPowAlpha(int e)
{
    e %= 255;
    if (e < 0) e += 255;
    return gfExp[e];
}

void rs_init()
{
    if (initialized) return;
    int x = 1;
    for (int i = 0; i < 255; i++) {
        gfExp[i] = (uint8_t)x;
        gfLog[x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11D;
    }
    for (int i = 255; i < 512; i++) gfExp[i] = gfExp[i - 255];
    gfLog[0] = 0;

    // Generator g(x) = prod_{i=0}^{23} (x - alpha^i)
    memset(gen, 0, sizeof(gen));
    gen[0] = 1;
    for (int i = 0; i < RS_R; i++) {
        uint8_t root = gfExp[i];
        for (int j = i + 1; j > 0; j--) {
            gen[j] = gen[j - 1] ^ gfMul(gen[j], root);
        }
        gen[0] = gfMul(gen[0], root);
    }
    initialized = true;
}

#ifdef HOST_TEST
void rs_encode(uint8_t cw[RS_N])
{
    // Systematic: parity = (sum msg_i x^(24+i)) mod g(x)
    uint8_t rem[RS_R];
    memset(rem, 0, sizeof(rem));
    for (int i = RS_N - 1; i >= RS_R; i--) {
        uint8_t fb = cw[i] ^ rem[RS_R - 1];
        for (int j = RS_R - 1; j > 0; j--) {
            rem[j] = rem[j - 1] ^ gfMul(fb, gen[j]);
        }
        rem[0] = gfMul(fb, gen[0]);
    }
    for (int i = 0; i < RS_R; i++) cw[i] = rem[i];
}
#endif

static uint8_t polyEvalAt(const uint8_t *p, int deg, uint8_t x)
{
    uint8_t y = 0;
    for (int i = deg; i >= 0; i--) y = gfMul(y, x) ^ p[i];
    return y;
}

int rs_decode(uint8_t cw[RS_N], const uint8_t *eraPos, int nEra)
{
    if (!initialized) rs_init();
    if (nEra > RS_R) return -1;

    // Syndromes S_j = c(alpha^j), j = 0..23
    uint8_t S[RS_R];
    bool anyErr = false;
    for (int j = 0; j < RS_R; j++) {
        S[j] = polyEvalAt(cw, RS_N - 1, gfExp[j]);
        if (S[j]) anyErr = true;
    }
    if (!anyErr) return 0;

    // Erasure locator Gamma(x) = prod (1 - X_k x), X_k = alpha^pos
    uint8_t lambda[RS_R + 1];
    memset(lambda, 0, sizeof(lambda));
    lambda[0] = 1;
    for (int k = 0; k < nEra; k++) {
        uint8_t Xk = gfPowAlpha(eraPos[k]);
        for (int j = k + 1; j > 0; j--) lambda[j] ^= gfMul(lambda[j - 1], Xk);
    }

    // Berlekamp-Massey initialized with erasure locator
    uint8_t B[RS_R + 1];
    memcpy(B, lambda, sizeof(B));
    int L = nEra;
    int m = 1;
    uint8_t b = 1;
    for (int r = nEra; r < RS_R; r++) {
        uint8_t delta = 0;
        for (int j = 0; j <= L; j++) {
            if (r - j >= 0) delta ^= gfMul(lambda[j], S[r - j]);
        }
        if (delta == 0) {
            m++;
            continue;
        }
        uint8_t T[RS_R + 1];
        memcpy(T, lambda, sizeof(T));
        uint8_t coef = gfDiv(delta, b);
        for (int j = m; j <= RS_R; j++) lambda[j] ^= gfMul(coef, B[j - m]);
        if (2 * L <= r + nEra) {
            L = r + nEra + 1 - L;
            memcpy(B, T, sizeof(B));
            b = delta;
            m = 1;
        } else {
            m++;
        }
    }

    int deg = RS_R;
    while (deg > 0 && lambda[deg] == 0) deg--;
    if (deg != L || 2 * (L - nEra) + nEra > RS_R) return -1;

    // Chien search: root at x = alpha^(-pos)
    uint8_t errPos[RS_R];
    int nRoots = 0;
    for (int pos = 0; pos < RS_N; pos++) {
        if (polyEvalAt(lambda, deg, gfPowAlpha(-pos)) == 0) {
            if (nRoots >= RS_R) return -1;
            errPos[nRoots++] = (uint8_t)pos;
        }
    }
    if (nRoots != deg) return -1;

    // Omega(x) = S(x) * Lambda(x) mod x^24
    uint8_t omega[RS_R];
    memset(omega, 0, sizeof(omega));
    for (int i = 0; i < RS_R; i++) {
        for (int j = 0; j <= deg && j <= i; j++) omega[i] ^= gfMul(S[i - j], lambda[j]);
    }

    // Forney (b = 0): e_k = X_k * Omega(X_k^-1) / Lambda'(X_k^-1)
    for (int k = 0; k < nRoots; k++) {
        uint8_t xinv = gfPowAlpha(-errPos[k]);
        uint8_t num = polyEvalAt(omega, RS_R - 1, xinv);
        uint8_t den = 0;
        for (int j = 1; j <= deg; j += 2) {
            den ^= gfMul(lambda[j], gfPowAlpha(-(int)errPos[k] * (j - 1)));
        }
        if (den == 0) return -1;
        uint8_t ev = gfMul(gfPowAlpha(errPos[k]), gfDiv(num, den));
        cw[errPos[k]] ^= ev;
    }

    for (int j = 0; j < RS_R; j++) {
        if (polyEvalAt(cw, RS_N - 1, gfExp[j]) != 0) return -1;
    }
    return nRoots;
}
