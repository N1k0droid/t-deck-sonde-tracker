// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include "netsvc.h"
#include "https_call.h"
#include <WiFi.h>
#include "uploader.h"
#include <esp32s3/rom/miniz.h>
#include <time.h>

#define SH_HOST SH_API_REAL // reads always from the real SondeHub (SH_API_BASE may point to a test server)
// SondeHub asks not to poll its API regularly: nearby sondes every 5 min, every 60 s only when needed
// (see NetRequest.sondesFast); the prediction of the sonde followed every 3 min (it is not redone more often).
#define SONDES_EVERY_MS (5UL * 60 * 1000)
#define SONDES_FAST_MS 60000 // SondeHub asks not to poll the API: once a minute at most
#define PRED_EVERY_MS (3UL * 60 * 1000)
#define SONDES_RADIUS_M 300000
#define SONDES_LAST_S 10800 // 3 hours: landed sondes stay listed for a while
#define BODY_MAX (512 * 1024)
#define LANDED_EVERY_MS 120000
#define LANDED_RADIUS_M 400000
#define SUMMARY_EVERY_MS 120000
#define SITES_EVERY_MS (24UL * 3600 * 1000)
#define SITES_RETRY_MS (10UL * 60 * 1000) // no site list yet (failed request): try again sooner
#define SITES_RADIUS_KM 400
#define LAUNCH_REOPEN_MS (30UL * 60 * 1000) // the Launches tab opened again within this: kept as it is
#define TAWHIRI_FAILED_MAX 32

static SemaphoreHandle_t lock;
static NetRequest req = {};
static NetResult *resp = nullptr; // in PSRAM

// HTTP body collected in PSRAM (writeToStream also undoes chunked transfer encoding).
class MemStream : public Stream {
public:
    uint8_t *buf = nullptr;
    size_t len = 0, cap = 0;
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *d, size_t n) override
    {
        if (len + n > cap) {
            size_t nc = cap ? cap * 2 : 16384;
            while (nc < len + n) nc *= 2;
            if (nc > BODY_MAX) return 0;
            uint8_t *nb = (uint8_t *)ps_realloc(buf, nc + 1);
            if (!nb) return 0;
            buf = nb;
            cap = nc;
        }
        memcpy(buf + len, d, n);
        len += n;
        buf[len] = 0;
        return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    void clear()
    {
        free(buf);
        buf = nullptr;
        len = cap = 0;
    }
};

// GET into body; gzip answers are inflated. Returns the HTTP status (negative on connection errors).
static int httpGet(const String &url, MemStream &body)
{
    body.clear();
    String enc;
    HttpsReq r;
    r.url = url.c_str();
    r.ua = "t-deck-sonde-tracker (read-only)";
    r.keep = "Content-Encoding";
    r.kept = &enc;
    r.timeoutMs = 15000;
    r.connectMs = 10000;
    r.body = &body;
    int code = https_call(r); // public read-only data: no certificate check
    bool gz = enc.indexOf("gzip") >= 0;
    if (code == 200 && gz) {
        size_t off, dl;
        uint32_t isz;
        if (!sh_gzip_payload(body.buf, body.len, &off, &dl, &isz) || isz > BODY_MAX) return -102;
        uint8_t *out = (uint8_t *)ps_malloc(isz + 1);
        if (!out) return -103;
        // The decompressor state (about 11 KB) in PSRAM, not on this task's stack: the stack went from 24 to 16 KB.
        tinfl_decompressor *dec = (tinfl_decompressor *)ps_malloc(sizeof(tinfl_decompressor));
        if (!dec) {
            free(out);
            return -103;
        }
        tinfl_init(dec);
        size_t inSz = dl, n = isz;
        tinfl_status st = tinfl_decompress(dec, body.buf + off, &inSz, out, out, &n, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        free(dec);
        if (st != TINFL_STATUS_DONE || n != isz) {
            free(out);
            return -104;
        }
        out[n] = 0;
        free(body.buf);
        body.buf = out;
        body.len = body.cap = n;
    }
    return code;
}

// Every answer is published the same way: under the lock, count the request, let the caller store its
// part of the result, then (when the shared result changed) bump the version that the main loop watches.
static void publishBegin(int code, int kind)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    resp->requests++;
    resp->perKind[kind]++;
    resp->lastHttp = code;
    if (code != 200) resp->failures++;
}

