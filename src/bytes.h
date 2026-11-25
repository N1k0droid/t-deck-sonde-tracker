// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stdint.h>

// Byte and bit access shared by the decoders. Portable, header only.

// Big-endian unsigned value of n bytes (n <= 4).
static inline uint32_t get_be(const uint8_t *p, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = v << 8 | p[i];
    return v;
}
static inline uint32_t get_be32(const uint8_t *p) { return get_be(p, 4); }
static inline uint16_t get_le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static inline void put_le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

// Value of a hexadecimal digit, -1 for any other character.
static inline int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// Bit k of a stream, most significant bit of each byte first.
static inline int get_bit(const uint8_t *p, int k) { return (p[k >> 3] >> (7 - (k & 7))) & 1; }
