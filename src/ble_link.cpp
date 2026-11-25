// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "ble_link.h"
#include <NimBLEDevice.h>

// MySondy GO service (UUIDs as used by MySondyGO receivers) and Nordic UART service.
#define MYSONDY_SVC "53797269-614d-6972-6b6f-44616c6d6f6e"
#define MYSONDY_RX "53797268-614d-6972-6b6f-44616c6d6f7e"
#define MYSONDY_TX "53797267-614d-6972-6b6f-44616c6d6f8e"
#define NUS_SVC "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define CHUNK_GAP_MS 8

static bool running = false;
static NimBLEServer *server = nullptr;
static NimBLECharacteristic *txMy = nullptr, *txNus = nullptr;
static volatile bool connected = false;
static volatile uint16_t mtu = 23;
static uint32_t linesSent = 0;
static volatile uint32_t bytesIn = 0;
static char devName[24] = "";

// Received bytes: written by the BLE task, read by the main loop (single producer, single consumer).
static uint8_t rxBuf[512];
static volatile uint16_t rxHead = 0, rxTail = 0;

// Line being sent: body in chunks, then "\r\n" as its own notification (as MySondyGO receivers do).
static char out[300];
static int outLen = 0, outPos = 0;
static bool outCrlf = false;
static uint32_t lastChunkMs = 0;

class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *s, ble_gap_conn_desc *desc) override
    {
        connected = true;
        mtu = s->getPeerMTU(desc->conn_handle);
        if (mtu < 23) mtu = 23;
        NimBLEDevice::startAdvertising(); // stay visible: a second app may connect
    }
    void onDisconnect(NimBLEServer *s) override
    {
        connected = s->getConnectedCount() > 0;
        if (!connected) mtu = 23;
    }
    void onMTUChange(uint16_t m, ble_gap_conn_desc *) override { mtu = m < 23 ? 23 : m; }
};

class RxCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c) override
    {
        std::string v = c->getValue();
        for (char ch : v) {
            uint16_t n = (rxHead + 1) % sizeof(rxBuf);
            if (n == rxTail) break; // full: drop
            rxBuf[rxHead] = (uint8_t)ch;
            rxHead = n;
        }
        bytesIn += v.size();
    }
};

static ServerCb serverCb;
static RxCb rxCb;

static uint64_t macBits()
{
    return ESP.getEfuseMac();
}

void ble_start()
{
    if (running) return;
    uint64_t mac = macBits();
    snprintf(devName, sizeof(devName), "MySondyGO-%04X", (unsigned)((mac >> 32) & 0xFFFF) ^ (unsigned)(mac & 0xFFFF));
    NimBLEDevice::init(devName);
    NimBLEDevice::setMTU(247);
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);
    server = NimBLEDevice::createServer();
    server->setCallbacks(&serverCb, false);

    NimBLEService *my = server->createService(MYSONDY_SVC);
    my->createCharacteristic(MYSONDY_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR)->setCallbacks(&rxCb);
    txMy = my->createCharacteristic(MYSONDY_TX, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
    my->start();

    NimBLEService *nus = server->createService(NUS_SVC);
    nus->createCharacteristic(NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR)->setCallbacks(&rxCb);
    txNus = nus->createCharacteristic(NUS_TX, NIMBLE_PROPERTY::NOTIFY);
    nus->start();

    // Only one 128-bit UUID fits in the advertising packet: the MySondy one. The name goes in the scan response.
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(MYSONDY_SVC);
    adv->setScanResponse(true);
    adv->start();
    running = true;
}

void ble_stop()
{
    if (!running) return;
    NimBLEDevice::deinit(true);
    server = nullptr;
    txMy = txNus = nullptr;
    connected = false;
    mtu = 23;
    outLen = outPos = 0;
    outCrlf = false;
    running = false;
}

bool ble_running()
{
    return running;
}

static bool anySubscribed()
{
    return (txMy && txMy->getSubscribedCount() > 0) || (txNus && txNus->getSubscribedCount() > 0);
}

void ble_send_line(const char *line)
{
    if (!running) return;
    // The latest value is also readable, for apps that poll instead of subscribing.
    if (txMy) txMy->setValue((const uint8_t *)line, strlen(line));
    if (!connected || !anySubscribed()) return;
    outLen = snprintf(out, sizeof(out), "%s", line);
    if (outLen >= (int)sizeof(out)) outLen = sizeof(out) - 1;
    outPos = 0;
    outCrlf = true;
}

static void notifyAll(const uint8_t *d, size_t n)
{
    if (txMy && txMy->getSubscribedCount() > 0) {
        txMy->setValue(d, n);
        txMy->notify();
    }
    if (txNus && txNus->getSubscribedCount() > 0) {
        txNus->setValue(d, n);
        txNus->notify();
    }
}

void ble_poll()
{
    if (!running || (outPos >= outLen && !outCrlf)) return;
    if (millis() - lastChunkMs < CHUNK_GAP_MS) return;
    lastChunkMs = millis();
    if (!connected) {
        outLen = outPos = 0;
        outCrlf = false;
        return;
    }
    if (outPos < outLen) {
        int chunk = mtu > 23 ? mtu - 3 : 20;
        int n = outLen - outPos < chunk ? outLen - outPos : chunk;
        notifyAll((const uint8_t *)out + outPos, n);
        outPos += n;
        return;
    }
    notifyAll((const uint8_t *)"\r\n", 2);
    outCrlf = false;
    linesSent++;
}

int ble_read()
{
    if (rxTail == rxHead) return -1;
    uint8_t c = rxBuf[rxTail];
    rxTail = (rxTail + 1) % sizeof(rxBuf);
    return c;
}

void ble_status(BleStatus *s)
{
    s->running = running;
    s->connected = running && connected;
    s->subscribed = running && anySubscribed();
    s->mtu = mtu;
    s->linesSent = linesSent;
    s->bytesIn = bytesIn;
    snprintf(s->name, sizeof(s->name), "%s", devName);
}
