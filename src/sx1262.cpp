// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Direct SPI driver for the SX1262 radio.
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "sx1262.h"

#define OP_SET_STANDBY 0x80
#define OP_SET_RX 0x82
#define OP_SET_RF_FREQ 0x86
#define OP_SET_PACKET_TYPE 0x8A
#define OP_SET_MOD_PARAMS 0x8B
#define OP_SET_PKT_PARAMS 0x8C
#define OP_SET_BUF_BASE 0x8F
#define OP_SET_DIO_IRQ 0x08
#define OP_CLEAR_IRQ 0x02
#define OP_GET_IRQ 0x12
#define OP_GET_RX_BUF_STATUS 0x13
#define OP_GET_PKT_STATUS 0x14
#define OP_GET_RSSI_INST 0x15
#define OP_GET_DEV_ERRORS 0x17
#define OP_CLEAR_DEV_ERRORS 0x07
#define OP_READ_BUFFER 0x1E
#define OP_WRITE_REG 0x0D
#define OP_READ_REG 0x1D
#define OP_SET_REGULATOR 0x96
#define OP_CALIBRATE 0x89
#define OP_CALIBRATE_IMAGE 0x98
#define OP_SET_DIO3_TCXO 0x97
#define OP_SET_DIO2_RF_SW 0x9D
#define OP_SET_FALLBACK 0x93
#define OP_GET_STATUS 0xC0

#define REG_SYNC_WORD_0 0x06C0
#define REG_RX_GAIN 0x08AC

const float Sx1262::BW_KHZ[] = {4.8f, 5.8f, 7.3f, 9.7f, 11.7f, 14.6f, 19.5f, 23.4f, 29.3f, 39.0f};
const uint8_t Sx1262::BW_CODE[] = {0x1F, 0x17, 0x0F, 0x1E, 0x16, 0x0E, 0x1D, 0x15, 0x0D, 0x1C};
const int Sx1262::BW_COUNT = sizeof(BW_CODE);

Sx1262::Sx1262(SPIClass &s, int cs, int rst, int busy, int dio1)
    : spi(s), csPin(cs), rstPin(rst), busyPin(busy), dio1Pin(dio1), settings(8000000, MSBFIRST, SPI_MODE0)
{
}

uint8_t Sx1262::bwCodeFromKhz(float khz)
{
    for (int i = 0; i < BW_COUNT; i++) {
        if (khz <= BW_KHZ[i] + 0.05f) return BW_CODE[i];
    }
    return BW_CODE[BW_COUNT - 1];
}

void Sx1262::waitBusy()
{
    uint32_t t0 = micros();
    while (digitalRead(busyPin) == HIGH) {
        if (micros() - t0 > 100000) break;
    }
}

void Sx1262::xfer(const uint8_t *hdr, int nHdr, const uint8_t *out, uint8_t *in, uint16_t len)
{
    waitBusy();
    spi.beginTransaction(settings);
    digitalWrite(csPin, LOW);
    for (int i = 0; i < nHdr; i++) spi.transfer(hdr[i]);
    for (uint16_t i = 0; i < len; i++) {
        uint8_t v = spi.transfer(out ? out[i] : 0x00);
        if (in) in[i] = v;
    }
    digitalWrite(csPin, HIGH);
    spi.endTransaction();
}

void Sx1262::cmd(uint8_t op, const uint8_t *data, uint8_t len) { xfer(&op, 1, data, nullptr, len); }

void Sx1262::cmdRead(uint8_t op, uint8_t *data, uint8_t len)
{
    const uint8_t h[2] = {op, 0x00}; // the status byte comes back first
    xfer(h, 2, nullptr, data, len);
}

void Sx1262::writeReg(uint16_t addr, const uint8_t *data, uint8_t len)
{
    const uint8_t h[3] = {OP_WRITE_REG, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF)};
    xfer(h, 3, data, nullptr, len);
}

void Sx1262::readReg(uint16_t addr, uint8_t *data, uint8_t len)
{
    const uint8_t h[4] = {OP_READ_REG, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF), 0x00};
    xfer(h, 4, nullptr, data, len);
}

