// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <Arduino.h>

// Tone output on the T-Deck I2S speaker amplifier. Calls never block.
bool audio_begin();
// Volumes 0 (mute) to 9 (loudest): main for beeps, alarms and the signal meter, frame for the sound of
// each decoded frame. 7 is the level of the versions before 0.68.
void audio_volume(uint8_t main, uint8_t frame);
// Single tone, at most 120 ms.
void audio_beep(uint16_t hz, uint16_t ms);
// Soft short tick for each decoded frame (frame volume).
void audio_tick();
// Beep for a decoded frame, at the frame volume.
void audio_frame_beep(uint16_t hz, uint16_t ms);
// count tones separated by gapMs, played from audio_poll().
void audio_pattern(uint16_t hz, uint16_t ms, uint8_t count, uint16_t gapMs);
void audio_poll();
// Signal meter: call at least every 600 ms with the current level while it should
// sound; it stops by itself when no longer called. Beeps are timed by audio_poll().
void audio_meter(float dbm);
// Maps a signal level to a pitch: stronger = higher.
uint16_t audio_rssi_pitch(float dbm);
