// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Capture of one sonde frame (every supported type) with the SX1262, and the RSSI sweep of the scan.
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "capture.h"
#include "rs41.h"
#include "dfm.h"
#include "m10m20.h"
#include "imet54.h"

#define REG_RX_ADDR_PTR 0x0803 // write position in the 256-byte receive buffer
#define REG_RXTX_PLD_LEN 0x06BB // the packet ends when the write position reaches this value
#define LONG_FIRST 150          // first buffer read after this many bytes
#define LONG_STEP 190           // then every this many bytes
#ifndef SYNC_QUIET_US
#define SYNC_QUIET_US 8000 // no SPI to the radio this long after the sync word (0: off, for comparisons)
#endif

// Radio settings of each family: symbol rate, sync word, bytes received after it, preamble detector.
struct TypeRadio {
    uint32_t baud;
    const uint8_t *sync;
    uint8_t syncLen;
    uint8_t payload;        // hardware packet length
    uint16_t total;         // bytes wanted after the sync; more than 255: long reception
    uint8_t preambleDetect; // 0x00 off (frames without a clean preamble), 0x05 16 bits
    float minBwKhz;         // wider filter for fast families
};

static TypeRadio radioOf(uint8_t t)
{
    switch (t) {
    case ST_DFM: return {DFM_BAUD, DFM_SYNC, 4, DFM_CAPTURE, DFM_CAPTURE, 0x00, 0};
    case ST_M10: return {M10_BAUD, M10_SYNC, 4, M10_CAPTURE, M10_CAPTURE, 0x00, 23.4f};
    case ST_IMET54: return {IMET54_BAUD, IMET54_SYNC, 5, CAP_P1_LEN, IMET54_CAPTURE, 0x00, 0};
    default: return {4800, RS41_SYNC_MSB, 8, CAP_P1_LEN, CAP_P1_LEN, 0x05, 0};
    }
}

int Capture::pktLen() const { return radioOf(ctype).payload; }
float Capture::byteUs() const { return 8e6f / radioOf(ctype).baud; }

static SxPacketCfg packetCfg(uint8_t type)
{
    TypeRadio r = radioOf(type);
    SxPacketCfg c;
    c.preambleDetect = r.preambleDetect;
    c.syncBits = (uint8_t)(r.syncLen * 8);
    c.payloadLen = r.payload;
    return c;
}

void Capture::configure(uint8_t type, double freqMhz, uint8_t bwCode, bool invert)
{
    inv = invert;
    ctype = type > ST_AUTO && type < ST_COUNT ? type : ST_RS41;
    TypeRadio r = radioOf(ctype);
    radio.standby();
    radio.setFrequency(freqMhz);
    uint8_t sync[8];
    if (r.minBwKhz > 0) {
        uint8_t wide = Sx1262::bwCodeFromKhz(r.minBwKhz);
        for (int i = 0; i < Sx1262::BW_COUNT; i++)
            if (Sx1262::BW_CODE[i] == bwCode && Sx1262::BW_KHZ[i] < r.minBwKhz) bwCode = wide;
    }
    radio.setGfsk(r.baud, 2400, bwCode);
    for (int i = 0; i < r.syncLen; i++) sync[i] = invert ? (uint8_t)~r.sync[i] : r.sync[i];
    radio.setSyncWord(sync, r.syncLen);
    radio.setIrq(SX_IRQ_RX_DONE | SX_IRQ_SYNC_VALID | SX_IRQ_PREAMBLE | SX_IRQ_TIMEOUT,
                 SX_IRQ_RX_DONE | SX_IRQ_SYNC_VALID);
    arm();
}

void Capture::arm()
{
    radio.standby();
    radio.setPacket(packetCfg(ctype));
    radio.setBufferBase(0, 0);
    radio.clearIrq(SX_IRQ_ALL);
    radio.setRxSingle();
    syncSeen = false;
    inFrame = false;
}

float Capture::rssiNow()
{
    return radio.rssiInst();
}

float Capture::measureRssi(double freqMhz, uint8_t settleMs)
{
    radio.standbyXosc();
    radio.setFrequencyFast(freqMhz);
    radio.clearIrq(SX_IRQ_ALL);
    radio.setRxContinuous();
    delay(settleMs);
    return radio.rssiInst();
}

