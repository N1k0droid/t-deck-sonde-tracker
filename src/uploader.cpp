// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "uploader.h"
#include "aprs.h"
#include "geo.h"
#include <WiFi.h>
#include <atomic>
#include <time.h>
#include "tls_client.h"
#ifndef PRED_SERVER
#define PRED_SERVER 0
#endif
#if PRED_SERVER
#include "predserver.h"
#endif
#include "https_call.h"

// Wi-Fi station for the web and debug server, the apps and the internet services, and the uploads (SondeHub,
// radiosondy.info, aprs.fi), each off until the user turns it on and confirms.
#define SH_MOCK (strcmp(SH_API_BASE, SH_API_REAL) != 0) // test build: frames marked "dev" as well
#define SH_DEV_MARK (SH_MOCK || SH_DEV)                   // SondeHub checks such frames and stores none
#define REPLY_MAX 1024
#define QUEUE_LEN 24
#define BATCH_MAX 10
#define BATCH_INTERVAL_MS 10000
#define WIFI_RETRY_MS 20000
#define BODY_MAX 8192
#define APRS_SLOTS 4               // sondes followed at the same time on radiosondy.info
#define APRS_OBJECT_MS 30000       // one object per sonde at most every 30 s (radiosondy.info)
#define APRS_STALE_MS (10UL * 60 * 1000)
#define APRS_RETRY_MIN_MS 10000
#define APRS_RETRY_MAX_MS (5UL * 60 * 1000)

static QueueHandle_t queue = nullptr;
static SemaphoreHandle_t lock = nullptr;
static UploaderCfg cfg = {};
static UploaderStatus status = {};
static char *body = nullptr;
static char *lastBody = nullptr;  // PSRAM: copy of the last batch sent, for /upload_last
static char *lastReply = nullptr; // PSRAM: SondeHub's full answer to it
static int lastCode = 0;
static bool scanWanted = false; // asked by uploader_scan_start, done by the task
static int scanCount = -2;      // -1 scanning, -2 none or failed
static WifiNet scanNets[WIFI_SCAN_MAX];
static bool posValid = false;
static double posLat, posLon, posAlt;
static float posSpd, posCrs;
struct AprsSlot {
    bool used, fresh;
    uint32_t sentMs, gotMs;
    ShFrame f;
};
static AprsSlot aprsSlots[APRS_SLOTS]; // under lock: written by uploader_aprs_push

// Network health. net_mark() runs in this task and in the internet task: the counters are atomic. The
// others have one writer each (this task, or the Wi-Fi event callback) and are read as whole 32-bit words.
static std::atomic<uint32_t> netOkMs{0}, netFails{0};
static volatile uint32_t netResets, connSinceMs, wifiDrops, wifiJoins;
static volatile int wifiReason, wifiRssiAtLoss, wifiRssiNow;
static volatile bool linkUp;   // copy of status.wifiConnected for the event callback
static volatile bool selfDrop; // this task is dropping the link on purpose: not a loss

