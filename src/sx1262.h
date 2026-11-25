// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <Arduino.h>
#include <SPI.h>

#define SX_IRQ_RX_DONE (1 << 1)
#define SX_IRQ_PREAMBLE (1 << 2)
#define SX_IRQ_SYNC_VALID (1 << 3)
#define SX_IRQ_TIMEOUT (1 << 9)
#define SX_IRQ_ALL 0x03FF

struct SxPacketCfg {
    uint8_t preambleDetect; // 0x00 off, 0x04 8b, 0x05 16b, 0x06 24b, 0x07 32b
    uint8_t syncBits;       // 0..64
    uint8_t payloadLen;     // fixed length
};

class Sx1262 {
public:
    Sx1262(SPIClass &spi, int cs, int rst, int busy, int dio1);
    bool begin(float tcxoVolt);
    void setFrequency(double mhz);
    // Retune inside the band already image-calibrated by setFrequency (no calibration, fast).
    void setFrequencyFast(double mhz);
    void setGfsk(uint32_t bitrate, uint32_t fdevHz, uint8_t bwCode);
    void setPacket(const SxPacketCfg &cfg);
    void setSyncWord(const uint8_t *sync, uint8_t len);
    void setBufferBase(uint8_t tx, uint8_t rx);
    void setIrq(uint16_t mask, uint16_t dio1Mask);
    void setRxContinuous();
    void setRxSingle();
    void standby();
    void standbyXosc(); // keeps the TCXO running: quick return to RX
    uint16_t irqStatus();
    void clearIrq(uint16_t mask);
    bool dio1() const { return digitalRead(dio1Pin) == HIGH; }
    void rxBufferStatus(uint8_t *len, uint8_t *start);
    void readBuffer(uint8_t offset, uint8_t *data, uint16_t len);
    float rssiInst();
    void packetStatus(float *rssiSync, float *rssiAvg);
    uint8_t status();
    uint16_t deviceErrors();
    void writeReg(uint16_t addr, const uint8_t *data, uint8_t len);
    void readReg(uint16_t addr, uint8_t *data, uint8_t len);

    static uint8_t bwCodeFromKhz(float khz);
    static const float BW_KHZ[];
    static const uint8_t BW_CODE[];
    static const int BW_COUNT;

private:
    bool selfTest();
    void waitBusy();
    // One SPI transaction: the header bytes, then len bytes written from out or read into in (one of the two).
    void xfer(const uint8_t *hdr, int nHdr, const uint8_t *out, uint8_t *in, uint16_t len);
    void cmd(uint8_t op, const uint8_t *data, uint8_t len);
    void cmdRead(uint8_t op, uint8_t *data, uint8_t len);
    SPIClass &spi;
    int csPin, rstPin, busyPin, dio1Pin;
    SPISettings settings;
};