static void publishEnd(bool changed = true)
{
    if (changed) resp->version++;
    xSemaphoreGive(lock);
}

// Sondes heard within radiusM of (lat, lon) in the last lastS seconds; -1 when the request failed.
static int fetchSondes(MemStream &body, double lat, double lon, long radiusM, unsigned long lastS, ShSonde *out, int max,
                       int *code)
{
    char url[160];
    // The query point is rounded to 0.1 degree (about 10 km): enough for a search of hundreds of km.
    snprintf(url, sizeof(url), SH_HOST "/sondes?lat=%.1f&lon=%.1f&distance=%ld&last=%lu", lat, lon, radiusM, lastS);
    *code = httpGet(url, body);
    int n = *code == 200 ? sh_parse_sondes((const char *)body.buf, body.len, out, max) : -1;
    body.clear();
    return n;
}

static void task(void *)
{
    MemStream body;
    uint32_t lastSondes = 0, lastPred = 0;
    char lastFollow[12] = "";
    const size_t scratchLen = 128 * 1024; // unescaped trajectory, in PSRAM
    char *scratch = (char *)ps_malloc(scratchLen);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        NetRequest r;
        xSemaphoreTake(lock, portMAX_DELAY);
        r = req;
        xSemaphoreGive(lock);
        if (!r.enabled || WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) continue;
        uint32_t now = millis();

        if (r.haveCentre && (!lastSondes || now - lastSondes > (r.sondesFast ? SONDES_FAST_MS : SONDES_EVERY_MS))) {
            lastSondes = now;
            static ShSonde tmp[NET_MAX_SONDES];
            int code, n = fetchSondes(body, r.lat, r.lon, SONDES_RADIUS_M, SONDES_LAST_S, tmp, NET_MAX_SONDES, &code);
            publishBegin(code, NET_Q_SONDES);
            if (n >= 0) {
                memcpy(resp->sondes, tmp, sizeof(ShSonde) * n);
                resp->nSondes = n;
                resp->sondesMs = millis();
            }
            publishEnd();
        }

        bool newFollow = strcmp(r.follow, lastFollow) != 0;
        if (r.follow[0] && !r.followStale && (newFollow || !lastPred || now - lastPred > PRED_EVERY_MS)) {
            lastPred = now;
            snprintf(lastFollow, sizeof(lastFollow), "%s", r.follow);
            String url = String(SH_HOST "/predictions?vehicles=") + r.follow;
            int code = httpGet(url, body);
            static ShPrediction p;
            bool ok = code == 200 && scratch &&
                      sh_parse_prediction((const char *)body.buf, body.len, r.follow, &p, scratch, scratchLen);
            body.clear();
            publishBegin(code, NET_Q_PRED);
            if (ok) {
                resp->pred = p;
                resp->predMs = millis();
            } else if (code == 200 || newFollow) {
                resp->pred.ok = false; // no prediction for this sonde (any more)
            }
            publishEnd();
        } else if (!r.follow[0] && lastFollow[0]) {
            lastFollow[0] = 0;
            xSemaphoreTake(lock, portMAX_DELAY);
            resp->pred.ok = false;
            resp->version++;
            xSemaphoreGive(lock);
        }

        // Landed / seen sondes over a longer period (asked only while that list is on screen).
        static uint32_t lastLanded = 0, landedPeriod = 0;
        if (r.landedLast && r.haveCentre &&
            (r.landedLast != landedPeriod || !lastLanded || now - lastLanded > LANDED_EVERY_MS)) {
            lastLanded = now;
            landedPeriod = r.landedLast;
            static ShSonde tmp[NET_MAX_LANDED];
            int code, n = fetchSondes(body, r.lat, r.lon, LANDED_RADIUS_M, r.landedLast, tmp, NET_MAX_LANDED, &code);
            publishBegin(code, NET_Q_LANDED);
            if (n >= 0) {
                memcpy(resp->landed, tmp, sizeof(ShSonde) * n);
                resp->nLanded = n;
                resp->landedLast = r.landedLast;
                resp->landedMs = millis();
            }
            publishEnd();
        }

        // Summary of one serial (search, details).
        static uint32_t lastSummary = 0;
        static char summaryOf[12] = "";
        if (r.summary[0] && (strcmp(r.summary, summaryOf) || now - lastSummary > SUMMARY_EVERY_MS)) {
            lastSummary = now;
            snprintf(summaryOf, sizeof(summaryOf), "%s", r.summary);
            String url = String(SH_HOST "/sondes/summary?serial=") + r.summary;
            int code = httpGet(url, body);
            static ShSummary s;
            bool ok = code == 200 && sh_parse_summary((const char *)body.buf, body.len, r.summary, &s);
            if (!ok) memset(&s, 0, sizeof(s));
            body.clear();
            publishBegin(code, NET_Q_SUMMARY);
            resp->summary = s;
            snprintf(resp->summaryFor, sizeof(resp->summaryFor), "%s", r.summary);
            resp->summaryMs = millis();
            publishEnd();
        }

        // Next launches, only while the Launches tab is open: sites once a day, the schedule when the tab is
        // opened (again after LAUNCH_REOPEN_MS, or with another horizon), then one Tawhiri prediction per pass.
        // A launch Tawhiri refused (beyond the weather model) is never asked again.
        static uint32_t sitesMs = 0, launchesBuiltMs = 0, builtGen = 0, builtHorizon = 0;
        static ShSite sites[NET_MAX_SITES];
        static int nSites = 0;
        static NetLaunch launch[NET_MAX_LAUNCH];
        static int nLaunch = 0;
        static struct {
            float lat, lon;
            uint32_t time;
        } failed[TAWHIRI_FAILED_MAX];
        static int nFailed = 0, nextFailed = 0;
        if (r.launches && r.haveCentre) {
            static bool sitesOk = false;
            if (!sitesMs || now - sitesMs > (sitesOk ? SITES_EVERY_MS : SITES_RETRY_MS)) {
                sitesMs = now;
                int code = httpGet(SH_HOST "/sites", body);
                sitesOk = code == 200;
                if (code == 200) nSites = sh_parse_sites((const char *)body.buf, body.len, r.lat, r.lon, SITES_RADIUS_KM,
                                                         sites, NET_MAX_SITES);
                body.clear();
                launchesBuiltMs = 0; // rebuild the schedule
                publishBegin(code, NET_Q_SITES);
                publishEnd(false); // the sites reach the result with the schedule below
            }
            uint32_t t = (uint32_t)time(nullptr);
            bool reopened = r.launchGen != builtGen;
            if (reopened && launchesBuiltMs && now - launchesBuiltMs < LAUNCH_REOPEN_MS &&
                r.launchHorizonS == builtHorizon)
                builtGen = r.launchGen, reopened = false; // just seen: keep what there is
            if (!launchesBuiltMs || reopened || r.launchHorizonS != builtHorizon) {
                launchesBuiltMs = now | 1;
                builtGen = r.launchGen;
                builtHorizon = r.launchHorizonS;
                static ShLaunch L[NET_MAX_LAUNCH];
                nLaunch = sh_next_launches(sites, nSites, t, t + r.launchHorizonS, L, NET_MAX_LAUNCH);
                for (int i = 0; i < nLaunch; i++) {
                    memset(&launch[i], 0, sizeof(NetLaunch));
                    launch[i].site = L[i].site;
                    launch[i].time = L[i].time;
                }
                xSemaphoreTake(lock, portMAX_DELAY);
                memcpy(resp->sites, sites, sizeof(sites));
                resp->nSites = nSites;
                memcpy(resp->launch, launch, sizeof(launch));
                resp->nLaunch = nLaunch;
                resp->version++;
                xSemaphoreGive(lock);
            }
            static uint32_t tawhiriRetryMs = 0; // after a network error: wait, then ask the same launch again
            for (int i = 0; i < nLaunch && (int32_t)(millis() - tawhiriRetryMs) >= 0; i++) {
                if (launch[i].tried) continue;
                launch[i].tried = true;
                const ShSite &s = sites[launch[i].site];
                bool refused = false;
                for (int k = 0; k < nFailed && !refused; k++)
                    refused = failed[k].time == launch[i].time && failed[k].lat == (float)s.lat && failed[k].lon == (float)s.lon;
                if (refused) continue;
                time_t lt = launch[i].time;
                struct tm u;
                gmtime_r(&lt, &u);
                char url[320];
                snprintf(url, sizeof(url),
                         SH_HOST "/tawhiri?launch_latitude=%.4f&launch_longitude=%.4f&launch_datetime=%04d-%02d-%02dT%02d:%02d:00Z"
                                 "&ascent_rate=%.1f&burst_altitude=%.0f&descent_rate=%.1f&launch_altitude=%.0f&profile=standard_profile",
                         s.lat, s.lon < 0 ? s.lon + 360 : s.lon, u.tm_year + 1900, u.tm_mon + 1, u.tm_mday, u.tm_hour,
                         u.tm_min, s.ascent, s.burst, s.descent, s.alt);
                int code = httpGet(url, body);
                ShFlightPred p;
                bool ok = code == 200 && sh_parse_tawhiri((const char *)body.buf, body.len, &p);
                if (ok) launch[i].pred = p;
                if (code <= 0) { // no answer (network): not a refusal, asked again in a minute
                    launch[i].tried = false;
                    tawhiriRetryMs = millis() + 60000;
                }
                if (!ok && code > 0) { // an answer, but no prediction: remembered, not asked again
                    failed[nextFailed].lat = (float)s.lat;
                    failed[nextFailed].lon = (float)s.lon;
                    failed[nextFailed].time = launch[i].time;
                    nextFailed = (nextFailed + 1) % TAWHIRI_FAILED_MAX;
                    if (nFailed < TAWHIRI_FAILED_MAX) nFailed++;
                }
                body.clear();
                publishBegin(code, NET_Q_TAWHIRI); // beyond the weather model range Tawhiri answers with an error
                resp->launch[i] = launch[i];
                resp->launchMs = millis();
                publishEnd();
                break; // one per pass: the other requests keep their pace
            }
        }
    }
}

static TaskHandle_t taskHandle = nullptr;
uint32_t netsvc_stack_free() { return taskHandle ? uxTaskGetStackHighWaterMark(taskHandle) : 0; }

void netsvc_begin()
{
    lock = xSemaphoreCreateMutex();
    resp = (NetResult *)ps_calloc(1, sizeof(NetResult));
    if (!lock || !resp) return; // no internet services; netsvc_request/result do nothing
    // Core 0 with Wi-Fi; inflating a gzip answer needs about 11 KB of stack (ROM tinfl).
    xTaskCreatePinnedToCore(task, "netsvc", 16384, nullptr, 1, &taskHandle, 0);
}

void netsvc_request(const NetRequest &r)
{
    if (!lock || !resp) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    req = r;
    xSemaphoreGive(lock);
}

bool netsvc_result(NetResult *out, uint32_t seen)
{
    if (!lock || !resp) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool changed = resp->version != seen;
    if (changed) *out = *resp;
    xSemaphoreGive(lock);
    return changed;
}