void Sx1262::readBuffer(uint8_t offset, uint8_t *data, uint16_t len)
{
    const uint8_t h[3] = {OP_READ_BUFFER, offset, 0x00};
    xfer(h, 3, nullptr, data, len);
}

uint8_t Sx1262::status()
{
    const uint8_t op = OP_GET_STATUS;
    uint8_t s = 0;
    xfer(&op, 1, nullptr, &s, 1);
    return s;
}

uint16_t Sx1262::deviceErrors()
{
    uint8_t d[2];
    cmdRead(OP_GET_DEV_ERRORS, d, 2);
    return (uint16_t)(d[0] << 8 | d[1]);
}

bool Sx1262::begin(float tcxoVolt)
{
    pinMode(csPin, OUTPUT);
    digitalWrite(csPin, HIGH);
    pinMode(busyPin, INPUT);
    pinMode(dio1Pin, INPUT);
    pinMode(rstPin, OUTPUT);
    digitalWrite(rstPin, LOW);
    delay(2);
    digitalWrite(rstPin, HIGH);
    delay(10);
    waitBusy();

    uint8_t p[4];
    p[0] = 0x00;
    cmd(OP_SET_STANDBY, p, 1);

    if (tcxoVolt > 0) {
        uint8_t code = 0x02;
        if (tcxoVolt < 1.65f) code = 0x00;
        else if (tcxoVolt < 1.75f) code = 0x01;
        else if (tcxoVolt < 2.0f) code = 0x02;
        else if (tcxoVolt < 2.3f) code = 0x03;
        else if (tcxoVolt < 2.5f) code = 0x04;
        else if (tcxoVolt < 2.8f) code = 0x05;
        else if (tcxoVolt < 3.1f) code = 0x06;
        else code = 0x07;
        uint32_t delayTicks = 320; // 5 ms in 15.625 us steps
        p[0] = code;
        p[1] = (delayTicks >> 16) & 0xFF;
        p[2] = (delayTicks >> 8) & 0xFF;
        p[3] = delayTicks & 0xFF;
        cmd(OP_SET_DIO3_TCXO, p, 4);
    }
    p[0] = 0x7F;
    cmd(OP_CALIBRATE, p, 1);
    delay(10);
    waitBusy();
    p[0] = 0x00;
    p[1] = 0x00;
    cmd(OP_CLEAR_DEV_ERRORS, p, 2);

    p[0] = 0x01;
    cmd(OP_SET_REGULATOR, p, 1);
    p[0] = 0x01;
    cmd(OP_SET_DIO2_RF_SW, p, 1);
    p[0] = 0x00;
    cmd(OP_SET_PACKET_TYPE, p, 1);
    p[0] = 0x40;
    cmd(OP_SET_FALLBACK, p, 1);

    uint8_t gain = 0x96;
    writeReg(REG_RX_GAIN, &gain, 1);
    setBufferBase(0, 0);
    return selfTest();
}

bool Sx1262::selfTest()
{
    uint8_t w[2] = {0xA5, 0x3C}, r[2] = {0, 0}, orig[2];
    readReg(REG_SYNC_WORD_0, orig, 2);
    writeReg(REG_SYNC_WORD_0, w, 2);
    readReg(REG_SYNC_WORD_0, r, 2);
    writeReg(REG_SYNC_WORD_0, orig, 2);
    return r[0] == w[0] && r[1] == w[1];
}

void Sx1262::setFrequency(double mhz)
{
    uint8_t p[4];
    uint8_t lo = (uint8_t)((mhz - 2.0) / 4.0);
    uint8_t hi = (uint8_t)((mhz + 2.0) / 4.0 + 0.999);
    p[0] = lo;
    p[1] = hi;
    standby();
    cmd(OP_CALIBRATE_IMAGE, p, 2);
    waitBusy();
    setFrequencyFast(mhz);
}

void Sx1262::setFrequencyFast(double mhz)
{
    uint32_t frf = (uint32_t)(mhz * 1e6 * 33554432.0 / 32e6 + 0.5);
    uint8_t p[4] = {(uint8_t)(frf >> 24), (uint8_t)(frf >> 16), (uint8_t)(frf >> 8), (uint8_t)frf};
    cmd(OP_SET_RF_FREQ, p, 4);
}