bool Capture::poll(CaptureResult &out)
{
    if (!radio.dio1()) return false;
    uint32_t seen = millis();
    // DIO1 before the sync flag is cleared can only be SyncWordValid (RxDone comes after it): leave the
    // radio alone for its first milliseconds of packet, SPI traffic there spoils weak frames.
    if (!syncSeen) delayMicroseconds(SYNC_QUIET_US);
    uint16_t irq = radio.irqStatus();
    if (!syncSeen && (irq & SX_IRQ_SYNC_VALID)) {
        lastSync = seen;
        syncSeen = true;
        inFrame = true;
        radio.clearIrq(SX_IRQ_SYNC_VALID | SX_IRQ_PREAMBLE);
    }
    if (!syncSeen) {
        radio.clearIrq(irq);
        return false;
    }

    memset(&out, 0, sizeof(out));
    out.invert = inv;
    out.type = ctype;
    out.tSync = lastSync;
    bool ok = (full && ctype == ST_RS41) || radioOf(ctype).total > CAP_P1_LEN ? readLong(out) : readPacket(out);
    out.devErrors = radio.deviceErrors();
    arm();
    return ok;
}

static bool waitIrq(Sx1262 &radio, uint16_t mask, uint32_t timeoutUs)
{
    uint32_t t0 = micros();
    while (micros() - t0 < timeoutUs) {
        if (radio.dio1() && (radio.irqStatus() & mask)) return true;
        delayMicroseconds(100); // not a busy spin for up to 670 ms (DFM): the other core's tasks get the bus
    }
    return false;
}

bool Capture::readPacket(CaptureResult &out)
{
    int n = pktLen();
    if (!waitIrq(radio, SX_IRQ_RX_DONE, (uint32_t)((n + 4) * byteUs()))) return false;
    uint8_t len, start;
    radio.rxBufferStatus(&len, &start);
    radio.packetStatus(&out.rssiSync, &out.rssiAvg);
    radio.readBuffer(start, out.p1, n);
    out.n1 = n;
    return true;
}

// Waits on the DIO1 line only (no SPI traffic near the radio while it receives). True if RxDone came.
static bool waitBytes(Sx1262 &radio, float bytes, float byteUs)
{
    uint32_t t0 = micros(), us = bytes > 0 ? (uint32_t)(bytes * byteUs) : 0;
    while (micros() - t0 < us) {
        if (radio.dio1()) return true;
        delayMicroseconds(200);
    }
    return radio.dio1();
}

// Whole frames: the buffer is a 256-byte ring with base 0. The bytes already arrived are copied out and
// the packet end (RXTX_PLD_LEN) is kept ahead of the write position; once the frame length is known
// (frame type byte) the real end is programmed and RxDone closes the frame. Other families: fixed length.
bool Capture::readLong(CaptureResult &out)
{
    bool rs41 = ctype == ST_RS41;
    uint8_t idx = 0; // next byte to copy, ring position
    int count = 0, total = rs41 ? 0 : radioOf(ctype).total;
    bool endSet = false;
    auto drain = [&]() {
        uint8_t ptr;
        radio.readReg(REG_RX_ADDR_PTR, &ptr, 1);
        int avail = (uint8_t)(ptr - idx);
        if (count + avail > CAP_MAX_LEN) avail = CAP_MAX_LEN - count;
        int first = avail < 256 - idx ? avail : 256 - idx;
        if (first > 0) radio.readBuffer(idx, out.p1 + count, first);
        if (avail > first) radio.readBuffer(0, out.p1 + count + first, avail - first);
        idx = (uint8_t)(idx + avail);
        count += avail;
    };
    uint8_t ptr;
    radio.readReg(REG_RX_ADDR_PTR, &ptr, 1);
    if (radio.dio1() || waitBytes(radio, LONG_FIRST - ptr, byteUs())) {
        if (rs41) nCut++; // too late: the hardware packet ended at 255 bytes
        return readPacket(out);
    }
    while (!endSet) {
        drain();
        if (rs41 && !total && count > 48) total = rs41_frame_len_from_raw(out.p1[48], inv) - RS41_HDR_LEN;
        uint8_t end;
        if (total && total - count <= 250) {
            end = (uint8_t)total; // ring position where the frame ends
            endSet = true;
        } else {
            end = (uint8_t)(idx - 1); // just behind the write position: up to 255 more bytes
        }
        radio.writeReg(REG_RXTX_PLD_LEN, &end, 1);
        if (!endSet && waitBytes(radio, LONG_STEP, byteUs())) break; // RxDone too early: keep what arrived
    }
    bool done = waitBytes(radio, endSet ? total - count + 4 : 0, byteUs());
    drain();
    uint8_t reset = 0xFF;
    radio.writeReg(REG_RXTX_PLD_LEN, &reset, 1);
    radio.packetStatus(&out.rssiSync, &out.rssiAvg);
    if (!rs41) {
        // fixed-length families: counters are RS41 only
    } else if (!done || count < total) nShort++;
    else if (total == RS41_EXT_LEN - RS41_HDR_LEN) nFull518++;
    else nFull320++;
    out.n1 = count;
    return count >= CAP_P1_LEN;
}
