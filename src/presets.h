// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stddef.h>
#include <stdint.h>

// Frequency presets kept in a text file on the SD card (/presets.txt). Portable, tested on host.
//
//   # comment
//   [Italy]                      optional group (country, region...)
//   404.500  RS41  Trapani       frequency (MHz), sonde type (AUTO, RS41, DFM, M10/M20, iMet54), name
//   403.000  DFM                 the name may be omitted
//
// The same frequency may appear in several groups (different countries).

#define PRESET_MAX 200
#define PRESET_SAVED_GROUP "Saved"

struct Preset {
    uint32_t khz;
    uint8_t type;   // SondeType
    char name[32];
    char group[16];
};

// Parses the whole file; returns the number of presets (lines that cannot be read are skipped).
int presets_parse(const char *text, Preset *out, int max);
// One line in the file format, without newline.
int presets_line(char *buf, size_t n, const Preset &p);
// The text written when the file does not exist yet: instructions, the European launch sites
// (PRESETS_DEFAULT_TEXT) and the given presets not already among them (under [Saved]).
int presets_template(char *buf, size_t n, const Preset *p, int count);
// Appends to text (capacity cap) the default lines not there yet; returns how many.
// A default is there when the file has the same site name (also at another frequency: edited by
// the user) or the same frequency in the same group.
int presets_merge_defaults(char *text, size_t cap);
extern const char *const PRESETS_DEFAULT_TEXT;
// Index of the first preset with this frequency, -1 if none.
int presets_find(const Preset *p, int count, uint32_t khz);
// Index of the preset with this group and name (khz preferred when several match), -1 if none.
int presets_find_named(const Preset *p, int count, const char *group, const char *name, uint32_t khz);
// Removes the line of preset number index (as counted by presets_parse); false if not found.
bool presets_remove(char *text, int index);
// Appends q under [Saved] (adding the header when the last group is another); returns its index or -1.
int presets_append(char *text, size_t cap, const Preset &q);
// Next preset in the same group as cur, wrapping inside the group (0 when cur < 0).
int presets_next_in_group(const Preset *p, int count, int cur);
// First preset of the next (dir > 0) or previous (dir < 0) group from index from, wrapping.
int presets_group_jump(const Preset *p, int count, int from, int dir);
// True when the query (case-insensitive) is in the name or the group, or starts the frequency ("404.5").
bool presets_match(const Preset &p, const char *query);
// A name for a new preset that no other preset has (case-insensitive): want itself, else "want 2", "want 3"...
// shortened so that it fits Preset::name. Spaces at the ends are trimmed; false when want is empty.
bool presets_unique_name(const Preset *p, int count, const char *want, char *out, size_t n);