void Sx1262::standbyXosc()
{
    uint8_t p = 0x01;
    cmd(OP_SET_STANDBY, &p, 1);
}

void Sx1262::setGfsk(uint32_t bitrate, uint32_t fdevHz, uint8_t bwCode)
{
    uint32_t br = (uint32_t)(32.0 * 32e6 / bitrate + 0.5);
    uint32_t fd = (uint32_t)(fdevHz * 33554432.0 / 32e6 + 0.5);
    uint8_t p[8];
    p[0] = br >> 16;
    p[1] = br >> 8;
    p[2] = br;
    p[3] = 0x00; // no pulse shaping
    p[4] = bwCode;
    p[5] = fd >> 16;
    p[6] = fd >> 8;
    p[7] = fd;
    cmd(OP_SET_MOD_PARAMS, p, 8);
}

void Sx1262::setPacket(const SxPacketCfg &cfg)
{
    uint8_t p[9];
    uint16_t preambleTx = 32;
    p[0] = preambleTx >> 8;
    p[1] = preambleTx & 0xFF;
    p[2] = cfg.preambleDetect;
    p[3] = cfg.syncBits;
    p[4] = 0x00; // no address filtering
    p[5] = 0x00; // fixed length
    p[6] = cfg.payloadLen;
    p[7] = 0x01; // CRC off
    p[8] = 0x00; // whitening off
    cmd(OP_SET_PKT_PARAMS, p, 9);
}

void Sx1262::setSyncWord(const uint8_t *sync, uint8_t len)
{
    uint8_t w[8] = {0};
    for (uint8_t i = 0; i < len && i < 8; i++) w[i] = sync[i];
    writeReg(REG_SYNC_WORD_0, w, 8);
}

void Sx1262::setBufferBase(uint8_t tx, uint8_t rx)
{
    uint8_t p[2] = {tx, rx};
    cmd(OP_SET_BUF_BASE, p, 2);
}

void Sx1262::setIrq(uint16_t mask, uint16_t dio1Mask)
{
    uint8_t p[8] = {(uint8_t)(mask >> 8), (uint8_t)mask, (uint8_t)(dio1Mask >> 8), (uint8_t)dio1Mask, 0, 0, 0, 0};
    cmd(OP_SET_DIO_IRQ, p, 8);
}

void Sx1262::setRxContinuous()
{
    uint8_t p[3] = {0xFF, 0xFF, 0xFF};
    cmd(OP_SET_RX, p, 3);
}

void Sx1262::setRxSingle()
{
    uint8_t p[3] = {0x00, 0x00, 0x00};
    cmd(OP_SET_RX, p, 3);
}

void Sx1262::standby()
{
    uint8_t p = 0x00;
    cmd(OP_SET_STANDBY, &p, 1);
}

uint16_t Sx1262::irqStatus()
{
    uint8_t d[2];
    cmdRead(OP_GET_IRQ, d, 2);
    return (uint16_t)(d[0] << 8 | d[1]);
}

void Sx1262::clearIrq(uint16_t mask)
{
    uint8_t p[2] = {(uint8_t)(mask >> 8), (uint8_t)mask};
    cmd(OP_CLEAR_IRQ, p, 2);
}

void Sx1262::rxBufferStatus(uint8_t *len, uint8_t *start)
{
    uint8_t d[2];
    cmdRead(OP_GET_RX_BUF_STATUS, d, 2);
    *len = d[0];
    *start = d[1];
}

float Sx1262::rssiInst()
{
    uint8_t d;
    cmdRead(OP_GET_RSSI_INST, &d, 1);
    return -(float)d / 2.0f;
}

void Sx1262::packetStatus(float *rssiSync, float *rssiAvg)
{
    uint8_t d[3];
    cmdRead(OP_GET_PKT_STATUS, d, 3);
    *rssiSync = -(float)d[1] / 2.0f;
    *rssiAvg = -(float)d[2] / 2.0f;
}
