// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "audio.h"
#include "board_pins.h"
#include <driver/i2s.h>
#include <math.h>

#define SAMPLE_RATE 16000
#define MAX_MS 120
#define AMPLITUDE 12000

static bool ready = false;
static int16_t buf[SAMPLE_RATE * MAX_MS / 1000 * 2];
static uint16_t patHz = 0, patMs = 0, patGap = 0;
static uint8_t patLeft = 0;
static uint32_t patNext = 0;
static float gainMain = 1.0f, gainFrame = 1.0f;

// 3 dB a step: equal steps sound alike; 9 is twice the amplitude of 7.
static float volGain(uint8_t v)
{
    if (v > 9) v = 9;
    return v ? powf(10.0f, (v - 7) * 3.0f / 20.0f) : 0.0f;
}

void audio_volume(uint8_t main, uint8_t frame)
{
    gainMain = volGain(main);
    gainFrame = volGain(frame);
}

bool audio_begin()
{
    i2s_config_t c = {};
    c.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    c.sample_rate = SAMPLE_RATE;
    c.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    c.dma_buf_count = 8;
    c.dma_buf_len = 256;
    c.tx_desc_auto_clear = true; // silence when no data is queued
    i2s_pin_config_t p = {};
    p.mck_io_num = I2S_PIN_NO_CHANGE;
    p.bck_io_num = BOARD_I2S_BCK;
    p.ws_io_num = BOARD_I2S_WS;
    p.data_out_num = BOARD_I2S_DOUT;
    p.data_in_num = I2S_PIN_NO_CHANGE;
    ready = i2s_driver_install(I2S_NUM_0, &c, 0, NULL) == ESP_OK && i2s_set_pin(I2S_NUM_0, &p) == ESP_OK;
    return ready;
}

// fadeMs ramps avoid clicks; h2 adds the second harmonic (fuller on the small speaker).
static void tone(uint16_t hz, uint16_t ms, int fadeMs, float h2, float gain)
{
    if (!ready || hz == 0 || gain <= 0.0f) return;
    if (ms > MAX_MS) ms = MAX_MS;
    int n = SAMPLE_RATE * ms / 1000;
    int fade = SAMPLE_RATE * fadeMs / 1000;
    float w = 2.0f * PI * hz / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float env = 1.0f;
        if (i < fade) env = (float)i / fade;
        else if (i > n - fade) env = (float)(n - i) / fade;
        float s = (sinf(w * i) + h2 * sinf(2.0f * w * i)) / (1.0f + h2);
        int16_t v = (int16_t)(AMPLITUDE * gain * env * s);
        buf[2 * i] = v;
        buf[2 * i + 1] = v;
    }
    size_t written;
    i2s_write(I2S_NUM_0, buf, (size_t)n * 4, &written, 0); // drops what does not fit: never blocks
}

void audio_beep(uint16_t hz, uint16_t ms)
{
    tone(hz, ms, 2, 0.0f, gainMain);
}

void audio_frame_beep(uint16_t hz, uint16_t ms)
{
    tone(hz, ms, 2, 0.0f, gainFrame);
}

// Frame received: short, soft and always the same, so it says "decoding" without being a nuisance.
void audio_tick()
{
    tone(1400, 30, 8, 0.0f, 0.35f * gainFrame);
}

// ---- Signal meter: same tables as tools/sound_preview.py ----
// One level every 10 dB from -120 dBm; pitch rises at every level, the rate only up to -90 dBm.
static const float METER_RATE[8] = {0.8f, 1.1f, 1.4f, 1.7f, 2.0f, 2.0f, 2.0f, 2.0f};
static const uint16_t METER_HZ[8] = {500, 601, 722, 868, 1043, 1253, 1506, 1800}; // 500 * 3.6^(i/7)
#define METER_BEEP_MS 70
#define METER_HYST_DB 2.0f // a level changes only 2 dB past its edge: no flapping on a boundary
#define METER_TIMEOUT_MS 600

static int meterLevel = -1;
static uint32_t meterSeen = 0, meterNext = 0;

void audio_meter(float dbm)
{
    float pos = (dbm + 120.0f) / 10.0f;
    int lv = (int)floorf(pos);
    if (meterLevel >= 0 && lv != meterLevel) {
        // Keep the current level until the signal is clearly inside the new one.
        if (lv > meterLevel && pos - lv < METER_HYST_DB / 10.0f && lv == meterLevel + 1) lv = meterLevel;
        if (lv < meterLevel && (meterLevel - pos) < METER_HYST_DB / 10.0f && lv == meterLevel - 1) lv = meterLevel;
    }
    if (lv < 0) lv = 0;
    if (lv > 7) lv = 7;
    if (meterLevel < 0 || millis() - meterSeen > METER_TIMEOUT_MS) meterNext = millis(); // just started: beep now
    meterLevel = lv;
    meterSeen = millis();
}

static void meterPoll()
{
    if (meterLevel < 0) return;
    uint32_t now = millis();
    if (now - meterSeen > METER_TIMEOUT_MS) { // no longer fed: the screen was left or sound turned off
        meterLevel = -1;
        return;
    }
    if ((int32_t)(now - meterNext) < 0) return;
    tone(METER_HZ[meterLevel], METER_BEEP_MS, 6, 0.25f, gainMain);
    meterNext = now + (uint32_t)(1000.0f / METER_RATE[meterLevel]);
}

void audio_pattern(uint16_t hz, uint16_t ms, uint8_t count, uint16_t gapMs)
{
    patHz = hz;
    patMs = ms;
    patGap = gapMs;
    patLeft = count;
    patNext = millis();
}

void audio_poll()
{
    meterPoll();
    if (!patLeft || (int32_t)(millis() - patNext) < 0) return;
    audio_beep(patHz, patMs);
    patLeft--;
    patNext = millis() + patMs + patGap;
}

uint16_t audio_rssi_pitch(float dbm)
{
    float hz = 300.0f + (dbm + 120.0f) * 25.0f;
    if (hz < 300.0f) hz = 300.0f;
    if (hz > 2800.0f) hz = 2800.0f;
    return (uint16_t)hz;
}
