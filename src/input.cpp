// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "input.h"
#include "board_pins.h"
#include <Wire.h>

// ---- Trackball ----
// Each Hall line toggles as the ball turns; tbStep edges make one step (2 fast, more for a slower ball).
// Edges left over, too few for a step, are dropped after a pause: a small nudge does not add to the next move.
#define TB_IDLE_MS 300
static const int TB_PIN[4] = {BOARD_TBOX_UP, BOARD_TBOX_DOWN, BOARD_TBOX_LEFT, BOARD_TBOX_RIGHT};
static volatile uint32_t tbEdges[4] = {0, 0, 0, 0};
static volatile uint32_t tbEdgeMs[4] = {0, 0, 0, 0};
static uint32_t tbUsed[4] = {0, 0, 0, 0};
static uint8_t tbStep = 2;
static bool clickDown = false, holdSent = false;
static uint32_t clickChangeMs = 0;
#define HOLD_MS 3000 // trackball held this long: IN_HOLD (screen lock) instead of a click

static void IRAM_ATTR isrUp() { tbEdges[0]++; tbEdgeMs[0] = millis(); }
static void IRAM_ATTR isrDown() { tbEdges[1]++; tbEdgeMs[1] = millis(); }
static void IRAM_ATTR isrLeft() { tbEdges[2]++; tbEdgeMs[2] = millis(); }
static void IRAM_ATTR isrRight() { tbEdges[3]++; tbEdgeMs[3] = millis(); }

// ---- Touch (GT911) ----
#define GT_STATUS 0x814E
#define GT_POINT1 0x8150
#define TOUCH_POLL_MS 25
#define LONG_PRESS_MS 700
static uint8_t gtAddr = 0;
static bool touchOn = true;
static uint32_t touchPollMs = 0;
static bool touching = false, longSent = false;
static uint32_t touchStartMs = 0;
static int16_t touchX = 0, touchY = 0;

// ---- Event queue ----
static InputEvent q[16];
static uint8_t qHead = 0, qTail = 0;
static InputStats st = {};

static void push(InputType t, int16_t x = 0, int16_t y = 0)
{
    uint8_t n = (qHead + 1) % 16;
    if (n == qTail) return;
    q[qHead] = {t, x, y};
    qHead = n;
}