// Lost link: count it and keep the chip's reason (beacon timeout, handshake, ...), to tell the network from us.
static void onWifiEvent(arduino_event_id_t ev, arduino_event_info_t info)
{
    if (ev != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;
    wifiReason = info.wifi_sta_disconnected.reason;
    if (linkUp && !selfDrop) { // a loss, not a failed attempt nor our own disconnect
        wifiDrops++;
        wifiRssiAtLoss = wifiRssiNow;
    }
    linkUp = false;
}
#define NET_DEAD_MS 180000UL // connected, but no server answered for 3 min ...
#define NET_DEAD_FAILS 5     // ... and at least this many requests failed, and the router's DNS is dead too

void net_mark(bool answered)
{
    if (answered) {
        netOkMs = millis() | 1;
        netFails = 0;
    } else {
        netFails++;
    }
}

// Drops the link on purpose. The internet task may be reading over it: wait (a while) for its HTTPS session.
static void wifiDrop(bool off)
{
    selfDrop = true;
    TlsSession wait(true, 10000);
    WiFi.disconnect(off);
    if (off) WiFi.mode(WIFI_OFF);
}

static void setStatus(bool on, bool connected)
{
    bool wasConnected = status.wifiConnected; // only this task writes the status
    int rssi = connected ? WiFi.RSSI() : 0;   // driver calls outside the lock
    char ip[16] = "";
    if (connected && !wasConnected) snprintf(ip, sizeof(ip), "%s", WiFi.localIP().toString().c_str());
    if (connected && !wasConnected) {
        wifiJoins = 0;
        connSinceMs = millis();
        netOkMs = 0;
        netFails = 0;
        selfDrop = false;
    }
    linkUp = connected;
    if (connected) wifiRssiNow = rssi;
    xSemaphoreTake(lock, portMAX_DELAY);
    status.wifiOn = on;
    status.wifiConnected = connected;
    uint32_t ok = netOkMs;
    status.netOkS = connected && ok ? (int)((millis() - ok) / 1000) : -1;
    status.netFails = netFails;
    status.netResets = netResets;
    status.wifiDrops = wifiDrops;
    status.wifiReason = wifiReason;
    status.wifiRssi = wifiRssiAtLoss;
    status.wifiJoins = wifiJoins;
    if (!connected) status.ip[0] = 0;
    else if (!wasConnected) snprintf(status.ip, sizeof(status.ip), "%s", ip);
    xSemaphoreGive(lock);
}

// The router still answers DNS: the network works, only some servers are down (no reason to rejoin).
static bool dnsAlive()
{
    IPAddress a;
    return WiFi.hostByName("pool.ntp.org", a) == 1;
}

// PUT of a JSON body to SH_API_BASE + path; HTTPS for the real server, plain HTTP for the PC mock.
// The first "error_message" of a SondeHub answer (or the start of the answer), on one line.
static void replySummary(const String &r, char *out, size_t n)
{
    int i = r.indexOf("\"error_message\"");
    String s = r;
    if (i >= 0) {
        int a = r.indexOf('"', r.indexOf(':', i) + 1);
        int b = a >= 0 ? r.indexOf('"', a + 1) : -1;
        if (a >= 0 && b > a) s = r.substring(a + 1, b);
    }
    snprintf(out, n, "%s", s.c_str());
    for (char *p = out; *p; p++)
        if (*p == '\r' || *p == '\n' || *p == '\t') *p = ' ';
}

// Notes the answer in the status when it differs from the last one (a plain 200 clears it).
static void noteReply(int code, const String &r)
{
    char msg[sizeof(status.shReply)] = "";
    if (code != 200) {
        if (r.length()) replySummary(r, msg, sizeof(msg));
        else snprintf(msg, sizeof(msg), code > 0 ? "HTTP %d, no text" : "no connection (%d)", code);
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    if (strcmp(msg, status.shReply) || code != status.shReplyCode) {
        snprintf(status.shReply, sizeof(status.shReply), "%s", msg);
        status.shReplyCode = code;
        status.shReplySeq++;
    }
    xSemaphoreGive(lock);
}

// 'reply' gets the answer's text, cut to REPLY_MAX.
static int shPut(const char *path, const char *data, size_t len, const char *date, const UploaderCfg &c,
                 String *reply = nullptr)
{
    String url = String(SH_API_BASE) + path, ua = String("t-deck-sonde-tracker/") + c.swVersion;
    HttpsReq r;
    r.method = "PUT";
    r.url = url.c_str();
    r.data = (const uint8_t *)data;
    r.len = len;
    r.ua = ua.c_str();
    r.date = date;
    r.json = true;
    r.waitMs = 2000; // the Wi-Fi task never waits long for the internet task's session
    r.reply = reply;
    r.replyMax = REPLY_MAX;
    int code = https_call(r);
    return code == HTTPS_BUSY || code == HTTPS_NO_URL ? -1 : code;
}

// Mobile station with a minimum distance: false while it stays within minMoveM of the last position sent.
static bool movedEnough(const UploaderCfg &c, bool sentBefore, double lastLat, double lastLon, double lat, double lon)
{
    return !c.mobile || !c.minMoveM || !sentBefore || geo_distance_m(lastLat, lastLon, lat, lon) >= c.minMoveM;
}

// Station position: a chase car every 30 s, a fixed station every 30 min (and at once when that changes).
static void stationUpdate(const UploaderCfg &c)
{
    static uint32_t lastMs = 0;
    static bool lastMobile = false;
    static double sentLat = 0, sentLon = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool ok = posValid;
    double lat = posLat, lon = posLon, alt = posAlt;
    xSemaphoreGive(lock);
    if (!c.shPos || !ok || (SH_DEV && !SH_MOCK)) return; // a "dev" build stores nothing: no station either
    uint32_t every = c.mobile ? c.mobileMs : c.fixedMs;
    if (lastMs && c.mobile == lastMobile && millis() - lastMs < every) return;
    if (c.mobile == lastMobile && !movedEnough(c, lastMs != 0, sentLat, sentLon, lat, lon)) return;
    lastMs = millis() | 1;
    sentLat = lat;
    sentLon = lon;
    lastMobile = c.mobile;
    char j[320];
    size_t n = sh_listener_json(j, sizeof(j), c.callsign, c.swVersion, lat, lon, alt, c.mobile);
    if (!n) return;
    String r;
    int code = shPut("/listeners", j, n, nullptr, c, &r);
    if (code != 200) noteReply(code, r);
    xSemaphoreTake(lock, portMAX_DELAY);
    status.stationHttp = code;
    if (code >= 200 && code < 300) status.stationSent++;
    xSemaphoreGive(lock);
}

// ---- APRS-IS clients on port 14580 (only what we send, nothing is received): radiosondy.info gets the sonde
// objects and the station beacon, aprs.fi (main network) only the station position with its comment ----
struct AprsLink {
    bool fi; // which status fields it reports to
    WiFiClient cli;
    uint32_t retryAt = 0, retryMs = APRS_RETRY_MIN_MS;
    uint32_t beaconMs = 0; // last station position sent
    char beaconSym = 0;
    char beaconText[61] = "";
    double sentLat = 0, sentLon = 0; // position of the last beacon
    explicit AprsLink(bool isFi) : fi(isFi) {}
};
static AprsLink rsLink(false), fiLink(true);

static const char *fiHost() { return strcmp(APRS_HOST, APRS_HOST_REAL) ? APRS_HOST : APRSFI_HOST_REAL; }

static void aprsStatus(AprsLink &l, bool connected, bool verified)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    (l.fi ? status.fiConnected : status.aprsConnected) = connected;
    (l.fi ? status.fiVerified : status.aprsVerified) = verified;
    xSemaphoreGive(lock);
}

static void aprsClose(AprsLink &l)
{
    if (l.cli.connected()) l.cli.stop();
    aprsStatus(l, false, false);
}

// Login, then the server's "# logresp CALL verified" within 5 s. False: not connected or not verified.
static bool aprsLogin(AprsLink &l, const UploaderCfg &c)
{
    bool up = l.cli.connect(l.fi ? fiHost() : APRS_HOST, APRS_PORT, 5000);
    net_mark(up);
    if (!up) return false;
    char line[128];
    size_t n = aprs_login(line, sizeof(line), c.callsign, c.aprsPass, c.swVersion);
    if (!n) return false;
    l.cli.write((const uint8_t *)line, n);
    String got;
    for (uint32_t t0 = millis(); millis() - t0 < 5000 && l.cli.connected();) {
        if (!l.cli.available()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        char ch = (char)l.cli.read();
        if (ch == '\n') {
            if (got.startsWith("# logresp")) {
                bool ok = got.indexOf(" unverified") < 0 && got.indexOf(" verified") >= 0;
                aprsStatus(l, true, ok);
                return ok;
            }
            got = "";
        } else if (ch != '\r' && got.length() < 200) {
            got += ch;
        }
    }
    return false;
}

// True when the whole line went out: only then the caller counts it as sent.
static bool aprsWrite(AprsLink &l, const char *line, size_t n, bool beacon)
{
    if (!n || l.cli.write((const uint8_t *)line, n) != n) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (l.fi) status.fiBeacons++;
    else if (beacon) status.aprsBeacons++;
    else status.aprsSent++;
    xSemaphoreGive(lock);
    return true;
}

// Connected and logged in (again if needed, waiting longer after each failure). False: not now.
static bool aprsUp(AprsLink &l, const UploaderCfg &c)
{
    if (!l.cli.connected()) {
        aprsStatus(l, false, false);
        if (l.retryAt && (int32_t)(millis() - l.retryAt) < 0) return false;
        if (!aprsLogin(l, c)) { // wrong passcode or server away
            l.cli.stop();
            l.retryAt = (millis() + l.retryMs) | 1;
            l.retryMs = l.retryMs * 2 > APRS_RETRY_MAX_MS ? APRS_RETRY_MAX_MS : l.retryMs * 2;
            return false;
        }
        l.retryMs = APRS_RETRY_MIN_MS;
        l.retryAt = 0;
        l.beaconMs = 0;
    }
    while (l.cli.available()) l.cli.read(); // server comments and keepalives
    return true;
}

// Station position: every 'every' ms, at once when the icon or the comment changes (at most every 30 s).
static void aprsBeacon(AprsLink &l, const UploaderCfg &c, uint32_t every, const char *comment)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool ok = posValid;
    double lat = posLat, lon = posLon;
    AprsMotion m = {c.mobile, posCrs, posSpd, true, posAlt};
    xSemaphoreGive(lock);
    if (!ok) return;
    char sym = c.mobile ? c.mobileSym : APRS_SYM_FIXED;
    bool changed = sym != l.beaconSym || strcmp(comment ? comment : "", l.beaconText);
    if (l.beaconMs && millis() - l.beaconMs < (changed ? 30000UL : every)) return;
    if (!changed && !movedEnough(c, l.beaconMs != 0, l.sentLat, l.sentLon, lat, lon)) return;
    char line[200];
    if (!aprsWrite(l, line, aprs_beacon(line, sizeof(line), c.callsign, lat, lon, sym, c.swVersion, comment, &m), true))
        return; // not sent: tried again at the next pass, not after a whole period
    l.beaconMs = millis() | 1;
    l.sentLat = lat;
    l.sentLon = lon;
    l.beaconSym = sym;
    snprintf(l.beaconText, sizeof(l.beaconText), "%s", comment ? comment : "");
}

static void aprsFiService(const UploaderCfg &c)
{
    if (!aprsUp(fiLink, c)) return;
    uint32_t every = c.mobile ? (c.mobileMs < APRSFI_MIN_MS ? APRSFI_MIN_MS : c.mobileMs) : c.fixedMs;
    aprsBeacon(fiLink, c, every, c.aprsFiText);
}

static void aprsService(const UploaderCfg &c)
{
    if (!aprsUp(rsLink, c)) return;
    char line[300];
    for (int i = 0; i < APRS_SLOTS; i++) {
        xSemaphoreTake(lock, portMAX_DELAY);
        AprsSlot &s = aprsSlots[i];
        if (s.used && millis() - s.gotMs > APRS_STALE_MS) s.used = false;
        bool due = s.used && s.fresh && (!s.sentMs || millis() - s.sentMs >= APRS_OBJECT_MS);
        ShFrame f;
        uint32_t prevSent = s.sentMs;
        if (due) {
            f = s.f;
            s.fresh = false;
            s.sentMs = millis() | 1;
        }
        xSemaphoreGive(lock);
        if (due && !aprsWrite(rsLink, line, aprs_object(line, sizeof(line), c.callsign, f), false)) {
            xSemaphoreTake(lock, portMAX_DELAY); // not sent: the object stays due
            if (aprsSlots[i].sentMs != prevSent) {
                aprsSlots[i].fresh = true;
                aprsSlots[i].sentMs = prevSent;
            }
            xSemaphoreGive(lock);
        }
    }
    if (c.rsPos) aprsBeacon(rsLink, c, c.mobile ? c.mobileMs : c.fixedMs, nullptr);
}

// Sends up to BATCH_MAX queued frames as one JSON array.
static void uploadBatch(const UploaderCfg &c)
{
    ShFrame f;
    size_t k = 0;
    int n = 0;
    uint32_t lastRx = 0;
    if (!body) return; // no buffer: nothing goes to SondeHub (the rest of the task runs anyway)
    body[k++] = '[';
    while (n < BATCH_MAX && xQueueReceive(queue, &f, 0) == pdTRUE) {
        if (n) body[k++] = ',';
        size_t w = sh_frame_json(body + k, BODY_MAX - k - 2, f, c.callsign, c.swVersion, SH_DEV_MARK);
        if (!w) {
            if (n) k--;
            break;
        }
        k += w;
        n++;
        lastRx = f.rxUnix;
    }
    body[k++] = ']';
    body[k] = 0;
    if (!n) return;

    time_t now = time(nullptr);
    char date[32];
    sh_http_date(now > 1600000000 ? (uint32_t)now : lastRx, date); // NTP when available, else sonde time

    String r;
    int code = shPut("/sondes/telemetry", body, k, date, c, &r);
    noteReply(code, r);
    xSemaphoreTake(lock, portMAX_DELAY);
    if (lastBody && lastReply) {
        memcpy(lastBody, body, k + 1);
        snprintf(lastReply, REPLY_MAX + 1, "%s", r.c_str());
        lastCode = code;
    }
    status.lastHttp = code;
    if (code >= 200 && code < 300) status.sent += n;
    else status.failed += n;
    xSemaphoreGive(lock);
}

// Only when asked: the stored network is not touched, the list just offers names to choose from.
static void scanNow()
{
    bool wasOff = WiFi.getMode() == WIFI_OFF;
    if (wasOff) WiFi.mode(WIFI_STA);
    else if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false); // a connection attempt would spoil the scan
    int n = WiFi.scanNetworks(false, false);
    WifiNet found[WIFI_SCAN_MAX];
    int k = 0;
    for (int i = 0; i < n; i++) {
        String name = WiFi.SSID(i);
        if (!name.length()) continue; // hidden network
        int8_t rssi = (int8_t)WiFi.RSSI(i);
        int j = 0;
        while (j < k && strcmp(found[j].ssid, name.c_str())) j++;
        if (j < k) { // the same network on another access point: keep the strongest
            if (rssi > found[j].rssi) found[j].rssi = rssi;
            continue;
        }
        if (k == WIFI_SCAN_MAX) {
            int weak = 0;
            for (int m = 1; m < k; m++)
                if (found[m].rssi < found[weak].rssi) weak = m;
            if (rssi <= found[weak].rssi) continue;
            j = weak;
        } else {
            j = k++;
        }
        snprintf(found[j].ssid, sizeof(found[j].ssid), "%s", name.c_str());
        found[j].rssi = rssi;
        found[j].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    for (int i = 1; i < k; i++) // strongest first
        for (int j = i; j > 0 && found[j].rssi > found[j - 1].rssi; j--) {
            WifiNet t = found[j];
            found[j] = found[j - 1];
            found[j - 1] = t;
        }
    if (wasOff) WiFi.mode(WIFI_OFF);
    xSemaphoreTake(lock, portMAX_DELAY);
    memcpy(scanNets, found, sizeof(WifiNet) * k);
    scanCount = n < 0 ? -2 : k;
    xSemaphoreGive(lock);
}

static void task(void *)
{
    uint32_t lastBegin = 0, lastUpload = 0;
    WiFi.onEvent(onWifiEvent);
    bool ntpStarted = false;
    char joined[33 + 64] = ""; // network and password of the last WiFi.begin
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        UploaderCfg c;
        xSemaphoreTake(lock, portMAX_DELAY);
        c = cfg;
        bool scan = scanWanted;
        scanWanted = false;
        xSemaphoreGive(lock);
        if (scan) {
            scanNow();
            lastBegin = 0; // reconnect at once if a network is set
        }

        if ((!c.enabled && !c.aprs && !c.aprsFi && !c.wifiDebug && !c.predSrv) || !c.ssid[0]) {
            aprsClose(rsLink);
            aprsClose(fiLink);
            if (WiFi.getMode() != WIFI_OFF) wifiDrop(true);
            if (queue) xQueueReset(queue);
            ntpStarted = false;
            lastBegin = 0;
            setStatus(false, false);
            continue;
        }
        char want[sizeof(joined)];
        snprintf(want, sizeof(want), "%s\n%s", c.ssid, c.pass);
        if (lastBegin && strcmp(want, joined)) { // network changed in the settings
            wifiDrop(false);
            lastBegin = 0;
        }
        if (WiFi.status() != WL_CONNECTED) {
            setStatus(true, false);
            if (lastBegin == 0 || millis() - lastBegin > WIFI_RETRY_MS) {
                // A new attempt starts from a Wi-Fi switched off: after some losses WiFi.begin alone never joins
                // again (25/11/2026: "connecting" until a reboot).
                if (lastBegin) {
                    wifiDrop(true);
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
                lastBegin = millis() | 1;
                wifiJoins++;
                memcpy(joined, want, sizeof(joined));
                WiFi.mode(WIFI_STA);
                WiFi.setSleep(c.wifiSleep); // a setting, off by default
                WiFi.begin(c.ssid, c.pass);
            }
            continue;
        }
        setStatus(true, true);
        if (WiFi.getSleep() != c.wifiSleep) WiFi.setSleep(c.wifiSleep); // changed in the settings
        // Says connected, but no server has answered for minutes: the link is dead (seen 25/11/2026, a reboot
        // was needed). Drop it and join again.
        uint32_t okMs = netOkMs;
        if (netFails >= NET_DEAD_FAILS && millis() - connSinceMs > NET_DEAD_MS &&
            (!okMs || millis() - okMs > NET_DEAD_MS)) {
            if (dnsAlive()) { // only some servers are down (e.g. radiosondy.info): the link is fine, keep it
                net_mark(true);
            } else {
                aprsClose(rsLink);
                aprsClose(fiLink);
                wifiDrop(false);
                lastBegin = 0;
                netResets++;
                setStatus(true, false);
                continue;
            }
        }
        if (c.aprs) aprsService(c);
        else aprsClose(rsLink);
        if (c.aprsFi && c.fiPos) aprsFiService(c); // aprs.fi gets only the station position
        else aprsClose(fiLink);
#if PRED_SERVER
        if (c.predSrv) {
            if (!ntpStarted) { // its HTTPS needs the clock
                configTime(0, 0, "pool.ntp.org");
                ntpStarted = true;
            }
            predsrv_service(true);
        }
#endif
        if (!c.enabled) { // nothing goes to SondeHub
            xQueueReset(queue);
            continue;
        }
        if (!ntpStarted) {
            configTime(0, 0, "pool.ntp.org");
            ntpStarted = true;
        }
        stationUpdate(c);
        UBaseType_t waiting = uxQueueMessagesWaiting(queue);
        if (waiting && (waiting >= BATCH_MAX || millis() - lastUpload > BATCH_INTERVAL_MS)) {
            uploadBatch(c);
            lastUpload = millis();
        }
    }
}

static TaskHandle_t taskHandle = nullptr;
uint32_t uploader_stack_free() { return taskHandle ? uxTaskGetStackHighWaterMark(taskHandle) : 0; }

void uploader_begin()
{
    lock = xSemaphoreCreateMutex();
    // SondeHub upload buffers in PSRAM: the upload waits for SondeHub's approval and the internal RAM is for
    // the Wi-Fi. The task does not depend on them (APRS, prediction server and debug pages run anyway).
    static StaticQueue_t qState;
    uint8_t *qStore = (uint8_t *)ps_malloc(QUEUE_LEN * sizeof(ShFrame));
    if (qStore) queue = xQueueCreateStatic(QUEUE_LEN, sizeof(ShFrame), qStore, &qState);
    body = (char *)ps_malloc(BODY_MAX);
    lastBody = (char *)ps_calloc(1, BODY_MAX);
    lastReply = (char *)ps_calloc(1, REPLY_MAX + 1);
    if (!queue) return;
    if (!lock) return;
    WiFi.mode(WIFI_OFF);
    xTaskCreatePinnedToCore(task, "uploader", 12288, nullptr, 1, &taskHandle, 0);
}

void uploader_configure(const UploaderCfg &c)
{
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    cfg = c;
    xSemaphoreGive(lock);
}

void uploader_position(bool valid, double lat, double lon, double alt, float speedKmh, float courseDeg)
{
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    posValid = valid;
    posLat = lat;
    posLon = lon;
    posAlt = alt;
    posSpd = speedKmh;
    posCrs = courseDeg;
    xSemaphoreGive(lock);
}

const char *uploader_target()
{
    if (strcmp(SH_API_BASE, SH_API_REAL) && strcmp(APRS_HOST, APRS_HOST_REAL)) return "mock";
    return SH_DEV ? "dev" : "real";
}

bool uploader_last_upload(String &b, String &r, int &code)
{
    if (!lock || !lastBody || !lastBody[0]) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    b = lastBody;
    r = lastReply;
    code = lastCode;
    xSemaphoreGive(lock);
    return true;
}

void uploader_aprs_push(const ShFrame &f)
{
    if (!lock || sh_serial_blocked(f.serial)) return; // the bench simulators never leave the T-Deck
    xSemaphoreTake(lock, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < APRS_SLOTS && slot < 0; i++)
        if (aprsSlots[i].used && !strcmp(aprsSlots[i].f.serial, f.serial)) slot = i;
    for (int i = 0; i < APRS_SLOTS && slot < 0; i++)
        if (!aprsSlots[i].used) slot = i;
    if (slot < 0) { // all taken: the sonde heard least recently gives way
        slot = 0;
        for (int i = 1; i < APRS_SLOTS; i++)
            if (aprsSlots[i].gotMs < aprsSlots[slot].gotMs) slot = i;
        aprsSlots[slot].used = false;
    }
    AprsSlot &s = aprsSlots[slot];
    if (!s.used) s.sentMs = 0;
    s.used = s.fresh = true;
    s.gotMs = millis();
    s.f = f;
    xSemaphoreGive(lock);
}

void uploader_scan_start()
{
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    scanWanted = true;
    scanCount = -1;
    xSemaphoreGive(lock);
}

int uploader_scan_result(WifiNet *out, int max)
{
    if (!lock) return -2;
    xSemaphoreTake(lock, portMAX_DELAY);
    int n = scanCount;
    if (n > 0) memcpy(out, scanNets, sizeof(WifiNet) * (n < max ? n : max));
    xSemaphoreGive(lock);
    return n > max ? max : n;
}

bool uploader_push(const ShFrame &f)
{
    if (!queue || sh_serial_blocked(f.serial)) return false; // the bench simulators never leave the T-Deck
    if (xQueueSend(queue, &f, 0) == pdTRUE) return true;
    xSemaphoreTake(lock, portMAX_DELAY);
    status.dropped++;
    xSemaphoreGive(lock);
    return false;
}

void uploader_status(UploaderStatus *st)
{
    if (!lock) {
        *st = {};
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    *st = status;
    xSemaphoreGive(lock);
}