static bool gtRead(uint16_t reg, uint8_t *buf, uint8_t n)
{
    Wire.beginTransmission(gtAddr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom(gtAddr, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

static void gtWrite8(uint16_t reg, uint8_t v)
{
    Wire.beginTransmission(gtAddr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(v);
    Wire.endTransmission();
}

// One address on the bus. The ESP32 I2C driver (core 2.0.9) sometimes fails the second request after a device
// answered, with a 1 s timeout even when a device is there (seen with /i2c: 0x14 answers, 0x16 "no" in 1 s;
// 0x55, 0x57 the same). Only a clean "no" (2, address not acknowledged) is believed: anything else is asked again.
static bool i2cAnswers(uint8_t a)
{
    for (int t = 0; t < 2; t++) {
        Wire.beginTransmission(a);
        uint8_t e = Wire.endTransmission();
        if (e == 0) return true;
        if (e == 2) return false;
    }
    return false;
}

// The GT911 can be slow to answer after power-on: probed at start and then again from the loop.
// 0x14 first: where it sits after the Launcher (0x5D seen in v0.55).
static uint32_t probeMs = 0;
static bool probeTouch()
{
    static const uint8_t ADDR[2] = {0x14, 0x5D};
    uint32_t t0 = millis();
    probeMs = t0;
    st.touchProbes++;
    if (st.touchProbes > 1) {
        // GT911 wake-up from sleep: INT driven high for a few ms (datasheet), then back to input.
        pinMode(BOARD_TOUCH_INT, OUTPUT);
        digitalWrite(BOARD_TOUCH_INT, HIGH);
        delay(5);
        pinMode(BOARD_TOUCH_INT, INPUT);
        delay(50);
    }
    for (uint8_t a : ADDR)
        if (i2cAnswers(a)) {
            gtAddr = a;
            break;
        }
    st.touchAddr = gtAddr;
    st.probeMs = millis() - t0;
    return gtAddr != 0;
}

bool input_touch_answers() { return i2cAnswers(0x14) || i2cAnswers(0x5D); }

void input_note_power_cycle() { st.powerCycled = true; }

bool input_begin()
{
    for (int i = 0; i < 4; i++) pinMode(TB_PIN[i], INPUT_PULLUP);
    pinMode(BOARD_BOOT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TB_PIN[0]), isrUp, CHANGE);
    attachInterrupt(digitalPinToInterrupt(TB_PIN[1]), isrDown, CHANGE);
    attachInterrupt(digitalPinToInterrupt(TB_PIN[2]), isrLeft, CHANGE);
    attachInterrupt(digitalPinToInterrupt(TB_PIN[3]), isrRight, CHANGE);

    pinMode(BOARD_TOUCH_INT, INPUT);
    return probeTouch(); // no scan of the whole bus: each false "no" of the driver costs 1 s (see i2cAnswers)
}

bool input_probe_now()
{
    gtAddr = 0;
    return probeTouch();
}

bool input_touch_ok()
{
    return gtAddr != 0;
}

void input_set_trackball_step(uint8_t edges)
{
    tbStep = edges < 2 ? 2 : edges;
    st.tbStep = tbStep;
}

void input_set_touch(bool on)
{
    touchOn = on;
    touching = false;
}

static void pollTrackball()
{
    static const InputType T[4] = {IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT};
    uint32_t now0 = millis();
    for (int i = 0; i < 4; i++) {
        uint32_t e = tbEdges[i];
        while (e - tbUsed[i] >= tbStep) {
            tbUsed[i] += tbStep;
            st.steps[i]++;
            push(T[i]);
        }
        if (e != tbUsed[i] && now0 - tbEdgeMs[i] > TB_IDLE_MS) tbUsed[i] = e;
    }
    bool down = digitalRead(BOARD_BOOT_PIN) == LOW;
    uint32_t now = millis();
    if (down != clickDown && now - clickChangeMs > 30) { // 30 ms debounce
        clickDown = down;
        clickChangeMs = now;
        if (down) holdSent = false;
        else if (!holdSent) { // released before HOLD_MS: a click
            st.clicks++;
            push(IN_SELECT);
        }
    }
    if (clickDown && !holdSent && now - clickChangeMs >= HOLD_MS) {
        holdSent = true;
        st.holds++;
        push(IN_HOLD);
    }
}

static void pollTouch()
{
    if (!gtAddr && touchOn && millis() < 30000 && millis() - probeMs > 5000) probeTouch();
    if (!gtAddr || !touchOn || millis() - touchPollMs < TOUCH_POLL_MS) return;
    touchPollMs = millis();
    uint8_t s;
    if (!gtRead(GT_STATUS, &s, 1)) return;
    uint32_t now = millis();
    if (s & 0x80) {
        uint8_t n = s & 0x0F;
        if (n > 0) {
            uint8_t p[4];
            if (gtRead(GT_POINT1, p, 4)) {
                int16_t rx = p[0] | (p[1] << 8), ry = p[2] | (p[3] << 8);
                // Panel coordinates are portrait (240 x 320): same transform as the LilyGO examples
                // (swap x/y, then mirror y), so that x/y match the landscape display.
                int16_t x = ry, y = 240 - rx;
                x = x < 0 ? 0 : x > 319 ? 319 : x;
                y = y < 0 ? 0 : y > 239 ? 239 : y;
                st.lastRawX = rx;
                st.lastRawY = ry;
                if (!touching) {
                    touching = true;
                    longSent = false;
                    touchStartMs = now;
                    touchX = x;
                    touchY = y;
                }
            }
        } else if (touching) {
            touching = false;
            if (!longSent) {
                st.taps++;
                st.lastX = touchX;
                st.lastY = touchY;
                push(IN_TAP, touchX, touchY);
            }
        }
        gtWrite8(GT_STATUS, 0); // data consumed
    }
    if (touching && !longSent && now - touchStartMs >= LONG_PRESS_MS) {
        longSent = true;
        st.longs++;
        st.lastX = touchX;
        st.lastY = touchY;
        push(IN_LONG, touchX, touchY);
    }
}

bool input_poll(InputEvent &e)
{
    pollTrackball();
    pollTouch();
    if (qTail == qHead) return false;
    e = q[qTail];
    qTail = (qTail + 1) % 16;
    return true;
}

void input_inject(const InputEvent &e)
{
    push(e.type, e.x, e.y);
}

void input_stats(InputStats *s)
{
    *s = st;
}
