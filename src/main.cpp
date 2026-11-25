// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Main program: display, keyboard, pages, settings, GPS, SD logs, internet.
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <functional>
#include <StreamString.h>
#include "board_pins.h"
#include "sx1262.h"
#include "capture.h"
#include "dfm.h"
#include "m10m20.h"
#include "imet54.h"
#include "presets.h"
#include "wifilist.h"
#include "sondetype.h"
#include "rs41.h"
#include "rs_ecc.h"
#include "nmea.h"
#include "geo.h"
#include "tracklog.h"
#include "scan.h"
#include "audio.h"
#include "tzone.h"
#include "timeutil.h"
#include "predict.h"
#include "sondehub.h"
#include "aprs.h"
#include "uploader.h"
// Optional link to a landing prediction server (separate build with -DPRED_SERVER=1 and its module).
#ifndef PRED_SERVER
#define PRED_SERVER 0
#endif
#ifndef PRED_SERVER_ALLOW_HTTP
#define PRED_SERVER_ALLOW_HTTP 0
#endif
#if PRED_SERVER
#include "predserver.h"
#endif
#include "mapview.h"
#include "mysondy.h"
#include "rdzjson.h"
#include "ble_link.h"
#include "rdz_link.h"
#include "input.h"
#include "ubx.h"
#include "netsvc.h"
#include "strout.h"
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <qrcode.h>
#include <time.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include "webui_gz.h" // the web page, gzip-compressed at build time by tools/web_gz.py

#define FW_VERSION "0.95"
// Build number of the beta: a new one for every firmware handed out, never two files with the same name
// (user's rule, 24/11/2026). 0 = none (from 1.0.0). Shown everywhere as FW_NAME, e.g. "0.93-beta2".
#define FW_BETA 4
#define FW_STR2(x) #x
#define FW_STR(x) FW_STR2(x)
#if FW_BETA > 0
#define FW_NAME_B FW_VERSION "-beta" FW_STR(FW_BETA)
#else
#define FW_NAME_B FW_VERSION
#endif
#if PRED_SERVER_ALLOW_HTTP
#define FW_NAME FW_NAME_B "-test" // bench build pointed at the PC mock
#else
#define FW_NAME FW_NAME_B
#endif
#define BW_DEFAULT 5 // receiver bandwidth 14.6 kHz
#define KB_ADDR 0x55

// ---- Themes (RGB565), chosen in the settings ----
// "Desert" is Navy with red and blue swapped, except warnings and alarms (orange, red: they must not turn
// blue); "Sun" is black on white for direct sunlight.
#define RB_SWAP(c) ((((c) & 0x1F) << 11) | ((c) & 0x07E0) | (((c) >> 11) & 0x1F))
struct Theme {
    const char *name;
    uint16_t bg, panel, line, label, text, dim, accent, good, warn, bad, pos, pred, msg;
};
static const Theme THEMES[] = {
    {"Desert", RB_SWAP(0x0863), RB_SWAP(0x1106), RB_SWAP(0x322B), RB_SWAP(0x7C74), 0xFFFF, RB_SWAP(0xCE9C),
     RB_SWAP(0x3E5F), RB_SWAP(0x3E8D), 0xFCE8, 0xFACB, RB_SWAP(0xFEC9), RB_SWAP(0xE35C),
     RB_SWAP(0xFFE0)},
    // Olive: the Desert family without blue touches, amber accents.
    {"Olive", 0x2902, 0x41E4, 0x6B08, 0xACAD, 0xFFFF, 0xE696, 0xFD80, 0x3E8D, 0xFCE8, 0xFACB, 0xFEC9, 0xE35C, 0xFF2F},
    {"Navy", 0x0863, 0x1106, 0x322B, 0x7C74, 0xFFFF, 0xCE9C, 0x3E5F, 0x3E8D, 0xFCE8, 0xFACB, 0xFEC9, 0xE35C, 0xFFE0},
    {"Sun", 0xFFFF, 0xE73C, 0x9CD3, 0x4208, 0x0000, 0x2104, 0x0019, 0x03E0, 0xCAC0, 0xB800, 0x7A60, 0x8010, 0x0013},
};
#define THEME_COUNT (int)(sizeof(THEMES) / sizeof(THEMES[0]))
static const Theme *th = &THEMES[0];
#define UI_BG (th->bg)         // page background
#define UI_PANEL (th->panel)   // cards and status bar
#define UI_LINE (th->line)     // outlines, inactive marks, selection
#define UI_LABEL (th->label)   // small captions
#define UI_TEXT (th->text)
#define UI_DIM (th->dim)
#define UI_ACCENT (th->accent)
#define UI_GOOD (th->good)     // green
#define UI_WARN (th->warn)     // orange
#define UI_BAD (th->bad)       // red
#define UI_POS (th->pos)       // coordinates
#define UI_PRED (th->pred)     // predicted landing
#define UI_MSG (th->msg)       // messages
// Files of this firmware on the SD card: one folder per build, public or private (user's request, 24/11/2026); /maps and
// /downloads stay at the root, shared with other firmware. sdLayout() fills the paths at boot.
#define SD_DIR_PUBLIC "/t-deck-sonde-tracker"
static char sdDir[32] = SD_DIR_PUBLIC;
static char logPath[48] = SD_DIR_PUBLIC "/log.txt", rawPath[48] = SD_DIR_PUBLIC "/raw.txt",
            trackDir[48] = SD_DIR_PUBLIC "/sonde", shLogDir[48] = SD_DIR_PUBLIC "/sondehub",
            fieldPath[56] = SD_DIR_PUBLIC "/field_debug.txt", aprsfiPath[48] = SD_DIR_PUBLIC "/aprsfi.txt",
            gpsdbPath[48] = SD_DIR_PUBLIC "/gpsdb.ubx", presetPath[48] = SD_DIR_PUBLIC "/presets.txt",
            wifiPath[48] = SD_DIR_PUBLIC "/wifi.txt";
#define UI_MSG_MS 6000
// Debug web page password (user "tdeck"): not real security, it only keeps the page closed to anyone on the
// same network. Changed in the settings (System tab).
// SondeHub telemetry upload: suspended until SondeHub approves the software (they asked for whole flights of
// every sonde type first). Meanwhile every frame is kept as SondeHub JSON on the SD card (/sondehub).
#define SH_TELEMETRY_APPROVED 0
#define SH_DOC_URL "https://github.com/N1k0droid/t-deck-sonde-tracker/blob/main/docs/SONDEHUB.md"
#define DEBUG_USER "tdeck" // fixed
#define DEBUG_PASS_DEFAULT "radiosonde"
#define SCAN_START_KHZ 400000
#define SCAN_STEP_KHZ 10
#define SCAN_BINS 601
#define SCAN_MAX_CAND 4
#define SCAN_DWELL_MS 3500  // per candidate: three frames worth of time
#define SCAN_MIN_ABOVE_DB 8 // over the sweep median
#define SCAN_CHUNK 40       // channels measured per loop pass: keys, GPS and screen stay alive
#define SCAN_SKIP_MS (10UL * 60 * 1000) // a channel tried without a decode is left alone this long
#define SCAN_SKIP_TOL_KHZ 10
#define AUTO_INVERT_MS 4000
#define AUTO_ROTATE_MS 2500 // per decoder and polarity while AUTO rotates: frames come every second (bench: 1-2 s to the first one)
#define GPS_FIX_MAX_AGE_MS 5000
#define GPS_BAUD_RETRY_MS 3000
#define LOST_SIGNAL_MS 30000
#define TRK_MAX 600
#define TRK_INTERVAL_MS 5000
#define LASTPOS_SAVE_MS 15000
#define HEADING_MIN_KMH 4.0f // below this the GPS course is noise: arrow goes north-up

static TFT_eSPI tft;
// Every screen is drawn whole into this PSRAM canvas and sent in one go: nothing of the
// previous screen can survive, and nothing flickers.
static TFT_eSprite gfx(&tft);

// "850 m" below 1 km, else km with 'dec' decimals.
static void fmtDist(char *b, size_t n, double m, int dec)
{
    if (m < 1000.0) snprintf(b, n, "%.0f m", m);
    else snprintf(b, n, "%.*f km", dec, m / 1000.0);
}

// Cuts a text so it fits maxW pixels in that font: from the end (names), or from the start (text being typed,
// whose end must stay in view).
static void fitText(char *s, uint8_t font, int maxW, bool keepEnd = false)
{
    size_t n = strlen(s);
    while (n > 1 && gfx.textWidth(s, font) > maxW) {
        if (keepEnd) memmove(s, s + 1, n--);
        else s[--n] = 0;
    }
}

// Sends the canvas to the display. Display, radio and SD card share the SPI bus, and the panel was seen
// ignoring (whole or half) frames sent right after a radio or SD transfer: a NOP command first takes
// that hit. Every push goes through here (screens, splash, boot steps, scan progress).
static void pushCanvas()
{
    tft.writecommand(TFT_NOP);
    gfx.pushSprite(0, 0);
}

// ---- Breadcrumbs and watchdog ----
// Before each step the main loop writes what it is doing into RTC memory, which survives a reset.
// If the loop ever stops for 15 s the task watchdog restarts the T-Deck, and the next start logs the
// last breadcrumb: the step that hung (e.g. the map tile being decoded).
#define CRUMB_MAGIC 0x5EC0DE41u
#define LOOP_WDT_S 15
RTC_NOINIT_ATTR static uint32_t crumbMagic;
RTC_NOINIT_ATTR static char crumbText[64];
static char lastRunNote[96] = ""; // what the previous run was doing when it was stopped, "" = clean start

// Loop profiler: the time between two breadcrumbs is charged to the first one; the slowest steps are kept.
struct SlowStep {
    char name[28];
    uint32_t ms, atMs;
};
static SlowStep slowSteps[5];
static uint32_t prevCrumbUs = 0;

// Field debug build (-DFIELD_DEBUG=1, command line only): the whole /status every 30 s in /field_debug.txt,
// and every loop step over 1 s with the last key or tap before it.
#ifndef FIELD_DEBUG
#define FIELD_DEBUG 0
#endif
static char fieldAction[48] = "none";  // last key or tap
static uint32_t fieldActionMs = 0;
static char fieldStall[128] = "";      // written by the loop, not inside crumb()

// Called many times per loop pass: the fast path is one string copy. crumbText still holds the previous
// step when the time is charged to it.
static void crumb(const char *what)
{
    uint32_t now = micros();
    uint32_t ms = (now - prevCrumbUs) / 1000;
    if (prevCrumbUs && ms >= 25 && crumbMagic == CRUMB_MAGIC) {
        const size_t nn = sizeof(slowSteps[0].name) - 1;
        int k = -1, low = 0; // the same step if already listed, else the fastest slot
        for (int i = 0; i < 5; i++) {
            if (!strncmp(slowSteps[i].name, crumbText, nn)) k = i;
            if (slowSteps[i].ms < slowSteps[low].ms) low = i;
        }
        bool take = k >= 0 ? ms >= slowSteps[k].ms || millis() - slowSteps[k].atMs > 30000 : ms > slowSteps[low].ms;
        if (k < 0) k = low;
        if (FIELD_DEBUG && ms >= 1000 && !fieldStall[0])
            snprintf(fieldStall, sizeof(fieldStall), "# stall %lu ms in \"%.40s\", last action %s %lu ms before",
                     (unsigned long)ms, crumbText, fieldAction, (unsigned long)(millis() - ms - fieldActionMs));
        if (take) {
            strncpy(slowSteps[k].name, crumbText, nn);
            slowSteps[k].name[nn] = 0;
            slowSteps[k].ms = ms;
            slowSteps[k].atMs = millis();
        }
    }
    prevCrumbUs = now;
    strncpy(crumbText, what, sizeof(crumbText) - 1);
    crumbText[sizeof(crumbText) - 1] = 0;
    crumbMagic = CRUMB_MAGIC;
}

static void crumbTile(const char *path)
{
    char b[64];
    snprintf(b, sizeof(b), "map tile %s", path);
    crumb(b);
}

// ---- Display backlight ----
// The backlight driver has 16 steps and is set with pulses on its enable pin (as in the LilyGO examples),
// not with PWM: each short low pulse lowers the level by one, going below 1 wraps to 16.
#define BL_STEPS 16
static const uint8_t BL_LEVEL[5] = {16, 12, 8, 4, 2}; // settings choices: 100, 75, 50, 25, 12 %
static uint8_t blLevel = 0;                           // 0 = off

static void blSet(uint8_t level)
{
    if (level > BL_STEPS) level = BL_STEPS;
    if (level == 0) {
        digitalWrite(BOARD_BL_PIN, LOW);
        delay(3); // long low: the driver switches off
        blLevel = 0;
        return;
    }
    if (blLevel == 0) {
        digitalWrite(BOARD_BL_PIN, HIGH); // switches on at full brightness
        delayMicroseconds(30);
        blLevel = BL_STEPS;
    }
    int pulses = (blLevel - level + BL_STEPS) % BL_STEPS;
    for (int i = 0; i < pulses; i++) {
        digitalWrite(BOARD_BL_PIN, LOW);
        digitalWrite(BOARD_BL_PIN, HIGH);
    }
    blLevel = level;
}
static Sx1262 radio(SPI, RADIO_CS_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN, RADIO_DIO1_PIN);
static Capture cap(radio);
// NVS namespaces ("rs41poc", "rs41last", "rs41me", "rs41pre") keep their first-version names: renaming
// them would lose every saved setting.
static Preferences prefs;

struct Settings {
    double freq = 404.500;  // Trapani launch site
    uint8_t bwIdx = BW_DEFAULT;
    bool invert = false;
    bool autoInvert = true;
    bool rawLog = true;
    uint8_t sound = 0; // frame sound: 0 soft tick, 1 beep whose pitch follows the signal
    uint8_t vol = 7;   // volume of beeps, alarms and the signal meter: 0 mute - 9 loudest
    uint8_t fvol = 7;  // volume of the sound of each decoded frame: 0 mute - 9 loudest
    // SondeHub upload is opt-in: a sonde on the ground must never be uploaded.
    bool shUpload = false;
    bool shFlightOnly = true; // extra guard: only sondes seen climbing or falling
    uint8_t sharePos = 0;     // own position on the maps: POS_OFF, POS_ALL or one service
    bool shMobile = true;     // station shown as a chase car or as a fixed receiver
    bool aprsUp = false;      // radiosondy.info upload (APRS-IS)
    bool aprsFi = false;      // station position on aprs.fi (main APRS-IS network)
    bool aprsFiCmt = true;    // with the comment of the situation (/aprsfi.txt)
    int32_t aprsPass = -1;    // APRS-IS passcode typed by the user, -1 not set
    uint8_t staMob = 1;       // station position, mobile: STA_MOB_S
    uint8_t staFix = 1;       // fixed: STA_FIX_MIN
    uint8_t fixWait = 1;      // station position only after a good GPS fix lasting FIX_WAIT_S seconds
    uint8_t minMove = 0;      // mobile: no position until moved MIN_MOVE_M metres (0 = off)
    uint8_t launchH = 1;      // next launches looked for within LAUNCH_H_S (24 h by default)
    bool shLog = true;        // SondeHub JSON of every frame on the SD card (/sondehub), for SondeHub's review
    uint8_t mobIcon = 0;      // APRS icon of the mobile station: APRS_MOBILE_SYM (person by default)
    bool dbgServer = false;   // HTTP debug pages at /debug (remote screen, keys, logs)
    bool webUi = false;       // web interface at / (dashboard, map, radio, settings)
    uint8_t apps = 0;         // hunting apps: 0 off, 1 Bluetooth (MySondy), 2 Bluetooth + Wi-Fi (rdz JSON)
    bool touch = true;        // touch screen input
    bool net = false;         // internet through the Wi-Fi (phone hotspot): time, GPS aid; never uploads
    bool wifiPs = false;      // Wi-Fi power save (modem sleep): off by default, it lost the link at home
    int16_t tzMin = 60;       // local time: UTC offset in minutes (Italy: +60)
    uint8_t dst = TZ_DST_EU;  // daylight saving time: off, on (+1 h), automatic with the European rule
    uint8_t bright = 0;       // display brightness choice, index in BL_LEVEL (0 = full)
    uint8_t theme = 0;        // index in THEMES: 0 Desert, 1 Olive, 2 Navy, 3 Sun
    uint8_t stype = ST_AUTO;  // sonde type: AUTO (decoders take turns) or fixed
    bool fullFrames = true;   // receive whole 320/518-byte frames (off: 255 bytes + erasures)
    uint8_t dim = 2;          // screen timeout: SCREEN_OFF_S[dim] seconds without input (0 = always on)
    uint8_t dimLvl = 0;       // at the timeout: 0 the screen goes off, 1-3 it is dimmed (3 darkest)
    uint8_t autoLock = 0;     // AUTOLOCK_S[autoLock] seconds without input lock keys and touch (0 = off)
    uint8_t tbSpeed = 0;      // trackball: 0 fast, 1 medium, 2 slow (TB_EDGES[tbSpeed] edges per step)
    char ssid[33] = "";
    char pass[64] = "";
    char call[16] = "";
    char dbgPass[24] = DEBUG_PASS_DEFAULT; // debug web page (HTTP Basic, user "tdeck")
#if PRED_SERVER
    bool psOn = false;   // frames to the prediction server (asks first)
    char psUrl[100] = "";
    char psTok[100] = ""; // never shown, logged or in /status
#endif
} cfg;

// Polarity that last decoded each type (1 inverted). Real sondes keep theirs: with automatic polarity
// every type starts with it, so a change of frequency, type or preset hears the first frame at once.
static uint8_t polInv[ST_COUNT] = {0};
static uint8_t polFlips = 0; // automatic switches since the type was set
static uint32_t tuneMs = 0, firstOkMs = 0; // last change of the radio; first frame after it (ms, 0 = none yet)

struct Stats {
    uint32_t sync = 0;
    uint32_t eccOk = 0;
    uint32_t gpsOk = 0;
} stats;

static Rs41Result last;
static Rs41Calib calib;

// Last valid sonde position, kept when frames stop (landing site).
struct SondeTrack {
    bool have = false;
    double lat = 0, lon = 0, alt = 0;
    float vH = 0, vDir = 0, vV = 0;
    uint8_t sats = 0;
    uint32_t ms = 0;
    char id[16] = "";
    bool manual = false;   // typed in by the user, not received
    bool restored = false; // loaded from flash after a reboot
    bool fromNet = false;  // taken from SondeHub (internet) while the radio does not hear the sonde
    uint32_t unixT = 0;    // sonde GPS time of this position, 0 = unknown
} track;

// Onboard GPS: u-blox M10Q at 38400 or L76K at 9600 depending on the batch.
static NmeaParser nmea;
static uint32_t gpsBaud = 38400;
static bool gpsSeen = false;
static uint32_t gpsLastGoodMs = 0;
static uint32_t gpsLastSwitchMs = 0;
static uint32_t gpsFixMs = 0;
static uint32_t gpsUtc = 0, gpsUtcMs = 0; // last UTC from the GPS and when it arrived
enum Screen : uint8_t {
    SCR_HUNT, SCR_MAP, SCR_APPROACH, SCR_DIAG, SCR_SETTINGS, SCR_HOME, SCR_GPS, SCR_NEARBY, SCR_COUNT
};
// SondeHub (internet, read only): last answers and the sonde we follow.
static NetResult *netr = nullptr; // SondeHub answers, in PSRAM (allocated in setup)
static uint32_t netSeen = 0;
static char followSerial[12] = ""; // picked in the Nearby list; "" = the sonde being received
static float shDiffM = -1;         // radio vs SondeHub position of the same sonde, metres
static int nearFocus = 0;
static double nearRefLat = 0, nearRefLon = 0; // Nearby list: reference point and order by distance
static int nearOrder[NET_MAX_LANDED + NET_MAX_LAUNCH];
static int nearCount = 0;
static void drawNearby();
static bool bestPred(double *lat, double *lon, int *etaS, bool *fromSh);
// Where the chosen landing point comes from (bestPred), for the labels.
enum { PSRC_NONE, PSRC_OWN, PSRC_SH, PSRC_SRV };
static uint8_t predSrc = PSRC_NONE;
static bool navSrv = false; // n key: the prediction server's point, whatever the automatic choice
static bool psActive();
static bool psPredOk();
static const char *predLabel();
static bool srvPoint(double *lat, double *lon, int *etaS);
#if PRED_SERVER
static PredSrvResult psRes; // latest answer of the prediction server
static bool psHave = false;
#endif
static double predGround(); // ground level for the prediction: own altitude, else sea level
static bool shPredOk();
struct ShPrediction;
static const ShPrediction *shPred();
static const char *followId();
#define NEAR_Y0 46
#define NEAR_ROW_H 18
#define NEAR_ROWS 9
static int qrMode = 0; // map QR overlay: 0 off, 1 sonde position, 2 predicted landing
static int shTab = 0;           // SondeHub page tab: 0 near, 1 landed, 2 launches, 3 search
static int landedIdx = 2;        // landed period: 3h, 12h, 24h, 7d
static const uint32_t LANDED_PERIOD_S[4] = {3 * 3600, 12 * 3600, 24 * 3600, 7 * 86400};
static const char *const LANDED_PERIOD_NAME[4] = {"3h", "12h", "24h", "7d"};
static int shDetail = 0;         // details view: 0 none, 1 serial summary, 2 launch
static char detailSerial[12] = "";
static uint32_t detailLaunchT = 0; // launch shown in the details, by its time (the list is rebuilt now and then)
static uint32_t launchGen = 0; // grows each time the Launches tab is opened (netsvc rebuilds the schedule)
static bool followStale = false, sondesFast = false; // last request, for /status
static bool nearKey(char k);
static bool nearBack();
static void nearInput(const InputEvent &e);
static bool mapOk = false;
static float *trkLat, *trkLon; // sonde track for the map, TRK_MAX points in PSRAM (psramArrays)
static int nTrk = 0;
static uint16_t trkGen = 0; // changes when the track is cleared or thinned: the web map then reloads it whole
static uint32_t trkMs = 0;
static uint8_t screen = SCR_HOME;
// Pages left behind, newest last: Back returns to them, Home clears them.
#define NAV_DEPTH 4
static uint8_t navStack[NAV_DEPTH];
static int navDepth = 0;
static int homeFocus = 0;    // selected button on the Home page (trackball)
static int setTab = 0;       // settings tab: Radio, Network, Display, System
static int setFocus = 0;     // selected row in the settings tab
static int setTop = 0;       // first settings row shown
static char navText[64] = ""; // text of the bottom bar, set by each page
static uint16_t navColor = UI_LABEL;
// How often the station position is sent (SondeHub and radiosondy.info).
#define STA_MOB_N 6
#define STA_FIX_N 3
static const uint16_t STA_MOB_S[STA_MOB_N] = {30, 60, 120, 300, 600, 900};
static const uint8_t STA_FIX_MIN[STA_FIX_N] = {10, 30, 60};
// "Send my position": nowhere, everywhere, or one service only.
enum { POS_OFF, POS_ALL, POS_SH, POS_RS, POS_FI, POS_N };
static const char *const POS_NAME[POS_N] = {"off", "all", "SondeHub", "radiosondy", "aprs.fi"};
// SondeHub gets nothing until it approves this software: "all" means radiosondy.info and aprs.fi meanwhile.
static bool posTo(uint8_t service)
{
    if (service == POS_SH && !SH_TELEMETRY_APPROVED) return false;
    return cfg.sharePos == POS_ALL || cfg.sharePos == service;
}
#define FIX_WAIT_N 4
static const uint16_t FIX_WAIT_S[FIX_WAIT_N] = {30, 60, 120, 300};
#define LAUNCH_H_N 5
static const uint32_t LAUNCH_H_S[LAUNCH_H_N] = {12 * 3600, 24 * 3600, 3 * 86400, 5 * 86400, 7 * 86400};
static const char *const LAUNCH_H_NAME[LAUNCH_H_N] = {"12 h", "24 h", "3 days", "5 days", "7 days"};
static const char *const FIX_WAIT_NAME[FIX_WAIT_N] = {"30 s", "1 min", "2 min", "5 min"};
static const char *const BRIGHT_NAME[5] = {"100%", "75%", "50%", "25%", "12%"};
static const char *const TB_NAME[3] = {"fast", "medium", "slow"};
#define MIN_MOVE_N 4
static const uint16_t MIN_MOVE_M[MIN_MOVE_N] = {0, 200, 500, 1000};
static uint8_t editKind = 0; // text entry: 0 frequency, 1 target, 2 Wi-Fi name, 3 Wi-Fi password, 4 callsign, 5 serial,
                             // 6 name of a new preset, 7 APRS passcode, 8 web password, 9 server URL, 10 server token
#define EDIT_KINDS 11
static const char *const EDIT_LABEL[EDIT_KINDS] = {"FREQ",          "LAT LON",      "WIFI NAME",  "WIFI PASS",
                                                   "CALLSIGN",      "SERIAL",       "PRESET NAME", "APRS PASSCODE",
                                                   "WEB PASSWORD",  "SERVER URL",   "SERVER TOKEN"};
static int flightRun = 0;
static bool flightSeen = false; // this sonde has really been flying
static float apPeak = -140;
static uint32_t apPeakMs = 0;
static int ascentFrames = 0;
static int descentFrames = 0;
static bool burstAnnounced = false;
static bool lostAnnounced = false;
static uint32_t lastPosSaveMs = 0;
static WindProfile wind;
static Prediction pred = {};
static float vvEma = 0;
static bool navPred = false; // arrow and distance point to the predicted landing
// Continuous scan (v0.89): the preset frequencies first, then sweeps of the band, listening on the strongest
// channels in between, until a sonde is found (the user keeps it or not) or the scan is stopped.
enum { SCAN_PRESETS, SCAN_SWEEP, SCAN_TRY, SCAN_ASK };
static uint8_t scanPhase = SCAN_SWEEP;
static int8_t scanRssi[SCAN_BINS];  // current sweep
static int8_t scanLast[SCAN_BINS];  // previous sweep: bursts missed by one sweep show in the other
static int8_t scanHold[SCAN_BINS];  // highest seen since the scan started (drawn dim)
static uint32_t scanCandKhz[SCAN_MAX_CAND];
static uint8_t scanCandType[SCAN_MAX_CAND]; // a preset's sonde type, ST_AUTO from the sweep
static int8_t scanCandRssi[SCAN_MAX_CAND];
static int scanNCand = 0;
static int scanIdx = 0;
static int scanPos = 0;      // next bin, or next preset, to measure
static int scanRound = 0;    // sweeps done
static int scanNoise = -110; // dBm, median of the last measurement
static uint32_t scanStartMs = 0;
static ScanSkip scanSkip;
static bool scanning = false;
static uint32_t scanDeadline = 0;
static double scanPrevFreq = 0;
static bool scanPrevInvert = false; // polarity before the scan: each candidate starts from it, a failed scan restores it
static uint8_t scanPrevRx = 0;
static uint32_t lastOkMs = 0;   // last RS-corrected frame
static char uiMsg[48] = "";
static uint32_t uiMsgMs = 0;
static bool haveLast = false;
static float lastRssi = 0;
static bool sdOk = false;
static bool radioOk = false;
static bool kbOk = false;
static bool textEditing = false;
static uint32_t sdHz = 0; // SD card clock chosen at boot (sdStart)
static String textInput;
// Starts a text entry on the bottom line; editKind says what it is for (EDIT_LABEL).
static bool wifiPassOpen = false; // password of the network picked in the scan, in a box over the list
static void startEdit(uint8_t kind)
{
    textEditing = true;
    editKind = kind;
    textInput = "";
}
// Passwords, passcodes and tokens being typed: shown on the screen (the user's choice, 25/11/2026), never
// written to the log.
static bool editSecret() { return textEditing && (editKind == 3 || editKind == 7 || editKind == 8 || editKind == 10); }
static uint32_t lastUi = 0;
static uint32_t lastInvertToggle = 0;
static float rssiIdle = 0;
static CaptureResult capRes;

static bool aprsReady();
static const char *shReplyWord(const char *msg);

static void loadSettings()
{
    prefs.begin("rs41poc", true);
    cfg.freq = prefs.getDouble("freq", cfg.freq);
    cfg.bwIdx = prefs.getUChar("bw", cfg.bwIdx);
    cfg.invert = prefs.getBool("inv", cfg.invert);
    cfg.autoInvert = prefs.getBool("ainv", cfg.autoInvert);
    cfg.rawLog = prefs.getBool("raw", cfg.rawLog);
    cfg.vol = prefs.getUChar("vol", 7);
    cfg.fvol = prefs.getUChar("fvol", 7);
    cfg.sound = prefs.getUChar("fsnd", 0);
    cfg.dimLvl = prefs.getUChar("dimlv", 0);
    if (prefs.getBytesLength("polinv") == sizeof(polInv)) prefs.getBytes("polinv", polInv, sizeof(polInv));
    for (uint8_t &v : polInv) v = v ? 1 : 0;
    cfg.shUpload = prefs.getBool("shup", false);
    cfg.shFlightOnly = prefs.getBool("shfl", true);
    cfg.sharePos = prefs.getUChar("pos", POS_OFF) % POS_N;
    cfg.shMobile = prefs.getBool("shmob", true);
    cfg.aprsUp = prefs.getBool("aprsup", false);
    cfg.aprsFi = prefs.getBool("aprsfi", false);
    cfg.aprsFiCmt = prefs.getBool("aprsfic", true);
    cfg.aprsPass = prefs.getInt("aprspc", -1);
    cfg.staMob = prefs.getUChar("stmob", 1) % STA_MOB_N;
    cfg.staFix = prefs.getUChar("stfix", 1) % STA_FIX_N;
    cfg.mobIcon = prefs.getUChar("mobic", 0) % APRS_MOBILE_ICONS;
    cfg.dbgServer = prefs.getBool("dbg", false);
    cfg.webUi = prefs.getBool("webui", false);
    cfg.apps = prefs.getUChar("apps", 0);
    if (cfg.apps > 2) cfg.apps = 0;
    cfg.touch = prefs.getBool("touch", true);
    cfg.net = prefs.getBool("net", false);
    cfg.wifiPs = prefs.getBool("wifips", false);
    cfg.tzMin = prefs.getShort("tzm", 60);
    cfg.dst = prefs.getUChar("dst", TZ_DST_EU);
    if (cfg.tzMin < TZ_MIN_MIN || cfg.tzMin > TZ_MAX_MIN) cfg.tzMin = 60;
    if (cfg.dst >= TZ_DST_COUNT) cfg.dst = TZ_DST_EU;
    cfg.bright = prefs.getUChar("bl", 0);
    cfg.theme = prefs.getUChar("th", 0);
    cfg.fullFrames = prefs.getBool("full", true);
    cfg.stype = prefs.getUChar("stype", ST_AUTO);
    if (cfg.stype >= ST_COUNT) cfg.stype = ST_AUTO;
    cfg.dim = prefs.getUChar("scr", 2);
    if (cfg.dim > 3) cfg.dim = 2;
    if (cfg.theme >= THEME_COUNT) cfg.theme = 0;
    if (cfg.bright > 4) cfg.bright = 0;
    if (prefs.isKey("ssid")) prefs.getString("ssid", cfg.ssid, sizeof(cfg.ssid));
    if (prefs.isKey("wpass")) prefs.getString("wpass", cfg.pass, sizeof(cfg.pass));
    if (prefs.isKey("call")) prefs.getString("call", cfg.call, sizeof(cfg.call));
    if (prefs.isKey("dbgpw")) prefs.getString("dbgpw", cfg.dbgPass, sizeof(cfg.dbgPass));
#if PRED_SERVER
    cfg.psOn = prefs.getBool("psrv", false);
    if (prefs.isKey("psurl")) prefs.getString("psurl", cfg.psUrl, sizeof(cfg.psUrl));
    if (prefs.isKey("pstok")) prefs.getString("pstok", cfg.psTok, sizeof(cfg.psTok));
    if (!cfg.psUrl[0] || !cfg.psTok[0]) cfg.psOn = false;
#endif
    if (!cfg.dbgPass[0]) snprintf(cfg.dbgPass, sizeof(cfg.dbgPass), "%s", DEBUG_PASS_DEFAULT);
    for (char *p = cfg.call; *p; p++) *p = (char)toupper((unsigned char)*p); // one callsign for SondeHub and APRS
    if (!cfg.ssid[0] || !sh_callsign_ok(cfg.call) || !SH_TELEMETRY_APPROVED) cfg.shUpload = false;
    if (cfg.sharePos == POS_SH && !SH_TELEMETRY_APPROVED) cfg.sharePos = POS_OFF;
    if (!aprsReady()) cfg.aprsUp = cfg.aprsFi = false;
    cfg.autoLock = prefs.getUChar("alock", 0);
    cfg.tbSpeed = prefs.getUChar("tbspd", 0);
    cfg.fixWait = prefs.getUChar("fixw", 1) % FIX_WAIT_N;
    cfg.minMove = prefs.getUChar("minmv", 0) % MIN_MOVE_N;
    cfg.launchH = prefs.getUChar("lnch", 1) % LAUNCH_H_N;
    cfg.shLog = prefs.getBool("shlog", true);
    prefs.end(); // every read above this line: after end() the getters return the defaults
    if (cfg.autoLock > 3) cfg.autoLock = 0;
    if (cfg.tbSpeed > 2) cfg.tbSpeed = 0;
    if (cfg.bwIdx >= Sx1262::BW_COUNT) cfg.bwIdx = BW_DEFAULT;
    if (cfg.sound > 1) cfg.sound = 0;
    if (cfg.vol > 9) cfg.vol = 7;
    if (cfg.fvol > 9) cfg.fvol = 7;
    if (cfg.dimLvl > 3) cfg.dimLvl = 0;
    audio_volume(cfg.vol, cfg.fvol);
    if (cfg.freq < 400.0 || cfg.freq > 406.0) cfg.freq = 404.5;
}

// Writes only the settings changed since the last save (the first save after boot writes them all):
// it runs on every frequency step and settings change, and each NVS write costs time and flash wear.
static uint32_t setGen = 0; // grows at every save: the web page reloads its settings
static void saveSettings()
{
    setGen++;
    static Settings saved;
    static bool savedOnce = false;
    prefs.begin("rs41poc", false);
#define SAVE(put, key, field)                                                                                          \
    if (!savedOnce || cfg.field != saved.field) prefs.put(key, cfg.field)
#define SAVE_STR(key, field)                                                                                           \
    if (!savedOnce || strcmp(cfg.field, saved.field)) prefs.putString(key, cfg.field)
    SAVE(putDouble, "freq", freq);
    SAVE(putUChar, "bw", bwIdx);
    SAVE(putBool, "inv", invert);
    SAVE(putBool, "ainv", autoInvert);
    SAVE(putBool, "raw", rawLog);
    SAVE(putUChar, "fsnd", sound);
    SAVE(putUChar, "vol", vol);
    SAVE(putUChar, "fvol", fvol);
    SAVE(putUChar, "dimlv", dimLvl);
    SAVE(putBool, "shup", shUpload);
    SAVE(putBool, "shfl", shFlightOnly);
    SAVE(putUChar, "pos", sharePos);
    SAVE(putBool, "shmob", shMobile);
    SAVE(putBool, "aprsup", aprsUp);
    SAVE(putBool, "aprsfi", aprsFi);
    SAVE(putBool, "aprsfic", aprsFiCmt);
    SAVE(putInt, "aprspc", aprsPass);
    SAVE(putUChar, "stmob", staMob);
    SAVE(putUChar, "stfix", staFix);
    SAVE(putUChar, "mobic", mobIcon);
    SAVE(putBool, "dbg", dbgServer);
    SAVE(putBool, "webui", webUi);
    SAVE(putUChar, "apps", apps);
    SAVE(putBool, "touch", touch);
    SAVE(putBool, "net", net);
    SAVE(putBool, "wifips", wifiPs);
    SAVE(putShort, "tzm", tzMin);
    SAVE(putUChar, "dst", dst);
    SAVE(putUChar, "bl", bright);
    SAVE(putUChar, "th", theme);
    SAVE(putBool, "full", fullFrames);
    SAVE(putUChar, "stype", stype);
    SAVE(putUChar, "scr", dim);
    SAVE(putUChar, "alock", autoLock);
    SAVE(putUChar, "tbspd", tbSpeed);
    SAVE(putUChar, "fixw", fixWait);
    SAVE(putUChar, "minmv", minMove);
    SAVE(putUChar, "lnch", launchH);
    SAVE(putBool, "shlog", shLog);
    SAVE_STR("ssid", ssid);
    SAVE_STR("wpass", pass);
    SAVE_STR("call", call);
    SAVE_STR("dbgpw", dbgPass);
#if PRED_SERVER
    SAVE(putBool, "psrv", psOn);
    SAVE_STR("psurl", psUrl);
    SAVE_STR("pstok", psTok);
#endif
#undef SAVE
#undef SAVE_STR
    prefs.end();
    saved = cfg;
    savedOnce = true;
    audio_volume(cfg.vol, cfg.fvol);
}

static void setMsg(const char *m);

// No Wi-Fi name yet: says so and returns true (the setting is not changed).
static bool needSsid()
{
    if (cfg.ssid[0]) return false;
    setMsg("set the Wi-Fi name first (Network tab)");
    return true;
}

// What radiosondy.info and aprs.fi still miss, or nullptr when ready.
static const char *aprsMissing()
{
    if (!cfg.ssid[0]) return "set the Wi-Fi name first (Network tab)";
    if (!aprs_call_ok(cfg.call)) return "callsign for APRS: capitals, e.g. IT9ABC-9";
    if (cfg.aprsPass != aprs_passcode(cfg.call)) return "APRS passcode missing or wrong";
    return nullptr;
}

// radiosondy.info: Wi-Fi, an APRS callsign and its passcode.
static bool aprsReady()
{
    return cfg.ssid[0] && aprs_call_ok(cfg.call) && cfg.aprsPass == aprs_passcode(cfg.call);
}

// ---- aprs.fi: the station position with a comment for the situation, texts in /aprsfi.txt on the SD card ----
static char *aprsFiFile = nullptr; // file contents (PSRAM), nullptr: the built-in texts
static char aprsFiText[APRS_COMMENT_MAX] = "Looking for radiosondes";

static void pushUploaderCfg()
{
    UploaderCfg u = {};
    u.enabled = SH_TELEMETRY_APPROVED && cfg.shUpload;
    u.wifiDebug = cfg.dbgServer || cfg.webUi || cfg.apps == 2 || cfg.net; // Wi-Fi on without any upload
    u.shPos = posTo(POS_SH);
    u.rsPos = posTo(POS_RS);
    u.fiPos = posTo(POS_FI);
    u.mobile = cfg.shMobile;
    u.aprs = cfg.aprsUp;
    u.aprsFi = cfg.aprsFi;
    u.wifiSleep = cfg.wifiPs;
#if PRED_SERVER
    u.predSrv = cfg.psOn;
    predsrv_configure(cfg.psOn, cfg.psUrl, cfg.psTok, cfg.call, FW_VERSION);
#endif
    snprintf(u.aprsFiText, sizeof(u.aprsFiText), "%s", cfg.aprsFiCmt ? aprsFiText : "");
    u.aprsPass = cfg.aprsPass;
    u.mobileSym = APRS_MOBILE_SYM[cfg.mobIcon];
    u.mobileMs = STA_MOB_S[cfg.staMob] * 1000UL;
    u.fixedMs = STA_FIX_MIN[cfg.staFix] * 60000UL;
    u.minMoveM = MIN_MOVE_M[cfg.minMove];
    snprintf(u.ssid, sizeof(u.ssid), "%s", cfg.ssid);
    snprintf(u.pass, sizeof(u.pass), "%s", cfg.pass);
    snprintf(u.callsign, sizeof(u.callsign), "%s", cfg.call);
    snprintf(u.swVersion, sizeof(u.swVersion), "%s", FW_VERSION);
    uploader_configure(u);
}

// A setting changed: the message (if any), saved, and passed to the Wi-Fi and upload task.
static void saveSettings();
static void settingDone(const char *msg = nullptr)
{
    if (msg) setMsg(msg);
    saveSettings();
    pushUploaderCfg();
}

static void toggleSetting(bool &v, const char *on, const char *off)
{
    v = !v;
    settingDone(v ? on : off);
}


// SondeHub JSON of a frame, one line, appended to /sondehub/<serial>.json: whole flights for SondeHub's review.
// Called from the main loop only (handleCapture): a second task on the SD card crashed the T-Deck on 21/11.
static uint32_t shLogLines = 0;
static void shLogWrite(const ShFrame &f)
{
    if (!sdOk) return;
    static bool dirOk = false;
    if (!dirOk) dirOk = SD.exists(shLogDir) || SD.mkdir(shLogDir);
    static char line[900];
    size_t n = sh_frame_json(line, sizeof(line), f, cfg.call, FW_VERSION, false);
    if (!n) return;
    char path[72];
    snprintf(path, sizeof(path), "%s/%s.json", shLogDir, f.serial);
    File fl = SD.open(path, FILE_APPEND);
    if (!fl) return;
    fl.write((const uint8_t *)line, n);
    fl.write('\n');
    fl.close();
    shLogLines++;
}

// Last received position survives a reboot (dead battery, crash) so the landing site is not lost.
static void saveLastPos()
{
    prefs.begin("rs41last", false);
    prefs.putDouble("lat", track.lat);
    prefs.putDouble("lon", track.lon);
    prefs.putDouble("alt", track.alt);
    prefs.putString("id", track.id);
    prefs.putUInt("t", track.unixT); // how old it is after a restart (followId)
    prefs.putBool("man", track.manual); // a typed or picked target is restored as a target
    prefs.end();
}

static void loadLastPos()
{
    prefs.begin("rs41last", true);
    if (prefs.isKey("lat")) {
        track.lat = prefs.getDouble("lat", 0);
        track.lon = prefs.getDouble("lon", 0);
        track.alt = prefs.getDouble("alt", 0);
        String id = prefs.getString("id", "");
        snprintf(track.id, sizeof(track.id), "%s", id.c_str());
        track.have = true;
        track.unixT = prefs.getUInt("t", 0);
        track.manual = prefs.getBool("man", false);
        track.restored = !track.manual;
        track.ms = millis();
    }
    prefs.end();
}

// ---- Sonde type: fixed in the settings, from a preset of this frequency, or taking turns (AUTO) ----
static Preset *plist; // PRESET_MAX presets in PSRAM (psramArrays)
static int nPlist = 0;
static int curPreset = -1; // preset chosen from the list (kept across restarts); -1: none, automatic
static uint8_t rxType = ST_RS41; // what the radio listens for now

static uint32_t freqKhz() { return (uint32_t)lround(cfg.freq * 1000.0); }

// The chosen preset while the radio is on its frequency, -1 otherwise.
static int activePreset()
{
    return curPreset >= 0 && curPreset < nPlist && plist[curPreset].khz == freqKhz() ? curPreset : -1;
}

static uint8_t presetTypeHere()
{
    int i = activePreset();
    return i >= 0 ? plist[i].type : ST_AUTO;
}

// AUTO with no preset type in use: the decoders take turns until a frame is decoded.
static bool typeRotates() { return cfg.stype == ST_AUTO && presetTypeHere() == ST_AUTO; }

static uint8_t wantedType()
{
    if (cfg.stype != ST_AUTO) return cfg.stype;
    uint8_t p = presetTypeHere();
    return p != ST_AUTO ? p : rxType;
}

static void decodersReset();

// keepPol: the polarity was just switched on purpose (automatic switch, scan); otherwise, with automatic
// polarity, the type starts with the one that last decoded it.
static void applyRadio(bool keepPol = false)
{
    if (!radioOk) return;
    static uint32_t tunedKhz = 0;
    if (freqKhz() != tunedKhz) { // another frequency, maybe another sonde: no serial or fix carried over
        tunedKhz = freqKhz();
        decodersReset();
    }
    rxType = wantedType();
    if (!keepPol) {
        tuneMs = millis();
        firstOkMs = 0;
    }
    if (!keepPol && cfg.autoInvert) {
        cfg.invert = polInv[rxType];
        polFlips = 0;
    }
    cap.setFullFrames(cfg.fullFrames);
    cap.configure(rxType, cfg.freq, Sx1262::BW_CODE[cfg.bwIdx], cfg.invert);
    lastInvertToggle = millis(); // after any change the automatic switch waits a full dwell
}

static void resetStats()
{
    stats = Stats();
    haveLast = false;
    if (!track.manual && !track.restored) track.have = false; // typed or saved targets survive retuning
    pred.ok = false;
}

// A radio setting changed: apply it, keep it, and count the frames from zero.
// Saved 2 s after the last change: every q/w step or trackball click used to write the flash.
static uint32_t saveDueMs = 0;
static void saveSoon() { saveDueMs = millis() | 1; }
static void savePoll()
{
    if (saveDueMs && millis() - saveDueMs > 2000) {
        saveDueMs = 0;
        saveSettings();
    }
}

static void radioChanged()
{
    applyRadio();
    saveSoon();
    resetStats();
}

// Last log lines in PSRAM, served by the debug server.
#define LOG_RING_LINES 60
#define LOG_RING_LEN 420
static char *logRing = nullptr;
static int logRingNext = 0, logRingCount = 0;

static void logRemember(const char *line)
{
    if (!logRing) logRing = (char *)ps_calloc(LOG_RING_LINES, LOG_RING_LEN);
    if (!logRing) return;
    snprintf(logRing + logRingNext * LOG_RING_LEN, LOG_RING_LEN, "%s", line);
    logRingNext = (logRingNext + 1) % LOG_RING_LINES;
    if (logRingCount < LOG_RING_LINES) logRingCount++;
}

static void logLine(const char *path, const char *line)
{
    Serial.println(line);
    logRemember(line);
    if (!sdOk) return;
    File f = SD.open(path, FILE_APPEND);
    if (!f) return;
    f.println(line);
    f.close();
}

// n bytes as hexadecimal text at out (2n characters and the terminator).
static void hexInto(char *out, const uint8_t *d, int n)
{
    static const char *H = "0123456789abcdef";
    for (int i = 0; i < n; i++) {
        out[2 * i] = H[d[i] >> 4];
        out[2 * i + 1] = H[d[i] & 0xF];
    }
    out[2 * n] = 0;
}

// ---- Screen timeout: after a while without input the display switches off, or is dimmed, to save the
// battery. Radio, GPS, logs, apps and sounds keep working; switched off, the redraws stop too.
static const uint16_t SCREEN_OFF_S[4] = {0, 60, 300, 600};
static uint32_t lastActivityMs = 0;
static bool dimmed = false; // screen off or dimmed: the first key or touch only wakes it

// ---- Screen lock (v0.84, trackball from v0.86): the trackball held 3 s locks and unlocks keyboard, trackball
// and touch (radio, alarms, uploads
// keep working). Never saved: unlocked at every start. Optional automatic lock after a time without input.
static const uint16_t AUTOLOCK_S[4] = {0, 60, 120, 300};
static const uint8_t TB_EDGES[3] = {2, 4, 6}; // trackball speed: fast, medium, slow
static bool screenLocked = false;
static uint32_t lastInputMs = 0; // last key, trackball or touch (messages and alarms do not count)
static void blSet(uint8_t level);
static void drawUi();

// Something happened that the user should see: screen on again. True when it was off.
static bool wakeScreen()
{
    lastActivityMs = millis();
    if (!dimmed) return false;
    dimmed = false;
    blSet(BL_LEVEL[cfg.bright]);
    drawUi(); // the canvas was not sent while off: show the current state at once
    return true;
}

static void dimPoll()
{
    if (!cfg.dim || dimmed || millis() - lastActivityMs < SCREEN_OFF_S[cfg.dim] * 1000UL) return;
    dimmed = true;
    uint8_t l = BL_LEVEL[cfg.bright] >> cfg.dimLvl; // each dimming step halves the chosen brightness
    blSet(!cfg.dimLvl ? 0 : l ? l : 1);
}

static void setMsg(const char *m)
{
    snprintf(uiMsg, sizeof(uiMsg), "%s", m);
    uiMsgMs = millis();
    wakeScreen(); // messages (alarms included) wake the screen, already showing this message
}

static void setMsgf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void setMsgf(const char *fmt, ...)
{
    char m[sizeof(uiMsg)];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m, sizeof(m), fmt, ap);
    va_end(ap);
    setMsg(m);
}

static bool msgActive()
{
    return uiMsg[0] && millis() - uiMsgMs < UI_MSG_MS;
}

static void trackAppend(const char *id, const TrackRow &row)
{
    if (!sdOk || !track_id_ok(id)) return;
    char path[72];
    snprintf(path, sizeof(path), "%s/%s.csv", trackDir, id);
    static char known[16] = ""; // its file exists, with the header
    bool isNew = strcmp(known, id) && !SD.exists(path);
    if (isNew && !SD.exists(trackDir)) SD.mkdir(trackDir);
    File f = SD.open(path, FILE_APPEND);
    if (!f) return;
    if (isNew) f.println(TRACK_CSV_HEADER);
    snprintf(known, sizeof(known), "%s", id);
    char line[256];
    track_csv_row(line, sizeof(line), row);
    f.println(line);
    f.close();
}

static void putFile(void *ctx, const char *s)
{
    ((File *)ctx)->print(s);
}

// Rebuilds /sonde/<ID>.kml from the CSV of the sonde currently received. Blocks while it runs.
// Serial of the sonde being hunted (heard by radio or followed on SondeHub), "" for none or a typed target.
// The last capture is not used: a failed one has no serial, and the name would flicker on a weak signal.
static const char *huntId() { return track.have && !track.manual ? track.id : ""; }

static void exportKml()
{
    if (!sdOk) return setMsg("KML: no SD card");
    const char *id = huntId();
    if (!track_id_ok(id)) return setMsg("KML: no sonde received yet");
    char csv[72], kml[72];
    snprintf(csv, sizeof(csv), "%s/%s.csv", trackDir, id);
    snprintf(kml, sizeof(kml), "%s/%s.kml", trackDir, id);
    File in = SD.open(csv, FILE_READ);
    if (!in) return setMsg("KML: no track file");
    SD.remove(kml);
    File out = SD.open(kml, FILE_WRITE);
    if (!out) {
        in.close();
        return setMsg("KML: cannot write file");
    }
    track_kml_begin(putFile, &out, id);
    // Read in blocks and keep the watchdog fed: a 1 MB track read byte by byte took over 15 s and the
    // loop watchdog restarted the T-Deck (25/11/2026, KML from the web page).
    static char line[256];
    static uint8_t blk[1024];
    TrackPoint pt, lastPt;
    unsigned n = 0;
    int len = 0, got;
    while ((got = in.read(blk, sizeof(blk))) > 0) {
        esp_task_wdt_reset();
        for (int i = 0; i < got; i++) {
            char c = (char)blk[i];
            if (c != '\n') {
                if (len < (int)sizeof(line) - 1) line[len++] = c;
                continue;
            }
            line[len] = 0;
            len = 0;
            if (track_csv_parse(line, &pt)) {
                track_kml_point(putFile, &out, pt);
                lastPt = pt;
                n++;
            }
        }
    }
    track_kml_end(putFile, &out, id, n ? &lastPt : nullptr, n);
    out.close();
    in.close();
    setMsgf("KML: %u points saved", n);
}

// ---- Decoding of a capture ----

// The received bytes follow the sync word; what is missing (255-byte packets) is rebuilt by erasure decoding.
static bool decodeCapture(const CaptureResult &c, Rs41Result *res)
{
    static uint8_t fr[RS41_EXT_LEN];
    memset(fr, 0, sizeof(fr));
    rs41_raw_to_frame(c.p1, c.n1, c.invert, fr);
    return rs41_decode(fr, RS41_HDR_LEN + c.n1, res, true);
}

// The other types fill the RS41 record, so tracking, logs and apps work the same way for every sonde.
static void recordStart(Rs41Result *res, uint8_t type, bool ok, int corrected, int failed)
{
    memset(res, 0, sizeof(*res));
    res->sondeType = type;
    res->calIdx = 0xFF;
    res->eccOk = res->frameCrcOk = ok;
    res->ecc1 = ok ? corrected : -1;
    res->ecc2 = ok ? failed : -1;
}

static void recordFix(Rs41Result *res, const char *model, const char *id, bool valid, double lat, double lon,
                      double alt, float vH, float dir, float vV, uint32_t unixTime)
{
    snprintf(res->model, sizeof(res->model), "%s", model);
    snprintf(res->id, sizeof(res->id), "%s", id);
    res->gpsValid = res->gpsCrcOk = valid;
    res->lat = lat;
    res->lon = lon;
    res->alt = alt;
    res->vH = vH;
    res->vDir = dir;
    res->vV = vV;
    res->unixTime = unixTime;
    res->timeCrcOk = unixTime != 0;
}

// DFM frames are spread over several packets: the state collects them.
static DfmState dfmState;

static bool decodeDfm(const CaptureResult &c, Rs41Result *res)
{
    DfmFrame f;
    bool ok = dfm_decode_stream(&dfmState, c.p1, c.n1, c.invert, &f) > 0;
    recordStart(res, ST_DFM, ok, f.corrected, f.failed);
    res->frameLen = 280;
    recordFix(res, f.model, f.id[0] ? f.id : "DFM", f.newPos, f.lat, f.lon, f.alt, f.vH, f.dir, f.vV, f.unixTime);
    res->frameNb = f.frameNb;
    res->battV = f.battV;
    res->numSV = f.sats;
    return ok;
}

static M10State m10State; // last good M10/M20 frame: confirms frames with many doubtful bits

static bool decodeM10(const CaptureResult &c, Rs41Result *res)
{
    M10Frame f;
    bool ok = m10_decode(c.p1, c.n1, c.invert, &f, &m10State);
    recordStart(res, ST_M10, ok, f.fixed, 0);
    if (!ok) return false;
    res->frameLen = f.len;
    recordFix(res, f.model, f.id, f.gpsValid, f.lat, f.lon, f.alt, f.vH, f.dir, f.vV, f.unixTime);
    res->frameNb = f.counter;
    res->battV = f.battV;
    res->numSV = f.sats;
    return true;
}

// iMet-54: no speeds in the frame (they come from the previous position), time of day only.
static Imet54State imetState;

// Decoders that keep data across frames (serial, last fix) start empty on a new frequency.
static void decodersReset()
{
    dfm_reset(&dfmState);
    imet54_reset(&imetState);
    m10State = M10State();
}

static int32_t imetSecOfDay = -1; // GPS time of day of the last iMet-54 frame (frameNb keeps only 16 bits)

static bool decodeImet54(const CaptureResult &c, Rs41Result *res)
{
    Imet54Frame f;
    bool ok = imet54_decode(&imetState, c.p1, c.n1, c.invert, &f);
    imetSecOfDay = ok ? f.secOfDay : -1;
    recordStart(res, ST_IMET54, ok, f.corrected, 0);
    if (!ok) return false;
    res->frameLen = f.bytes;
    recordFix(res, f.model, f.id, f.gpsValid, f.lat, f.lon, f.alt, f.vH, f.dir, f.vV, 0); // time of day only
    res->frameNb = (uint16_t)f.secOfDay;
    return true;
}

// ---- Display ----

static void uiLine(int row, uint16_t color, const String &text)
{
    const int h = 16; // 13 rows end above the bottom bar
    gfx.setTextColor(color, UI_BG);
    gfx.setTextPadding(320);
    gfx.drawString(text, 2, 2 + row * h, 2);
}

// Battery, temperature and config data collected from the calibration subframes.
// RS41 temperature ("12.3C" or "--") and kill timer ("12m", with seconds while counting down, "off", "?").
static void rs41TempKill(char t[12], char kill[16], bool seconds)
{
    float degC;
    int kt, cd;
    snprintf(t, 12, "--");
    snprintf(kill, 16, "?");
    if (rs41_temperature(&calib, &last, &degC)) snprintf(t, 12, "%.1fC", degC);
    if (rs41_calib_countdown(&calib, &cd) && cd >= 0) {
        if (seconds) snprintf(kill, 16, "%dm%02ds", cd / 60, cd % 60);
        else snprintf(kill, 16, "%dm", cd / 60);
    } else if (rs41_calib_kill_timer(&calib, &kt)) {
        if (kt < 0) snprintf(kill, 16, "off");
        else snprintf(kill, 16, "%dm", kt / 60);
    }
}

static bool sondeStatusText(char *b, size_t n)
{
    b[0] = 0;
    if (!haveLast || !last.frameCrcOk) return false;
    char t[12], tx[12] = "--", kill[16];
    uint32_t khz;
    rs41TempKill(t, kill, true);
    if (rs41_calib_freq_khz(&calib, &khz)) snprintf(tx, sizeof(tx), "%.3f", khz / 1000.0);
    snprintf(b, n, "%.1fV %s cal %d/%d TX %s kill %s", last.battV, t, calib.nHave, RS41_CAL_FRAMES, tx, kill);
    return true;
}

// ---- Onboard GPS ----

static void updateOwnHeading();

static void gpsUbxFrame(const uint8_t *f, size_t n);
static UbxReader ubxIn; // UBX frames mixed with the NMEA output (navigation database dump)

static void pollGps()
{
    uint32_t now = millis();
    bool gotSentence = false;
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (ubxIn.feed((uint8_t)c)) gpsUbxFrame(ubxIn.frame(), ubxIn.len());
        if (nmea.feed(c)) {
            gpsSeen = true;
            gpsLastGoodMs = now;
            if (nmea.fix.valid) gpsFixMs = now;
            if (nmea.fix.utc && nmea.fix.utc != gpsUtc) {
                gpsUtc = nmea.fix.utc;
                gpsUtcMs = now;
            }
            gotSentence = true;
        }
    }
    if (gotSentence) updateOwnHeading();
    if (now - gpsLastGoodMs > GPS_BAUD_RETRY_MS && now - gpsLastSwitchMs > GPS_BAUD_RETRY_MS) {
        gpsBaud = gpsBaud == 38400 ? 9600 : 38400;
        Serial1.updateBaudRate(gpsBaud);
        gpsLastSwitchMs = now;
        gpsSeen = false;
    }
}

static uint32_t fixGoodSince = 0; // start of the current good GPS fix, 0 = none

static bool myFixOk()
{
    return gpsSeen && nmea.fix.valid && millis() - gpsFixMs < GPS_FIX_MAX_AGE_MS;
}

static bool fixStable() { return fixGoodSince && millis() - fixGoodSince >= FIX_WAIT_S[cfg.fixWait] * 1000UL; }

// ---- Internet time and help for the first GPS fix ----
// A cold GPS needs minutes to find itself. Telling the u-blox the time and a rough position cuts the
// search. Time comes from the internet (NTP through the phone hotspot) or, with no internet at all, from
// the sonde itself (its GPS time). The position is the sonde's (tens of km away at most) or the last
// own fix saved in flash. Sent once per start, only to u-blox receivers (38400 baud).
#define MYPOS_SAVE_MS 600000UL
#define AID_POS_SONDE_ACC_M 100000UL
#define AID_POS_SAVED_ACC_M 300000UL
static bool ntpStarted = false;
static bool myPosSaved = false;
static double myLat = 0, myLon = 0;
static float myAltEll = 0;
static uint32_t myPosSaveMs = 0;
static uint32_t firstFixMs = 0;
static bool aidSent = false;
static uint32_t aidMs = 0;
static char aidText[48] = "";

static bool netTimeOk()
{
    return time(nullptr) > 1700000000;
}

static void netPoll()
{
    if (cfg.net && !ntpStarted && WiFi.status() == WL_CONNECTED) {
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        ntpStarted = true;
    }
}

static void loadMyPos()
{
    prefs.begin("rs41me", true);
    if (prefs.isKey("lat")) {
        myLat = prefs.getDouble("lat", 0);
        myLon = prefs.getDouble("lon", 0);
        myAltEll = prefs.getFloat("alt", 0);
        myPosSaved = true;
    }
    prefs.end();
}

static void saveMyPos()
{
    const GpsFix &g = nmea.fix;
    myLat = g.lat;
    myLon = g.lon;
    myAltEll = g.altM + g.geoidSepM;
    myPosSaved = true;
    prefs.begin("rs41me", false);
    prefs.putDouble("lat", myLat);
    prefs.putDouble("lon", myLon);
    prefs.putFloat("alt", myAltEll);
    prefs.end();
}

// ---- GPS navigation database kept on the SD card (UBX-MGA-DBD) ----
// With a fix, every 20 minutes the receiver is asked to dump its database (ephemeris, almanac...) and
// the answer is saved to /gpsdb.ubx. At power-on the file is sent back before anything else: with
// recent data the receiver needs seconds instead of minutes, without internet.
#define GPSDB_MAX (32 * 1024)
#define GPSDB_EVERY_MS (20UL * 60 * 1000)
#define GPSDB_FIRST_MS (2UL * 60 * 1000) // first dump this long after the first fix
static uint8_t *gpsdbBuf = nullptr;
static size_t gpsdbLen = 0;
static int gpsdbEntries = 0;
static bool gpsdbCollecting = false;
static uint32_t gpsdbAskMs = 0, gpsdbLastMs = 0, gpsdbSavedMs = 0;
static size_t gpsdbSavedBytes = 0, gpsdbRestoredBytes = 0;
static bool gpsdbRestoreDone = false;

static void gpsUbxFrame(const uint8_t *f, size_t n)
{
    if (!gpsdbCollecting || f[2] != 0x13 || f[3] != 0x80 || n <= 8) return; // MGA-DBD entries only
    if (!gpsdbBuf) gpsdbBuf = (uint8_t *)ps_malloc(GPSDB_MAX);
    if (!gpsdbBuf || gpsdbLen + n > GPSDB_MAX) return;
    memcpy(gpsdbBuf + gpsdbLen, f, n);
    gpsdbLen += n;
    gpsdbEntries++;
    gpsdbLastMs = millis();
}

static void gpsdbPoll()
{
    uint32_t now = millis();
    if (!sdOk || gpsBaud != 38400) return;
    // Restore once, as soon as the receiver talks: the file is read into PSRAM, then sent a little at
    // each loop (38400 baud is about 4 KB/s: sending it in one go held the loop for ~2 s).
    static size_t restoreLen = 0, restorePos = 0;
    if (!gpsdbRestoreDone && gpsSeen) {
        gpsdbRestoreDone = true;
        File f = SD.open(gpsdbPath, FILE_READ);
        if (f) {
            if (!gpsdbBuf) gpsdbBuf = (uint8_t *)ps_malloc(GPSDB_MAX);
            if (gpsdbBuf) restoreLen = f.read(gpsdbBuf, GPSDB_MAX);
            f.close();
        }
    }
    if (restorePos < restoreLen) {
        int room = Serial1.availableForWrite();
        size_t n = restoreLen - restorePos;
        if (room > 0 && (size_t)room < n) n = room;
        if (room > 0) {
            Serial1.write(gpsdbBuf + restorePos, n);
            restorePos += n;
            gpsdbRestoredBytes = restorePos;
        }
        if (restorePos >= restoreLen) {
            char b[64];
            snprintf(b, sizeof(b), "# gps database restored: %u bytes", (unsigned)gpsdbRestoredBytes);
            logLine(logPath, b);
        }
        return; // no database dump while restoring
    }
    // A dump in progress ends when no entry came for 1.5 s.
    if (gpsdbCollecting) {
        uint32_t last = gpsdbLastMs > gpsdbAskMs ? gpsdbLastMs : gpsdbAskMs;
        if (now - last < 1500) return;
        gpsdbCollecting = false;
        if (gpsdbEntries > 0) {
            File f = SD.open(gpsdbPath, FILE_WRITE);
            if (f) {
                f.write(gpsdbBuf, gpsdbLen);
                f.close();
                gpsdbSavedMs = now;
                gpsdbSavedBytes = gpsdbLen;
                char b[64];
                snprintf(b, sizeof(b), "# gps database saved: %d entries, %u bytes", gpsdbEntries, (unsigned)gpsdbLen);
                logLine(logPath, b);
            }
        }
        return;
    }
    if (!myFixOk() || !firstFixMs) return;
    bool due = gpsdbSavedMs ? now - gpsdbSavedMs > GPSDB_EVERY_MS : now - firstFixMs > GPSDB_FIRST_MS;
    if (!due || (gpsdbAskMs && now - gpsdbAskMs < 60000)) return;
    uint8_t m[UBX_MGA_DBD_POLL_LEN];
    ubx_mga_dbd_poll(m);
    gpsdbLen = 0;
    gpsdbEntries = 0;
    gpsdbCollecting = true;
    gpsdbAskMs = now;
    Serial1.write(m, sizeof(m));
}

static void gpsAidPoll()
{
    gpsdbPoll();
    uint32_t now = millis();
    if (myFixOk()) {
        if (!firstFixMs) {
            firstFixMs = now;
            char b[96];
            snprintf(b, sizeof(b), "# gps first fix after %lu s, aid: %s", (unsigned long)(now / 1000),
                     aidSent ? aidText : "none");
            logLine(logPath, b);
            saveMyPos();
            myPosSaveMs = now;
        } else if (now - myPosSaveMs > MYPOS_SAVE_MS) {
            saveMyPos();
            myPosSaveMs = now;
        }
    }
    if (aidSent || firstFixMs || gpsBaud != 38400 || !gpsSeen || now < 3000) return;
    uint32_t t = 0;
    uint16_t tacc = 0;
    const char *tsrc = "";
    if (netTimeOk()) {
        t = (uint32_t)time(nullptr);
        tacc = 1;
        tsrc = "internet";
    } else if (haveLast && last.timeCrcOk && now - lastOkMs < 3000) {
        t = last.unixTime + (now - lastOkMs) / 1000;
        tacc = 2;
        tsrc = "sonde";
    }
    if (!t) return;
    uint8_t m[40];
    size_t n = ubx_mga_ini_time_utc(m, t, tacc);
    Serial1.write(m, n);
    const char *psrc = "none";
    if (track.have && !track.manual && !track.restored && now - track.ms < 60000) {
        n = ubx_mga_ini_pos_llh(m, track.lat, track.lon, 0, AID_POS_SONDE_ACC_M);
        Serial1.write(m, n);
        psrc = "sonde";
    } else if (myPosSaved) {
        n = ubx_mga_ini_pos_llh(m, myLat, myLon, myAltEll, AID_POS_SAVED_ACC_M);
        Serial1.write(m, n);
        psrc = "last fix";
    } else if (track.have) {
        // No own fix ever saved: the last sonde position (saved or typed) is usually not far away.
        n = ubx_mga_ini_pos_llh(m, track.lat, track.lon, 0, AID_POS_SAVED_ACC_M);
        Serial1.write(m, n);
        psrc = "last sonde";
    }
    aidSent = true;
    aidMs = now;
    snprintf(aidText, sizeof(aidText), "time %s, pos %s", tsrc, psrc);
    char b[80];
    snprintf(b, sizeof(b), "# gps aid at %lu s: %s", (unsigned long)(now / 1000), aidText);
    logLine(logPath, b);
}

// ---- Clock: UTC from the internet or the GPS, shown in local time ----

static uint32_t nowUnix()
{
    if (netTimeOk()) return (uint32_t)time(nullptr);
    if (gpsUtc) return gpsUtc + (millis() - gpsUtcMs) / 1000;
    return 0;
}

static int localOffsetS(uint32_t t)
{
    return tz_offset_s(t, cfg.tzMin, cfg.dst);
}

// "21:36" in local time, "--:--" when the time is unknown. withSec adds the seconds.
static void localClock(char *b, size_t n, bool withSec)
{
    uint32_t t = nowUnix();
    if (!t) {
        snprintf(b, n, withSec ? "--:--:--" : "--:--");
        return;
    }
    uint32_t s = (t + localOffsetS(t)) % 86400;
    if (withSec) snprintf(b, n, "%02lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else snprintf(b, n, "%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
}

// DST row: "off", "on", "auto: summer" (the season shown once the time is known).
static const char *dstName()
{
    if (cfg.dst == TZ_DST_OFF) return "off";
    if (cfg.dst == TZ_DST_ON) return "on";
    uint32_t t = nowUnix();
    return !t ? "auto (EU)" : tz_eu_summer(t) ? "auto: summer" : "auto: winter";
}

// ---- Own direction of travel ----
// The T-Deck has no compass: the only direction it knows is the way we are moving. Above 4 km/h the
// GPS course is good; walking slowly it is noise, so the direction is taken from the GPS track instead:
// every HEADING_STEP_M of movement gives a new bearing. It stays valid for a while after stopping.
#define HEADING_STEP_M 10.0
#define HEADING_KEEP_MS 15000
static bool hdAnchor = false;
static double hdLat = 0, hdLon = 0;
static float ownHeading = 0;
static uint32_t ownHeadingMs = 0;

static void updateOwnHeading()
{
    if (!myFixOk()) {
        hdAnchor = false;
        return;
    }
    const GpsFix &g = nmea.fix;
    uint32_t now = millis();
    if (g.speedKmh >= HEADING_MIN_KMH) {
        ownHeading = g.courseDeg;
        ownHeadingMs = now;
        hdLat = g.lat;
        hdLon = g.lon;
        hdAnchor = true;
        return;
    }
    if (!hdAnchor) {
        hdLat = g.lat;
        hdLon = g.lon;
        hdAnchor = true;
        return;
    }
    if (geo_distance_m(hdLat, hdLon, g.lat, g.lon) >= HEADING_STEP_M) {
        ownHeading = (float)geo_bearing_deg(hdLat, hdLon, g.lat, g.lon);
        ownHeadingMs = now;
        hdLat = g.lat;
        hdLon = g.lon;
    }
}

static bool ownHeadingOk()
{
    return myFixOk() && ownHeadingMs && millis() - ownHeadingMs < HEADING_KEEP_MS;
}

// ---- Hunt screen ----

#define ARROW_CX 258
#define ARROW_CY 70
#define ARROW_R 44

static void huntText(int x, int y, int w, uint8_t font, uint16_t color, const char *text)
{
    gfx.setTextColor(color, UI_BG);
    gfx.setTextPadding(w);
    gfx.drawString(text, x, y, font);
}

// ---- Cards and status bar ----

// Lists that scroll to keep the selected row visible: first row shown, and the selected row's background.
static int listTop(int focus, int rows) { return focus >= rows ? focus - rows + 1 : 0; }
static void rowHighlight(int y, int h) { gfx.fillRect(6, y - 1, 308, h, UI_LINE); }
#define POP_Y0 28    // lists over the settings page (presets, time zones): first row
#define POP_ROW_H 20 // and row height
// Row of a popup list under a tap at y, -1 above the list.
static int popRowAt(int y, int top) { return y >= POP_Y0 - 1 ? top + (y - POP_Y0 + 1) / POP_ROW_H : -1; }

// Tab bars (settings, SondeHub page): four tabs 80 px wide under the status bar.
#define TAB_Y 22
#define TAB_H 18
static bool tabHit(int y) { return y >= TAB_Y && y < TAB_Y + TAB_H; }
static void drawTab(int t, const char *label, bool on)
{
    gfx.fillRoundRect(t * 80 + 2, TAB_Y, 76, TAB_H, 4, on ? UI_LINE : UI_PANEL);
    gfx.setTextDatum(TC_DATUM);
    gfx.setTextPadding(0);
    gfx.setTextColor(on ? UI_TEXT : UI_LABEL, on ? UI_LINE : UI_PANEL);
    gfx.drawString(label, t * 80 + 40, TAB_Y + 2, 2);
    gfx.setTextDatum(TL_DATUM);
}

static void cardFrame(int x, int y, int w, int h)
{
    gfx.fillRoundRect(x, y, w, h, 5, UI_PANEL);
}

static void cardText(int x, int y, int w, uint8_t font, uint16_t color, const char *text)
{
    gfx.setTextColor(color, UI_PANEL);
    gfx.setTextPadding(w);
    gfx.drawString(text, x, y, font);
}

// ---- A Yes / No question over any page. No is chosen until the user moves to Yes: Enter or a tap
// outside the buttons answer No, y / n answer at once. ----
enum { ASK_NONE, ASK_SAVE_PRESET, ASK_DELETE_PRESET, ASK_UPLOAD_SH, ASK_UPLOAD_APRS, ASK_SAVE_WIFI, ASK_DELETE_WIFI,
       ASK_UPLOAD_APRSFI, ASK_SCAN_FOUND, ASK_UPLOAD_PS };
static uint8_t askKind = ASK_NONE;
static bool askYes = false; // the button in focus
static char askLine1[48] = "", askLine2[48] = "";
#define ASK_X 20
#define ASK_Y 64
#define ASK_W 280
#define ASK_H 104
#define ASK_BTN_Y (ASK_Y + 62)
#define ASK_BTN_W 100
#define ASK_BTN_H 30
#define ASK_YES_X (ASK_X + 28)
#define ASK_NO_X (ASK_X + ASK_W - 28 - ASK_BTN_W)

static void askOpen(uint8_t kind, const char *line1, const char *line2)
{
    askKind = kind;
    askYes = false;
    snprintf(askLine1, sizeof(askLine1), "%s", line1);
    snprintf(askLine2, sizeof(askLine2), "%s", line2);
}

static void drawAsk()
{
    gfx.fillRoundRect(ASK_X - 2, ASK_Y - 2, ASK_W + 4, ASK_H + 4, 7, UI_LINE);
    gfx.fillRoundRect(ASK_X, ASK_Y, ASK_W, ASK_H, 6, UI_PANEL);
    gfx.setTextDatum(TC_DATUM);
    gfx.setTextPadding(0);
    gfx.setTextColor(UI_TEXT, UI_PANEL);
    gfx.drawString(askLine1, ASK_X + ASK_W / 2, ASK_Y + 10, 2);
    gfx.setTextColor(UI_LABEL, UI_PANEL);
    gfx.drawString(askLine2, ASK_X + ASK_W / 2, ASK_Y + 32, 2);
    for (int b = 0; b < 2; b++) {
        bool yes = b == 0, on = yes == askYes;
        int x = yes ? ASK_YES_X : ASK_NO_X;
        gfx.fillRoundRect(x, ASK_BTN_Y, ASK_BTN_W, ASK_BTN_H, 5, on ? UI_ACCENT : UI_BG);
        gfx.setTextColor(on ? UI_BG : UI_TEXT, on ? UI_ACCENT : UI_BG);
        gfx.drawString(yes ? "Yes (y)" : "No (n)", x + ASK_BTN_W / 2, ASK_BTN_Y + 7, 2);
    }
    gfx.setTextDatum(TL_DATUM);
}

// Right-aligned at xr.
static void cardTextR(int xr, int y, int w, uint8_t font, uint16_t color, const char *text)
{
    gfx.setTextDatum(TR_DATUM);
    cardText(xr, y, w, font, color, text);
    gfx.setTextDatum(TL_DATUM);
}

// T-Deck battery, 0..100 %. The ADC pin sees half the cell voltage.
static float deckBattV = 0; // smoothed cell voltage, also reported to the apps

// Battery: sampled once a second from the loop (the same smoothing on every page and with the screen off).
static void battPoll()
{
    static uint32_t lastMs = 0;
    if (lastMs && millis() - lastMs < 1000) return;
    lastMs = millis();
    float now = analogReadMilliVolts(BOARD_BAT_ADC) * 2 / 1000.0f;
    float &v = deckBattV;
    v = v == 0 ? now : 0.9f * v + 0.1f * now;
}

static int deckBattPct()
{
    if (deckBattV == 0) battPoll();
    int p = (int)((deckBattV - 3.3f) / 0.9f * 100.0f);
    return p < 0 ? 0 : p > 100 ? 100 : p;
}

static void drawStatusBar()
{
    char b[96];
    gfx.fillRect(0, 0, 320, 20, UI_PANEL);
    if (textEditing && !wifiPassOpen) {
        // The label, then the end of what is typed: a long URL, token or password keeps its cursor in view.
        snprintf(b, sizeof(b), "%s> ", EDIT_LABEL[editKind]);
        int lw = gfx.textWidth(b, 2);
        char t[72];
        snprintf(t, sizeof(t), "%s_", textInput.c_str());
        fitText(t, 2, 312 - lw, true);
        snprintf(b + strlen(b), sizeof(b) - strlen(b), "%s", t);
        cardText(4, 2, 316, 2, UI_MSG, b);
        return;
    }
    // Frame light: green while frames arrive, then the warning colour, then red when they stop.
    uint32_t age = millis() - lastOkMs;
    uint16_t dot = !haveLast ? UI_LINE : age < 1500 ? UI_GOOD : age < 10000 ? UI_WARN : UI_BAD;
    gfx.fillCircle(9, 10, 4, dot);
    snprintf(b, sizeof(b), "%.3f", cfg.freq);
    cardText(18, 2, 50, 2, UI_ACCENT, b);
    if (!radioOk) cardText(78, 2, 80, 2, UI_BAD, "RADIO ERR");
    else {
        snprintf(b, sizeof(b), "%s", track.manual ? "TARGET" : huntId()[0] ? huntId() : "no sonde");
        fitText(b, 2, 80); // long DFM/M10 serials stop before DBG
        cardText(78, 2, 80, 2, UI_TEXT, b);
    }
    if (screenLocked) { // padlock in place of DBG
        cardText(160, 2, 34, 2, UI_ACCENT, "");
        gfx.drawRoundRect(167, 2, 10, 12, 4, UI_WARN); // shackle
        gfx.drawRoundRect(168, 3, 8, 10, 3, UI_WARN);
        gfx.fillRoundRect(165, 8, 14, 10, 2, UI_WARN); // body
        gfx.fillRect(171, 11, 2, 4, UI_PANEL);        // keyhole
    } else {
        cardText(160, 2, 34, 2, UI_ACCENT, cfg.dbgServer ? "DBG" : cfg.webUi ? "WEB" : "");
    }
    if (cfg.shUpload || cfg.aprsUp || cfg.aprsFi) {
        // Every upload that is on: one in the normal size, two or three stacked in the small font.
        const char *tag[3];
        int nt = 0;
        if (cfg.shUpload) tag[nt++] = "SH";
        if (cfg.aprsUp) tag[nt++] = "RS";
        if (cfg.aprsFi) tag[nt++] = "FI";
        if (nt == 1) {
            cardText(196, 2, 18, 2, UI_BAD, tag[0]);
        } else {
            gfx.setTextPadding(0);
            gfx.setTextColor(UI_BAD, UI_PANEL);
            gfx.drawString(tag[0], 197, 1, 1);
            gfx.drawString(nt == 3 ? "RS+" : tag[1], 197, 11, 1);
        }
    } else {
        // BT: dim while waiting, green once an app is linked (Bluetooth or the Wi-Fi rdz link).
        BleStatus bs;
        ble_status(&bs);
        bool linked = bs.subscribed || rdz_link_client();
        cardText(196, 2, 18, 2, linked ? UI_GOOD : UI_LABEL, cfg.apps ? "BT" : "");
    }
    cardText(216, 2, 18, 2, sdOk ? UI_GOOD : UI_BAD, "SD");
    if (gpsSeen) snprintf(b, sizeof(b), "G%u", nmea.fix.sats);
    else snprintf(b, sizeof(b), "G--");
    cardText(238, 2, 40, 2, myFixOk() ? UI_GOOD : gpsSeen ? UI_WARN : UI_LABEL, b);
    int pct = deckBattPct();
    int w = pct * 18 / 100;
    gfx.drawRect(286, 5, 22, 10, UI_DIM);
    gfx.fillRect(308, 8, 2, 4, UI_DIM);
    gfx.fillRect(288, 7, w, 6, pct > 30 ? UI_GOOD : pct > 15 ? UI_WARN : UI_BAD);
    gfx.fillRect(288 + w, 7, 18 - w, 6, UI_PANEL);
}

// ---- Bottom bar: Back on the left, Home on the right, the page's text in between ----
#define NAV_Y 220
#define NAV_BTN_W 36
#define NAV_HIT_Y 206 // touch areas reach a bit above the bar: easier to hit with a finger
#define NAV_HIT_W 52

// Text for the bottom bar; a pending message wins over it when the bar is drawn.
static void bottomLine(const char *text, uint16_t color)
{
    snprintf(navText, sizeof(navText), "%s", text);
    navColor = color;
}

static void drawNavBar()
{
    gfx.fillRect(0, NAV_Y, 320, 240 - NAV_Y, UI_BG);
    if (screen != SCR_HOME) {
        // Back: a left-pointing arrow.
        gfx.fillRoundRect(2, NAV_Y + 1, NAV_BTN_W, 18, 4, UI_PANEL);
        gfx.fillTriangle(12, NAV_Y + 10, 20, NAV_Y + 4, 20, NAV_Y + 16, UI_ACCENT);
        gfx.fillRect(20, NAV_Y + 8, 9, 4, UI_ACCENT);
        // Home: a small house.
        int hx = 320 - 2 - NAV_BTN_W;
        gfx.fillRoundRect(hx, NAV_Y + 1, NAV_BTN_W, 18, 4, UI_PANEL);
        int cx = hx + NAV_BTN_W / 2;
        gfx.fillTriangle(cx - 8, NAV_Y + 10, cx, NAV_Y + 3, cx + 8, NAV_Y + 10, UI_ACCENT);
        gfx.fillRect(cx - 5, NAV_Y + 10, 10, 7, UI_ACCENT);
        gfx.fillRect(cx - 1, NAV_Y + 13, 3, 4, UI_PANEL);
    }
    bool m = msgActive();
    int x = screen == SCR_HOME ? 6 : NAV_BTN_W + 8;
    int w = screen == SCR_HOME ? 308 : 320 - 2 * (NAV_BTN_W + 8);
    gfx.setTextColor(m ? UI_MSG : navColor, UI_BG);
    gfx.setTextPadding(w);
    gfx.setViewport(x, NAV_Y, w, 240 - NAV_Y); // long texts are cut, they do not run over the buttons
    gfx.drawString(m ? uiMsg : navText, 0, 2, 2);
    gfx.resetViewport();
    gfx.setTextPadding(0);
}

// Compass ring with the pointing arrow.
// angleDeg, northDeg: clockwise from screen top (northDeg is minus the direction of travel, or 0 in N UP).
static void drawArrow(bool have, double angleDeg, bool headingUp, double northDeg)
{
    auto px = [&](float ang, float r) { return (int)lroundf(ARROW_CX + sinf(ang) * r); };
    auto py = [&](float ang, float r) { return (int)lroundf(ARROW_CY - cosf(ang) * r); };
    gfx.fillCircle(ARROW_CX, ARROW_CY, ARROW_R, UI_PANEL);
    gfx.drawCircle(ARROW_CX, ARROW_CY, ARROW_R, UI_LINE);
    gfx.drawCircle(ARROW_CX, ARROW_CY, ARROW_R - 1, UI_LINE);
    float north = (float)geo_norm360(northDeg) * DEG_TO_RAD;
    for (int d = 0; d < 360; d += 30) {
        float a = north + d * DEG_TO_RAD;
        float inner = d % 90 == 0 ? ARROW_R - 9 : ARROW_R - 6;
        gfx.drawLine(px(a, ARROW_R - 3), py(a, ARROW_R - 3), px(a, inner), py(a, inner), UI_LABEL);
    }
    // Cardinal points just inside the ticks; N stands out.
    static const char *const CARD[4] = {"N", "E", "S", "W"};
    gfx.setTextDatum(MC_DATUM);
    gfx.setTextPadding(0);
    for (int k = 0; k < 4; k++) {
        float a = north + k * (PI / 2);
        gfx.setTextColor(k == 0 ? UI_BAD : UI_LABEL, UI_PANEL);
        gfx.drawString(CARD[k], px(a, ARROW_R - 15), py(a, ARROW_R - 15), k == 0 ? 2 : 1);
    }
    gfx.setTextDatum(TL_DATUM);
    if (headingUp) { // marker of the direction of travel, at the top
        gfx.fillTriangle(ARROW_CX, ARROW_CY - ARROW_R + 4, ARROW_CX - 5, ARROW_CY - ARROW_R - 5, ARROW_CX + 5,
                         ARROW_CY - ARROW_R - 5, UI_ACCENT);
    }
    if (have) {
        int step = ((int)lround(geo_norm360(angleDeg) / 3.0) * 3) % 360; // 3-degree steps: no jitter
        float a = (float)step * DEG_TO_RAD;
        float rt = ARROW_R - 12, rw = ARROW_R * 0.55f, rn = ARROW_R * 0.2f;
        uint16_t col = headingUp ? UI_GOOD : UI_POS;
        int tx = px(a, rt), ty = py(a, rt);
        int nx = px(a + PI, rn), ny = py(a + PI, rn);
        gfx.fillTriangle(tx, ty, px(a + 2.6f, rw), py(a + 2.6f, rw), nx, ny, col);
        gfx.fillTriangle(tx, ty, px(a - 2.6f, rw), py(a - 2.6f, rw), nx, ny, col);
    }
    gfx.fillCircle(ARROW_CX, ARROW_CY, 3, UI_BG);
}

static bool trackStale()
{
    return !track.manual && !track.restored && millis() - track.ms > 10000;
}

static void ageText(char *b, size_t n)
{
    if (track.manual) snprintf(b, n, "manual target");
    else if (track.restored) snprintf(b, n, "saved position");
    else snprintf(b, n, "age %lu s", (unsigned long)((millis() - track.ms) / 1000));
}

// What the receiver is listening for, while no frame has been decoded.
static const char *waitText()
{
    static char b[40];
    if (typeRotates()) snprintf(b, sizeof(b), "listening: %s (auto)", sonde_type_name(rxType));
    else snprintf(b, sizeof(b), "waiting for %s frames", sonde_type_name(rxType));
    return b;
}

// Battery, temperature, kill timer, ozone and calibration of the last frame (Hunt page and web).
static void sondeInfoLine(char *b, size_t n)
{
    if (haveLast && last.frameCrcOk && last.sondeType != ST_RS41) {
        if (last.battV > 0) snprintf(b, n, "%s  %.1fV", last.model, last.battV);
        else snprintf(b, n, "%s", last.model);
    } else if (haveLast && last.frameCrcOk) {
        char t[12], kill[16];
        rs41TempKill(t, kill, false);
        int k = last.o3Valid ? snprintf(b, n, "%.1fV %s kill %s O3 %.1f", last.battV, t, kill, last.o3Mpa)
                             : snprintf(b, n, "%.1fV  %s  kill %s", last.battV, t, kill);
        if (calib.nHave < RS41_CAL_FRAMES && k > 0 && (size_t)k < n) snprintf(b + k, n - k, "  cal %d", calib.nHave);
    } else {
        b[0] = 0;
    }
}

// Altitude (on the ellipsoid) below which the sonde drops under the radio horizon, -1 when not worth saying.
// 4/3 earth radio horizon, observer 2 m above ground, flat terrain at own altitude.
static double signalLostM(double sdist)
{
    if (sdist < 0) return -1;
    double reach = sdist / 1000.0 / 4.12 - 1.41;
    double hLost = reach > 0 ? reach * reach : 0;
    return hLost > 20 ? nmea.fix.altM + nmea.fix.geoidSepM + hLost : -1;
}

static void drawHunt()
{
    char b[96];
    bool me = myFixOk();
    const GpsFix &g = nmea.fix;

    cardFrame(4, 24, 192, 94);
    cardFrame(4, 122, 116, 44);
    cardFrame(124, 122, 96, 44);
    cardFrame(224, 122, 92, 44);
    cardFrame(4, 170, 172, 42);
    cardFrame(180, 170, 64, 42);
    cardFrame(248, 170, 68, 42);
    cardText(10, 126, 0, 1, UI_LABEL, "ALTITUDE");
    cardText(130, 126, 0, 1, UI_LABEL, "V-SPEED");
    cardText(230, 126, 0, 1, UI_LABEL, "H-SPEED");
    cardText(186, 174, 0, 1, UI_LABEL, "SIGNAL");
    cardText(254, 174, 0, 1, UI_LABEL, "ME");
    cardText(102, 146, 0, 2, UI_LABEL, "m");
    cardText(196, 152, 0, 1, UI_LABEL, "m/s");
    cardText(294, 152, 0, 1, UI_LABEL, "m/s");
    drawStatusBar();

    // Distance card.
    double dist = -1, sdist = -1, brg = 0, elev = 0;
    bool toPred = false;
    if (track.have && me) {
        geo_dist_brg(g.lat, g.lon, track.lat, track.lon, &dist, &brg);
        sdist = dist;
        // Sonde altitude is ellipsoidal, NMEA altitude is MSL: compare on the ellipsoid.
        elev = geo_elevation_deg(dist, track.alt - (g.altM + g.geoidSepM));
        double pl, pn;
        int eta;
        bool fromSh;
        toPred = navPred && bestPred(&pl, &pn, &eta, &fromSh);
        if (toPred) {
            geo_dist_brg(g.lat, g.lon, pl, pn, &dist, &brg);
        }
    }
    cardText(10, 28, 110, 1, toPred ? (predSrc == PSRC_SRV ? UI_WARN : UI_PRED) : UI_LABEL,
             toPred ? (predSrc == PSRC_SRV ? "TO SERVER LANDING" : "TO LANDING POINT")
                    : track.manual ? "TO TARGET" : "DISTANCE");
    gfx.setTextPadding(0);
    if (dist >= 0) {
        const char *unit = "km";
        if (dist < 1000.0) {
            snprintf(b, sizeof(b), "%.0f", dist);
            unit = "m";
        } else if (dist < 100000.0) {
            snprintf(b, sizeof(b), "%.2f", dist / 1000.0);
        } else {
            snprintf(b, sizeof(b), "%.0f", dist / 1000.0);
        }
        gfx.setTextColor(UI_TEXT, UI_PANEL);
        int w = gfx.drawString(b, 10, 40, 6);
        gfx.fillRect(10 + w, 40, 184 - w, 48, UI_PANEL);
        gfx.setTextColor(UI_DIM, UI_PANEL);
        gfx.drawString(unit, 14 + w, 60, 4);
        snprintf(b, sizeof(b), "%03.0f %s", brg, geo_cardinal(brg));
        cardText(10, 90, 100, 4, UI_TEXT, b);
        if (toPred) b[0] = 0;
        else snprintf(b, sizeof(b), "elev %+.0f", elev);
#if PRED_SERVER
        if (toPred && predSrc == PSRC_SRV && psRes.pred.conf >= 0) { // confidence and expected error
            if (psRes.pred.m50Km >= 0) snprintf(b, sizeof(b), "%d%% %.1fkm", psRes.pred.conf, psRes.pred.m50Km);
            else snprintf(b, sizeof(b), "%d%%", psRes.pred.conf);
        }
#endif
        cardTextR(190, 96, 78, 2, UI_LABEL, b);
        bool headingUp = ownHeadingOk();
        drawArrow(true, headingUp ? brg - ownHeading : brg, headingUp, headingUp ? -ownHeading : 0);
    } else {
        gfx.setTextColor(UI_LINE, UI_PANEL);
        int w = gfx.drawString("---", 10, 40, 6);
        gfx.fillRect(10 + w, 40, 184 - w, 48, UI_PANEL);
        gfx.fillRect(10, 90, 184, 26, UI_PANEL);
        cardText(10, 96, 180, 2, UI_WARN,
                 track.have ? "no own GPS fix" : haveLast ? "sonde has no GPS fix" : waitText());
        drawArrow(false, 0, ownHeadingOk(), ownHeadingOk() ? -ownHeading : 0);
    }

    // Compass mode, always spelled out: the arrow means two different things.
    if (dist >= 0) {
        bool ahead = ownHeadingOk();
        gfx.setTextDatum(TR_DATUM);
        gfx.setTextPadding(0);
        gfx.setTextColor(ahead ? UI_GOOD : UI_POS, UI_BG);
        gfx.drawString(ahead ? "AHEAD" : "N UP", 318, 25, 1);
        gfx.setTextDatum(TL_DATUM);
    }

    // Altitude and speeds.
    bool live = track.have && !track.manual;
    if (live) {
        snprintf(b, sizeof(b), "%.0f", track.alt);
        cardText(10, 138, 90, 4, UI_TEXT, b);
        snprintf(b, sizeof(b), "%+.1f", track.vV);
        cardText(130, 138, 64, 4, track.vV > 0.5f ? UI_GOOD : track.vV < -0.5f ? UI_WARN : UI_TEXT, b);
        snprintf(b, sizeof(b), track.vH < 100.0f ? "%.1f" : "%.0f", track.vH);
        cardText(230, 138, 62, 4, UI_TEXT, b);
        snprintf(b, sizeof(b), "%s %03.0f", geo_cardinal(track.vDir), track.vDir);
        cardTextR(310, 126, 36, 1, UI_LABEL, b);
    } else {
        cardText(10, 138, 90, 4, UI_LINE, "--");
        cardText(130, 138, 64, 4, UI_LINE, "--");
        cardText(230, 138, 62, 4, UI_LINE, "--");
        cardTextR(310, 126, 36, 1, UI_LABEL, "");
    }

    // Sonde position card.
    cardText(10, 174, 70, 1, track.fromNet ? UI_ACCENT : UI_LABEL,
             track.manual ? "TARGET" : track.fromNet ? "SONDE (NET)" : haveLast ? last.model : "SONDE");
    if (track.manual) snprintf(b, sizeof(b), "typed in");
    else if (track.restored) snprintf(b, sizeof(b), "saved");
    else if (track.have && track.fromNet) snprintf(b, sizeof(b), "net  age %lu s", (unsigned long)((millis() - track.ms) / 1000));
    else if (track.have) snprintf(b, sizeof(b), "%u sat  age %lu s", track.sats, (unsigned long)((millis() - track.ms) / 1000));
    else b[0] = 0;
    cardTextR(170, 174, 90, 1, trackStale() && track.have ? UI_BAD : UI_LABEL, b); // 80..170: the label stays
    if (track.have) {
        snprintf(b, sizeof(b), "%.5f %.5f", track.lat, track.lon);
        cardText(10, 184, 160, 2, UI_POS, b);
    } else {
        cardText(10, 184, 160, 2, UI_LINE, "no position");
    }
    sondeInfoLine(b, sizeof(b));
    fitText(b, 1, 162); // stays in its card, before the SIGNAL one
    cardText(10, 202, 160, 1, UI_LABEL, b);

    // Signal card: the level, then five bars from the last frame level (drawn after it: its padding would cut
    // the fifth bar).
    if (haveLast) snprintf(b, sizeof(b), "%.0f", lastRssi);
    else snprintf(b, sizeof(b), "--");
    cardTextR(240, 186, 28, 2, UI_TEXT, b);
    int lvl = haveLast ? (int)((lastRssi + 118.0f) / 14.0f) + 1 : 0;
    if (lvl > 5) lvl = 5;
    for (int i = 0; i < 5; i++) {
        int h = 4 + 3 * i;
        gfx.fillRect(186 + 6 * i, 200 - h, 4, h, i < lvl ? UI_GOOD : UI_LINE);
    }
    if (stats.sync) snprintf(b, sizeof(b), "ok %lu%%", (unsigned long)(100 * stats.eccOk / stats.sync));
    else b[0] = 0;
    cardText(186, 203, 54, 1, UI_LABEL, b);

    // Own GPS card.
    if (me) {
        snprintf(b, sizeof(b), "%u sat", g.sats);
        cardText(254, 186, 58, 2, UI_TEXT, b);
        snprintf(b, sizeof(b), "%.0f km/h", g.speedKmh);
        cardText(254, 203, 58, 1, UI_LABEL, b);
    } else {
        cardText(254, 186, 58, 2, UI_WARN, gpsSeen ? "no fix" : "no GPS");
        if (gpsSeen) snprintf(b, sizeof(b), "%u sat", g.sats);
        else b[0] = 0;
        cardText(254, 203, 58, 1, UI_LABEL, b);
    }

    // Bottom line: message, landing prediction (alternating with the radio horizon), or key hints.
    uint16_t col = UI_LABEL;
    if (sdist >= 0 && !ownHeadingOk()) {
        snprintf(b, sizeof(b), "N UP: screen top = north, or walk");
        col = UI_POS;
    } else {
        snprintf(b, sizeof(b), "f freq  x scan  t target");
    }
    double pl, pn;
    int eta;
    bool fromSh;
    if (bestPred(&pl, &pn, &eta, &fromSh)) {
        double lostM = signalLostM(sdist);
        if (lostM > 0 && (millis() / 4000) % 2) {
            snprintf(b, sizeof(b), "signal lost below ~%.0f m", lostM);
        } else {
            snprintf(b, sizeof(b), "%s %.4f %.4f in %dm%02ds", predLabel(), pl, pn, eta / 60, eta % 60);
        }
        col = predSrc == PSRC_SRV ? UI_WARN : fromSh ? UI_ACCENT : UI_PRED;
    }
    bottomLine(b, col);
}

// The Info page as lines (row, colour, text): drawn on the screen, sent to the web page.
static void diagLines(const std::function<void(int, uint16_t, const String &)> &line)
{
    char b[96];
    snprintf(b, sizeof(b), "SondeTracker v%s  SX1262%s  SD%s", FW_NAME, radioOk ? "" : " ERR", sdOk ? "" : " no");
    line(0, UI_ACCENT, b);
    if (textEditing) {
        snprintf(b, sizeof(b), "%s> %s_  (Enter ok)", EDIT_LABEL[editKind], textInput.c_str());
        line(1, UI_MSG, b);
    } else {
        snprintf(b, sizeof(b), "F %.3f MHz  BW %.1fk  %s%s", cfg.freq, Sx1262::BW_KHZ[cfg.bwIdx], sonde_type_name(rxType),
                 cfg.stype == ST_AUTO ? " (auto)" : "");
        line(1, UI_TEXT, b);
    }
    snprintf(b, sizeof(b), "POL %s%s  RSSI idle %.1f  pkt %.1f dBm", cfg.invert ? "INV" : "NOR",
             cfg.autoInvert ? "(auto)" : "", rssiIdle, lastRssi);
    line(2, UI_TEXT, b);
    float pct = stats.sync ? 100.0f * stats.gpsOk / stats.sync : 0;
    snprintf(b, sizeof(b), "SYNC %lu  ECC %lu  GPS %lu (%.0f%%)", (unsigned long)stats.sync,
             (unsigned long)stats.eccOk, (unsigned long)stats.gpsOk, pct);
    line(3, UI_GOOD, b);
    if (haveLast) {
        char type[10] = "";
        rs41_calib_type(&calib, type);
        snprintf(b, sizeof(b), "ID %s %s  FN %u", last.id[0] ? last.id : "--------", type, last.frameNb);
        line(4, UI_TEXT, b);
        if (last.gpsValid) {
            snprintf(b, sizeof(b), "%.5f %.5f  %.0f m", last.lat, last.lon, last.alt);
            line(5, UI_MSG, b);
            snprintf(b, sizeof(b), "vH %.1f m/s  dir %.0f  vV %.1f m/s  sats %u", last.vH, last.vDir, last.vV,
                     last.numSV);
            line(6, UI_TEXT, b);
        } else {
            snprintf(b, sizeof(b), "no GPS fix (sats %u)", last.numSV);
            line(5, UI_LABEL, b);
            line(6, UI_LABEL, "");
        }
        snprintf(b, sizeof(b), "ecc %d/%d era %d crcF %d crcG %d", last.ecc1, last.ecc2, last.nEra,
                 last.frameCrcOk, last.gpsCrcOk);
        line(7, UI_DIM, b);
    } else {
        for (int r = 4; r <= 7; r++) line(r, UI_LABEL, r == 4 ? "waiting for frames..." : "");
    }
    if (cap.fullFrames())
        snprintf(b, sizeof(b), "full %lu/%lu cut %lu short %lu  %.0f dBm", (unsigned long)cap.nFull320,
                 (unsigned long)cap.nFull518, (unsigned long)cap.nCut, (unsigned long)cap.nShort, capRes.rssiAvg);
    else snprintf(b, sizeof(b), "255 B + erasures  pkt avg %.1f dBm", capRes.rssiAvg);
    line(8, UI_WARN, b);
    sondeStatusText(b, sizeof(b));
    line(9, UI_ACCENT, b);
    if (cfg.net) {
        // Internet: what SondeHub answered, and how far its position is from the one heard by radio.
        char d[24] = "";
        if (shDiffM >= 0) snprintf(d, sizeof(d), "  radio-SH %.0f m", shDiffM);
        snprintf(b, sizeof(b), "SH %d sondes  http %d  pred %s%s", netr->nSondes, netr->lastHttp,
                 shPredOk() ? "yes" : "no", d);
        line(10, UI_ACCENT, b);
    } else {
        line(10, UI_LABEL, "u pol  y type  f freq  p/a/j preset  x scan");
    }
    if (haveLast && last.o3Valid) {
        snprintf(b, sizeof(b), "O3 %.2f mPa (est.)  I %.3fuA  Tp %.1fC  #%u", last.o3Mpa, last.o3IcellUa,
                 last.o3TpumpC, last.o3Instr);
        line(11, UI_GOOD, b);
    } else if (haveLast && last.xdata) {
        snprintf(b, sizeof(b), "XDATA %.40s", last.xdataStr);
        line(11, UI_GOOD, b);
    } else {
        line(11, UI_LABEL, "q/w -+1kHz  d/e -+10kHz  r reset  l log  k KML");
    }
    snprintf(b, sizeof(b), "kb %s  touch %s  rawlog %s  gps %s %lu bd", kbOk ? "ok" : "--",
             !input_touch_ok() ? "--" : cfg.touch ? "on" : "off", cfg.rawLog ? "on" : "off",
             gpsSeen ? (myFixOk() ? "fix" : "nofix") : "--", (unsigned long)gpsBaud);
    line(12, UI_LABEL, b);
}

static void drawDiag()
{
    diagLines(uiLine);
    bottomLine("Info: trackball, q/w = freq", UI_LABEL);
}

// ---- Presets ----

// Tunes to mhz (kept within 400-406 MHz) and saves it; msg: also says so on the bottom line.
static void setFreq(double mhz, bool msg = false)
{
    cfg.freq = mhz < 400.0 ? 400.0 : mhz > 406.0 ? 406.0 : mhz;
    radioChanged();
    if (msg) setMsgf("freq %.3f MHz", cfg.freq); // visible on every screen, also where the status bar is not shown
}

#define PRESET_FILE_MAX 16384
static char *presetText = nullptr; // file contents (PSRAM)

// The chosen preset survives restarts: frequency, group and name in NVS.
static void savePresetChoice()
{
    prefs.begin("rs41pre", false);
    if (curPreset >= 0 && curPreset < nPlist) {
        prefs.putUInt("khz", plist[curPreset].khz);
        prefs.putString("grp", plist[curPreset].group);
        prefs.putString("name", plist[curPreset].name);
    } else {
        prefs.clear();
    }
    prefs.end();
}

// At boot: the preset in use when the T-Deck was switched off. If its frequency was edited in the
// file meanwhile, the new one is used.
static void restorePresetChoice()
{
    curPreset = -1;
    uint32_t khz = 0;
    char grp[16] = "", name[32] = "";
    prefs.begin("rs41pre", true);
    if (prefs.isKey("khz")) {
        khz = prefs.getUInt("khz", 0);
        prefs.getString("grp", grp, sizeof(grp));
        prefs.getString("name", name, sizeof(name));
    }
    prefs.end();
    if (!khz || khz != freqKhz()) return; // none, or the frequency was changed by hand later
    curPreset = presets_find_named(plist, nPlist, grp, name, khz);
    if (curPreset >= 0 && plist[curPreset].khz != khz) {
        cfg.freq = plist[curPreset].khz / 1000.0;
        saveSettings();
        savePresetChoice();
    }
}

// Reads /presets.txt; creates it (instructions + the presets kept until now in flash) when missing.
static void loadPresets()
{
    if (!presetText) presetText = (char *)ps_malloc(PRESET_FILE_MAX);
    if (!presetText) return;
    presetText[0] = 0;
    if (sdOk && SD.exists(presetPath)) {
        File f = SD.open(presetPath, FILE_READ);
        if (f) {
            int n = f.read((uint8_t *)presetText, PRESET_FILE_MAX - 1);
            presetText[n > 0 ? n : 0] = 0;
            f.close();
        }
    } else {
        presets_template(presetText, PRESET_FILE_MAX, nullptr, 0); // the template with the European sites
        if (sdOk) {
            File f = SD.open(presetPath, FILE_WRITE);
            if (f) {
                f.print(presetText);
                f.close();
            }
        }
    }
    nPlist = presets_parse(presetText, plist, PRESET_MAX);
    restorePresetChoice();
}

// Writes the file and reads the list again; the chosen preset is found again by group and name.
static void savePresetText()
{
    if (sdOk) {
        File f = SD.open(presetPath, FILE_WRITE);
        if (f) {
            f.print(presetText);
            f.close();
        }
    }
    Preset was = {};
    bool had = curPreset >= 0 && curPreset < nPlist;
    if (had) was = plist[curPreset];
    nPlist = presets_parse(presetText, plist, PRESET_MAX);
    curPreset = had ? presets_find_named(plist, nPlist, was.group, was.name, was.khz) : -1;
    if (curPreset >= 0 && plist[curPreset].khz != was.khz) curPreset = -1;
}

static void usePreset(int i)
{
    if (i < 0 || i >= nPlist) return;
    curPreset = i;
    savePresetChoice();
    cfg.stype = ST_AUTO; // the preset gives the type (AUTO in the preset: the decoders take turns)
    setFreq(plist[i].khz / 1000.0);
    setMsgf("preset %.3f %s %.24s", cfg.freq, sonde_type_name(plist[i].type), plist[i].name);
}

// "auto" in the list: no preset, the receiver finds the type again.
static void presetsAuto()
{
    curPreset = -1;
    savePresetChoice();
    cfg.stype = ST_AUTO;
    applyRadio();
    saveSettings();
    setMsg("no preset: sonde type auto");
}

static void addDefaultPresets()
{
    if (!presetText) return;
    int n = presets_merge_defaults(presetText, PRESET_FILE_MAX);
    if (n) savePresetText();
    if (n) setMsgf("%d European presets added", n);
    else setMsg("European presets already there");
}

// Key p: the next preset of the same group (country).
static void nextPreset()
{
    if (nPlist == 0) return setMsg("no presets: key a adds this freq");
    usePreset(presets_next_in_group(plist, nPlist, curPreset));
}

// Key a: a new preset of the current frequency and type, under [Saved]. The name is asked first (an empty
// one cancels), then made unique, then confirmed. A preset already in use is never changed: a new one is added.
static char askPresetName[sizeof(((Preset *)0)->name)] = "";
static int askPresetIndex = -1; // preset to delete

static void savePresetAsk()
{
    if (!presetText) return setMsg("no presets");
    startEdit(6);
}

// Enter in the name entry.
static void savePresetNamed(const String &typed)
{
    String in = typed;
    for (unsigned i = 0; i < in.length(); i++)
        if ((uint8_t)in[i] < 0x20) in.setCharAt(i, ' '); // a line break would add a line to presets.txt
    in.trim();
    while (in.startsWith("[")) in.remove(0, 1); // "[Name]" starts a group
    in.trim();
    if (!in.length() || !presets_unique_name(plist, nPlist, in.c_str(), askPresetName, sizeof(askPresetName)))
        return setMsg("preset not saved");
    char l1[48], l2[48];
    snprintf(l1, sizeof(l1), "Save preset \"%.24s\"?", askPresetName);
    snprintf(l2, sizeof(l2), "%.3f MHz", cfg.freq);
    askOpen(ASK_SAVE_PRESET, l1, l2);
}

static void savePresetNow()
{
    Preset q = {};
    q.khz = freqKhz();
    q.type = cfg.stype != ST_AUTO ? cfg.stype : (haveLast && millis() - lastOkMs < 60000 ? rxType : (uint8_t)ST_AUTO);
    snprintf(q.name, sizeof(q.name), "%s", askPresetName);
    if (presets_append(presetText, PRESET_FILE_MAX, q) < 0) return setMsg("preset list full");
    savePresetText();
    curPreset = presets_find_named(plist, nPlist, PRESET_SAVED_GROUP, q.name, q.khz);
    savePresetChoice();
    applyRadio();
    setMsgf("preset \"%.24s\" saved", q.name);
}

// Alt+D in the preset list.
static void deletePresetAsk(int i)
{
    if (i < 0 || i >= nPlist) return;
    askPresetIndex = i;
    char l1[48], l2[48];
    snprintf(l1, sizeof(l1), "Delete preset \"%.24s\"?", plist[i].name[0] ? plist[i].name : "(no name)");
    snprintf(l2, sizeof(l2), "%lu.%03lu MHz  %.15s", (unsigned long)(plist[i].khz / 1000),
             (unsigned long)(plist[i].khz % 1000), plist[i].group);
    askOpen(ASK_DELETE_PRESET, l1, l2);
}

static void presetFilter();

static void deletePresetNow()
{
    int i = askPresetIndex;
    if (i < 0 || i >= nPlist || !presets_remove(presetText, i)) return setMsg("preset not found");
    bool wasInUse = curPreset == i;
    savePresetText(); // finds the preset in use again by its name; the deleted one is gone
    if (wasInUse) curPreset = -1;
    savePresetChoice();
    presetFilter();
    applyRadio();
    setMsg("preset deleted");
}

// ---- Saved Wi-Fi networks: /wifi.txt on the SD card (name,password per line, also written by hand). The
// list opens over the settings page; choosing one replaces the Wi-Fi name and password (kept in NVS). ----
static char *wifiText = nullptr; // file contents (PSRAM)
static bool wifiTextFull = false; // the file is larger than the buffer: read only, never written back
static SavedNet *wifiSaved; // WIFI_SAVED_MAX networks in PSRAM (psramArrays)
static int nWifiSaved = 0;
static bool savedOpen = false;
static int savedFocus = 0; // row 0 "save this network", then the saved networks
static int askWifiIndex = -1; // network to delete
#define SAVED_ROWS 9

// Reads /wifi.txt; creates it with the instructions when missing.
static void wifiSavedLoad()
{
    if (!wifiText) wifiText = (char *)ps_malloc(WIFI_FILE_MAX);
    if (!wifiText) return;
    wifiText[0] = 0;
    wifiTextFull = false;
    if (sdOk && SD.exists(wifiPath)) {
        File f = SD.open(wifiPath, FILE_READ);
        if (f) {
            wifiTextFull = f.size() >= WIFI_FILE_MAX;
            int n = f.read((uint8_t *)wifiText, WIFI_FILE_MAX - 1);
            wifiText[n > 0 ? n : 0] = 0;
            f.close();
        }
    } else {
        snprintf(wifiText, WIFI_FILE_MAX, "%s", WIFI_FILE_TEMPLATE);
        if (sdOk) {
            File f = SD.open(wifiPath, FILE_WRITE);
            if (f) {
                f.print(wifiText);
                f.close();
            }
        }
    }
    nWifiSaved = wifilist_parse(wifiText, wifiSaved, WIFI_SAVED_MAX);
}

static bool wifiSavedWrite()
{
    if (!sdOk) return false;
    File f = SD.open(wifiPath, FILE_WRITE);
    if (!f) return false;
    size_t n = strlen(wifiText);
    bool ok = f.write((const uint8_t *)wifiText, n) == n;
    f.close();
    nWifiSaved = wifilist_parse(wifiText, wifiSaved, WIFI_SAVED_MAX);
    return ok;
}

static void savedOpenList()
{
    wifiSavedLoad();
    if (!wifiText) return setMsg("no memory for the network list");
    savedOpen = true;
    int cur = wifilist_find(wifiSaved, nWifiSaved, cfg.ssid);
    savedFocus = cur >= 0 ? cur + 1 : 0;
}

// Row 0: asks to save the network now set (name and password).
static void savedSaveAsk()
{
    if (needSsid()) return;
    if (!sdOk) return setMsg("no SD card: networks are saved in wifi.txt");
    if (wifiTextFull) return setMsg("wifi.txt too big, edit it on a PC");
    if (!wifilist_ok(cfg.ssid, cfg.pass)) return setMsg("this name cannot be saved (comma or spaces at the ends)");
    char l1[48], l2[48];
    bool there = wifilist_find(wifiSaved, nWifiSaved, cfg.ssid) >= 0;
    snprintf(l1, sizeof(l1), there ? "Update \"%.22s\"?" : "Save network \"%.22s\"?", cfg.ssid);
    snprintf(l2, sizeof(l2), cfg.pass[0] ? "with its password, in wifi.txt" : "open network, in wifi.txt");
    askOpen(ASK_SAVE_WIFI, l1, l2);
}

static void savedSaveNow()
{
    int r = wifilist_put(wifiText, WIFI_FILE_MAX, cfg.ssid, cfg.pass);
    if (r < 0) return setMsgf("list full (%d networks)", WIFI_SAVED_MAX);
    if (!wifiSavedWrite()) return setMsg("SD card: wifi.txt not written");
    int i = wifilist_find(wifiSaved, nWifiSaved, cfg.ssid);
    if (i >= 0) savedFocus = i + 1;
    setMsgf(r ? "network \"%.24s\" saved" : "network \"%.24s\" updated", cfg.ssid);
}

// Enter on a saved network: it becomes the Wi-Fi in use.
static void savedPick(int row)
{
    if (row == 0) return savedSaveAsk();
    int i = row - 1;
    if (i < 0 || i >= nWifiSaved) return;
    savedOpen = false;
    snprintf(cfg.ssid, sizeof(cfg.ssid), "%s", wifiSaved[i].ssid);
    snprintf(cfg.pass, sizeof(cfg.pass), "%s", wifiSaved[i].pass);
    settingDone();
    setMsgf("Wi-Fi: %.24s", cfg.ssid);
}

// Alt+D on a saved network.
static void savedDeleteAsk(int row)
{
    int i = row - 1;
    if (i < 0 || i >= nWifiSaved) return;
    if (!sdOk || wifiTextFull) return setMsg("wifi.txt cannot be written, edit it on a PC");
    askWifiIndex = i;
    char l1[48];
    snprintf(l1, sizeof(l1), "Delete \"%.24s\"?", wifiSaved[i].ssid);
    askOpen(ASK_DELETE_WIFI, l1, "from the list (Wi-Fi in use unchanged)");
}

static void savedDeleteNow()
{
    if (!wifilist_remove(wifiText, askWifiIndex)) return setMsg("network not found");
    if (!wifiSavedWrite()) return setMsg("SD card: wifi.txt not written");
    if (savedFocus > nWifiSaved) savedFocus = nWifiSaved;
    setMsg("network deleted from the list");
}

static void uploadOn();
static void aprsOn();
static void aprsFiOn();
static void aprsFiLoad();
static void scanKeep(bool yes);
static void goPage(uint8_t s);

static void askAnswer(bool yes)
{
    uint8_t k = askKind;
    askKind = ASK_NONE;
    if (k == ASK_SCAN_FOUND) return scanKeep(yes);
    if (!yes)
        return setMsg(k == ASK_SAVE_PRESET    ? "preset not saved"
                      : k == ASK_SAVE_WIFI    ? "network not saved"
                      : k == ASK_UPLOAD_SH    ? "SondeHub upload stays off"
                      : k == ASK_UPLOAD_APRS  ? "radiosondy.info upload stays off"
                      : k == ASK_UPLOAD_APRSFI ? "aprs.fi upload stays off"
                      : k == ASK_UPLOAD_PS    ? "prediction server stays off"
                                              : "nothing changed");
    if (k == ASK_SAVE_PRESET) savePresetNow();
    else if (k == ASK_DELETE_PRESET) deletePresetNow();
    else if (k == ASK_UPLOAD_SH) uploadOn();
    else if (k == ASK_UPLOAD_APRS) aprsOn();
    else if (k == ASK_UPLOAD_APRSFI) aprsFiOn();
#if PRED_SERVER
    else if (k == ASK_UPLOAD_PS) {
        if (!cfg.ssid[0] || !cfg.psUrl[0] || !cfg.psTok[0]) return setMsg("Wi-Fi, server URL and token first");
        cfg.psOn = true;
        settingDone("prediction server ON: real flights only!");
    }
#endif
    else if (k == ASK_SAVE_WIFI) savedSaveNow();
    else if (k == ASK_DELETE_WIFI) savedDeleteNow();
}

static void cycleSondeType()
{
    cfg.stype = (cfg.stype + 1) % ST_COUNT;
    radioChanged();
    if (cfg.stype == ST_AUTO) setMsg("sonde type: auto (decoders take turns)");
    else setMsgf("sonde type: %s only", sonde_type_name(cfg.stype));
}

// ---- Scan: RSSI sweep, then try to decode on the strongest channels ----

#define SCAN_GX 10
#define SCAN_GY 26
#define SCAN_GH 100

static double scanBinMhz(int bin)
{
    return (SCAN_START_KHZ + bin * SCAN_STEP_KHZ) / 1000.0;
}

static int scanBinOf(uint32_t khz) { return (int)((khz - SCAN_START_KHZ) / SCAN_STEP_KHZ); }

static int scanLevel(int v) // bar height, -125..-50 dBm
{
    int h = (v + 125) * SCAN_GH / 75;
    return h < 1 ? 1 : h > SCAN_GH ? SCAN_GH : h;
}

// Spectrum: the highest level seen (dim) under the current sweep, the noise threshold, candidates in red,
// channels left alone in grey.
static void drawScan()
{
    char b[64];
    uint32_t s = (millis() - scanStartMs) / 1000;
    snprintf(b, sizeof(b), "SCAN  round %d  %lu:%02lu  noise %d dBm", scanRound, (unsigned long)(s / 60),
             (unsigned long)(s % 60), scanNoise);
    huntText(2, 2, 320, 2, UI_ACCENT, b);
    gfx.drawFastHLine(SCAN_GX, SCAN_GY + SCAN_GH, 301, UI_LABEL);
    huntText(SCAN_GX, SCAN_GY + SCAN_GH + 2, 0, 2, UI_LABEL, "400");
    huntText(SCAN_GX + 140, SCAN_GY + SCAN_GH + 2, 0, 2, UI_LABEL, "403");
    huntText(SCAN_GX + 278, SCAN_GY + SCAN_GH + 2, 0, 2, UI_LABEL, "406");
    int shown = scanPhase == SCAN_SWEEP ? scanPos : SCAN_BINS;
    for (int i = 1; i < SCAN_BINS; i += 2) { // two channels per pixel column
        int x = SCAN_GX + i / 2;
        int hold = scanHold[i] > scanHold[i - 1] ? scanHold[i] : scanHold[i - 1];
        int hh = scanLevel(hold);
        gfx.drawFastVLine(x, SCAN_GY + SCAN_GH - hh, hh, UI_LINE);
        if (i < shown) {
            int v = scanRssi[i] > scanRssi[i - 1] ? scanRssi[i] : scanRssi[i - 1];
            int h = scanLevel(v);
            gfx.drawFastVLine(x, SCAN_GY + SCAN_GH - h, h, UI_GOOD);
        }
    }
    int ty = SCAN_GY + SCAN_GH - scanLevel(scan_threshold(scanNoise, SCAN_MIN_ABOVE_DB));
    for (int x = SCAN_GX; x < SCAN_GX + 300; x += 6) gfx.drawFastHLine(x, ty, 3, UI_WARN);
    uint32_t now = millis();
    for (int i = 0; i < scanSkip.n; i++)
        if ((int32_t)(scanSkip.untilMs[i] - now) > 0)
            gfx.drawFastVLine(SCAN_GX + scanBinOf(scanSkip.khz[i]) / 2, SCAN_GY - 6, 6, UI_LABEL);
    for (int i = 0; i < scanNCand; i++) gfx.drawFastVLine(SCAN_GX + scanBinOf(scanCandKhz[i]) / 2, SCAN_GY - 6, 6, UI_BAD);
    for (int i = 0; i < SCAN_MAX_CAND; i++) {
        if (scanPhase >= SCAN_TRY && i < scanNCand)
            snprintf(b, sizeof(b), "%c %.3f MHz  %d dBm  %s", i == scanIdx ? '>' : ' ', scanCandKhz[i] / 1000.0,
                     scanCandRssi[i], scanCandType[i] == ST_AUTO ? "" : sonde_type_name(scanCandType[i]));
        else
            b[0] = 0;
        huntText(2, 148 + 17 * i, 320, 2, i == scanIdx ? UI_MSG : UI_TEXT, b);
    }
    if (scanPhase == SCAN_PRESETS) snprintf(b, sizeof(b), "presets %d/%d  x stops", scanPos, nPlist);
    else if (scanPhase == SCAN_SWEEP) snprintf(b, sizeof(b), "sweeping %d%%  skipped %d  x stops", scanPos * 100 / SCAN_BINS,
                                             scan_skip_count(&scanSkip, now));
    else snprintf(b, sizeof(b), "listening %d/%d %s %s  x stops", scanIdx + 1, scanNCand, sonde_type_name(rxType),
                  cfg.invert ? "INV" : "NOR");
    if (msgActive()) huntText(2, 220, 320, 2, UI_MSG, uiMsg); // a message (burst, app...) wins for its seconds
    else huntText(2, 220, 320, 2, UI_ACCENT, b);
}

static void scanFinish(bool found)
{
    scanning = false;
    if (found) {
        saveSettings();
    } else {
        cfg.freq = scanPrevFreq;
        cfg.invert = scanPrevInvert;
        rxType = scanPrevRx;
        applyRadio();
        resetStats();
    }
}

static int scanStep = 0; // combination of polarity (and decoder, in AUTO) tried on the current candidate
// A preset candidate brings its type: only the two polarities. A swept one tries every decoder in AUTO.
static bool scanRotates() { return cfg.stype == ST_AUTO && scanCandType[scanIdx] == ST_AUTO; }
static int scanSteps() { return scanRotates() ? 2 * (ST_COUNT - 1) : 2; }

// Listens on candidate i. Each candidate gets both polarities: with the wrong one a strong sonde is
// never decoded, and the automatic switch (every 4 s) is too slow for a 3.5 s window.
static void scanTry(int i)
{
    scanIdx = i;
    scanStep = 0;
    scanPhase = SCAN_TRY;
    cfg.freq = scanCandKhz[i] / 1000.0;
    rxType = cfg.stype != ST_AUTO ? cfg.stype : scanCandType[i] != ST_AUTO ? scanCandType[i] : ST_RS41;
    if (!cfg.autoInvert) cfg.invert = scanPrevInvert; // the previous candidate left it switched
    applyRadio(); // also keeps the automatic switch out of the way for a full dwell
    resetStats();
    scanDeadline = millis() + SCAN_DWELL_MS;
}

static void scanOtherPolarity()
{
    scanStep++;
    cfg.invert = !cfg.invert;
    if (scanRotates() && scanStep % 2 == 0) rxType = sonde_type_next(rxType);
    applyRadio(true);
    scanDeadline = millis() + SCAN_DWELL_MS;
}

static int8_t scanMeasure(double mhz)
{
    int v = (int)lroundf(cap.measureRssi(mhz, 4));
    return (int8_t)(v < -128 ? -128 : v > 0 ? 0 : v);
}

// The scan runs from the main loop (scanService) until a sonde is kept or x / Back stops it.
static void scanStart()
{
    if (!radioOk) return setMsg("scan: radio error");
    scanPrevFreq = cfg.freq;
    scanPrevInvert = cfg.invert;
    scanPrevRx = rxType;
    memset(scanRssi, -128, sizeof(scanRssi));
    memset(scanLast, -128, sizeof(scanLast));
    memset(scanHold, -128, sizeof(scanHold));
    scan_skip_reset(&scanSkip);
    scanNCand = scanIdx = scanPos = scanRound = 0;
    scanNoise = -110;
    scanStartMs = millis();
    scanPhase = nPlist ? SCAN_PRESETS : SCAN_SWEEP;
    // The first readings after the radio leaves receive mode are too high (seen on the T-Deck as a
    // -94 dBm "peak" at 400.02 MHz with nothing on air): settle first.
    for (int w = 0; w < 5; w++) cap.measureRssi(scanBinMhz(0), 4);
    scanning = true;
}

// Measuring (presets or sweep): the radio is not receiving, the loop leaves the capture alone.
static bool scanMeasuring() { return scanning && (scanPhase == SCAN_PRESETS || scanPhase == SCAN_SWEEP); }

// Candidates from the measured levels, strongest first, without the channels left alone.
static void scanAddCand(uint32_t khz, uint8_t type, int8_t rssi)
{
    if (scan_skip_has(&scanSkip, khz, millis(), SCAN_SKIP_TOL_KHZ)) return;
    for (int i = 0; i < scanNCand; i++)
        if (scanCandKhz[i] == khz) return;
    int pos = scanNCand;
    while (pos > 0 && scanCandRssi[pos - 1] < rssi) pos--;
    if (pos >= SCAN_MAX_CAND) return;
    int last = scanNCand < SCAN_MAX_CAND ? scanNCand : SCAN_MAX_CAND - 1;
    for (int m = last; m > pos; m--) {
        scanCandKhz[m] = scanCandKhz[m - 1];
        scanCandType[m] = scanCandType[m - 1];
        scanCandRssi[m] = scanCandRssi[m - 1];
    }
    scanCandKhz[pos] = khz;
    scanCandType[pos] = type;
    scanCandRssi[pos] = rssi;
    if (scanNCand < SCAN_MAX_CAND) scanNCand++;
}

static void scanNextSweep()
{
    scanPhase = SCAN_SWEEP;
    scanPos = 0;
    scanNCand = scanIdx = 0;
}

// After a candidate: left alone for a while, then the next one or a new sweep.
static void scanNextCandidate()
{
    scan_skip_add(&scanSkip, scanCandKhz[scanIdx], millis(), SCAN_SKIP_MS);
    if (scanIdx + 1 < scanNCand) scanTry(scanIdx + 1);
    else scanNextSweep();
}

static void scanService()
{
    if (!scanning || askKind) return;
    if (scanPhase == SCAN_PRESETS) { // each distinct preset frequency once, then the noisy ones are candidates
        static int8_t lv[PRESET_MAX];
        static int nLv = 0;
        if (scanPos == 0) nLv = 0;
        for (int k = 0; k < SCAN_CHUNK && scanPos < nPlist; k++, scanPos++) {
            bool dup = false;
            for (int j = 0; j < scanPos && !dup; j++) dup = plist[j].khz == plist[scanPos].khz;
            if (dup) continue;
            int8_t v = scanMeasure(plist[scanPos].khz / 1000.0);
            lv[nLv++] = v;
            int b = scanBinOf(plist[scanPos].khz);
            if (b >= 0 && b < SCAN_BINS && v > scanHold[b]) scanHold[b] = v;
        }
        if (scanPos < nPlist) return;
        scanNoise = nLv ? scan_median(lv, nLv) : scanNoise;
        scanNCand = 0;
        for (int i = 0; i < nPlist; i++) {
            int b = scanBinOf(plist[i].khz);
            int8_t v = b >= 0 && b < SCAN_BINS ? scanHold[b] : -128;
            if (v >= scan_threshold(scanNoise, SCAN_MIN_ABOVE_DB)) scanAddCand(plist[i].khz, plist[i].type, v);
        }
        if (scanNCand) scanTry(0);
        else scanNextSweep();
        return;
    }
    if (scanPhase == SCAN_SWEEP) {
        for (int k = 0; k < SCAN_CHUNK && scanPos < SCAN_BINS; k++, scanPos++) {
            int8_t v = scanBinMhz(scanPos) < 400.145 ? -128 : scanMeasure(scanBinMhz(scanPos)); // no sonde at the edge
            scanRssi[scanPos] = v;
            if (v > scanHold[scanPos]) scanHold[scanPos] = v;
        }
        if (scanPos < SCAN_BINS) return;
        scanRound++;
        scanNoise = scan_median(scanRssi, SCAN_BINS);
        static int8_t both[SCAN_BINS];
        for (int i = 0; i < SCAN_BINS; i++) both[i] = scanRssi[i] > scanLast[i] ? scanRssi[i] : scanLast[i];
        memcpy(scanLast, scanRssi, sizeof(scanLast));
        int idx[SCAN_MAX_CAND * 3];
        int n = scan_pick_thr(both, SCAN_BINS, scan_threshold(scanNoise, SCAN_MIN_ABOVE_DB), 3, idx, SCAN_MAX_CAND * 3);
        scanNCand = 0;
        for (int i = 0; i < n; i++) scanAddCand(SCAN_START_KHZ + idx[i] * SCAN_STEP_KHZ, ST_AUTO, both[idx[i]]);
        if (scanNCand) scanTry(0);
        else scanPos = 0; // nothing new above the noise: sweep again
        return;
    }
    if (scanPhase == SCAN_TRY && (int32_t)(millis() - scanDeadline) > 0) {
        if (scanStep + 1 < scanSteps()) scanOtherPolarity();
        else scanNextCandidate();
    }
}

// A frame decoded while listening on a candidate: beep, and ask whether to keep it.
static void scanFound(const Rs41Result &r)
{
    scanPhase = SCAN_ASK;
    if (cfg.vol) audio_pattern(1600, 120, 3, 120);
    char l1[48], l2[48];
    snprintf(l1, sizeof(l1), "%s %s on %.3f", r.model[0] ? r.model : sonde_type_name(rxType), r.id[0] ? r.id : "",
             cfg.freq);
    snprintf(l2, sizeof(l2), "Keep it and go to Hunt?");
    askOpen(ASK_SCAN_FOUND, l1, l2);
    askYes = true; // keeping it is the usual answer; No goes on scanning
}

// Yes: the scan ends on this sonde. No: it is left alone and the scan goes on.
static void scanKeep(bool yes)
{
    if (!scanning) return;
    if (yes) {
        scanFinish(true);
        goPage(SCR_HUNT);
        return setMsg("scan: sonde kept");
    }
    scanNextCandidate();
    setMsg("scan goes on");
}

// ---- Approach screen: live signal strength for the last metres ----

#define AP_BAR_X 10
#define AP_BAR_Y 148
#define AP_BAR_W 300
#define AP_BAR_H 18

static int apBarPos(float dbm)
{
    int w = (int)((dbm + 120.0f) * AP_BAR_W / 100.0f); // -120..-20 dBm
    return w < 0 ? 0 : w > AP_BAR_W ? AP_BAR_W : w;
}

static void drawApproach()
{
    char b[96];
    uint32_t now = millis();
    if (rssiIdle > apPeak || now - apPeakMs > 4000) {
        apPeak = rssiIdle;
        apPeakMs = now;
    }
    cardFrame(4, 24, 312, 104);
    cardFrame(4, 132, 312, 40);
    cardFrame(4, 176, 312, 36);
    cardText(10, 28, 0, 1, UI_LABEL, "SIGNAL NOW");
    cardText(206, 34, 0, 1, UI_LABEL, "PEAK (4 s)");
    cardText(206, 80, 0, 1, UI_LABEL, "LAST FRAME");
    cardText(10, 136, 0, 1, UI_LABEL, "STRENGTH");
    cardText(10, 180, 0, 1, UI_LABEL, "GPS DISTANCE");
    gfx.drawRect(AP_BAR_X - 1, AP_BAR_Y - 1, AP_BAR_W + 2, AP_BAR_H + 2, UI_LINE);
    drawStatusBar();

    snprintf(b, sizeof(b), "%.0f", rssiIdle);
    gfx.setTextColor(UI_TEXT, UI_PANEL);
    gfx.setTextPadding(0);
    int w = gfx.drawString(b, 12, 50, 6);
    gfx.fillRect(12 + w, 50, 184 - w, 48, UI_PANEL);
    gfx.setTextColor(UI_DIM, UI_PANEL);
    gfx.drawString("dBm", 16 + w, 70, 4);
    snprintf(b, sizeof(b), "%.0f", apPeak);
    cardText(206, 46, 100, 4, UI_POS, b);
    if (haveLast) snprintf(b, sizeof(b), "%.0f", lastRssi);
    else snprintf(b, sizeof(b), "--");
    cardText(206, 92, 100, 4, UI_TEXT, b);

    int wv = apBarPos(rssiIdle), wp = apBarPos(apPeak);
    if (wp > AP_BAR_W - 2) wp = AP_BAR_W - 2;
    gfx.fillRect(AP_BAR_X, AP_BAR_Y, wv, AP_BAR_H, UI_GOOD);
    gfx.fillRect(AP_BAR_X + wv, AP_BAR_Y, AP_BAR_W - wv, AP_BAR_H, UI_BG);
    gfx.fillRect(AP_BAR_X + wp, AP_BAR_Y, 2, AP_BAR_H, UI_BAD);

    if (track.have && myFixOk()) {
        const GpsFix &g = nmea.fix;
        double dist, brg;
        geo_dist_brg(g.lat, g.lon, track.lat, track.lon, &dist, &brg);
        char ag[24];
        ageText(ag, sizeof(ag));
        snprintf(b, sizeof(b), "%.0f m   %03.0f %s   %s", dist, brg, geo_cardinal(brg), ag);
        cardText(10, 191, 300, 2, UI_POS, b);
    } else {
        cardText(10, 191, 300, 2, UI_LINE, "not available");
    }
    bottomLine("strongest signal = sonde   s sound", UI_LABEL);
    if (cfg.vol) audio_meter(rssiIdle);
}

// ---- Settings screen ----

static void drawUiBody();

// Settings are four tabs of rows: in a tab of up to ten rows digits 1-9 and 0 reach them directly (a longer
// tab has no digits, half the rows would lack one); the trackball and the touch screen reach every row.
// Items keep their numbers (0-47) across the tabs.
// Rows 44-46 exist only in the build with the prediction server; 47 is in both.
#define SET_ROWS 48
#if PRED_SERVER
#define SET_TAB_MAX 26
#else
#define SET_TAB_MAX 23
#endif
#define SET_TABS 4
#define SET_VISIBLE 9
#define SET_Y0 46
#define SET_ROW_H 16
static const char *const SET_TAB_NAME[SET_TABS] = {"Radio", "Network", "Display", "System"};
static const uint8_t SET_TAB_N[SET_TABS] = {5, 23 + 3 * PRED_SERVER, 8, 9};
static const uint8_t SET_TAB_ITEMS[SET_TABS][SET_TAB_MAX] = {
    {18, 19, 43, 17, 1},       // sonde type, preset, bandwidth, full frames, raw log
    {25, 32, 2, 3, 11, 47, 40, 9, 4, 28, 5, 41, 27, 35, 36,
#if PRED_SERVER
     44, 45, 46, // prediction server, its URL and token
#endif
     6, 7, 26, 31, 29, 30, 37, 38}, // Wi-Fi, internet, apps, callsign, passcode, uploads, station
    {13, 16, 23, 15, 14, 10, 33, 34}, // brightness, timeout, dimming, theme, keyboard light, touch, auto lock, trackball
    {21, 22, 0, 12, 24, 42, 8, 39, 20}}; // volume, frame volume, frame sound, time zone, DST, web, debug, password, about

// Row of an item in the current tab, -1 when it is in another tab.
static int setRowOf(int item)
{
    for (int r = 0; r < SET_TAB_N[setTab]; r++)
        if (SET_TAB_ITEMS[setTab][r] == item) return r;
    return -1;
}

static void setTabGo(int t)
{
    setTab = (t + SET_TABS) % SET_TABS;
    setFocus = setTop = 0;
}

// Draws a QR code of text with its 4-module white border at (x, y), as large as fits in maxSide pixels.
// Returns the side drawn, 0 when the text is too long.
static int drawQrCode(const char *text, int x, int y, int maxSide)
{
    static uint8_t data[400]; // enough for version 8
    // The library does not report text that does not fit (it silently made a broken version 3 code):
    // pick the version from the byte-mode capacity at error correction level L.
    static const uint8_t CAP_L[9] = {0, 17, 32, 53, 78, 106, 134, 154, 192};
    size_t len = strlen(text);
    int v;
    for (v = 1; v <= 8 && CAP_L[v] < len; v++) {
    }
    if (v > 8) return 0;
    QRCode qr;
    qrcode_initText(&qr, data, v, ECC_LOW, text);
    int scale = maxSide / (qr.size + 8);
    if (scale < 1) return 0;
    int side = (qr.size + 8) * scale;
    gfx.fillRect(x, y, side, side, TFT_WHITE);
    int x0 = x + 4 * scale, y0 = y + 4 * scale;
    for (int r = 0; r < qr.size; r++)
        for (int c = 0; c < qr.size; c++)
            if (qrcode_getModule(&qr, c, r)) gfx.fillRect(x0 + c * scale, y0 + r * scale, scale, scale, TFT_BLACK);
    return side;
}

// ---- About (last settings row): a full page over the settings ----
#define ABOUT_NAME "T-Deck SondeTracker"
#define ABOUT_AUTHOR "IT9KVB"
#define ABOUT_REPO "github.com/N1k0droid/t-deck-sonde-tracker"
#define ABOUT_REPO_URL "https://github.com/N1k0droid/t-deck-sonde-tracker"
static bool aboutOpen = false;
static bool shInfoOpen = false; // SondeHub not approved yet: how to help, QR to the document
static bool presetOpen = false; // preset list over the settings page
static bool tzOpen = false;     // time zone list over the settings page
static int tzFocus = 0;         // row of the time zone list (index in TZ_ZONES)
#define TZ_ROWS 9
static bool presetFromKey = false; // opened with key j from another page: closing goes back there
static int presetFocus = 0;        // row of the visible list
static char presetQuery[17] = "";  // search text
static int16_t presetRows[PRESET_MAX + 2]; // visible rows: -1 auto, preset index, -2 add the European sites
static int nPresetRows = 0;
#define PRESET_ROWS 9
static void openPresetList(bool fromKey);

static void presetFilter()
{
    nPresetRows = 0;
    presetRows[nPresetRows++] = -1;
    for (int i = 0; i < nPlist; i++)
        if (presets_match(plist[i], presetQuery)) presetRows[nPresetRows++] = (int16_t)i;
    if (!presetQuery[0]) presetRows[nPresetRows++] = -2;
    if (presetFocus >= nPresetRows) presetFocus = nPresetRows - 1;
}

// Networks around, over the settings page: asked with "Scan networks", the list fills when the scan ends.
static bool wifiOpen = false;
static int wifiFocus = 0;
static int wifiN = -1; // -1 scanning, -2 failed
static WifiNet wifiNets[WIFI_SCAN_MAX];

#define WIFI_ROWS 9

static void wifiScan()
{
    wifiOpen = true;
    wifiFocus = 0;
    wifiN = -1;
    uploader_scan_start();
}

// The chosen network replaces the typed name; a protected one asks its password at once (Enter alone keeps it).
// A network from the scan becomes the Wi-Fi name; on the T-Deck the password field opens next (the web page
// asks it in its own field).
static bool wifiPick(int i, bool askPass = true, const char *pass = nullptr)
{
    wifiN = uploader_scan_result(wifiNets, WIFI_SCAN_MAX); // the scan may have ended since the last redraw
    if (i < 0 || i >= wifiN) return false;
    wifiOpen = false;
    const WifiNet &w = wifiNets[i];
    bool same = !strcmp(cfg.ssid, w.ssid);
    snprintf(cfg.ssid, sizeof(cfg.ssid), "%s", w.ssid);
    if (!w.secure) {
        cfg.pass[0] = 0;
    } else if (pass) { // the web page sends name and password together
        snprintf(cfg.pass, sizeof(cfg.pass), "%s", pass);
    } else if (!same) { // another network: its saved password, never the one of the previous network
        int k = wifilist_find(wifiSaved, nWifiSaved, w.ssid);
        snprintf(cfg.pass, sizeof(cfg.pass), "%s", k >= 0 ? wifiSaved[k].pass : "");
    }
    settingDone();
    if (!w.secure) setMsgf("Wi-Fi: %.24s (open)", w.ssid);
    else if (!askPass) setMsgf(pass ? "Wi-Fi: %.24s" : "Wi-Fi: %.24s, now the password", w.ssid);
    else {
        startEdit(3);
        wifiPassOpen = true;
    }
    return true;
}

static void tzPick(int i)
{
    if (i < 0 || i >= TZ_ZONE_COUNT) return; // a tap below the list keeps it open
    tzOpen = false;
    cfg.tzMin = TZ_ZONES[i].min;
    saveSettings();
    char z[12];
    tz_offset_name(cfg.tzMin, z, sizeof(z));
    setMsgf("time zone: %s", z);
}

static void drawShInfo()
{
    gfx.fillRect(0, 21, 320, 199, UI_BG);
    gfx.setTextPadding(0);
    gfx.setTextColor(UI_ACCENT, UI_BG);
    gfx.drawString("SondeHub", 14, 28, 4);
    static const char *const L[] = {"SondeHub has yet to", "authorize this app.", "", "Help by sending your",
                                    "data: the frames are", "kept in sondehub/ on", "the SD card.", "",
                                    "How to send them:", "scan the QR code."};
    for (int i = 0; i < 10; i++) {
        gfx.setTextColor(i < 2 ? UI_TEXT : i >= 8 ? UI_POS : UI_LABEL, UI_BG);
        gfx.drawString(L[i], 14, 58 + 15 * i, 2);
    }
    drawQrCode(SH_DOC_URL, 186, 46, 126);
}

static void drawAbout()
{
    gfx.fillRect(0, 21, 320, 199, UI_BG);
    // Header: name and version, centred.
    gfx.setTextPadding(0);
    gfx.setTextDatum(TC_DATUM);
    gfx.setTextColor(UI_ACCENT, UI_BG);
    gfx.drawString(ABOUT_NAME, 160, 26, 4);
    gfx.setTextColor(UI_LABEL, UI_BG);
    gfx.drawString("Radiosonde tracker  -  v" FW_NAME, 160, 52, 2);
    gfx.setTextDatum(TL_DATUM);
    gfx.drawFastHLine(16, 72, 288, UI_LINE);
    // Body: author on the left, QR of the project on the right.
    int q = drawQrCode(ABOUT_REPO_URL, 204, 78, 111); // 111 px, ends above the footer line
    gfx.setTextColor(UI_LABEL, UI_BG);
    gfx.drawString("Developed by", 18, 82, 2);
    gfx.setTextColor(UI_TEXT, UI_BG);
    gfx.drawString(ABOUT_AUTHOR, 18, 100, 4);
    gfx.setTextColor(UI_DIM, UI_BG);
    gfx.drawString("for the radio", 18, 132, 2);
    gfx.drawString("amateur community", 18, 149, 2);
    if (q) {
        gfx.setTextColor(UI_LABEL, UI_BG);
        gfx.drawString("project page: scan the QR", 18, 174, 1);
    }
    // Footer: project address and map credits.
    gfx.drawFastHLine(16, 194, 288, UI_LINE);
    gfx.setTextDatum(TC_DATUM);
    gfx.setTextColor(UI_POS, UI_BG);
    gfx.drawString(ABOUT_REPO, 160, 198, 1);
    gfx.setTextColor(UI_LABEL, UI_BG);
    gfx.drawString("Map data (c) OpenStreetMap contributors", 160, 209, 1);
    gfx.setTextDatum(TL_DATUM);
}

static const char *const SET_NAME[SET_ROWS] = {"Frame sound",    "Raw frame log",         "Wi-Fi name",
                                               "Wi-Fi password", "Callsign",              "SondeHub upload",
                                               "Only sondes in flight", "Send my position", "Debug server",
                                               "Hunting apps",   "Touch screen",          "Internet via Wi-Fi",
                                               "Time zone",      "Screen brightness",     "Keyboard light",
                                               "Theme",          "Screen timeout",        "Full frames",
                                               "Sonde type",     "Frequency preset",      "About",
                                               "Volume",         "Frame volume",          "Screen dimming",
                                               "Daylight saving", "Scan networks",       "Station",
                                               "radiosondy.info upload", "APRS passcode", "Mobile update",
                                               "Fixed update",    "Mobile icon",           "Saved networks",
                                               "Auto lock",       "Trackball speed",       "aprs.fi upload",
                                               "aprs.fi comment", "GPS fix wait",          "Min distance",
                                               "Web password",    "Launch predictions",    "SondeHub JSON log",
                                               "Web interface",   "Receiver bandwidth"
#if PRED_SERVER
                                               , "Prediction server", "Server URL", "Server token"
#else
                                               , "", "", ""
#endif
                                               , "Wi-Fi power save"
};

// Value of every settings row, through val(item, colour, text): drawn by drawSettings, sent by /api/settings.
static void settingsValues(const std::function<void(int, uint16_t, const char *)> &val)
{
    static const char *const APPS[3] = {"off", "Bluetooth", "BT + Wi-Fi"};
    char b[96];
    UploaderStatus st;
    uploader_status(&st);
    val(0, cfg.fvol ? UI_DIM : UI_LABEL, cfg.sound ? "pitch beep" : "soft tick");
    snprintf(b, sizeof(b), cfg.vol ? "%u" : "%u  mute", cfg.vol);
    val(21, cfg.vol ? UI_DIM : UI_LABEL, b);
    snprintf(b, sizeof(b), cfg.fvol ? "%u" : "%u  mute", cfg.fvol);
    val(22, cfg.fvol ? UI_DIM : UI_LABEL, b);
    {
        static const char *const DIML[4] = {"screen off", "light", "medium", "strong"};
        val(23, cfg.dimLvl ? UI_ACCENT : UI_DIM, DIML[cfg.dimLvl]);
    }
    val(1, cfg.rawLog ? UI_ACCENT : UI_DIM, cfg.rawLog ? "on" : "off");
    snprintf(b, sizeof(b), "%.18s", cfg.ssid[0] ? cfg.ssid : "not set");
    val(2, cfg.ssid[0] ? UI_DIM : UI_LABEL, b);
    val(3, cfg.pass[0] ? UI_DIM : UI_LABEL, cfg.pass[0] ? "change" : "set");
    val(25, UI_ACCENT, "search");
    if (nWifiSaved) snprintf(b, sizeof(b), "%d saved", nWifiSaved);
    val(32, nWifiSaved ? UI_DIM : UI_LABEL, nWifiSaved ? b : "none");
    val(4, cfg.call[0] ? UI_DIM : UI_LABEL, cfg.call[0] ? cfg.call : "not set");
    if (!SH_TELEMETRY_APPROVED) val(5, UI_LINE, "awaiting approval");
    else val(5, cfg.shUpload ? UI_ACCENT : UI_DIM, cfg.shUpload ? "on" : "off"); // as every row: on accent, off dim
    val(41, cfg.shLog ? UI_ACCENT : UI_DIM, cfg.shLog ? "on" : "off");
    val(6, cfg.shFlightOnly ? UI_ACCENT : UI_DIM, cfg.shFlightOnly ? "on" : "off");
    if (cfg.sharePos == POS_SH && !SH_TELEMETRY_APPROVED) val(7, UI_LINE, "SondeHub (n/a)");
    else val(7, cfg.sharePos ? UI_ACCENT : UI_DIM, POS_NAME[cfg.sharePos]);
    val(26, UI_DIM, cfg.shMobile ? "mobile" : "fixed");
    val(27, cfg.aprsUp ? UI_ACCENT : UI_DIM, cfg.aprsUp ? "on" : "off");
    val(28, cfg.aprsPass >= 0 ? UI_DIM : UI_LABEL, cfg.aprsPass >= 0 ? "change" : "set");
    snprintf(b, sizeof(b), STA_MOB_S[cfg.staMob] < 60 ? "%u s" : "%u min",
             STA_MOB_S[cfg.staMob] < 60 ? STA_MOB_S[cfg.staMob] : STA_MOB_S[cfg.staMob] / 60);
    val(29, cfg.shMobile ? UI_DIM : UI_LABEL, b);
    snprintf(b, sizeof(b), "%u min", STA_FIX_MIN[cfg.staFix]);
    val(30, cfg.shMobile ? UI_LABEL : UI_DIM, b);
    val(31, cfg.shMobile ? UI_DIM : UI_LABEL, APRS_MOBILE_NAME[cfg.mobIcon]);
    val(35, cfg.aprsFi ? UI_ACCENT : UI_DIM, cfg.aprsFi ? "on" : "off");
    val(40, UI_DIM, LAUNCH_H_NAME[cfg.launchH]);
    val(47, cfg.wifiPs ? UI_ACCENT : UI_DIM, cfg.wifiPs ? "on" : "off");
    {
        static const char *const MM[MIN_MOVE_N] = {"off", "200 m", "500 m", "1 km"};
        val(37, UI_DIM, FIX_WAIT_NAME[cfg.fixWait]);
        val(38, cfg.minMove ? UI_ACCENT : UI_DIM, MM[cfg.minMove]);
    }
    val(36, cfg.aprsFiCmt ? UI_ACCENT : UI_DIM, cfg.aprsFiCmt ? "on" : "off");
    val(8, cfg.dbgServer ? UI_ACCENT : UI_DIM, !cfg.dbgServer ? "off" : st.wifiConnected ? "on, /debug" : "connecting");
    val(42, cfg.webUi ? UI_ACCENT : UI_DIM, !cfg.webUi ? "off" : st.wifiConnected ? st.ip : "connecting");
    val(39, strcmp(cfg.dbgPass, DEBUG_PASS_DEFAULT) ? UI_DIM : UI_WARN,
        strcmp(cfg.dbgPass, DEBUG_PASS_DEFAULT) ? "changed" : "default");
    val(9, cfg.apps ? UI_ACCENT : UI_DIM, APPS[cfg.apps]);
    val(10, input_touch_ok() && cfg.touch ? UI_ACCENT : UI_DIM, !input_touch_ok() ? "not found" : cfg.touch ? "on" : "off");
    {
        char clk[12], z[12];
        localClock(clk, sizeof(clk), false);
        tz_offset_name(cfg.tzMin, z, sizeof(z));
        snprintf(b, sizeof(b), "%s  %s", z, clk);
        val(12, UI_DIM, b);
        val(24, cfg.dst == TZ_DST_EU ? UI_DIM : UI_ACCENT, dstName());
    }
    {
        val(13, UI_DIM, BRIGHT_NAME[cfg.bright]);
        val(14, UI_LABEL, "Alt+B");
        val(15, UI_DIM, th->name);
        static const char *const DIMN[4] = {"always on", "1 min", "5 min", "10 min"};
        val(16, UI_DIM, DIMN[cfg.dim]);
        static const char *const ALN[4] = {"off", "1 min", "2 min", "5 min"};
        val(33, cfg.autoLock ? UI_ACCENT : UI_DIM, ALN[cfg.autoLock]);
        val(34, cfg.tbSpeed ? UI_ACCENT : UI_DIM, TB_NAME[cfg.tbSpeed]);
        val(17, cfg.fullFrames ? UI_ACCENT : UI_DIM, cfg.fullFrames ? "on" : "off");
        snprintf(b, sizeof(b), "%.1f kHz%s", Sx1262::BW_KHZ[cfg.bwIdx], cfg.bwIdx == BW_DEFAULT ? "" : " (default 14.6)");
        val(43, cfg.bwIdx == BW_DEFAULT ? UI_LABEL : UI_WARN, b);
#if PRED_SERVER
        val(44, cfg.psOn ? UI_BAD : UI_DIM, cfg.psOn ? "on" : "off");
        val(45, cfg.psUrl[0] ? UI_DIM : UI_LABEL, cfg.psUrl[0] ? "change" : "set"); // not shown: screenshots
        val(46, cfg.psTok[0] ? UI_DIM : UI_LABEL, cfg.psTok[0] ? "change" : "set");
#endif
        if (cfg.stype == ST_AUTO) snprintf(b, sizeof(b), "auto (%s)", sonde_type_name(rxType));
        else snprintf(b, sizeof(b), "%s", sonde_type_name(cfg.stype));
        val(18, cfg.stype == ST_AUTO ? UI_DIM : UI_ACCENT, b);
        int pi = activePreset();
        if (pi >= 0) snprintf(b, sizeof(b), "%.16s", plist[pi].name[0] ? plist[pi].name : sonde_type_name(plist[pi].type));
        else snprintf(b, sizeof(b), nPlist ? "auto (%d in list)" : "none", nPlist);
        val(19, pi >= 0 ? UI_ACCENT : UI_DIM, b);
        val(20, UI_LABEL, ABOUT_AUTHOR);
    }
    {
        bool fresh = st.netOkS >= 0 && st.netOkS < 300; // a server answered in the last 5 min
        const char *t = !cfg.net               ? "off"
                        : !st.wifiConnected    ? "connecting"
                        : fresh && netTimeOk() ? "online"
                        : st.netFails          ? "no internet"
                        : fresh                ? "no time yet"
                                               : "connected";
        val(11, !cfg.net ? UI_DIM : st.netFails && !fresh ? UI_WARN : UI_ACCENT, t);
    }
}

static void drawSettings()
{
    char b[96];
    UploaderStatus st;
    uploader_status(&st);
    const int nRows = SET_TAB_N[setTab];
    if (setFocus >= nRows) setFocus = nRows - 1;
    if (setFocus < setTop) setTop = setFocus;
    if (setFocus >= setTop + SET_VISIBLE) setTop = setFocus - SET_VISIBLE + 1;
    drawStatusBar();
    for (int t = 0; t < SET_TABS; t++) drawTab(t, SET_TAB_NAME[t], t == setTab);
    cardFrame(4, 42, 312, 148);
    cardFrame(4, 193, 312, 20);
    // A scrolling tab keeps a strip on the right for the scroll marks. The selection bar is drawn first and
    // covers the whole text cell (16 px); text is drawn without background so no row paints over another.
    const bool scrolls = nRows > SET_VISIBLE;
    const int right = scrolls ? 300 : 308;
    gfx.fillRoundRect(6, SET_Y0 + SET_ROW_H * (setFocus - setTop) - 1, right - 2, SET_ROW_H + 1, 3, UI_LINE);
    auto val = [&](int i, uint16_t col, const char *t) {
        int r = setRowOf(i);
        int row = r - setTop;
        if (r < 0 || row < 0 || row >= SET_VISIBLE) return;
        int y = SET_Y0 + SET_ROW_H * row;
        if (r == setFocus && col == UI_LINE) col = UI_LABEL; // grey values ("n/a") on the grey bar
        gfx.setTextPadding(0);
        if (nRows <= 10) { // digit of the row
            char n[4];
            snprintf(n, sizeof(n), "%d", (r + 1) % 10);
            gfx.setTextColor(UI_ACCENT);
            gfx.drawString(n, 12, y, 2);
        }
        gfx.setTextColor(UI_TEXT);
        gfx.drawString(SET_NAME[i], nRows <= 10 ? 30 : 12, y, 2);
        gfx.setTextDatum(TR_DATUM);
        gfx.setTextColor(col);
        gfx.drawString(t, right, y, 2);
        gfx.setTextDatum(TL_DATUM);
    };
    settingsValues(val);
    // Scroll marks in the right strip, at the first and the last visible row.
    if (setTop > 0) gfx.fillTriangle(309, SET_Y0 + 2, 305, SET_Y0 + 9, 313, SET_Y0 + 9, UI_LABEL);
    if (setTop + SET_VISIBLE < nRows) {
        int yb = SET_Y0 + SET_ROW_H * SET_VISIBLE - 4;
        gfx.fillTriangle(309, yb, 305, yb - 7, 313, yb - 7, UI_LABEL);
    }

    // One status line: Wi-Fi, then the app link.
    BleStatus bs;
    ble_status(&bs);
    const char *wifi = !st.wifiOn ? "Wi-Fi off" : !st.wifiConnected ? "Wi-Fi connecting" : st.ip;
    if (!cfg.apps) snprintf(b, sizeof(b), "%s", wifi);
    else snprintf(b, sizeof(b), "%s   %s %s%s", wifi, bs.name, bs.subscribed ? "app: linked" : "app: waiting",
                  rdz_link_client() ? " +Wi-Fi app" : "");
    fitText(b, 1, 300);
    cardText(10, 199, 300, 1, UI_LABEL, b);
    if (aboutOpen) {
        drawAbout();
        return bottomLine("About   -   any key or tap: close", UI_LABEL);
    }
    if (shInfoOpen) {
        drawShInfo();
        return bottomLine("SondeHub   -   any key or tap: close", UI_LABEL);
    }
    if (wifiPassOpen) {
        cardFrame(4, 21, 312, 193);
        gfx.setTextPadding(0);
        gfx.setTextColor(UI_LABEL, UI_PANEL);
        gfx.drawString("Password of", 14, 48, 2);
        char t[40];
        snprintf(t, sizeof(t), "%.32s", cfg.ssid);
        fitText(t, 4, 290);
        gfx.setTextColor(UI_TEXT, UI_PANEL);
        gfx.drawString(t, 14, 68, 4);
        gfx.drawRoundRect(10, 110, 300, 30, 4, UI_ACCENT);
        String shown = textInput + "_";
        while (shown.length() > 1 && gfx.textWidth(shown.c_str(), 2) > 286) shown.remove(0, 1); // the end stays in view
        gfx.setTextColor(UI_MSG, UI_PANEL);
        gfx.drawString(shown.c_str(), 18, 117, 2);
        gfx.setTextColor(UI_LABEL, UI_PANEL);
        gfx.drawString(cfg.pass[0] ? "Enter alone keeps the saved password" : "type it, then Enter", 14, 156, 2);
        return bottomLine("Enter: save   Esc: back to the list", UI_LABEL);
    }
    if (wifiOpen) {
        int n = uploader_scan_result(wifiNets, WIFI_SCAN_MAX);
        wifiN = n;
        cardFrame(4, 21, 312, 193);
        if (n <= 0) {
            gfx.setTextPadding(0);
            gfx.setTextColor(UI_LABEL, UI_PANEL);
            gfx.drawString(n == -1 ? "scanning..." : n == 0 ? "no networks found" : "scan failed", 10, 48, 2);
        }
        if (wifiFocus >= n) wifiFocus = n > 0 ? n - 1 : 0;
        int top = listTop(wifiFocus, WIFI_ROWS);
        for (int r = 0; r < WIFI_ROWS && top + r < n; r++) {
            int i = top + r, y = POP_Y0 + r * POP_ROW_H;
            bool sel = i == wifiFocus, cur = !strcmp(wifiNets[i].ssid, cfg.ssid);
            uint16_t bg = sel ? UI_LINE : UI_PANEL;
            if (sel) rowHighlight(y, POP_ROW_H - 1);
            char name[34], sig[16];
            snprintf(name, sizeof(name), "%s", wifiNets[i].ssid);
            for (size_t k = strlen(name); k && gfx.textWidth(name, 2) > 200; k--) name[k - 1] = 0;
            gfx.setTextPadding(0);
            gfx.setTextColor(cur ? UI_GOOD : UI_TEXT, bg);
            gfx.drawString(name, 10, y, 2);
            snprintf(sig, sizeof(sig), "%d dBm %s", wifiNets[i].rssi, wifiNets[i].secure ? "lock" : "open");
            gfx.setTextDatum(TR_DATUM);
            gfx.setTextColor(UI_LABEL, bg);
            gfx.drawString(sig, 308, y, 2);
            gfx.setTextDatum(TL_DATUM);
        }
        return bottomLine(n > 0 ? "Enter: use this network   Back: close" : "Back: close", UI_LABEL);
    }
    if (savedOpen) {
        cardFrame(4, 21, 312, 193);
        int nr = nWifiSaved + 1;
        if (savedFocus >= nr) savedFocus = nr - 1;
        int top = listTop(savedFocus, SAVED_ROWS);
        for (int r = 0; r < SAVED_ROWS && top + r < nr; r++) {
            int row = top + r, y = POP_Y0 + r * POP_ROW_H;
            bool sel = row == savedFocus;
            uint16_t bg = sel ? UI_LINE : UI_PANEL;
            if (sel) rowHighlight(y, POP_ROW_H - 1);
            gfx.setTextPadding(0);
            if (row == 0) {
                char t[48];
                snprintf(t, sizeof(t), cfg.ssid[0] ? "+ save this network (%s)" : "+ save this network", cfg.ssid);
                for (size_t k = strlen(t); k && gfx.textWidth(t, 2) > 298; k--) t[k - 1] = 0;
                gfx.setTextColor(UI_ACCENT, bg);
                gfx.drawString(t, 10, y, 2);
                continue;
            }
            const SavedNet &w = wifiSaved[row - 1];
            bool cur = !strcmp(w.ssid, cfg.ssid);
            char name[34];
            snprintf(name, sizeof(name), "%s", w.ssid);
            for (size_t k = strlen(name); k && gfx.textWidth(name, 2) > 220; k--) name[k - 1] = 0;
            gfx.setTextColor(cur ? UI_GOOD : UI_TEXT, bg);
            gfx.drawString(name, 10, y, 2);
            gfx.setTextDatum(TR_DATUM);
            gfx.setTextColor(UI_LABEL, bg);
            gfx.drawString(cur ? "in use" : w.pass[0] ? "lock" : "open", 308, y, 2);
            gfx.setTextDatum(TL_DATUM);
        }
        if (!nWifiSaved) {
            gfx.setTextColor(UI_LABEL, UI_PANEL);
            gfx.drawString(sdOk ? "no saved networks yet" : "no SD card", 10, POP_Y0 + POP_ROW_H, 2);
        }
        return bottomLine(savedFocus ? "Enter: use   Alt+D: delete   Back: close" : "Enter: save   Back: close",
                          UI_LABEL);
    }
    if (tzOpen) {
        cardFrame(4, 21, 312, 193);
        int top = listTop(tzFocus, TZ_ROWS);
        for (int r = 0; r < TZ_ROWS && top + r < TZ_ZONE_COUNT; r++) {
            int i = top + r, y = POP_Y0 + r * POP_ROW_H;
            bool sel = i == tzFocus, cur = TZ_ZONES[i].min == cfg.tzMin;
            uint16_t bg = sel ? UI_LINE : UI_PANEL;
            if (sel) rowHighlight(y, POP_ROW_H - 1);
            char z[12];
            tz_offset_name(TZ_ZONES[i].min, z, sizeof(z));
            gfx.setTextPadding(0);
            gfx.setTextColor(cur ? UI_GOOD : UI_TEXT, bg);
            gfx.drawString(z, 10, y, 2);
            gfx.setTextColor(cur ? UI_GOOD : UI_LABEL, bg);
            gfx.drawString(TZ_ZONES[i].places, 96, y, 2);
        }
        return bottomLine("winter time; DST in the next row", UI_LABEL);
    }
    if (presetOpen) {
        cardFrame(4, 21, 312, 193);
        int top = listTop(presetFocus, PRESET_ROWS);
        int act = activePreset();
        for (int r = 0; r < PRESET_ROWS && top + r < nPresetRows; r++) {
            int row = top + r, i = presetRows[row], y = POP_Y0 + r * POP_ROW_H;
            bool sel = row == presetFocus;
            uint16_t bg = sel ? UI_LINE : UI_PANEL;
            if (sel) rowHighlight(y, POP_ROW_H - 1);
            gfx.setTextPadding(0);
            if (i < 0) { // auto, or the action at the end
                gfx.setTextColor(i == -1 && act < 0 ? UI_GOOD : UI_ACCENT, bg);
                gfx.drawString(i == -1 ? "auto  (no preset: the type is found)" : "+ add the European launch sites", 10, y, 2);
                continue;
            }
            const Preset &p = plist[i];
            char f[12], name[32];
            snprintf(f, sizeof(f), "%lu.%03lu", (unsigned long)(p.khz / 1000), (unsigned long)(p.khz % 1000));
            gfx.setTextColor(i == act ? UI_GOOD : UI_TEXT, bg);
            gfx.drawString(f, 10, y, 2);
            gfx.setTextColor(UI_LABEL, bg);
            gfx.drawString(p.type == ST_AUTO ? "" : sonde_type_short(p.type), 64, y, 2);
            int gw = p.group[0] ? gfx.textWidth(p.group, 1) + 8 : 0;
            snprintf(name, sizeof(name), "%s", p.name);
            for (size_t n = strlen(name); n && gfx.textWidth(name, 2) > 308 - gw - 122; n--) name[n - 1] = 0;
            gfx.setTextColor(i == act ? UI_GOOD : UI_TEXT, bg);
            gfx.drawString(name, 122, y, 2);
            if (gw) {
                gfx.setTextDatum(TR_DATUM);
                gfx.setTextColor(UI_LABEL, bg);
                gfx.drawString(p.group, 308, y + 4, 1);
                gfx.setTextDatum(TL_DATUM);
            }
        }
        if (nPresetRows == 1) {
            gfx.setTextColor(UI_LABEL, UI_PANEL);
            gfx.drawString("nothing found", 10, 48, 2);
        }
        char t[48];
        if (presetQuery[0]) snprintf(t, sizeof(t), "search: %s_   %d found", presetQuery, nPresetRows - 1);
        else snprintf(t, sizeof(t), "type: search  < > country  Alt+D delete");
        return bottomLine(t, presetQuery[0] ? UI_ACCENT : UI_LABEL);
    }
    bottomLine(SET_TAB_N[setTab] <= 10 ? "0-9/click change   space: tab" : "click change   space: tab", UI_LABEL);
}

// ---- Map screen ----

static void trackPointAdd(double lat, double lon)
{
    if (nTrk && millis() - trkMs < TRK_INTERVAL_MS) return;
    trkMs = millis();
    if (nTrk == TRK_MAX) { // full: keep every other point
        for (int i = 0; i < TRK_MAX / 2; i++) {
            trkLat[i] = trkLat[2 * i];
            trkLon[i] = trkLon[2 * i];
        }
        nTrk = TRK_MAX / 2, trkGen++;
    }
    trkLat[nTrk] = (float)lat;
    trkLon[nTrk] = (float)lon;
    nTrk++;
}

#define MAP_BTN 30
#define MAP_BTN_X (320 - MAP_BTN - 6)
#define MAP_BTN_Y 8
#define QR_BTN_X 6
#define QR_BTN_Y 8

// ---- QR code of a position: the phone camera opens Google Maps with navigation to it ----

static bool qrPoint(int mode, double *lat, double *lon)
{
    if (mode == 1 && track.have) {
        *lat = track.lat;
        *lon = track.lon;
        return true;
    }
    int eta;
    bool sh;
    return mode == 2 && bestPred(lat, lon, &eta, &sh);
}

// Opens the QR (sonde first), or switches to the other point when both exist.
static void qrNext()
{
    double la, lo;
    bool sonde = qrPoint(1, &la, &lo), land = qrPoint(2, &la, &lo);
    if (!sonde && !land) return setMsg("no sonde position or landing point yet");
    if (qrMode == 0) qrMode = sonde ? 1 : 2;
    else if (qrMode == 1 && land) qrMode = 2;
    else if (qrMode == 2 && sonde) qrMode = 1;
}

static void drawQr()
{
    double lat, lon;
    if (!qrPoint(qrMode, &lat, &lon)) {
        qrMode = 0;
        return;
    }
    char url[120];
    snprintf(url, sizeof(url), "https://www.google.com/maps/dir/?api=1&destination=%.6f,%.6f", lat, lon);
    gfx.fillSprite(UI_BG);
    int side = drawQrCode(url, 4, 4, 212); // stays above the bottom bar
    if (!side) {
        qrMode = 0;
        return setMsg("QR: text too long");
    }
    // Caption on the right.
    int cx = side + 12;
    char b[24];
    cardText(cx, 10, 320 - cx, 2, UI_ACCENT, qrMode == 1 ? "SONDE" : "LANDING");
    snprintf(b, sizeof(b), "%.5f", lat);
    cardText(cx, 32, 320 - cx, 2, UI_POS, b);
    snprintf(b, sizeof(b), "%.5f", lon);
    cardText(cx, 50, 320 - cx, 2, UI_POS, b);
    cardText(cx, 80, 320 - cx, 1, UI_LABEL, "scan with the");
    cardText(cx, 92, 320 - cx, 1, UI_LABEL, "phone camera:");
    cardText(cx, 104, 320 - cx, 1, UI_LABEL, "Google Maps");
    cardText(cx, 116, 320 - cx, 1, UI_LABEL, "navigation");
    double l2, o2;
    if (qrPoint(qrMode == 1 ? 2 : 1, &l2, &o2)) {
        cardText(cx, 150, 320 - cx, 1, UI_TEXT, "tap / click:");
        cardText(cx, 162, 320 - cx, 1, UI_TEXT, qrMode == 1 ? "landing point" : "sonde position");
    }
    cardText(cx, 196, 320 - cx, 1, UI_LABEL, "Back: map");
}

static void drawMap()
{
    char b[96];
    if (qrMode) {
        drawQr();
        if (qrMode) return bottomLine("QR: navigation on the phone", UI_LABEL);
    }
    bool me = myFixOk();
    const GpsFix &g = nmea.fix;
    if (!mapOk) {
        huntText(2, 2, 320, 2, UI_ACCENT, "MAP");
        snprintf(b, sizeof(b), "not available: %s", mapview_error());
        huntText(2, 40, 320, 2, UI_WARN, b);
        huntText(2, 70, 320, 2, UI_DIM, "Put PNG tiles on the SD card as");
        huntText(2, 88, 320, 2, UI_DIM, "/maps/<zoom>/<x>/<y>.png and restart.");
        huntText(2, 106, 320, 2, UI_DIM, "See the README, Offline map.");
        bottomLine("map not available", UI_LABEL);
        return;
    }
    MapScene s = {};
    s.haveMe = me;
    s.meLat = g.lat;
    s.meLon = g.lon;
    s.meMoving = ownHeadingOk();
    s.meCourse = ownHeading;
    s.haveTarget = track.have;
    s.tLat = track.lat;
    s.tLon = track.lon;
    s.targetManual = track.manual;
    s.targetStale = track.restored || trackStale();
    s.havePred = pred.ok && !track.manual;
    s.pLat = pred.lat;
    s.pLon = pred.lon;
    s.haveSrvPred = psPredOk();
    if (s.haveSrvPred) srvPoint(&s.srvLat, &s.srvLon, &s.srvEta);
    const ShPrediction *sp = shPred();
    s.haveShPred = shPredOk() && sp->n > 0;
    if (s.haveShPred) {
        s.shpLat = sp->landLat;
        s.shpLon = sp->landLon;
        s.shLat = sp->lat;
        s.shLon = sp->lon;
        s.nShPred = sp->n;
    }
    s.trkLat = trkLat;
    s.trkLon = trkLon;
    s.nTrk = nTrk;
    crumb("map compose");
    mapview_draw(s, &gfx);

    if (track.have && me) {
        double pl, pn;
        int eta;
        bool fromSh;
        bool toPred = navPred && bestPred(&pl, &pn, &eta, &fromSh);
        double tl = toPred ? pl : track.lat, tn = toPred ? pn : track.lon;
        double dist, brg;
        geo_dist_brg(g.lat, g.lon, tl, tn, &dist, &brg);
        char d[16];
        fmtDist(d, sizeof(d), dist, 2);
        snprintf(b, sizeof(b), "%s%s%s %03.0f %s  alt %.0f  vV %+.1f", toPred ? predLabel() : "", toPred ? " " : "", d,
                 brg, geo_cardinal(brg), track.alt, track.vV);
    } else if (track.have) {
        snprintf(b, sizeof(b), "no own GPS fix   alt %.0f  vV %+.1f", track.alt, track.vV);
    } else {
        snprintf(b, sizeof(b), "%s", me ? "no sonde position" : "no sonde position, no own GPS fix");
    }
    gfx.fillRect(0, 200, 320, 20, UI_PANEL);
    cardText(6, 203, 308, 2, trackStale() && track.have ? UI_WARN : UI_TEXT, b);
    // QR button, top left: three finder squares like a tiny QR code.
    gfx.fillRoundRect(QR_BTN_X, QR_BTN_Y, MAP_BTN, MAP_BTN, 5, UI_PANEL);
    gfx.drawRoundRect(QR_BTN_X, QR_BTN_Y, MAP_BTN, MAP_BTN, 5, UI_LINE);
    for (int k = 0; k < 3; k++) {
        int fx = QR_BTN_X + 6 + (k == 1 ? 11 : 0), fy = QR_BTN_Y + 6 + (k == 2 ? 11 : 0);
        gfx.drawRect(fx, fy, 8, 8, UI_TEXT);
        gfx.fillRect(fx + 3, fy + 3, 2, 2, UI_TEXT);
    }
    gfx.fillRect(QR_BTN_X + 18, QR_BTN_Y + 18, 3, 3, UI_TEXT);
    gfx.fillRect(QR_BTN_X + 22, QR_BTN_Y + 21, 2, 3, UI_TEXT);
    // Touch zoom buttons on the right edge of the map.
    for (int i = 0; i < 2; i++) {
        int y = MAP_BTN_Y + i * (MAP_BTN + 6);
        gfx.fillRoundRect(MAP_BTN_X, y, MAP_BTN, MAP_BTN, 5, UI_PANEL);
        gfx.drawRoundRect(MAP_BTN_X, y, MAP_BTN, MAP_BTN, 5, UI_LINE);
        gfx.fillRect(MAP_BTN_X + 8, y + MAP_BTN / 2 - 1, MAP_BTN - 16, 3, UI_TEXT);
        if (i == 0) gfx.fillRect(MAP_BTN_X + MAP_BTN / 2 - 1, y + 8, 3, MAP_BTN - 16, UI_TEXT);
    }
    int z = mapview_zoom_level(), zt = mapview_tile_zoom_max();
    char zx[8] = "";
    if (z > zt) snprintf(zx, sizeof(zx), " x%d", 1 << (z - zt)); // digital enlargement
    // Alternate the key hints with the tile counters, so a loading problem can be told apart.
    if ((millis() / 4000) % 2) mapview_stats(b, sizeof(b));
    else snprintf(b, sizeof(b), "z%d%s %s%s  i/o zoom  c centre", z, zx, mapview_center_name(), mapview_panned() ? "+" : "");
    bottomLine(b, UI_LABEL);
}

// ---- Home page: eight large buttons, each with a line icon and a live status line ----

#define HOME_COLS 4
#define HOME_BTNS 8
#define HOME_X0 4
#define HOME_Y0 26
#define HOME_W 75
#define HOME_H 92
#define HOME_GAP 4

static void homeRect(int i, int *x, int *y)
{
    *x = HOME_X0 + (i % HOME_COLS) * (HOME_W + HOME_GAP);
    *y = HOME_Y0 + (i / HOME_COLS) * (HOME_H + HOME_GAP);
}

static void drawIcon(int i, int cx, int cy, uint16_t c)
{
    switch (i) {
    case 0: // hunt: compass with arrow
        gfx.drawCircle(cx, cy, 15, c);
        gfx.drawCircle(cx, cy, 14, c);
        gfx.fillTriangle(cx, cy - 11, cx - 6, cy + 8, cx, cy + 3, c);
        gfx.fillTriangle(cx, cy - 11, cx + 6, cy + 8, cx, cy + 3, UI_DIM);
        break;
    case 1: // map: folded sheet with a pin
        gfx.drawLine(cx - 16, cy - 10, cx - 6, cy - 14, c);
        gfx.drawLine(cx - 6, cy - 14, cx + 5, cy - 10, c);
        gfx.drawLine(cx + 5, cy - 10, cx + 16, cy - 14, c);
        gfx.drawLine(cx - 16, cy + 12, cx - 6, cy + 8, c);
        gfx.drawLine(cx - 6, cy + 8, cx + 5, cy + 12, c);
        gfx.drawLine(cx + 5, cy + 12, cx + 16, cy + 8, c);
        gfx.drawLine(cx - 16, cy - 10, cx - 16, cy + 12, c);
        gfx.drawLine(cx - 6, cy - 14, cx - 6, cy + 8, c);
        gfx.drawLine(cx + 5, cy - 10, cx + 5, cy + 12, c);
        gfx.drawLine(cx + 16, cy - 14, cx + 16, cy + 8, c);
        gfx.fillCircle(cx + 10, cy - 3, 4, UI_POS);
        break;
    case 2: // signal: rising bars
        for (int b = 0; b < 5; b++) {
            int h = 6 + 5 * b;
            gfx.fillRect(cx - 16 + 7 * b, cy + 13 - h, 5, h, b < 4 ? c : UI_LINE);
        }
        break;
    case 3: // GPS: satellite with panels and waves
        gfx.fillRect(cx - 4, cy - 4, 8, 8, c);
        gfx.drawRect(cx - 16, cy - 3, 10, 6, c);
        gfx.drawRect(cx + 6, cy - 3, 10, 6, c);
        gfx.drawLine(cx - 6, cy, cx - 4, cy, c);
        gfx.drawLine(cx + 4, cy, cx + 6, cy, c);
        gfx.drawCircle(cx, cy + 16, 7, UI_DIM);
        gfx.fillRect(cx - 8, cy + 16, 17, 9, UI_PANEL);
        break;
    case 4: // nearby: radar arcs around a dot
        gfx.fillCircle(cx, cy + 10, 3, c);
        for (int r = 9; r <= 25; r += 8) {
            gfx.drawCircle(cx, cy + 10, r, c);
            gfx.fillRect(cx - r - 1, cy + 11, 2 * r + 3, r + 1, UI_PANEL); // keep the upper half only
        }
        gfx.fillCircle(cx + 9, cy - 6, 3, UI_POS);
        break;
    case 7: // scan: peaks over a baseline
        gfx.drawFastHLine(cx - 16, cy + 12, 33, c);
        for (int k = 0; k < 9; k++) {
            int h = (k == 3) ? 22 : (k == 6) ? 14 : 3 + (k * 7) % 5;
            gfx.drawFastVLine(cx - 14 + 3 * k + (k > 4), cy + 12 - h, h, k == 3 ? UI_POS : c);
        }
        break;
    case 5: // tools: a trace on a screen
        gfx.drawRoundRect(cx - 16, cy - 12, 32, 24, 3, c);
        gfx.drawLine(cx - 12, cy + 4, cx - 6, cy + 4, c);
        gfx.drawLine(cx - 6, cy + 4, cx - 2, cy - 7, c);
        gfx.drawLine(cx - 2, cy - 7, cx + 3, cy + 7, c);
        gfx.drawLine(cx + 3, cy + 7, cx + 6, cy + 1, c);
        gfx.drawLine(cx + 6, cy + 1, cx + 12, cy + 1, c);
        break;
    default: // settings: gear
        for (int t = 0; t < 8; t++) {
            float a = t * PI / 4;
            int x = cx + (int)lroundf(cosf(a) * 13), y = cy + (int)lroundf(sinf(a) * 13);
            gfx.fillCircle(x, y, 3, c);
        }
        gfx.fillCircle(cx, cy, 11, c);
        gfx.fillCircle(cx, cy, 5, UI_PANEL);
        break;
    }
}

static void drawHome()
{
    static const char *const LABEL[HOME_BTNS] = {"Hunt", "Map", "Signal", "GPS", "Nearby", "Info", "Settings", "Scan"};
    char st[HOME_BTNS][24];
    // Status lines: what each page would show first.
    if (track.have && myFixOk()) {
        fmtDist(st[0], sizeof(st[0]), geo_distance_m(nmea.fix.lat, nmea.fix.lon, track.lat, track.lon), 1);
    } else {
        snprintf(st[0], sizeof(st[0]), "%s", huntId()[0] ? huntId() : track.manual ? "TARGET" : "no sonde");
    }
    if (mapOk) snprintf(st[1], sizeof(st[1]), "z%d %s", mapview_zoom_level(), mapview_center_name());
    else snprintf(st[1], sizeof(st[1]), "no tiles");
    snprintf(st[2], sizeof(st[2]), "%.0f dBm", haveLast && millis() - lastOkMs < 3000 ? lastRssi : rssiIdle);
    if (myFixOk()) snprintf(st[3], sizeof(st[3]), "fix %u sat", nmea.fix.sats);
    else snprintf(st[3], sizeof(st[3]), "%s", gpsSeen ? "no fix" : "no GPS");
    if (!cfg.net) snprintf(st[4], sizeof(st[4]), "internet off");
    else if (netr->sondesMs) snprintf(st[4], sizeof(st[4]), "%d sondes", netr->nSondes);
    else snprintf(st[4], sizeof(st[4]), "waiting");
    snprintf(st[5], sizeof(st[5]), "%.3f", cfg.freq);
    snprintf(st[6], sizeof(st[6]), "%s", cfg.apps ? "apps on" : cfg.net ? "internet" : cfg.dbgServer ? "debug" : "");
    snprintf(st[7], sizeof(st[7]), "400-406 MHz");

    drawStatusBar();
    for (int i = 0; i < HOME_BTNS; i++) {
        int x, y;
        homeRect(i, &x, &y);
        bool sel = i == homeFocus;
        gfx.fillRoundRect(x, y, HOME_W, HOME_H, 7, UI_PANEL);
        if (sel) {
            gfx.drawRoundRect(x, y, HOME_W, HOME_H, 7, UI_ACCENT);
            gfx.drawRoundRect(x + 1, y + 1, HOME_W - 2, HOME_H - 2, 6, UI_ACCENT);
        }
        drawIcon(i, x + HOME_W / 2, y + 28, sel ? UI_ACCENT : UI_TEXT);
        gfx.setTextDatum(TC_DATUM);
        gfx.setTextPadding(0);
        gfx.setTextColor(UI_TEXT, UI_PANEL);
        gfx.drawString(LABEL[i], x + HOME_W / 2, y + 52, 2);
        gfx.setTextColor(UI_LABEL, UI_PANEL);
        fitText(st[i], 1, HOME_W - 6); // long DFM/M10 serials stay in their button
        gfx.drawString(st[i], x + HOME_W / 2, y + 72, 1);
        gfx.setTextColor(UI_LINE, UI_PANEL);
        char n[2] = {(char)('1' + i), 0};
        gfx.setTextDatum(TL_DATUM);
        gfx.drawString(n, x + 6, y + 5, 1);
    }
    char clk[12], hb[40];
    localClock(clk, sizeof(clk), false);
    snprintf(hb, sizeof(hb), "%s   tap, trackball or 1-8", clk);
    bottomLine(hb, UI_LABEL);
}

// ---- GPS page: fix state, satellites in view (signal bars), position, aid ----

static void drawGps()
{
    char b[80];
    const GpsFix &g = nmea.fix;
    bool fix = myFixOk();
    drawStatusBar();
    cardFrame(4, 24, 312, 40);
    cardFrame(4, 68, 312, 78);
    cardFrame(4, 150, 312, 62);

    // State: fix type and time to first fix, or how long we have been searching.
    // Line 1: the state in large type. Line 2: details (only meaningful values).
    uint32_t sec = millis() / 1000;
    if (fix) snprintf(b, sizeof(b), "%s FIX", g.fixType == 2 ? "2D" : "3D");
    else if (gpsSeen) snprintf(b, sizeof(b), "NO FIX  %lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    else snprintf(b, sizeof(b), "NO GPS DATA");
    cardText(12, 28, 296, 4, fix ? UI_GOOD : gpsSeen ? UI_WARN : UI_BAD, b);
    int k = 0;
    if (fix) k = snprintf(b, sizeof(b), "%u used  HDOP %.1f  PDOP %.1f", g.sats, g.hdop, g.pdop);
    else b[0] = 0;
    if (firstFixMs) snprintf(b + k, sizeof(b) - k, "%sfirst fix after %lu s", k ? "   " : "", (unsigned long)(firstFixMs / 1000));
    else if (!fix && gpsSeen) snprintf(b, sizeof(b), "searching since power-on");
    cardText(12, 54, 296, 1, UI_LABEL, b);

    // Satellites in view: one bar per satellite, height = signal (C/N0), grouped by constellation.
    static const char SYS[GNSS_COUNT] = {'G', 'R', 'E', 'C', 'J'};
    static const char *const SYSNAME[GNSS_COUNT] = {"GPS", "GLO", "GAL", "BDS", "QZS"};
    int nBars = 0, inView = 0, tracked = 0;
    const int x0 = 10, base = 128, maxH = 50, bw = 8;
    for (int s = 0; s < GNSS_COUNT; s++) {
        for (int i = 0; i < nmea.nSv[s]; i++) {
            const SatInView &v = nmea.sv[s][i];
            inView++;
            if (!v.snr) continue;
            tracked++;
            if (nBars >= 30) continue;
            int h = v.snr * maxH / 50;
            if (h > maxH) h = maxH;
            uint16_t col = v.snr >= 35 ? UI_GOOD : v.snr >= 25 ? UI_POS : UI_WARN;
            int x = x0 + nBars * (bw + 2);
            gfx.fillRect(x, base - h, bw, h, col);
            char c[2] = {SYS[s], 0};
            gfx.setTextPadding(0);
            gfx.setTextColor(UI_LABEL, UI_PANEL);
            gfx.drawString(c, x + 1, base + 3, 1);
            nBars++;
        }
    }
    gfx.drawFastHLine(x0, base, 300, UI_LINE);
    k = snprintf(b, sizeof(b), "IN VIEW %d  TRACKED %d ", inView, tracked);
    for (int s = 0; s < GNSS_COUNT; s++)
        if (nmea.nSv[s]) k += snprintf(b + k, sizeof(b) - k, " %s %u", SYSNAME[s], nmea.nSv[s]);
    cardText(10, 70, 300, 1, UI_LABEL, b);
    if (!inView) cardText(12, 96, 290, 2, UI_LINE, gpsSeen ? "no satellites seen yet: sky in view?" : "no data");

    // Position, time and aid.
    if (fix) {
        snprintf(b, sizeof(b), "%.6f  %.6f", g.lat, g.lon);
        cardText(12, 154, 296, 2, UI_POS, b);
        snprintf(b, sizeof(b), "%.0f m asl  %.1f km/h  %03.0f %s", g.altM, g.speedKmh, g.courseDeg, geo_cardinal(g.courseDeg));
        cardText(12, 172, 296, 2, UI_TEXT, b);
    } else {
        if (myPosSaved) snprintf(b, sizeof(b), "last fix %.4f %.4f", myLat, myLon);
        else snprintf(b, sizeof(b), "no position yet");
        cardText(12, 154, 296, 2, UI_LINE, b);
        cardText(12, 172, 296, 2, UI_LINE, "");
    }
    {
        char clk[12];
        localClock(clk, sizeof(clk), true);
        snprintf(b, sizeof(b), "%s %s", clk, !cfg.tzMin && cfg.dst == TZ_DST_OFF ? "UTC" : "local");
    }
    cardText(12, 194, 90, 1, UI_LABEL, b);
    if (aidSent) snprintf(b, sizeof(b), "aid: %s", aidText);
    else if (gpsBaud != 38400) snprintf(b, sizeof(b), "aid: not for this GPS model");
    else if (firstFixMs) snprintf(b, sizeof(b), "aid: not needed");
    else snprintf(b, sizeof(b), "aid: needs time (net or sonde)");
    cardText(100, 194, 210, 1, aidSent ? UI_GOOD : UI_LABEL, b);
    bottomLine(cfg.net ? (netTimeOk() ? "internet: online" : "internet: no time yet") : "open sky helps the fix", UI_LABEL);
}

// Display push timing and skipped pushes, shown in the status.
static uint32_t pushCount = 0, pushLastUs = 0, pushMaxUs = 0, pushLastMs = 0, pushHeld = 0;
static uint32_t mapChangeMs = 0; // last zoom or centre change on the map
#define MAP_HOLD_MS 2500         // keep the old map on the panel this long while new tiles load

// The whole screen is rebuilt every time (static parts and compass included).
static void drawUi()
{
    if (dimmed && !blLevel) { // screen off: nothing to show, save the time and the power
        // ...but the sounds go on: the approach meter is normally fed while drawing its page.
        if (screen == SCR_APPROACH && !scanning && cfg.vol) audio_meter(rssiIdle);
        return;
    }
    {
        char c[24];
        snprintf(c, sizeof(c), "draw page %u%s", (unsigned)screen, qrMode ? " qr" : "");
        crumb(c);
    }
    gfx.fillSprite(UI_BG);
    navText[0] = 0;
    navColor = UI_LABEL;
    drawUiBody();
    if (!scanning) drawNavBar();
    if (askKind) drawAsk();
    // Map just zoomed or recentred: show it only once complete, not while tiles fill in.
    if (screen == SCR_MAP && !qrMode && !scanning && mapOk && mapview_pending() > 0 && millis() - mapChangeMs < MAP_HOLD_MS) {
        pushHeld++;
        return;
    }
    uint32_t t = micros();
    // Display, radio and SD card share the SPI bus. The panel was seen ignoring a frame sent right after
    // a radio or SD transfer: a NOP command first takes that hit.
    pushCanvas();
    pushLastUs = micros() - t;
    if (pushLastUs > pushMaxUs) pushMaxUs = pushLastUs;
    pushCount++;
    pushLastMs = millis();
}

static void drawUiBody()
{
    if (scanning) drawScan();
    else if (screen == SCR_MAP) drawMap();
    else if (screen == SCR_HUNT) drawHunt();
    else if (screen == SCR_APPROACH) drawApproach();
    else if (screen == SCR_SETTINGS) drawSettings();
    else if (screen == SCR_HOME) drawHome();
    else if (screen == SCR_GPS) drawGps();
    else if (screen == SCR_NEARBY) drawNearby();
    else drawDiag();
}

// ---- Keyboard ----

// Keyboard read counters (debug).
static uint32_t kbReads = 0, kbNoByte = 0, kbZero = 0, kbKeys = 0, kbLastReadMs = 0, kbMaxGapMs = 0;

static char readKey()
{
    uint32_t now = millis();
    if (kbLastReadMs && now - kbLastReadMs > kbMaxGapMs) kbMaxGapMs = now - kbLastReadMs;
    kbLastReadMs = now;
    kbReads++;
    uint8_t got = Wire.requestFrom(KB_ADDR, 1);
    if (got == 0 || Wire.available() <= 0) {
        kbNoByte++;
        return 0;
    }
    char k = (char)Wire.read();
    if (k == 0) kbZero++;
    else kbKeys++;
    return k;
}

static void changeFreq(double delta) { setFreq(cfg.freq + delta, true); }

// "lat lon" in decimal degrees, separated by space or comma.
// A target that is not a received sonde: typed coordinates, or a point taken from SondeHub.
static void manualTrack(double lat, double lon)
{
    track = SondeTrack();
    track.have = true;
    track.manual = true;
    track.lat = lat;
    track.lon = lon;
    track.ms = millis();
}

static void setTarget(const String &text)
{
    String in = text;
    in.replace(',', ' ');
    double lat = 0, lon = 0;
    if (sscanf(in.c_str(), "%lf %lf", &lat, &lon) != 2 || lat < -90 || lat > 90 || lon < -180 || lon > 180 ||
        (lat == 0 && lon == 0)) {
        return setMsg("target: type lat lon, e.g. 38.01 12.53");
    }
    manualTrack(lat, lon);
    track.alt = myFixOk() ? nmea.fix.altM + nmea.fix.geoidSepM : 0;
    saveLastPos();
    setMsg("manual target set");
}

// ---- Settings ----

// Text fields: empty input keeps the old value, a single '-' clears it.
static void applyText()
{
    String in = textInput;
    in.trim();
    if (in.length() == 0) return;
    if (in == "-") in = "";
    if (editKind == 2) {
        if (in.length() > 32) return setMsg("Wi-Fi name too long");
        snprintf(cfg.ssid, sizeof(cfg.ssid), "%s", in.c_str());
    } else if (editKind == 3) {
        snprintf(cfg.pass, sizeof(cfg.pass), "%s", in.c_str());
    } else if (editKind == 8) { // '-' goes back to the default
        if (in.length() >= sizeof(cfg.dbgPass)) return setMsg("web password: up to 23 characters");
        snprintf(cfg.dbgPass, sizeof(cfg.dbgPass), "%s", in.length() ? in.c_str() : DEBUG_PASS_DEFAULT);
        saveSettings();
        return setMsg(in.length() ? "web password saved" : "web password: back to the default");
#if PRED_SERVER
    } else if (editKind == 9) {
        if (in.length() >= sizeof(cfg.psUrl)) return setMsg("URL: up to 99 characters");
        snprintf(cfg.psUrl, sizeof(cfg.psUrl), "%s", in.c_str());
        if (!cfg.psUrl[0]) cfg.psOn = false;
    } else if (editKind == 10) {
        if (in.length() >= sizeof(cfg.psTok)) return setMsg("token: up to 99 characters");
        snprintf(cfg.psTok, sizeof(cfg.psTok), "%s", in.c_str());
        if (!cfg.psTok[0]) cfg.psOn = false;
#endif
    } else {
        in.toUpperCase(); // the same callsign serves SondeHub and radiosondy.info (APRS wants capitals)
        if (in.length() && !sh_callsign_ok(in.c_str())) return setMsg("callsign: 3-15 letters, digits, - _");
        snprintf(cfg.call, sizeof(cfg.call), "%s", in.c_str());
    }
    if (!cfg.ssid[0] || !sh_callsign_ok(cfg.call)) cfg.shUpload = false;
    if (!aprsReady()) cfg.aprsUp = cfg.aprsFi = false; // a new callsign needs its own passcode
    settingDone();
    setMsg("saved");
}

static void settingsToggle(int item);

// Next value of a setting that takes turns, with its message; saved at once.
static void cycleSetting(uint8_t &v, int n, const char *const *msg)
{
    v = (uint8_t)((v + 1) % n);
    setMsg(msg[v]);
    saveSettings();
}

// The same with the message made of a format and the value name the settings row shows.
static void cycleNamed(uint8_t &v, int n, const char *const *names, const char *fmt)
{
    v = (uint8_t)((v + 1) % n);
    setMsgf(fmt, names[v]);
    saveSettings();
}

static void settingsAction(int item)
{
    if (item < 0 || item >= SET_ROWS) return;
    int r = setRowOf(item);
    if (r >= 0) setFocus = r;
#if PRED_SERVER
    if (item == 44) {
        if (cfg.psOn) {
            cfg.psOn = false;
            return settingDone("prediction server off");
        }
        if (needSsid()) return;
        if (!cfg.psUrl[0] || !cfg.psTok[0]) return setMsg("server URL and token first");
        return askOpen(ASK_UPLOAD_PS, "Send data to the prediction server?", "Are you sure? Real flights only");
    }
    if (item == 45) {
        startEdit(9);
        return setMsg("server URL, e.g. https://host:port");
    }
    if (item == 46) {
        startEdit(10);
        return setMsg("server token, Enter saves, - clears");
    }
#endif
    if (item == 43) { // filters below 7 kHz cut the RS41 signal: the list goes 9.7 ... 39 kHz
        cfg.bwIdx = (uint8_t)((cfg.bwIdx + 1) % Sx1262::BW_COUNT);
        if (Sx1262::BW_KHZ[cfg.bwIdx] < 7.0f) cfg.bwIdx = 3;
        radioChanged();
        return setMsgf("bandwidth %.1f kHz%s", Sx1262::BW_KHZ[cfg.bwIdx],
                       cfg.bwIdx == BW_DEFAULT ? " (default)" : " (default 14.6)");
    }
    if (item == 42) {
        if (!cfg.webUi && needSsid()) return;
        toggleSetting(cfg.webUi, "web interface on (user tdeck)", "web interface off");
        return;
    }
    if (item < 10) return settingsToggle(item);
    if (item == 13) {
        cycleNamed(cfg.bright, 5, BRIGHT_NAME, "brightness %s");
        blSet(BL_LEVEL[cfg.bright]);
        return;
    }
    if (item == 16) {
        static const char *const M[4] = {"screen always on", "screen off after 1 min", "screen off after 5 min",
                                         "screen off after 10 min"};
        return cycleSetting(cfg.dim, 4, M);
    }
    if (item == 17) {
        cfg.fullFrames = !cfg.fullFrames;
        cap.setFullFrames(cfg.fullFrames);
        setMsg(cfg.fullFrames ? "full frames on: 320/518 bytes" : "full frames off: 255 + erasures");
        saveSettings();
        return;
    }
    if (item == 21 || item == 22) { // volume 0-9, a sample at the new level
        uint8_t &v = item == 21 ? cfg.vol : cfg.fvol;
        v = (v + 1) % 10;
        saveSettings();
        if (item == 21) audio_beep(1200, 60);
        else if (cfg.sound) audio_frame_beep(1200, 40);
        else audio_tick();
        return;
    }
    if (item == 34) {
        cycleNamed(cfg.tbSpeed, 3, TB_NAME, "trackball: %s");
        input_set_trackball_step(TB_EDGES[cfg.tbSpeed]);
        return;
    }
    if (item == 33) {
        static const char *const M[4] = {"auto lock off (trackball 3 s locks)", "auto lock after 1 min without input",
                                         "auto lock after 2 min without input", "auto lock after 5 min without input"};
        return cycleSetting(cfg.autoLock, 4, M);
    }
    if (item == 23) {
        static const char *const M[4] = {"screen timeout: screen off", "screen timeout: dimmed a little",
                                         "screen timeout: dimmed more", "screen timeout: dimmed most"};
        return cycleSetting(cfg.dimLvl, 4, M);
    }
    if (item == 18) return cycleSondeType();
    if (item == 19) {
        openPresetList(false);
        return;
    }
    if (item == 20) {
        aboutOpen = true;
        return;
    }
    if (item == 15) {
        cfg.theme = (cfg.theme + 1) % THEME_COUNT;
        th = &THEMES[cfg.theme];
        setMsgf("theme: %s", th->name);
        saveSettings();
        return;
    }
    if (item == 14) // reminder only: the keyboard handles Alt+B itself, the key never reaches this firmware
        return setMsg("keyboard light: hold Alt and press B");
    if (item == 25) return wifiScan();
    if (item == 32) return savedOpenList();
    if (item == 35) {
        if (cfg.aprsFi) {
            cfg.aprsFi = false;
            settingDone("aprs.fi upload off");
            return;
        }
        if (const char *m = aprsMissing()) return setMsg(m);
        if (!posTo(POS_FI)) return setMsg("Send my position: choose all or aprs.fi first");
        return askOpen(ASK_UPLOAD_APRSFI, "Show your position on aprs.fi?", "Are you sure? Everyone can see it");
    }
    if (item == 36) {
        toggleSetting(cfg.aprsFiCmt, "aprs.fi: comment on (aprsfi.txt)", "aprs.fi: position only, no comment");
        return;
    }
    if (item == 27) {
        if (cfg.aprsUp) {
            cfg.aprsUp = false;
            settingDone("radiosondy.info upload off");
            return;
        }
        if (const char *m = aprsMissing()) return setMsg(m);
        return askOpen(ASK_UPLOAD_APRS, "Send data to radiosondy.info?", "Are you sure? Real flights only");
    }
    if (item == 28) return startEdit(7);
    if (item == 39) {
        startEdit(8);
        return setMsg("web password (user tdeck), Enter saves, - = default");
    }
    if (item == 31) {
        cfg.mobIcon = (cfg.mobIcon + 1) % APRS_MOBILE_ICONS;
        setMsgf("mobile station icon on radiosondy.info: %s", APRS_MOBILE_NAME[cfg.mobIcon]);
        settingDone();
        return;
    }
    if (item == 29) {
        cfg.staMob = (cfg.staMob + 1) % STA_MOB_N;
        if (STA_MOB_S[cfg.staMob] < 60) setMsgf("mobile station: position every %u s", STA_MOB_S[cfg.staMob]);
        else setMsgf("mobile station: position every %u min", STA_MOB_S[cfg.staMob] / 60);
        settingDone();
        return;
    }
    if (item == 40) {
        cycleNamed(cfg.launchH, LAUNCH_H_N, LAUNCH_H_NAME, "next launches: %s");
        launchGen++; // the open tab follows at once
        return;
    }
    if (item == 41) {
        cfg.shLog = !cfg.shLog;
        setMsg(cfg.shLog ? "SondeHub JSON kept in sondehub/ (SD)" : "SondeHub JSON log off");
        saveSettings();
        return;
    }
    if (item == 37) {
        return cycleNamed(cfg.fixWait, FIX_WAIT_N, FIX_WAIT_NAME, "position after %s of good GPS fix");
    }
    if (item == 38) {
        static const char *const M[MIN_MOVE_N] = {"mobile: position at every update", "mobile: new position after 200 m",
                                                  "mobile: new position after 500 m", "mobile: new position after 1 km"};
        cycleSetting(cfg.minMove, MIN_MOVE_N, M);
        pushUploaderCfg();
        return;
    }
    if (item == 30) {
        cfg.staFix = (cfg.staFix + 1) % STA_FIX_N;
        setMsgf("fixed station: position every %u min", STA_FIX_MIN[cfg.staFix]);
        settingDone();
        return;
    }
    if (item == 47) {
        toggleSetting(cfg.wifiPs, "Wi-Fi power save on: less current, the link may drop",
                      "Wi-Fi power save off: steadier link");
        return;
    }
    if (item == 26) {
        toggleSetting(cfg.shMobile, "station: mobile, shown as a chase car", "station: fixed receiver");
        return;
    }
    if (item == 12) { // list of the zones, on the current one
        tzOpen = true;
        tzFocus = tz_zone_index(cfg.tzMin);
        return;
    }
    if (item == 24) {
        static const char *const M[TZ_DST_COUNT] = {"daylight saving: off", "daylight saving: on (+1 h)",
                                                    "daylight saving: automatic, European dates"};
        return cycleSetting(cfg.dst, TZ_DST_COUNT, M);
    }
    if (item == 11) {
        if (!cfg.net && needSsid()) return;
        toggleSetting(cfg.net, "internet on: time and GPS aid, no upload", "internet off");
        return;
    }
    if (item == 10) {
        if (!input_touch_ok()) return setMsg("no touch controller found");
        cfg.touch = !cfg.touch;
        input_set_touch(cfg.touch);
        setMsg(cfg.touch ? "touch screen on" : "touch screen off");
        saveSettings();
    }
}

// Yes in the SondeHub question.
static void uploadOn()
{
    if (!SH_TELEMETRY_APPROVED) return;
    if (!cfg.ssid[0] || !sh_callsign_ok(cfg.call)) return setMsg("set Wi-Fi name and callsign first");
    cfg.shUpload = true;
    settingDone("SondeHub ON: real flights only!");
}

// Yes in the aprs.fi question.
static void aprsFiOn()
{
    if (const char *m = aprsMissing()) return setMsg(m);
    cfg.aprsFi = true;
    aprsFiLoad(); // texts edited on the PC since the start
    settingDone("aprs.fi ON: your position is public");
}

// Yes in the radiosondy.info question.
static void aprsOn()
{
    if (const char *m = aprsMissing()) return setMsg(m);
    cfg.aprsUp = true;
    settingDone("radiosondy.info ON: real flights only!");
}

// APRS passcode entry: digits only; empty keeps the old one, '-' clears it.
static void setAprsPass(const String &typed)
{
    String in = typed;
    in.trim();
    if (!in.length()) return;
    if (in == "-") {
        cfg.aprsPass = -1;
        cfg.aprsUp = cfg.aprsFi = false;
    } else {
        for (unsigned i = 0; i < in.length(); i++)
            if (in[i] < '0' || in[i] > '9') return setMsg("APRS passcode: digits only");
        long v = in.toInt();
        if (in.length() > 5 || v > 32767) return setMsg("APRS passcode: 0-32767");
        cfg.aprsPass = (int32_t)v;
        if (!aprsReady()) cfg.aprsUp = cfg.aprsFi = false;
    }
    settingDone();
    if (cfg.aprsPass >= 0 && cfg.call[0] && cfg.aprsPass != aprs_passcode(cfg.call))
        return setMsg("saved, but it does not match the callsign");
    setMsg("saved");
}

// Items 0-9: frame sound, raw log, Wi-Fi name and password, callsign, SondeHub (3), debug server, apps.
static void settingsToggle(int item)
{
    switch (item) {
    case 0: // tick or pitch beep: a sample at the frame volume
        cfg.sound = !cfg.sound;
        if (cfg.sound) audio_frame_beep(1400, 40);
        else audio_tick();
        break;
    case 1: cfg.rawLog = !cfg.rawLog; break;
    case 2:
    case 3:
    case 4:
        startEdit((uint8_t)item);
        return;
    case 5:
        if (!SH_TELEMETRY_APPROVED) {
            shInfoOpen = true;
            return;
        }
        if (!cfg.shUpload && (!cfg.ssid[0] || !sh_callsign_ok(cfg.call))) {
            return setMsg("set Wi-Fi name and callsign first");
        }
        if (!cfg.shUpload) return askOpen(ASK_UPLOAD_SH, "Send data to SondeHub?", "Are you sure? Real flights only");
        cfg.shUpload = false;
        setMsg("SondeHub upload off");
        break;
    case 6: cfg.shFlightOnly = !cfg.shFlightOnly; break;
    case 7: {
        static const char *const MSG[POS_N] = {"my position: shared nowhere", "my position: SondeHub, radiosondy, aprs.fi",
                                               "my position: SondeHub only", "my position: radiosondy.info only",
                                               "my position: aprs.fi only"};
        cfg.sharePos = (cfg.sharePos + 1) % POS_N;
        if (cfg.sharePos == POS_SH && !SH_TELEMETRY_APPROVED) {
            cfg.sharePos = POS_RS; // not available yet: the info page says why, the choice goes on
            shInfoOpen = true;
        }
        setMsg(MSG[cfg.sharePos]);
        break;
    }
    case 8:
        if (!cfg.dbgServer && needSsid()) return;
        cfg.dbgServer = !cfg.dbgServer;
        setMsg(cfg.dbgServer ? "debug server on (trusted networks only)" : "debug server off");
        break;
    case 9:
        cfg.apps = (cfg.apps + 1) % 3;
        if (cfg.apps == 2 && !cfg.ssid[0]) cfg.apps = 0; // the Wi-Fi link needs a network
        // The apps can upload to SondeHub on their own: remind to check that in the app.
        setMsg(cfg.apps ? "apps on: keep SondeHub off in the app" : "hunting apps off");
        break;
    default: return;
    }
    settingDone();
}

// Digit k: row 1-9 or 10 (key 0) of the current tab; nothing in a tab of more than ten rows.
static void settingsKey(char k)
{
    if (SET_TAB_N[setTab] > 10) return;
    int r = k == '0' ? 9 : k - '1';
    if (r >= 0 && r < SET_TAB_N[setTab]) settingsAction(SET_TAB_ITEMS[setTab][r]);
}

// ---- Page navigation ----

static void enterPage(uint8_t s)
{
    screen = s;
    qrMode = 0;
    aboutOpen = shInfoOpen = presetOpen = presetFromKey = tzOpen = wifiOpen = savedOpen = false;
    if (s == SCR_MAP) mapChangeMs = millis(); // entering the map: show it once its tiles are in
    apPeak = -140;
}

// Opens a page and remembers where we came from (Back returns there).
static void goPage(uint8_t s)
{
    if (s == screen) return;
    if (navDepth == NAV_DEPTH) { // drop the oldest
        memmove(navStack, navStack + 1, NAV_DEPTH - 1);
        navDepth--;
    }
    navStack[navDepth++] = screen;
    enterPage(s);
}

static void goBack()
{
    if (tzOpen || wifiOpen || savedOpen) {
        tzOpen = wifiOpen = savedOpen = false;
        return;
    }
    if (presetOpen) { // the list closes; opened with key j, the page it came from comes back too
        presetOpen = false;
        if (!presetFromKey) return;
        presetFromKey = false;
    } else if (aboutOpen || shInfoOpen) {
        aboutOpen = shInfoOpen = false;
        return;
    }
    if (nearBack()) return;
    if (screen == SCR_MAP && qrMode) {
        qrMode = 0;
        return;
    }
    enterPage(navDepth ? navStack[--navDepth] : SCR_HOME);
}

// Preset list over the settings page; with key j it opens from any page, ready for a search.
static void openPresetList(bool fromKey)
{
    if (!presetText) return setMsg("no presets");
    bool from = fromKey && screen != SCR_SETTINGS;
    if (from) goPage(SCR_SETTINGS);
    presetFromKey = from;
    presetOpen = true;
    presetQuery[0] = 0;
    presetFocus = curPreset >= 0 && curPreset < nPlist ? curPreset + 1 : 0; // row = index + 1 without a search
    presetFilter();
}

static void presetPick(int row)
{
    if (row < 0 || row >= nPresetRows) return;
    int i = presetRows[row];
    goBack(); // closes the list (and returns to the page of key j)
    if (i == -1) presetsAuto();
    else if (i == -2) addDefaultPresets();
    else usePreset(i);
}

static void goHome()
{
    navDepth = 0;
    enterPage(SCR_HOME);
}

// Home buttons, in grid order (4 columns, 2 rows); the last one starts a scan instead of opening a page.
#define HOME_SCAN 0xFF
static const uint8_t HOME_PAGE[8] = {SCR_HUNT,   SCR_MAP,  SCR_APPROACH, SCR_GPS,
                                     SCR_NEARBY, SCR_DIAG, SCR_SETTINGS, HOME_SCAN};
static void scanStart();

static void homeOpen(int i)
{
    homeFocus = i;
    if (HOME_PAGE[i] == HOME_SCAN) return scanStart();
    goPage(HOME_PAGE[i]);
}

static void setLocked(bool on)
{
    screenLocked = on;
    logRemember(on ? "# screen locked" : "# screen unlocked");
    setMsg(on ? "locked: hold the trackball 3 s to unlock" : "unlocked");
    audio_beep(on ? 900 : 1400, 50);
}

// Reads /aprsfi.txt; writes it with the built-in texts when missing (the user can then edit it).
static void aprsFiLoad()
{
    if (!sdOk) return;
    if (!SD.exists(aprsfiPath)) {
        File w = SD.open(aprsfiPath, FILE_WRITE);
        if (w) {
            w.print(APRS_COMMENT_TEMPLATE);
            w.close();
        }
    }
    File f = SD.open(aprsfiPath, FILE_READ);
    if (!f) return;
    if (!aprsFiFile) aprsFiFile = (char *)ps_malloc(APRS_COMMENT_FILE_MAX + 1);
    if (aprsFiFile) aprsFiFile[f.read((uint8_t *)aprsFiFile, APRS_COMMENT_FILE_MAX)] = 0;
    f.close();
}

// Every few seconds: the comment for what the T-Deck is doing now. A sonde position decoded by radio in the
// last 2 min (never a bench simulator): "chase" for a mobile station, "receive" for a fixed one, with the launch
// site from the frequency preset in use; anything else (a remembered or SondeHub position) is "idle". The
// track, not the last capture: a capture that fails to decode has no serial and would flip the comment.
static void aprsFiPoll()
{
    static uint32_t lastMs = 0;
    if (!cfg.aprsFi || millis() - lastMs < 5000) return;
    lastMs = millis();
    bool rf = track.have && !track.manual && !track.restored && !track.fromNet && track.id[0] &&
              millis() - track.ms < 120000 && !sh_serial_blocked(track.id);
    int pi = activePreset();
    const char *site = pi >= 0 ? plist[pi].name : "";
    char t[APRS_COMMENT_MAX];
    aprs_comment(aprsFiFile, rf ? (cfg.shMobile ? APRS_CHASE : APRS_RECEIVE) : APRS_IDLE, rf ? track.id : "", site, t,
                 sizeof(t));
    if (!strcmp(t, aprsFiText)) return;
    snprintf(aprsFiText, sizeof(aprsFiText), "%s", t);
    pushUploaderCfg();
}

static void lockPoll()
{
    if (!cfg.autoLock || screenLocked || millis() - lastInputMs < AUTOLOCK_S[cfg.autoLock] * 1000UL) return;
    screenLocked = true; // quietly: no message, so a dark screen stays dark
    logRemember("# screen locked (auto)");
}

// Key actions shared with the web page.
// Polarity: 0 automatic, 1 normal, 2 inverted.
static void polSet(int m)
{
    if (m < 0 || m > 2) return;
    cfg.autoInvert = m == 0;
    if (m) cfg.invert = m == 2; // automatic keeps the last polarity as its start
    applyRadio();
    saveSettings();
    setMsg(m == 0 ? "polarity: automatic" : m == 2 ? "polarity: inverted" : "polarity: normal");
}

// Where distance and arrow point: 0 the sonde, 1 the landing point (automatic choice), 2 the server's point.
static void navSet(int m)
{
    if (m == 2 && !psPredOk()) m = 0;
    navPred = m >= 1;
    navSrv = m == 2;
    if (m == 1) setMsg("nav: predicted landing point");
#if PRED_SERVER
    else if (m == 2) setMsgf("nav: %s landing point", predsrv_tag());
#endif
    else setMsg("nav: sonde position");
}

static void soundToggle()
{
    static uint8_t keepVol = 7, keepFvol = 7;
    char m[40];
    if (cfg.vol || cfg.fvol) {
        keepVol = cfg.vol;
        keepFvol = cfg.fvol;
        cfg.vol = cfg.fvol = 0;
        snprintf(m, sizeof(m), "sound off (s: back on)");
    } else {
        cfg.vol = keepVol;
        cfg.fvol = keepFvol;
        if (!cfg.vol && !cfg.fvol) cfg.vol = cfg.fvol = 7;
        snprintf(m, sizeof(m), "sound on: volume %u, frames %u", cfg.vol, cfg.fvol);
    }
    saveSettings();
    setMsg(m);
    audio_beep(1200, 60);
}

static void handleKey(char k)
{
    if (FIELD_DEBUG) {
        bool hide = editSecret() && k >= 0x20 && k < 0x7F;
        snprintf(fieldAction, sizeof(fieldAction), k >= 0x20 && k < 0x7F ? "key '%c' screen %u" : "key 0x%02x screen %u",
                 hide ? '*' : (unsigned char)k, (unsigned)screen);
        fieldActionMs = millis();
    }
    if (screenLocked) return setMsg("locked: hold the trackball 3 s");
    if (askKind) { // y / n answer at once; Enter takes the button in focus (No unless moved); Back is No
        if (k == 'y' || k == 'Y') askAnswer(true);
        else if (k == 'n' || k == 'N' || k == 0x08 || k == 0x1B) askAnswer(false);
        else if (k == '\r' || k == '\n') askAnswer(askYes);
        return;
    }
    if (aboutOpen || shInfoOpen) {
        aboutOpen = shInfoOpen = false;
        return;
    }
    if (tzOpen) {
        if (k == '\r' || k == '\n') tzPick(tzFocus);
        else if (k == 0x08 || k == 0x1B) tzOpen = false;
        return;
    }
    if (wifiOpen) {
        if (k == '\r' || k == '\n') wifiPick(wifiFocus);
        else if (k == 0x08 || k == 0x1B) wifiOpen = false;
        return;
    }
    if (savedOpen) {
        if (k == '\r' || k == '\n') savedPick(savedFocus);
        else if (k == 0x08 || k == 0x1B) savedOpen = false;
        else if (k == '5' || k == 0x04) savedDeleteAsk(savedFocus); // Alt+D
        return;
    }
    if (presetOpen) {
        size_t ql = strlen(presetQuery);
        if (k == '\r' || k == '\n') {
            presetPick(presetFocus);
        } else if (k == 0x08) { // delete a letter, or close when the search is empty
            if (!ql) return goBack();
            presetQuery[ql - 1] = 0;
            presetFilter();
        } else if (k == 0x1B) {
            goBack();
        } else if (!ql && (k == '5' || k == 0x04)) { // Alt+D: delete (with an empty search, where 5 means nothing)
            deletePresetAsk(presetRows[presetFocus]);
        } else if (k >= 0x20 && k < 0x7F && ql + 1 < sizeof(presetQuery) && (ql || k != ' ')) {
            presetQuery[ql] = k;
            presetQuery[ql + 1] = 0;
            presetFocus = 1; // the first match (row 0 is auto)
            presetFilter();
        }
        return;
    }
    if (textEditing && wifiPassOpen) {
        bool enter = k == '\r' || k == '\n';
        if (enter) {
            applyText();
            textEditing = false;
        } else if (k == 0x1B || (k == 0x08 && !textInput.length())) {
            textEditing = false;
            wifiOpen = true; // back to the networks found
        } else if (k == 0x08) {
            textInput.remove(textInput.length() - 1);
        } else if (k >= 0x20 && k < 0x7F && textInput.length() < 63) {
            textInput += k;
        }
        if (!textEditing) wifiPassOpen = false;
        return;
    }
    if (textEditing) {
        if (k == '\r' || k == '\n') {
            if (editKind == 1) {
                setTarget(textInput);
            } else if (editKind == 6) {
                textEditing = false; // before the question opens over the page
                savePresetNamed(textInput);
            } else if (editKind == 7) {
                setAprsPass(textInput);
            } else if (editKind == 5) {
                String in = textInput;
                in.trim();
                in.toUpperCase();
                if (in.length()) {
                    snprintf(detailSerial, sizeof(detailSerial), "%.11s", in.c_str());
                    shDetail = 1; // the summary is asked by netAppPoll
                }
            } else if (editKind >= 2) {
                applyText();
            } else {
                double f = textInput.toDouble();
                if (f >= 400.0 && f <= 406.0) {
                    setFreq(f, true);
                } else if (textInput.length()) {
                    setMsg("freq: 400.000 - 406.000 MHz");
                }
            }
            textEditing = false;
        } else if (k == 0x08) {
            if (textInput.length()) textInput.remove(textInput.length() - 1);
            else {
                textEditing = false; // backspace on an empty field leaves the text entry
                setMsg("input cancelled");
            }
        } else if (k == 0x1B || (k == 'q' && editKind < 2)) { // Esc leaves every field, q only the numeric ones
            textEditing = false;
            setMsg("input cancelled");
        } else if (editKind >= 2) {
            if (k >= 0x20 && k < 0x7F && textInput.length() < 63) textInput += k;
        } else if ((k >= '0' && k <= '9') || k == '.') {
            if (textInput.length() < (editKind == 1 ? 24u : 8u)) textInput += k;
        } else if (editKind == 1 && (k == ' ' || k == ',' || k == '-')) {
            if (textInput.length() < 24) textInput += k;
        }
        return;
    }
    if (scanning) {
        if (k == 'x' || k == 0x1B || k == 0x08) {
            scanFinish(false);
            setMsg("scan stopped");
        }
        return;
    }
    if (screen == SCR_SETTINGS && k >= '0' && k <= '9') return settingsKey(k);
    if (screen == SCR_HOME && k >= '1' && k <= '8') return homeOpen(k - '1');
    if (screen == SCR_NEARBY && nearKey(k)) return;
    if (k == 'h') return goHome();
    if (k == 0x08 || k == 0x1B) return goBack();
    if (screen == SCR_SETTINGS && k == '\r') return settingsAction(SET_TAB_ITEMS[setTab][setFocus]);
    if (screen == SCR_SETTINGS && k == ' ') return setTabGo(setTab + 1);
    // Each key means one thing on every screen: i/o/c act only on the map and are ignored elsewhere.
    switch (k) {
    case 'i':
    case 'o':
    case 'c':
        if (screen != SCR_MAP) return setMsg("i/o zoom, c centre: map screen only");
        if (!mapOk) return setMsg("map not available");
        mapChangeMs = millis();
        if (k == 'c') return mapview_cycle_center();
        return mapview_zoom(k == 'i' ? 1 : -1);
    case 'g':
        if (screen != SCR_MAP) return setMsg("g: QR code, on the map screen");
        return qrNext();
    case 'x': scanStart(); break;
    case 'p': nextPreset(); break;
    case 'a': savePresetAsk(); break;
    case 'j': openPresetList(true); break;
    case 'm': goPage(SCR_MAP); break;
    case 'y': cycleSondeType(); break;
    case 'u': // polarity: automatic -> normal -> inverted -> automatic
        polSet(cfg.autoInvert ? 1 : !cfg.invert ? 2 : 0);
        break;
    case 'w': case '+': changeFreq(0.001); break;  // q/w: -/+1 kHz, d/e: -/+10 kHz
    case 'q': case '-': changeFreq(-0.001); break;
    case 'e': changeFreq(0.010); break;
    case 'd': changeFreq(-0.010); break;
    case 'f':
        startEdit(0);
        break;
    case 'n': // sonde -> landing point (automatic) -> prediction server's point (when there is one) -> sonde
        navSet(!navPred ? 1 : !navSrv && psPredOk() ? 2 : 0);
        break;
    case 't':
        startEdit(1);
        break;
    case 's': // all sounds off, or back to the volumes they had
        soundToggle();
        break;
    case 'r':
        resetStats();
        setMsg("counters and sonde position reset");
        break;
    case 'k':
        setMsg("KML: exporting...");
        drawUi();
        exportKml();
        break;
    case 'v': // quick switch between the three hunting pages
        goPage(screen == SCR_HUNT ? SCR_MAP : screen == SCR_MAP ? SCR_APPROACH : SCR_HUNT);
        break;
    case 'l':
        cfg.rawLog = !cfg.rawLog;
        saveSettings();
        setMsg(cfg.rawLog ? "raw frame log on" : "raw frame log off");
        break;
    default: break;
    }
}

// Trackball and touch. The keyboard keeps every function; these add pointing where it helps.
// Trackball and touch in a list over the settings (networks found, saved networks, time zones, presets): up and
// down move, select picks, a tap picks its row (and focuses it), a tap on the bottom bar goes back.
// False: not handled here (left/right are each list's own).
static bool listInput(const InputEvent &e, int &focus, int n, int rows, void (*pick)(int))
{
    if (e.type == IN_UP) {
        if (focus > 0) focus--;
    } else if (e.type == IN_DOWN) {
        if (focus < n - 1) focus++;
    } else if (e.type == IN_SELECT) {
        if (n > 0) pick(focus);
    } else if (e.type == IN_TAP || e.type == IN_LONG) {
        if (e.y >= NAV_HIT_Y) {
            goBack();
        } else {
            int row = popRowAt(e.y, listTop(focus, rows));
            if (row >= 0 && row < n) {
                focus = row;
                pick(row);
            }
        }
    } else {
        return false;
    }
    return true;
}

static void handleInput(const InputEvent &e)
{
    if (FIELD_DEBUG) {
        snprintf(fieldAction, sizeof(fieldAction), "input %d at %d,%d screen %u", (int)e.type, e.x, e.y,
                 (unsigned)screen);
        fieldActionMs = millis();
    }
    if (e.type == IN_HOLD) return setLocked(!screenLocked); // trackball held 3 s: lock / unlock
    if (screenLocked) return setMsg("locked: hold the trackball 3 s");
    if (askKind) {
        if (e.type == IN_LEFT || e.type == IN_RIGHT || e.type == IN_UP || e.type == IN_DOWN) askYes = !askYes;
        else if (e.type == IN_SELECT) askAnswer(askYes);
        else if (e.type == IN_TAP || e.type == IN_LONG) { // a tap outside the buttons is No
            bool inRow = e.y >= ASK_BTN_Y - 4 && e.y < ASK_BTN_Y + ASK_BTN_H + 4;
            askAnswer(inRow && e.x >= ASK_YES_X && e.x < ASK_YES_X + ASK_BTN_W);
        }
        return;
    }
    if (aboutOpen || shInfoOpen) {
        aboutOpen = shInfoOpen = false;
        return;
    }
    if (wifiOpen) {
        listInput(e, wifiFocus, wifiN, WIFI_ROWS, [](int i) { wifiPick(i); });
        return;
    }
    if (savedOpen) {
        listInput(e, savedFocus, nWifiSaved + 1, SAVED_ROWS, savedPick); // row 0: save this network
        return;
    }
    if (tzOpen) {
        if (e.type == IN_LEFT || e.type == IN_RIGHT) { // a page at a time
            tzFocus += e.type == IN_RIGHT ? TZ_ROWS : -TZ_ROWS;
            if (tzFocus < 0) tzFocus = 0;
            if (tzFocus >= TZ_ZONE_COUNT) tzFocus = TZ_ZONE_COUNT - 1;
        } else {
            listInput(e, tzFocus, TZ_ZONE_COUNT, TZ_ROWS, tzPick);
        }
        return;
    }
    if (presetOpen) {
        if ((e.type == IN_LEFT || e.type == IN_RIGHT) && !presetQuery[0] && nPlist) {
            int i = presetRows[presetFocus]; // without a search, row = preset index + 1
            presetFocus = 1 + presets_group_jump(plist, nPlist, i >= 0 ? i : 0, e.type == IN_RIGHT ? 1 : -1);
        } else {
            listInput(e, presetFocus, nPresetRows, PRESET_ROWS, presetPick);
        }
        return;
    }
    if (textEditing) { // typing: a touch on the screen leaves the text entry
        if (e.type == IN_TAP || e.type == IN_LONG) {
            textEditing = false;
            if (wifiPassOpen) wifiOpen = true; // the password box goes back to the networks found
            else setMsg("input cancelled");
            wifiPassOpen = false;
        }
        return;
    }
    if (scanning) {
        if (e.type == IN_SELECT || e.type == IN_TAP) {
            scanFinish(false);
            setMsg("scan stopped");
        }
        return;
    }
    if (e.type == IN_TAP || e.type == IN_LONG) {
        if (e.y >= NAV_HIT_Y && screen != SCR_HOME) {
            if (e.x < NAV_HIT_W) return goBack();
            if (e.x >= 320 - NAV_HIT_W) return goHome();
        }
        if (e.y < 20 && e.x < 76 && screen != SCR_MAP) { // frequency in the status bar: type a new one
            startEdit(0);
            return;
        }
        if (screen == SCR_HOME) {
            for (int i = 0; i < HOME_BTNS; i++) {
                int x, y;
                homeRect(i, &x, &y);
                if (e.x >= x && e.x < x + HOME_W && e.y >= y && e.y < y + HOME_H) return homeOpen(i);
            }
        } else if (screen == SCR_NEARBY) {
            return nearInput(e);
        } else if (screen == SCR_SETTINGS) {
            if (tabHit(e.y)) return setTabGo(e.x / 80);
            int top = SET_Y0 - 1, r = setTop + (e.y - top) / SET_ROW_H;
            if (e.y >= top && e.y < top + SET_VISIBLE * SET_ROW_H && r < SET_TAB_N[setTab])
                settingsAction(SET_TAB_ITEMS[setTab][r]);
        } else if (screen == SCR_MAP && qrMode) {
            return qrNext();
        } else if (screen == SCR_MAP && e.x < QR_BTN_X + MAP_BTN + 6 && e.y < QR_BTN_Y + MAP_BTN + 6) {
            return qrNext();
        } else if (screen == SCR_MAP && mapOk && e.x >= MAP_BTN_X - 6) {
            for (int i = 0; i < 2; i++) {
                int y = MAP_BTN_Y + i * (MAP_BTN + 6);
                if (e.y >= y - 4 && e.y < y + MAP_BTN + 4) {
                    mapChangeMs = millis();
                    return mapview_zoom(i == 0 ? 1 : -1);
                }
            }
        }
        return;
    }
    int dx = e.type == IN_LEFT ? -1 : e.type == IN_RIGHT ? 1 : 0;
    int dy = e.type == IN_UP ? -1 : e.type == IN_DOWN ? 1 : 0;
    switch (screen) {
    case SCR_HOME:
        if (e.type == IN_SELECT) return homeOpen(homeFocus);
        homeFocus = ((homeFocus / HOME_COLS + dy + 2) % 2) * HOME_COLS + (homeFocus % HOME_COLS + dx + HOME_COLS) % HOME_COLS;
        return;
    case SCR_NEARBY:
        return nearInput(e);
    case SCR_SETTINGS:
        if (e.type == IN_SELECT) return settingsAction(SET_TAB_ITEMS[setTab][setFocus]);
        if (dx) return setTabGo(setTab + dx);
        setFocus += dy;
        if (setFocus < 0) setFocus = 0;
        if (setFocus >= SET_TAB_N[setTab]) setFocus = SET_TAB_N[setTab] - 1;
        return;
    case SCR_MAP:
        if (qrMode) {
            if (e.type == IN_SELECT || dx) qrNext();
            return;
        }
        if (!mapOk) return;
        if (e.type == IN_SELECT) {
            mapChangeMs = millis();
            return mapview_cycle_center();
        }
        return mapview_pan(dx * 40, dy * 40);
    case SCR_HUNT:
    case SCR_APPROACH:
    case SCR_DIAG:
        if (dy) changeFreq(-dy * 0.001); // ball up = frequency up
        return;
    default:
        return;
    }
}

// ---- Frame handling ----

// Records the wind at this altitude and, while descending, predicts the landing point.
static void updatePrediction()
{
    pred_add(&wind, track.alt, track.vH, track.vDir);
    vvEma = 0.8f * vvEma + 0.2f * track.vV;
    if (vvEma > -1.5f) {
        pred.ok = false; // climbing, floating or already on the ground: the last position is the answer
        return;
    }
    // Ground level: own altitude when known (ellipsoidal, like the sonde), else sea level.
    double ground = predGround();
    float dir = track.vDir * DEG_TO_RAD;
    pred = pred_landing(&wind, track.lat, track.lon, track.alt, -vvEma, track.vH * cosf(dir), track.vH * sinf(dir),
                        ground);
}

// Burst = a real ascent followed by three frames falling faster than 3 m/s.
static void checkBurst()
{
    // In flight: ten frames in a row moving vertically faster than 3 m/s, or clearly up in the air.
    // A sonde lying on the ground never qualifies, GPS noise included.
    flightRun = fabsf(track.vV) > 3.0f ? flightRun + 1 : 0;
    if (flightRun >= 10 || track.alt > 3000.0) flightSeen = true;

    if (track.vV > 2.0f) {
        ascentFrames++;
        descentFrames = 0;
    } else if (track.vV < -3.0f) {
        descentFrames++;
    } else {
        descentFrames = 0;
    }
    if (!burstAnnounced && ascentFrames >= 5 && descentFrames >= 3) {
        burstAnnounced = true;
        setMsgf("BURST at about %.0f m", track.alt);
        if (cfg.vol) audio_pattern(2200, 100, 3, 120);
    }
}

// Another sonde (heard by radio or taken from SondeHub): nothing of the previous one may stay, not its winds,
// its prediction, its burst or its "really flying" mark (that one lets uploads through).
static void sondeChanged(float vV)
{
    ascentFrames = descentFrames = 0;
    burstAnnounced = false;
    flightRun = 0;
    flightSeen = false;
    nTrk = 0, trkGen++;
    pred_reset(&wind);
    pred.ok = false;
    vvEma = vV;
}

static void handleCapture(const CaptureResult &c)
{
    stats.sync++;
    Rs41Result res;
    bool isRs41 = c.type == ST_RS41;
    bool ok = isRs41                ? decodeCapture(c, &res)
              : c.type == ST_DFM       ? decodeDfm(c, &res)
              : c.type == ST_IMET54    ? decodeImet54(c, &res)
                                       : decodeM10(c, &res);
    if (ok) {
        stats.eccOk++;
        lastOkMs = millis();
        if (!firstOkMs) firstOkMs = lastOkMs - tuneMs ? lastOkMs - tuneMs : 1; // bench: time to the first frame
        if (c.type < ST_COUNT && polInv[c.type] != (uint8_t)c.invert) { // remembered across restarts
            polInv[c.type] = c.invert;
            prefs.begin("rs41poc", false);
            prefs.putBytes("polinv", polInv, sizeof(polInv));
            prefs.end();
        }
        if (isRs41) rs41_calib_update(&calib, &res);
        if (res.gpsValid && res.gpsCrcOk) {
            track.have = true;
            track.lat = res.lat;
            track.lon = res.lon;
            track.alt = res.alt;
            track.vH = (float)res.vH;
            track.vDir = (float)res.vDir;
            track.vV = (float)res.vV;
            track.sats = res.numSV;
            track.ms = millis();
            if (track.manual || strcmp(track.id, res.id) != 0) sondeChanged(track.vV);
            snprintf(track.id, sizeof(track.id), "%s", res.id);
            track.manual = track.restored = track.fromNet = false;
            track.unixT = res.unixTime;
            lostAnnounced = false;
            checkBurst();
            updatePrediction();
            trackPointAdd(track.lat, track.lon);
            if (millis() - lastPosSaveMs > LASTPOS_SAVE_MS) {
                lastPosSaveMs = millis();
                saveLastPos();
            }
        }
        // Every decoded frame is heard on every page at the frame volume: a soft tick, or a beep whose pitch
        // follows the signal (on the approach page always the tick: the meter there already gives the pitch).
        if (cfg.sound && screen != SCR_APPROACH) audio_frame_beep(audio_rssi_pitch(c.rssiSync), 40);
        else audio_tick();
        if (scanning && scanPhase == SCAN_TRY) scanFound(res);
    }
    if (res.gpsValid && res.gpsCrcOk) stats.gpsOk++;
    last = res;
    haveLast = true;
    lastRssi = c.rssiSync;


    float degC = -999;
    uint32_t txKhz = 0;
    int kt = -2, bt = -2, cd = -2; // -2 = not received yet, -1 = not set
    if (isRs41) {
        rs41_temperature(&calib, &res, &degC);
        rs41_calib_freq_khz(&calib, &txKhz);
        rs41_calib_kill_timer(&calib, &kt);
        rs41_calib_burst_timer(&calib, &bt);
        rs41_calib_countdown(&calib, &cd);
    }

    bool me = myFixOk();
    const GpsFix &g = nmea.fix;
    double dist = -1, brg = -1;
    if (me && res.gpsValid) {
        geo_dist_brg(g.lat, g.lon, res.lat, res.lon, &dist, &brg);
    }

    if (ok && res.frameCrcOk) {
        TrackRow row = {};
        row.utc = res.unixTime;
        row.frame = res.frameNb;
        row.hasPos = res.gpsValid && res.gpsCrcOk;
        row.lat = res.lat;
        row.lon = res.lon;
        row.alt = res.alt;
        row.vH = (float)res.vH;
        row.vDir = (float)res.vDir;
        row.vV = (float)res.vV;
        row.sats = res.numSV;
        row.rssi = c.rssiSync;
        row.battV = res.battV;
        row.hasTemp = degC > -900;
        row.tempC = degC;
        row.hasMe = me;
        row.myLat = g.lat;
        row.myLon = g.lon;
        row.myAlt = g.altM + g.geoidSepM;
        row.dist = dist;
        row.brg = brg;
        row.hasPred = pred.ok && row.hasPos;
        row.predLat = pred.lat;
        row.predLon = pred.lon;
        row.hasO3 = res.o3Valid;
        row.o3Mpa = res.o3Mpa;
        trackAppend(res.id, row);

        // SondeHub format for the uploads and the JSON log: every type, never the bench simulators (checked
        // again in uploader_push). The log keeps whole flights; the uploads only sondes in flight if so set.
        bool upNow = (cfg.shUpload || cfg.aprsUp) && (!cfg.shFlightOnly || flightSeen);
        bool blocked = sh_serial_blocked(res.id);
        if ((upNow || cfg.shLog || psActive()) && row.hasPos && (!blocked || PRED_SERVER_ALLOW_HTTP)) {
            ShFrame f = {};
            f.type = c.type;
            snprintf(f.serial, sizeof(f.serial), "%s", res.id);
            snprintf(f.model, sizeof(f.model), "%s", res.model);
            uint32_t rxNow = nowUnix();
            if (isRs41) { // raw GPS time, as SondeHub wants it, and the frame counter
                f.timeUnix = row.utc ? row.utc + GPS_UTC_LEAP_S : 0;
                f.timeMs = (uint16_t)(res.gpsTowMs % 1000);
                f.timeGps = true;
                f.frame = res.frameNb;
            } else if (c.type == ST_IMET54) { // time of day only: the date from our clock
                f.timeUnix = sh_day_time(rxNow, imetSecOfDay);
                f.timeGps = true;
                f.frame = f.timeUnix;
            } else { // DFM, M10/M20: UTC; no unique frame counter, so the time (SondeHub rule)
                f.timeUnix = f.frame = row.utc;
            }
            f.rxUnix = rxNow ? rxNow : row.utc;
            f.lat = res.lat;
            f.lon = res.lon;
            f.alt = res.alt;
            f.velH = (float)res.vH;
            f.velV = (float)res.vV;
            f.heading = (float)res.vDir;
            f.sats = res.numSV;
            f.batt = res.battV;
            f.hasTemp = row.hasTemp;
            f.temp = degC;
            rs41_calib_type(&calib, f.subtype);
            f.burstTimer = bt >= 0 ? bt : -1;
            f.txKhz = txKhz;
            f.freqMhz = (float)cfg.freq;
            f.rssi = c.rssiSync;
            f.hasUploaderPos = posTo(POS_SH) && me && fixStable();
            f.upLat = g.lat;
            f.upLon = g.lon;
            f.upAlt = g.altM + g.geoidSepM;
#if PRED_SERVER
            if (f.timeUnix && cfg.psOn) predsrv_push(f); // simulators: only a test build to its mock takes them
#endif
            if (blocked) f.timeUnix = 0; // nowhere else
            if (f.timeUnix && upNow && cfg.shUpload) uploader_push(f);
            if (f.timeUnix && upNow && cfg.aprsUp) uploader_aprs_push(f);
            if (f.timeUnix && cfg.shLog) { // own position only when shared everywhere ("all")
                f.hasUploaderPos = cfg.sharePos == POS_ALL && me && fixStable();
                shLogWrite(f);
            }
        }
    }

    char b[512];
    snprintf(b, sizeof(b),
             "%lu,%s,%.3f,%.1f,%s,rssi=%.1f,ecc=%d/%d,era=%d,len=%d,id=%s,fn=%u,crcF=%d,crcG=%d,gps=%d,"
             "lat=%.6f,lon=%.6f,alt=%.1f,vH=%.1f,vV=%.1f,sats=%u,"
             "derr=%04x,batt=%.1f,T=%.1f,cal=%d,txkhz=%lu,kt=%d,bt=%d,cd=%d,utc=%lu,sacc=%u,pdop=%u,"
             "myfix=%d,mylat=%.6f,mylon=%.6f,myalt=%.0f,mysats=%u,mykmh=%.1f,mycrs=%.0f,dist=%.0f,brg=%.0f",
             (unsigned long)c.tSync, !isRs41 ? res.model : c.n1 > CAP_P1_LEN ? "F FULL" : "A 255+ERA", cfg.freq, Sx1262::BW_KHZ[cfg.bwIdx],
             c.invert ? "INV" : "NOR", c.rssiSync, res.ecc1, res.ecc2, res.nEra, res.frameLen,
             res.id[0] ? res.id : "-", res.frameNb, res.frameCrcOk, res.gpsCrcOk, res.gpsValid, res.lat, res.lon,
             res.alt, res.vH, res.vV, res.numSV, c.devErrors, res.battV, degC, calib.nHave, (unsigned long)txKhz, kt, bt, cd,
             (unsigned long)res.unixTime, res.sAcc, res.pDOP,
             me, me ? g.lat : 0.0, me ? g.lon : 0.0, me ? g.altM + g.geoidSepM : 0.0f, g.sats, me ? g.speedKmh : 0.0f,
             me ? g.courseDeg : 0.0f, dist, brg);
    if (res.xdata) {
        size_t k = strlen(b);
        snprintf(b + k, sizeof(b) - k, ",xd=%s,o3=%.3f", res.xdataStr, res.o3Valid ? res.o3Mpa : -1.0f);
    }
    logLine(logPath, b);

    if (cfg.rawLog) {
        // Same layout as earlier versions (mode field 0 = A) so old analysis scripts still work.
        static char r[32 + 2 * CAP_MAX_LEN];
        int k = snprintf(r, sizeof(r), "%lu,0,%d,P1,", (unsigned long)c.tSync, c.invert ? 1 : 0);
        hexInto(r + k, c.p1, c.n1 < CAP_MAX_LEN ? c.n1 : CAP_MAX_LEN);
        logLine(rawPath, r);
    }
}

static uint32_t lastKeys = 0;

// Keys from the debug console (USB or Wi-Fi) are queued and handled here, exactly like the keyboard.
static char injQ[64];
static uint8_t injHead = 0, injTail = 0;

static void injectKey(char k)
{
    uint8_t n = (injHead + 1) % sizeof(injQ);
    if (n == injTail) return; // full: drop
    injQ[injHead] = k;
    injHead = n;
}

// Every key goes to the log ring (not to the SD): /log shows what arrived, on which screen.
static void keyIn(char k, const char *src)
{
    char b[80];
    if (editSecret() && k >= 0x20 && k < 0x7F) // a character of a password or token: not even its code
        snprintf(b, sizeof(b), "# key t=%lu %s '*' screen=%u edit=%d", (unsigned long)millis(), src, (unsigned)screen,
                 textEditing);
    else
        snprintf(b, sizeof(b), "# key t=%lu %s 0x%02x '%c' screen=%u edit=%d", (unsigned long)millis(), src, (uint8_t)k,
                 k >= 0x20 && k < 0x7F ? k : '?', (unsigned)screen, textEditing);
    logRemember(b);
    lastInputMs = millis();
    // On a dimmed screen the first key of the keyboard only wakes it (no command typed in the dark).
    if (wakeScreen() && !strcmp(src, "kb")) return;
    handleKey(k);
}

static bool pollKeys()
{
    bool any = false;
    if (kbOk) {
        for (char k = readKey(); k; k = readKey()) {
            keyIn(k, "kb");
            any = true;
        }
    }
    while (injTail != injHead) {
        keyIn(injQ[injTail], "dbg");
        injTail = (injTail + 1) % sizeof(injQ);
        any = true;
    }
    // Trackball and touch (real or injected by the debug server).
    InputEvent e;
    while (input_poll(e)) {
        static const char *const N[] = {"none", "up", "down", "left", "right", "select", "tap", "long", "hold"};
        char b[80];
        snprintf(b, sizeof(b), "# input t=%lu %s x=%d y=%d screen=%u", (unsigned long)millis(), N[e.type], e.x, e.y,
                 (unsigned)screen);
        logRemember(b);
        any = true;
        lastInputMs = millis();
        if (wakeScreen() && e.type != IN_HOLD) continue; // the first touch or move only wakes the screen
        handleInput(e);
    }
    return any;
}

// ---- USB debug console: a PC can read the state, take screenshots and press keys ----
// Protocol in tools/tdeck_console.py. Commands are handled only between frames.

static char conBuf[128];
static int conLen = 0;

static void statusText(Print &o)
{
    const GpsFix &g = nmea.fix;
    UploaderStatus st;
    uploader_status(&st);
    char m[64];
    mapview_stats(m, sizeof(m));
    o.printf("STATUS fw=%s screen=%u scanning=%d editing=%d freq=%.3f radio=%d sd=%d sd_dir=%s kb=%d locked=%d autolock_s=%u\n",
             FW_NAME, (unsigned)screen, scanning, textEditing, cfg.freq, radioOk, sdOk, sdDir, kbOk, screenLocked,
             AUTOLOCK_S[cfg.autoLock]);
    o.printf("STATUS slow");
    for (const SlowStep &st : slowSteps)
        if (st.ms) o.printf(" %s=%lums@%lus", st.name, (unsigned long)st.ms, (unsigned long)((millis() - st.atMs) / 1000));
    o.printf("\n");
    char pol[ST_COUNT + 1]; // polarity remembered per type, in SondeType order (AUTO RS41 DFM M10 iMet54)
    for (int t = 0; t < ST_COUNT; t++) pol[t] = polInv[t] ? '1' : '0';
    pol[ST_COUNT] = 0;
    o.printf("STATUS type set=%s rx=%s rotates=%d presets=%d inv=%d ainv=%d polinv=%s bw=%.1f\n", sonde_type_name(cfg.stype),
             sonde_type_name(rxType), typeRotates(), nPlist, cfg.invert, cfg.autoInvert, pol, Sx1262::BW_KHZ[cfg.bwIdx]);
    o.printf("STATUS full on=%d full320=%lu full518=%lu cut=%lu short=%lu last_len=%d\n", cap.fullFrames(),
             (unsigned long)cap.nFull320, (unsigned long)cap.nFull518, (unsigned long)cap.nCut,
             (unsigned long)cap.nShort, capRes.n1);
    o.printf("STATUS frames sync=%lu ok=%lu last_rssi=%.1f idle_rssi=%.1f last_ok_age=%lus id=%s fn=%u first_ms=%lu\n",
                  (unsigned long)stats.sync, (unsigned long)stats.eccOk, lastRssi, rssiIdle,
                  (unsigned long)((millis() - lastOkMs) / 1000), haveLast ? last.id : "-", haveLast ? last.frameNb : 0,
                  (unsigned long)firstOkMs);
    o.printf("STATUS sonde have=%d manual=%d restored=%d lat=%.5f lon=%.5f alt=%.0f vV=%.1f vH=%.1f sats=%u "
                  "age=%lus flight=%d cal=%d\n",
                  track.have, track.manual, track.restored, track.lat, track.lon, track.alt, track.vV, track.vH,
                  track.sats, (unsigned long)((millis() - track.ms) / 1000), flightSeen, calib.nHave);
    o.printf("STATUS gps seen=%d fix=%d baud=%lu sats=%u lat=%.5f lon=%.5f kmh=%.1f good=%lu bad=%lu\n", gpsSeen,
                  myFixOk(), (unsigned long)gpsBaud, g.sats, g.lat, g.lon, g.speedKmh, (unsigned long)nmea.good,
                  (unsigned long)nmea.bad);
    o.printf("STATUS map ok=%d error=%s zoom=%d tile_zoom_max=%d centre=%s %s\n", mapOk, mapview_error(),
                  mapview_zoom_level(), mapview_tile_zoom_max(), mapview_center_name(), m);
    {
        double bl, bn;
        int be;
        bool bsh = false, bok = bestPred(&bl, &bn, &be, &bsh);
        o.printf("STATUS pred ok=%d lat=%.5f lon=%.5f time=%.0fs wind=%d%% nav_pred=%d source=%s\n", pred.ok, pred.lat,
                 pred.lon, pred.timeS, pred.coveragePct, navPred,
                 !bok ? "-" : predSrc == PSRC_SRV ? "srv" : bsh ? "sh" : "own");
    }
#if PRED_SERVER
    {
        char ps[320];
        predsrv_status(ps, sizeof(ps));
        o.printf("STATUS %s nav_srv=%d\n", ps, navSrv);
    }
#endif
    char fiWord[APRS_COMMENT_MAX]; // shReplyWord has one buffer: the aprs.fi text gets its own
    snprintf(fiWord, sizeof(fiWord), "%s", cfg.aprsFiCmt && aprsFiText[0] ? aprsFiText : "-");
    for (char *p = fiWord; *p; p++)
        if (*p == ' ') *p = '_';
    o.printf("STATUS wifi on=%d connected=%d ip=%s net_ok_s=%d net_fails=%lu net_resets=%lu drops=%lu reason=%d rssi_lost=%d joins=%lu debug=%d upload=%d target=%s sent=%lu failed=%lu http=%d "
             "station=%d mobile=%d every_s=%lu station_sent=%lu station_http=%d aprs=%d aprs_conn=%d aprs_ok=%d "
             "aprs_sent=%lu aprs_beacons=%lu icon=/%c sh_reply_code=%d sh_reply=%s aprsfi=%d aprsfi_conn=%d "
             "aprsfi_ok=%d aprsfi_beacons=%lu aprsfi_text=%s\n",
             st.wifiOn, st.wifiConnected, st.ip, st.netOkS, (unsigned long)st.netFails, (unsigned long)st.netResets, (unsigned long)st.wifiDrops, st.wifiReason, st.wifiRssi, (unsigned long)st.wifiJoins, cfg.dbgServer, cfg.shUpload, uploader_target(), (unsigned long)st.sent,
             (unsigned long)st.failed, st.lastHttp, cfg.sharePos, cfg.shMobile,
             cfg.shMobile ? (unsigned long)STA_MOB_S[cfg.staMob] : STA_FIX_MIN[cfg.staFix] * 60UL,
             (unsigned long)st.stationSent, st.stationHttp, cfg.aprsUp, st.aprsConnected, st.aprsVerified,
             (unsigned long)st.aprsSent, (unsigned long)st.aprsBeacons,
             cfg.shMobile ? APRS_MOBILE_SYM[cfg.mobIcon] : APRS_SYM_FIXED, st.shReplyCode, shReplyWord(st.shReply),
             cfg.aprsFi, st.fiConnected, st.fiVerified, (unsigned long)st.fiBeacons,
             fiWord);
    o.printf("STATUS kb reads=%lu nobyte=%lu zero=%lu keys=%lu last_read_age=%lums max_gap=%lums\n",
             (unsigned long)kbReads, (unsigned long)kbNoByte, (unsigned long)kbZero, (unsigned long)kbKeys,
             (unsigned long)(millis() - kbLastReadMs), (unsigned long)kbMaxGapMs);
    uint32_t tl, ta, tc;
    mapview_timing(&tl, &ta, &tc);
    uint32_t tto;
    const char *bad = mapview_last_bad(&tto);
    o.printf("STATUS sd hz=%lu tiles_loaded=%lu tile_ms_last=%lu tile_ms_avg=%lu tiles_timed_out=%lu last_bad=%s\n",
             (unsigned long)sdHz, (unsigned long)tc, (unsigned long)tl, (unsigned long)ta, (unsigned long)tto,
             bad[0] ? bad : "-");
    BleStatus bs;
    ble_status(&bs);
    o.printf("STATUS apps mode=%u ble=%d name=%s connected=%d subscribed=%d mtu=%u lines=%lu bytes_in=%lu "
             "rdz_client=%d rdz_lines=%lu\n",
             (unsigned)cfg.apps, bs.running, bs.name, bs.connected, bs.subscribed, (unsigned)bs.mtu,
             (unsigned long)bs.linesSent, (unsigned long)bs.bytesIn, rdz_link_client(), (unsigned long)rdz_link_lines());
    o.printf("STATUS gpsdb restored=%u saved=%u entries=%d saved_age_s=%lu collecting=%d\n",
             (unsigned)gpsdbRestoredBytes, (unsigned)gpsdbSavedBytes, gpsdbEntries,
             gpsdbSavedMs ? (unsigned long)((millis() - gpsdbSavedMs) / 1000) : 0UL, gpsdbCollecting);
    o.printf("STATUS gnss fixtype=%u pdop=%.1f utc=%lu inview=%u/%u/%u/%u/%u gsv_groups=%lu first_fix_s=%lu "
             "aid=%s aid_s=%lu net=%d net_time=%d saved_pos=%d good_fix_s=%lu stable=%d\n",
             g.fixType, g.pdop, (unsigned long)g.utc, nmea.nSv[0], nmea.nSv[1], nmea.nSv[2], nmea.nSv[3], nmea.nSv[4],
             (unsigned long)nmea.gsvGroups, (unsigned long)(firstFixMs / 1000), aidSent ? aidText : "-",
             (unsigned long)(aidMs / 1000), cfg.net, netTimeOk(), myPosSaved,
             fixGoodSince ? (unsigned long)((millis() - fixGoodSince) / 1000) : 0UL, fixStable());
    o.printf("STATUS sondehub follow=%s sondes=%d sondes_age=%lus pred=%d pred_land=%.5f,%.5f http=%d req=%lu fail=%lu "
             "track_net=%d radio_sh_m=%.0f fast=%d stale=%d n_sondes=%lu n_pred=%lu n_landed=%lu n_summary=%lu n_sites=%lu "
             "n_tawhiri=%lu launch_h=%lu launches=%d json_log=%d json_lines=%lu\n",
             followId(), netr->nSondes, netr->sondesMs ? (unsigned long)((millis() - netr->sondesMs) / 1000) : 0UL, shPredOk(),
             netr->pred.landLat, netr->pred.landLon, netr->lastHttp, (unsigned long)netr->requests, (unsigned long)netr->failures,
             track.fromNet, shDiffM, sondesFast, followStale, (unsigned long)netr->perKind[NET_Q_SONDES],
             (unsigned long)netr->perKind[NET_Q_PRED], (unsigned long)netr->perKind[NET_Q_LANDED],
             (unsigned long)netr->perKind[NET_Q_SUMMARY], (unsigned long)netr->perKind[NET_Q_SITES],
             (unsigned long)netr->perKind[NET_Q_TAWHIRI], (unsigned long)(LAUNCH_H_S[cfg.launchH] / 3600), netr->nLaunch,
             cfg.shLog, (unsigned long)shLogLines);
    InputStats is;
    input_stats(&is);
    o.printf("STATUS input touch_addr=0x%02x probes=%lu probe_ms=%lu touch=%d steps=%lu/%lu/%lu/%lu clicks=%lu taps=%lu longs=%lu last=%d,%d "
             "raw=%d,%d nav_depth=%d touch_power_cycle=%d tb_step=%u\n",
             is.touchAddr, (unsigned long)is.touchProbes, (unsigned long)is.probeMs, cfg.touch, (unsigned long)is.steps[0], (unsigned long)is.steps[1],
             (unsigned long)is.steps[2], (unsigned long)is.steps[3], (unsigned long)is.clicks, (unsigned long)is.taps,
             (unsigned long)is.longs, is.lastX, is.lastY, is.lastRawX, is.lastRawY, navDepth, is.powerCycled, is.tbStep);
    o.printf("STATUS lcd pushes=%lu held=%lu last_us=%lu max_us=%lu last_age=%lums\n", (unsigned long)pushCount,
             (unsigned long)pushHeld, (unsigned long)pushLastUs, (unsigned long)pushMaxUs,
             (unsigned long)(millis() - pushLastMs));
    o.printf("STATUS lastrun %s\n", lastRunNote[0] ? lastRunNote : "clean");
    o.printf("STATUS mem heap=%u heap_min=%u heap_block=%u psram=%u stack_free=%u/%u/%u uptime=%lus message=%s\n",
             (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)ESP.getFreePsram(),
             (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned)uploader_stack_free(), (unsigned)netsvc_stack_free(), (unsigned long)(millis() / 1000), msgActive() ? uiMsg : "");
    o.println("STATUS END");
}

static void conStatus()
{
    statusText(Serial);
}

// Screenshot from the canvas, which is exactly what the display shows (big-endian RGB565).
static void conShot()
{
    static uint8_t line[320 * 2];
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
    Serial.setTxTimeoutMs(500); // the host is reading: wait for it instead of dropping data
#endif
    Serial.printf("SHOT %d %d BE565\n", gfx.width(), gfx.height());
    for (int y = 0; y < gfx.height(); y++) {
        esp_task_wdt_reset(); // a slow USB host takes up to 500 ms a row
        for (int x = 0; x < gfx.width(); x++) {
            uint16_t v = gfx.readPixel(x, y);
            line[2 * x] = v >> 8;
            line[2 * x + 1] = v & 0xFF;
        }
        Serial.write(line, gfx.width() * 2);
    }
    Serial.print("\nSHOT END\n");
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
    Serial.setTxTimeoutMs(0);
#endif
}

// An injected key or touch (debug console, web): handled at once, then one redraw.
static void inputNow()
{
    pollKeys();
    drawUi();
    lastUi = millis();
}

// Same path as the keyboard: queued, handled by pollKeys, then one redraw.
static void conKey(char k)
{
    injectKey(k);
    inputNow();
}

// "enter", "bksp", "esc", "space", "tab", or a byte value: "0x1b", "27".
static int keyByName(const char *n)
{
    if (!strcmp(n, "enter")) return '\r';
    if (!strcmp(n, "bksp")) return 0x08;
    if (!strcmp(n, "esc")) return 0x1B;
    if (!strcmp(n, "space")) return ' ';
    if (!strcmp(n, "tab")) return '\t';
    char *end;
    long v = strtol(n, &end, 0);
    if (*n && !*end && v > 0 && v < 256) return (int)v;
    return -1;
}

static void conCommand(char *cmd)
{
    while (*cmd == ' ') cmd++;
    if (!strcmp(cmd, "status")) {
        conStatus();
    } else if (!strcmp(cmd, "shot")) {
        conShot();
    } else if (!strncmp(cmd, "key ", 4)) {
        for (char *c = cmd + 4; *c; c++) conKey(*c);
        Serial.println("OK");
    } else if (!strncmp(cmd, "code ", 5) && keyByName(cmd + 5) > 0) {
        conKey((char)keyByName(cmd + 5));
        Serial.println("OK");
    } else if (keyByName(cmd) > 0 && !isdigit((unsigned char)cmd[0])) {
        conKey((char)keyByName(cmd));
        Serial.println("OK");
    } else {
        Serial.println("commands: status, shot, key <chars>, enter, bksp, esc, space, tab, code <byte>");
    }
}

static void pollConsole()
{
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n') {
            if (conLen) {
                conBuf[conLen] = 0;
                conLen = 0;
                conCommand(conBuf);
            }
        } else if (conLen < (int)sizeof(conBuf) - 1) {
            conBuf[conLen++] = c;
        }
    }
}

// ---- SondeHub, read only: sondes nearby, the followed sonde without radio, its predicted landing ----

#define RF_FRESH_MS 10000 // radio frames younger than this always win over internet data

// Serial we follow: picked in the Nearby list, else the sonde being received (or the last one).
// Sonde whose SondeHub data is asked for: the one picked in Nearby, else the one the radio heard since the start.
// A sonde restored after a restart is followed only if its last position is recent (a restart mid-hunt);
// one picked up yesterday stays on the map and the compass without asking SondeHub anything.
#define FOLLOW_RESTORED_S (3UL * 3600)
static const char *followIdAny();

// The sonde SondeHub is asked about: never a bench simulator (no requests for fake serials).
static const char *followId()
{
    const char *id = followIdAny();
    return sh_serial_blocked(id) ? "" : id;
}

static const char *followIdAny()
{
    if (followSerial[0]) return followSerial;
    if (track.have && !track.manual && track.id[0]) {
        if (!track.restored) return track.id;
        uint32_t now = nowUnix();
        return now && track.unixT && now - track.unixT < FOLLOW_RESTORED_S ? track.id : "";
    }
    return haveLast ? last.id : "";
}

// SondeHub prediction kept in flash (namespace rs41shp): the last one received survives a restart and the
// loss of internet. The live answer wins when it is for the same sonde.
static ShPrediction savedShPred = {};

static void saveShPred(const ShPrediction &p)
{
    savedShPred = p;
    prefs.begin("rs41shp", false);
    prefs.putBytes("p", &p, sizeof(p));
    prefs.end();
}

static void loadShPred()
{
    prefs.begin("rs41shp", true);
    if (prefs.getBytesLength("p") == sizeof(ShPrediction)) prefs.getBytes("p", &savedShPred, sizeof(savedShPred));
    prefs.end();
}

// The prediction to use for the followed sonde: live from SondeHub, else the saved one; null if none.
static const ShPrediction *shPred()
{
    const char *id = followId();
    if (!id[0]) return nullptr;
    if (netr && netr->pred.ok && !strcmp(netr->pred.vehicle, id)) return &netr->pred;
    if (savedShPred.ok && !strcmp(savedShPred.vehicle, id)) return &savedShPred;
    return nullptr;
}

static uint32_t nowUnix();

static bool shPredOk()
{
    const ShPrediction *p = shPred();
    if (!p) return false;
    // SondeHub keeps the last prediction of old flights: ignore it
    // once its landing time is more than 6 hours ago. Without any clock it cannot be checked: not used.
    uint32_t now = nowUnix();
    return now && (long)p->landTime > (long)now - 6L * 3600;
}

// Landing point to navigate to: the T-Deck's own prediction first, else SondeHub's.
static double predGround() { return myFixOk() ? nmea.fix.altM + nmea.fix.geoidSepM : 0.0; }

// Own prediction when it can be trusted (pred_prefer_own), else SondeHub's, else the own one anyway.
// Landing point to steer to. Above 2500 m from the ground a fresh point of the prediction server (when
// built in) wins, as it knows the low winds; below, the T-Deck's own (pred_prefer_own), then the server's,
// then SondeHub's. The n key can pin the server's point.

static uint32_t loopSeq = 0; // loop passes: bestPred keeps its answer within one pass
static bool bestPredCalc(double *lat, double *lon, int *etaS, bool *fromSh);

static bool bestPred(double *lat, double *lon, int *etaS, bool *fromSh)
{
    static struct {
        uint32_t seq, trackMs, net, srv;
        bool predOk, nav, navS, manual;
        bool ok, sh;
        double lat, lon;
        int eta;
        uint8_t src;
    } c = {};
    uint32_t srv = 0;
#if PRED_SERVER
    srv = psRes.seq;
#endif
    bool same = c.seq == loopSeq && c.trackMs == track.ms && c.net == netSeen && c.srv == srv && c.predOk == pred.ok &&
                c.nav == navPred && c.navS == navSrv && c.manual == track.manual;
    if (!same) {
        c.ok = bestPredCalc(&c.lat, &c.lon, &c.eta, &c.sh);
        c.src = predSrc;
        c.seq = loopSeq, c.trackMs = track.ms, c.net = netSeen, c.srv = srv, c.predOk = pred.ok;
        c.nav = navPred, c.navS = navSrv, c.manual = track.manual;
    }
    predSrc = c.src;
    *lat = c.lat, *lon = c.lon, *etaS = c.eta, *fromSh = c.sh;
    return c.ok;
}

static bool bestPredCalc(double *lat, double *lon, int *etaS, bool *fromSh)
{
    const ShPrediction *sp = shPred();
    bool shOk = shPredOk() && !sp->landed;
    bool own = pred.ok && !track.manual;
    bool srvOk = psPredOk();
    *fromSh = false;
    predSrc = PSRC_NONE;
    if (srvOk && (navSrv || track.alt - predGround() >= PRED_OWN_BELOW_M)) {
        predSrc = PSRC_SRV;
        return srvPoint(lat, lon, etaS);
    }
    if (own && (!shOk || pred_prefer_own(pred.coveragePct, track.alt - predGround()))) {
        *lat = pred.lat;
        *lon = pred.lon;
        *etaS = (int)pred.timeS;
        predSrc = PSRC_OWN;
        return true;
    }
    if (srvOk) {
        predSrc = PSRC_SRV;
        return srvPoint(lat, lon, etaS);
    }
    if (shOk) {
        *lat = sp->landLat;
        *lon = sp->landLon;
        long eta = (long)sp->landTime - (long)nowUnix();
        *etaS = eta > 0 ? (int)eta : 0;
        *fromSh = true;
        predSrc = PSRC_SH;
        return true;
    }
    return false;
}

// ---- Prediction server link (built only with PRED_SERVER) ----
#if PRED_SERVER
static bool psActive() { return cfg.psOn; }

// Sonde followed, for the server: bench simulators too, but only in the test build pointed at the PC mock.
static const char *psFollowId()
{
    return PRED_SERVER_ALLOW_HTTP && !strncmp(cfg.psUrl, "http://", 7) ? followIdAny() : followId();
}

// The server's point is used for the sonde followed, while fresh (2 min).
static bool psPredOk()
{
    if (!cfg.psOn || !psHave || !psRes.pred.ok || !psRes.predMs || track.manual) return false;
    if (millis() - psRes.predMs > 120000UL) return false;
    const char *id = psFollowId();
    return id && id[0] && !strcmp(id, psRes.pred.serial);
}

static bool srvPoint(double *lat, double *lon, int *etaS)
{
    *lat = psRes.pred.lat;
    *lon = psRes.pred.lon;
    long eta = psRes.pred.landUnix ? (long)psRes.pred.landUnix - (long)nowUnix() : 0;
    *etaS = eta > 0 ? (int)eta : 0;
    return true;
}

// Once a second: device position and followed sonde to the link, its answer back, and a reminder when the
// server hunts another sonde.
static void predServerPoll()
{
    static uint32_t lastMs = 0, huntMsgMs = 0;
    if (millis() - lastMs < 1000) return;
    lastMs = millis();
    if (!cfg.psOn) {
        psHave = false;
        navSrv = false;
        return;
    }
    PredSrvDevice d = {};
    const GpsFix &g = nmea.fix;
    d.fix = myFixOk();
    if (d.fix) {
        d.lat = g.lat;
        d.lon = g.lon;
        d.alt = g.altM;
        d.velH = g.speedKmh / 3.6f;
        d.dir = g.courseDeg;
    }
    const char *id = psFollowId();
    snprintf(d.follow, sizeof(d.follow), "%s", id ? id : "");
    predsrv_device(d);
    psHave = predsrv_result(&psRes);
    if (navSrv && !psPredOk()) navSrv = false;
    if (psHave && psRes.hunt && d.follow[0] && strcmp(psRes.huntSerial, d.follow) &&
        (!huntMsgMs || millis() - huntMsgMs > 60000UL)) {
        huntMsgMs = millis();
        setMsgf("%s hunts %s, not %s", predsrv_tag(), psRes.huntSerial, d.follow);
    }
}
#else
static bool psActive() { return false; }
static bool psPredOk() { return false; }
static bool srvPoint(double *, double *, int *) { return false; }
static void predServerPoll() {}
#endif

// Short label of the chosen landing point: LAND (own), SH LAND, or the server's tag.
static const char *predLabel()
{
#if PRED_SERVER
    static char b[16];
    if (predSrc == PSRC_SRV) {
        snprintf(b, sizeof(b), "%s LAND", predsrv_tag());
        return b;
    }
#endif
    return predSrc == PSRC_SH ? "SH LAND" : "LAND";
}

static const ShSonde *netFind(const char *serial)
{
    for (int i = 0; i < netr->nSondes; i++)
        if (!strcmp(netr->sondes[i].serial, serial)) return &netr->sondes[i];
    return nullptr;
}

static void trackPointAdd(double lat, double lon);

// Internet position of the followed sonde becomes the target while the radio does not hear it.
static void applyNet()
{
    const char *id = followId();
    const ShSonde *s = id[0] ? netFind(id) : nullptr;
    if (!s) return;
    bool rfFresh = haveLast && millis() - lastOkMs < RF_FRESH_MS && !strcmp(last.id, s->serial);
    if (rfFresh) {
        // Same sonde heard by radio and seen online: how far apart are the two positions?
        shDiffM = (float)geo_distance_m(track.lat, track.lon, s->lat, s->lon);
        return;
    }
    if (track.manual && !followSerial[0]) return; // a typed target stays until something is picked
    bool newer = !track.have || track.restored || strcmp(track.id, s->serial) || s->time > track.unixT;
    if (!newer || !s->time) return;
    long age = netTimeOk() ? (long)time(nullptr) - (long)s->time : 0;
    if (age < 0) age = 0;
    if (strcmp(track.id, s->serial)) sondeChanged(s->velV); // another sonde: forget the old one
    track.have = true;
    track.lat = s->lat;
    track.lon = s->lon;
    track.alt = s->alt;
    track.vH = s->velH;
    track.vV = s->velV;
    track.vDir = s->heading;
    track.sats = 0;
    track.ms = millis() - (uint32_t)(age > 86400 ? 86400 : age) * 1000u;
    snprintf(track.id, sizeof(track.id), "%.15s", s->serial);
    track.manual = track.restored = false;
    track.fromNet = true;
    track.unixT = s->time;
    trackPointAdd(track.lat, track.lon);
}

// What the web page's SondeHub tab asks, alive for 90 s after its last request: the queries follow the page
// as they follow the T-Deck's own SondeHub page (same limits: fast list only while a Near list is open,
// launches only while a Launches tab is open).
#define WEB_SH_KEEP_MS 90000UL
static uint32_t webNearMs, webLandedMs, webLaunchMs, webSumMs;
static int webLandedIdx = 2;
static char webSummary[12] = "";
static bool webWants(uint32_t ms) { return ms && millis() - ms < WEB_SH_KEEP_MS; }

static void netAppPoll()
{
    static uint32_t lastReq = 0;
    if (millis() - lastReq < 1000) return;
    lastReq = millis();
    NetRequest r = {};
    r.enabled = cfg.net;
    if (myFixOk()) {
        r.haveCentre = true;
        r.lat = nmea.fix.lat;
        r.lon = nmea.fix.lon;
    } else if (myPosSaved) {
        r.haveCentre = true;
        r.lat = myLat;
        r.lon = myLon;
    } else if (track.have) {
        r.haveCentre = true;
        r.lat = track.lat;
        r.lon = track.lon;
    }
    snprintf(r.follow, sizeof(r.follow), "%s", followId());
    bool onPage = screen == SCR_NEARBY;
    r.landedLast = onPage && shTab == 1 ? LANDED_PERIOD_S[landedIdx]
                   : webWants(webLandedMs) ? LANDED_PERIOD_S[webLandedIdx] : 0;
    if (shDetail == 1) snprintf(r.summary, sizeof(r.summary), "%s", detailSerial);
    else if (webWants(webSumMs)) snprintf(r.summary, sizeof(r.summary), "%s", webSummary);
    static bool webLaunchWas = false;
    bool webLaunch = webWants(webLaunchMs);
    if (webLaunch && !webLaunchWas) launchGen++; // the web tab opened: as the T-Deck's tab
    webLaunchWas = webLaunch;
    r.launches = (onPage && shTab == 2) || webLaunch;
    r.launchGen = launchGen;
    r.launchHorizonS = LAUNCH_H_S[cfg.launchH];
    // The followed sonde standing still on SondeHub (landed, or nobody hears it): no fast list, no predictions.
    static ShFollowWatch fw = {};
    const ShSonde *fs = r.follow[0] ? netFind(r.follow) : nullptr;
    r.followStale = followStale = r.follow[0] && sh_follow_stale(&fw, r.follow, fs, netr->sondesMs != 0, millis(), nowUnix());
    // Near list on screen, or following a sonde the radio does not hear now (its position comes from SondeHub).
    r.sondesFast = (onPage && shTab == 0) || webWants(webNearMs) ||
                   (r.follow[0] && !r.followStale && (!haveLast || millis() - lastOkMs > 60000));
    sondesFast = r.sondesFast;
    netsvc_request(r);
    if (netsvc_result(netr, netSeen)) {
        netSeen = netr->version;
        // A new prediction (other sonde or other landing time) is kept in flash.
        const ShPrediction &p = netr->pred;
        if (p.ok && (strcmp(p.vehicle, savedShPred.vehicle) || p.landTime != savedShPred.landTime ||
                     p.landLat != savedShPred.landLat))
            saveShPred(p);
        applyNet();
    }
}

// ---- SondeHub page: four tabs (Near, Landed, Launches, Search) and a details view ----
// Everything here is read from SondeHub over the internet; nothing is written there.


static bool nearRefOk = false; // false: no own position and no sonde, distances are not shown

// Reference point of the SondeHub lists: my GPS, else my saved position, else the sonde. False: none.
static bool refPoint(double *lat, double *lon)
{
    bool me = myFixOk();
    *lat = me ? nmea.fix.lat : myPosSaved ? myLat : track.lat;
    *lon = me ? nmea.fix.lon : myPosSaved ? myLon : track.lon;
    return me || myPosSaved || track.have;
}

static void nearRef() { nearRefOk = refPoint(&nearRefLat, &nearRefLon); }

// A flight counts as over when its last frame is older than 20 minutes, or it is low and not moving
// up or down any more.
static bool shLanded(const ShSonde &s, long now)
{
    if (now && s.time && now - (long)s.time > 1200) return true;
    return s.alt < 3000 && s.velV > -2.0f && s.velV < 2.0f;
}

// Rows of the current tab, in nearOrder[] (indices into the tab's array), nearest first.
static void nearSort()
{
    nearRef();
    long now = netTimeOk() ? (long)time(nullptr) : 0;
    nearCount = 0;
    if (shTab == 0) {
        for (int i = 0; i < netr->nSondes; i++) nearOrder[nearCount++] = i;
    } else if (shTab == 1) {
        for (int i = 0; i < netr->nLanded; i++)
            if (shLanded(netr->landed[i], now)) nearOrder[nearCount++] = i;
    } else if (shTab == 2) {
        for (int i = 0; i < netr->nLaunch; i++) nearOrder[nearCount++] = i; // already in time order
        return;
    } else {
        return;
    }
    // Each distance once (this runs at every redraw of the page), then an insertion sort on them.
    const ShSonde *arr = shTab == 0 ? netr->sondes : netr->landed;
    static float dist[NET_MAX_LANDED + NET_MAX_LAUNCH];
    for (int k = 0; k < nearCount; k++)
        dist[k] = (float)geo_distance_m(nearRefLat, nearRefLon, arr[nearOrder[k]].lat, arr[nearOrder[k]].lon);
    for (int i = 1; i < nearCount; i++)
        for (int j = i; j > 0 && dist[j] < dist[j - 1]; j--) {
            int t = nearOrder[j];
            nearOrder[j] = nearOrder[j - 1];
            nearOrder[j - 1] = t;
            float u = dist[j];
            dist[j] = dist[j - 1];
            dist[j - 1] = u;
        }
}

static void tuneTo(float mhz, char *m, size_t n, const char *what)
{
    if (mhz >= 400.0f && mhz <= 406.0f && fabs(mhz - cfg.freq) > 0.0005) {
        setFreq(mhz);
        snprintf(m, n, "%s, tuned %.3f", what, mhz);
    } else {
        snprintf(m, n, "%s", what);
    }
}

// A fixed point (predicted landing) becomes the hunting target, like a typed one.
static void targetPoint(double lat, double lon, const char *serial)
{
    manualTrack(lat, lon);
    snprintf(track.id, sizeof(track.id), "%.15s", serial ? serial : "");
    followSerial[0] = 0;
    nTrk = 0, trkGen++;
    saveLastPos();
}

// Follow a sonde of the Near list: SondeHub positions when the radio does not hear it, tuned to its frequency.
static void followSonde(const ShSonde &s)
{
    snprintf(followSerial, sizeof(followSerial), "%s", s.serial);
    track.manual = false;
    uint8_t t = sonde_type_from_sondehub(s.type);
    bool typeChanged = cfg.stype == ST_AUTO && t != ST_AUTO && t != rxType;
    if (typeChanged) rxType = t;
    char m[48], w[32];
    snprintf(w, sizeof(w), "following %s", s.serial);
    auto f0 = cfg.freq;
    tuneTo(s.freqMhz, m, sizeof(m), w);
    if (typeChanged && cfg.freq == f0) applyRadio(); // no retune: the radio must still switch decoder
    applyNet();
    setMsg(m);
}

static void unfollowSonde()
{
    if (!followSerial[0]) return setMsg("no sonde picked to follow");
    setMsgf("not following %s any more", followSerial);
    followSerial[0] = 0;
}

// Predicted landing of a summary (or its last position) as the target.
static void targetSummary(const ShSummary &s)
{
    if (s.havePredLand) targetPoint(s.landLat, s.landLon, s.serial);
    else targetPoint(s.lastLat, s.lastLon, s.serial);
    char m[48], w[40];
    snprintf(w, sizeof(w), "target: %s %s", s.serial, s.havePredLand ? "landing" : "last pos");
    tuneTo(s.freqMhz, m, sizeof(m), w);
    setMsg(m);
}

// Launch with that time in the current list, -1 when it is no longer there.
static int launchIndex(uint32_t t)
{
    for (int i = 0; i < netr->nLaunch; i++)
        if (netr->launch[i].time == t) return i;
    return -1;
}

static void targetLaunch(int i)
{
    if (i < 0 || i >= netr->nLaunch) return setMsg("launch no longer in the list");
    const NetLaunch &l = netr->launch[i];
    if (!l.pred.ok) return setMsg("no prediction for this launch");
    char m[48];
    targetPoint(l.pred.landLat, l.pred.landLon, "LAUNCH");
    tuneTo(netr->sites[l.site].freqMhz, m, sizeof(m), "target: predicted landing");
    setMsg(m);
}

// Click on a row: Near follows the sonde live (again: stops following); Landed and Launches open the details.
static void nearPick(int i)
{
    if (i < 0 || i >= nearCount) return;
    nearFocus = i;
    if (shTab == 0) {
        const ShSonde &s = netr->sondes[nearOrder[i]];
        if (!strcmp(followSerial, s.serial)) unfollowSonde(); // the followed one again: stop following
        else followSonde(s);
    } else if (shTab == 1) {
        snprintf(detailSerial, sizeof(detailSerial), "%s", netr->landed[nearOrder[i]].serial);
        shDetail = 1;
    } else if (shTab == 2) {
        detailLaunchT = netr->launch[nearOrder[i]].time;
        shDetail = 2;
    }
}

static void nearStartSearch()
{
    startEdit(5);
}

static void nearSetTab(int t)
{
    if (t == shTab && t == 1) landedIdx = (landedIdx + 1) % 4; // Landed again: next period
    shTab = t;
    nearFocus = 0;
    shDetail = 0;
    if (t == 2) launchGen++;
    if (t == 3) nearStartSearch();
}

// Click inside the details: make the predicted landing (or the last position) the target.
static void nearDetailAction()
{
    if (shDetail == 1) {
        const ShSummary &s = netr->summary;
        if (!s.ok || strcmp(netr->summaryFor, detailSerial)) return setMsg("no data for this serial yet");
        targetSummary(s);
    } else if (shDetail == 2) {
        targetLaunch(launchIndex(detailLaunchT));
    }
}

// Keys on the SondeHub page; true when used.
static bool nearKey(char k)
{
    if (k >= '1' && k <= '4') {
        nearSetTab(k - '1');
        return true;
    }
    if (k == '\r') {
        if (shDetail) nearDetailAction();
        else if (shTab == 3) nearStartSearch();
        else nearPick(nearFocus);
        return true;
    }
    return false;
}

static void nearInput(const InputEvent &e)
{
    if (e.type == IN_TAP || e.type == IN_LONG) {
        if (tabHit(e.y)) return nearSetTab(e.x / 80);
        if (shDetail) return nearDetailAction();
        if (shTab == 3) return nearStartSearch();
        int top = listTop(nearFocus, NEAR_ROWS);
        if (e.y >= NEAR_Y0 - 1 && e.y < NEAR_Y0 - 1 + NEAR_ROWS * NEAR_ROW_H)
            nearPick(top + (e.y - NEAR_Y0 + 1) / NEAR_ROW_H);
        return;
    }
    if (e.type == IN_LEFT || e.type == IN_RIGHT) {
        if (shDetail) return;
        return nearSetTab((shTab + (e.type == IN_RIGHT ? 1 : 3)) % 4);
    }
    if (e.type == IN_SELECT) {
        if (shDetail) return nearDetailAction();
        if (shTab == 3) return nearStartSearch();
        return nearPick(nearFocus);
    }
    if (shDetail) return;
    nearFocus += e.type == IN_DOWN ? 1 : -1;
    if (nearFocus >= nearCount) nearFocus = nearCount - 1;
    if (nearFocus < 0) nearFocus = 0;
}

// Back on the SondeHub page closes the details first.
static bool nearBack()
{
    if (screen != SCR_NEARBY || !shDetail) return false;
    shDetail = 0;
    return true;
}

// "Wed 18 14:00" in local time.
static void localDayTime(uint32_t t, char *b, size_t n)
{
    static const char *const D[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    uint32_t l = t + localOffsetS(t), s = l % 86400;
    int y, m, d;
    tu_civil_from_days((int32_t)(l / 86400), &y, &m, &d);
    snprintf(b, n, "%s %02d %02lu:%02lu", D[tu_weekday(l)], d, (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
}

static void ageShort(long age, char *b, size_t n)
{
    if (age < 0) snprintf(b, n, "?");
    else if (age < 120) snprintf(b, n, "%lds", age);
    else if (age < 7200) snprintf(b, n, "%ldm", age / 60);
    else if (age < 3 * 86400) snprintf(b, n, "%ldh", age / 3600);
    else snprintf(b, n, "%ldd", age / 86400);
}

static void distDir(double lat, double lon, char *b, size_t n)
{
    if (!nearRefOk) {
        snprintf(b, n, "-");
        return;
    }
    double d = geo_distance_m(nearRefLat, nearRefLon, lat, lon) / 1000.0;
    snprintf(b, n, d < 10 ? "%.1fkm %s" : "%.0fkm %s", d, geo_cardinal(geo_bearing_deg(nearRefLat, nearRefLon, lat, lon)));
}

static void drawNearDetail()
{
    char b[80], dd[24], t1[24];
    int y = 46;
    auto line = [&](uint16_t c, const char *s) {
        char t[80];
        snprintf(t, sizeof(t), "%s", s);
        fitText(t, 2, 296); // long site names stay in the card
        cardText(12, y, 296, 2, c, t);
        y += 18;
    };
    // The hint first: the early returns below (waiting, not found) keep it.
    bottomLine(shDetail == 1 ? "click/Enter: target + tune" : "click/Enter: landing as target", UI_LABEL);
    if (shDetail == 1) {
        const ShSummary &s = netr->summary;
        bool have = !strcmp(netr->summaryFor, detailSerial);
        snprintf(b, sizeof(b), "%s  %s", detailSerial, have && s.type[0] ? s.type : "");
        line(UI_ACCENT, b);
        if (!have) return line(UI_LINE, "asking SondeHub...");
        if (!s.ok) return line(UI_WARN, "not found on SondeHub");
        if (s.haveLaunch) {
            localDayTime(s.launchTime, t1, sizeof(t1));
            snprintf(b, sizeof(b), "launch %s  site %s", t1, s.site[0] ? s.site : "?");
            line(UI_TEXT, b);
        }
        if (s.haveBurst) {
            snprintf(b, sizeof(b), "highest %.0f m", s.burstAlt);
            line(UI_TEXT, b);
        }
        if (s.haveLast) {
            long now = netTimeOk() ? (long)time(nullptr) : 0;
            char ag[8];
            ageShort(now && s.lastTime ? now - (long)s.lastTime : -1, ag, sizeof(ag));
            distDir(s.lastLat, s.lastLon, dd, sizeof(dd));
            snprintf(b, sizeof(b), "last %.0f m %+.1f m/s, %s ago", s.lastAlt, s.lastVelV, ag);
            line(UI_TEXT, b);
            snprintf(b, sizeof(b), "  %.5f %.5f  %s", s.lastLat, s.lastLon, dd);
            line(UI_POS, b);
        }
        if (s.havePredLand) {
            distDir(s.landLat, s.landLon, dd, sizeof(dd));
            snprintf(b, sizeof(b), "landing %.5f %.5f  %s", s.landLat, s.landLon, dd);
            line(UI_PRED, b);
        }
        snprintf(b, sizeof(b), "%s   %.3f MHz", s.recovered ? "RECOVERED (reported)" : "not reported recovered", s.freqMhz);
        line(s.recovered ? UI_WARN : UI_LABEL, b);
    } else {
        int li = launchIndex(detailLaunchT);
        if (li < 0) return; // gone from the list meanwhile
        const NetLaunch &l = netr->launch[li];
        const ShSite &st = netr->sites[l.site];
        line(UI_ACCENT, st.name);
        localDayTime(l.time, t1, sizeof(t1));
        snprintf(b, sizeof(b), "launch %s   %.0f km from you", t1, st.distKm);
        line(UI_TEXT, b);
        snprintf(b, sizeof(b), "burst %.0f m  up %.1f  down %.1f m/s", st.burst, st.ascent, st.descent);
        line(UI_LABEL, b);
        if (st.freqMhz > 0) {
            snprintf(b, sizeof(b), "frequency %.3f MHz", st.freqMhz);
            line(UI_TEXT, b);
        }
        if (!l.tried) return line(UI_LINE, "prediction: waiting...");
        if (!l.pred.ok) return line(UI_WARN, "no prediction (beyond the weather data?)");
        distDir(l.pred.landLat, l.pred.landLon, dd, sizeof(dd));
        snprintf(b, sizeof(b), "landing %.4f %.4f  %s", l.pred.landLat, l.pred.landLon, dd);
        line(UI_PRED, b);
        localDayTime(l.pred.landTime, t1, sizeof(t1));
        snprintf(b, sizeof(b), "at about %s", t1);
        line(UI_TEXT, b);
    }
}

static void drawNearby()
{
    static const char *const TAB[4] = {"Near", "Landed", "Launches", "Search"};
    char b[80];
    drawStatusBar();
    nearSort();
    if (nearFocus >= nearCount) nearFocus = nearCount ? nearCount - 1 : 0;
    // Tabs.
    for (int t = 0; t < 4; t++) {
        if (t == 1) snprintf(b, sizeof(b), "Landed %s", LANDED_PERIOD_NAME[landedIdx]);
        else snprintf(b, sizeof(b), "%s", TAB[t]);
        drawTab(t, b, t == shTab);
    }
    cardFrame(4, 42, 312, 172);
    if (!cfg.net) {
        cardText(12, 60, 296, 2, UI_LINE, "Internet is off: settings, tab Network.");
        cardText(12, 80, 296, 2, UI_LINE, "Use the phone hotspot as Wi-Fi (same tab).");
        return bottomLine("SondeHub is only read, never written", UI_LABEL);
    }
    if (shDetail) return drawNearDetail();
    long now = netTimeOk() ? (long)time(nullptr) : 0;
    if (shTab == 3) {
        cardText(12, 60, 296, 2, UI_TEXT, "Type a serial number, then Enter.");
        cardText(12, 80, 296, 2, UI_LABEL, "Example: X1422184. Click or tap to type.");
        return bottomLine("search by serial", UI_LABEL);
    }
    uint32_t got = shTab == 0 ? netr->sondesMs : shTab == 1 ? (netr->landedLast == LANDED_PERIOD_S[landedIdx] ? netr->landedMs : 0)
                                                          : (netr->nSites || netr->nLaunch ? 1 : 0);
    if (!got) cardText(12, 60, 296, 2, UI_LINE, netTimeOk() ? "asking SondeHub..." : "waiting for internet...");
    else if (!nearCount) cardText(12, 60, 296, 2, UI_LINE, shTab == 2 ? "no launch sites within 400 km"
                                                         : shTab == 0 ? "no sondes in the last 3 h" : "none in this period");
    int top = listTop(nearFocus, NEAR_ROWS);
    const char *fid = followId();
    for (int r = 0; r < NEAR_ROWS && top + r < nearCount; r++) {
        int i = top + r, y = NEAR_Y0 + r * NEAR_ROW_H;
        bool sel = i == nearFocus;
        uint16_t bg = sel ? UI_LINE : UI_PANEL, col = UI_TEXT;
        if (sel) rowHighlight(y, NEAR_ROW_H);
        char dd[24], ag[8];
        // Columns at fixed places: the font is proportional, padding with spaces does not line them up.
        auto col2 = [&](const char *t, int x, int w, bool right) {
            char c[32];
            snprintf(c, sizeof(c), "%s", t);
            fitText(c, 2, w);
            gfx.setTextDatum(right ? TR_DATUM : TL_DATUM);
            gfx.drawString(c, right ? x + w : x, y, 2);
            gfx.setTextDatum(TL_DATUM);
        };
        gfx.setTextPadding(0);
        if (shTab == 2) {
            const NetLaunch &l = netr->launch[nearOrder[i]];
            char when[24];
            localDayTime(l.time, when, sizeof(when));
            if (l.pred.ok) distDir(l.pred.landLat, l.pred.landLon, dd, sizeof(dd));
            else snprintf(dd, sizeof(dd), l.tried ? "no pred." : "...");
            gfx.setTextColor(l.pred.ok ? UI_TEXT : UI_LABEL, bg);
            col2(when, 10, 88, false);
            col2(netr->sites[l.site].name, 102, 110, false);
            col2(dd, 216, 92, true);
        } else {
            const ShSonde &s = (shTab == 0 ? netr->sondes : netr->landed)[nearOrder[i]];
            distDir(s.lat, s.lon, dd, sizeof(dd));
            ageShort(now && s.time ? now - (long)s.time : -1, ag, sizeof(ag));
            col = !strcmp(s.serial, fid) ? UI_GOOD : shTab == 0 && s.velV < -1.5f ? UI_WARN : UI_TEXT;
            gfx.setTextColor(col, bg);
            char a[16], v[12];
            snprintf(a, sizeof(a), "%.0fm", s.alt);
            snprintf(v, sizeof(v), "%+.1f", s.velV);
            col2(s.serial, 10, 74, false);
            col2(dd, 86, 76, false);
            col2(a, 164, 54, true);
            col2(v, 222, 40, true);
            col2(ag, 266, 42, true);
        }
    }
    if (shTab == 0) snprintf(b, sizeof(b), "click: follow / stop   (last 3 h)");
    else if (shTab == 1) snprintf(b, sizeof(b), "click: details   2 again: period");
    else snprintf(b, sizeof(b), "next %s, sites within 400 km", LAUNCH_H_NAME[cfg.launchH]);
    bottomLine(b, UI_LABEL);
}

// ---- Hunting apps: MySondy over Bluetooth, rdz JSON over Wi-Fi ----

#define APP_LIVE_MS 3000 // a frame younger than this counts as "receiving now"

static bool appReceiving()
{
    return haveLast && millis() - lastOkMs < APP_LIVE_MS;
}

static bool appHavePos()
{
    return appReceiving() && track.have && !track.manual && !track.restored && millis() - track.ms < APP_LIVE_MS;
}

// Type name for the hunting apps: they know M10 and M20 apart.
static const char *appTypeName()
{
    if (rxType != ST_M10) return sonde_type_name(rxType);
    return haveLast && last.sondeType == ST_M10 ? last.model : "M20";
}

static void mysondyFill(MySondyData &d)
{
    d = MySondyData();
    d.type = appTypeName();
    d.freqMhz = cfg.freq;
    d.decoding = appReceiving();
    d.ser = d.decoding ? last.id : "";
    d.havePos = appHavePos();
    d.lat = track.lat;
    d.lon = track.lon;
    d.altM = track.alt;
    d.hvelMs = track.vH;
    d.vvelMs = track.vV;
    d.dirDeg = track.vDir;
    d.rssiDbm = d.decoding ? lastRssi : rssiIdle;
    d.batPct = deckBattPct();
    d.batMv = (int)(deckBattV * 1000.0f);
    int kt;
    if (d.decoding && rs41_calib_kill_timer(&calib, &kt)) {
        d.killKnown = true;
        d.killSec = kt;
    }
}

static void rdzFill(RdzSonde &s, RdzGps &g)
{
    s = RdzSonde();
    s.valid = appReceiving();
    s.havePos = appHavePos();
    s.lat = track.lat;
    s.lon = track.lon;
    s.altM = track.alt;
    s.vsMs = track.vV;
    s.hsMs = track.vH;
    s.dirDeg = track.vDir;
    s.type = appTypeName();
    s.ser = s.valid ? last.id : "";
    s.frame = s.valid ? last.frameNb : 0;
    s.time = s.valid ? last.unixTime : 0;
    s.sats = track.sats;
    s.freqMhz = cfg.freq;
    s.rssiDbm = s.valid ? lastRssi : rssiIdle;
    s.battV = s.valid ? last.battV : 0;
    int kt;
    s.burstKT = s.valid && rs41_calib_kill_timer(&calib, &kt) ? kt : -1;
    s.countKT = s.valid && rs41_calib_countdown(&calib, &kt) ? kt : -1;
    g = RdzGps();
    if (myFixOk()) {
        const GpsFix &f = nmea.fix;
        g.valid = true;
        g.lat = f.lat;
        g.lon = f.lon;
        g.altM = (int)f.altM;
        g.sats = f.sats;
        g.dirDeg = (int)f.courseDeg;
        g.hdop = (int)f.hdop;
        g.speedKmh = f.speedKmh;
    }
}

static MySondyParser appParser;
static uint32_t appLastLineMs = 0;
static bool appQueryPending = false;
static bool appWasLinked = false;

// Commands from an app: frequency and bandwidth are applied like keys on the T-Deck; anything that
// would make sense only on a TTGO board (pins, display, buzzer, reboot, sleep) is ignored.
static void appCommand(const MySondyCmd &c)
{
    if (c.hasFreq) {
        if (c.freqMhz >= 400.0 && c.freqMhz <= 406.0) {
            setFreq(c.freqMhz, true);
        } else {
            setMsg("app: frequency out of 400-406 MHz");
        }
    }
    uint8_t want = !c.hasType ? ST_AUTO : c.type == 1 ? ST_RS41 : c.type == 5 ? ST_DFM : c.type == 2 || c.type == 3 ? ST_M10 : 0xFF;
    if (want == 0xFF) setMsg("app asked for a sonde type not decoded here");
    else if (want != ST_AUTO && cfg.stype != want) { // MySondy: 1 RS41, 2 M20, 3 M10, 5 DFM
        cfg.stype = want;
        applyRadio();
        saveSettings();
    }
    // A bandwidth from the app is only logged: its values are for the SX1278 of the TTGO boards, and taken
    // as they are they once left this receiver at 19.5 kHz (weak RS41 lost). Settings, Radio tab, instead.
    if (c.query) appQueryPending = true;
    char b[80];
    snprintf(b, sizeof(b), "# app cmd f=%d:%.3f type=%d:%d bw=%d:%d q=%d", c.hasFreq, c.freqMhz, c.hasType, c.type,
             c.hasBw, c.bwIndex, c.query);
    logRemember(b);
}

static void appsPoll()
{
    bool bleWanted = cfg.apps >= 1;
    if (bleWanted && !ble_running()) ble_start();
    if (!bleWanted && ble_running()) ble_stop();
    rdz_link_poll(cfg.apps == 2 && WiFi.status() == WL_CONNECTED);
    if (!cfg.apps) return;

    for (int c = ble_read(); c >= 0; c = ble_read())
        if (appParser.feed((char)c)) appCommand(appParser.cmd());
    ble_poll();

    BleStatus bs;
    ble_status(&bs);
    bool linked = bs.subscribed || rdz_link_client();
    if (linked && !appWasLinked) setMsg("app linked: keep SondeHub off in the app");
    appWasLinked = linked;

    if (millis() - appLastLineMs < 1000) return;
    appLastLineMs = millis();
    char b[300];
    if (appQueryPending) {
        appQueryPending = false;
        mysondy_settings(b, sizeof(b), appTypeName(), cfg.freq, mysondy_bw_index((uint32_t)(Sx1262::BW_KHZ[cfg.bwIdx] * 1000)),
                         cfg.call);
    } else {
        MySondyData d;
        mysondyFill(d);
        mysondy_line(b, sizeof(b), d);
    }
    ble_send_line(b);
    if (rdz_link_client()) {
        RdzSonde s;
        RdzGps g;
        rdzFill(s, g);
        static char j[1024];
        int n = rdz_tcp_line(j, sizeof(j), s, g);
        if (n > 0) rdz_link_send(j, n);
    }
}

// ---- Wi-Fi debug server: same functions as the USB console, over HTTP ----
// GET /status  /shot.bmp  /log  /key?k=<chars>  /key?name=enter|bksp|esc

static WebServer web(80);
static bool webRunning = false;

static void webStatus()
{
    StreamString t;
    statusText(t);
    web.send(200, "text/plain", t);
}

// The ring copied once into a buffer in PSRAM and sent with its length (no String growing line by line).
static void webLog()
{
    static char *t = nullptr;
    if (!t) t = (char *)ps_malloc(LOG_RING_LINES * LOG_RING_LEN + 1);
    if (!t || !logRing) return web.send(200, "text/plain", "");
    size_t n = 0;
    for (int i = 0; i < logRingCount; i++) {
        const char *line = logRing + ((logRingNext - logRingCount + i + LOG_RING_LINES) % LOG_RING_LINES) * LOG_RING_LEN;
        size_t l = strnlen(line, LOG_RING_LEN - 1);
        memcpy(t + n, line, l);
        n += l;
        t[n++] = '\n';
    }
    t[n] = 0;
    web.send_P(200, "text/plain", t, n);
}

// 16-bit BMP (RGB565 bitfields), rows bottom-up, straight from the canvas.
// (The T-Deck panel cannot be read back: its data-out line is not wired, reads return white.)
// Answers longer than a few KB go out in 2 KB pieces with the watchdog fed between them: on a slow Wi-Fi a
// single write may wait up to 10 s per stalled retry, and a whole page or screenshot used to pass the 15 s
// loop watchdog (restart "task-wdt during: web", 24/11/2026). A client that went away ends the answer.
// WebServer::sendContent does not look at how much the socket took: on a busy Wi-Fi a write gave up after
// its retries and the rest of the piece was lost, so /shot.bmp sometimes arrived cut (2 times in 11,
// 25/11/2026). Here every byte is written or the connection is closed (the browser sees an error, not a
// short file).
static bool webWriteAll(const uint8_t *data, size_t len)
{
    WiFiClient c = web.client();
    uint32_t t0 = millis();
    while (len) {
        esp_task_wdt_reset();
        if (!c.connected()) return false;
        size_t k = c.write(data, len);
        data += k;
        len -= k;
        if (!k && millis() - t0 > 12000) {
            logRemember("# web: write stalled, answer dropped");
            c.stop();
            return false;
        }
    }
    esp_task_wdt_reset();
    return true;
}

// One piece of an answer with a known length, or one chunk (chunked = true, CONTENT_LENGTH_UNKNOWN; the
// last empty chunk still goes with web.sendContent("")).
static bool webPiece(const char *data, size_t len, bool chunked = false)
{
    if (!len) return true;
    if (chunked) {
        char h[12];
        int n = snprintf(h, sizeof(h), "%x\r\n", (unsigned)len);
        if (!webWriteAll((const uint8_t *)h, n)) return false;
    }
    if (!webWriteAll((const uint8_t *)data, len)) return false;
    return !chunked || webWriteAll((const uint8_t *)"\r\n", 2);
}

static void webSendBig(const char *type, const char *data, size_t len)
{
    web.setContentLength(len);
    web.send(200, type, "");
    for (size_t off = 0; off < len; off += 2048)
        if (!webPiece(data + off, min((size_t)2048, len - off))) return;
}

static void webShot()
{
    const int w = gfx.width(), h = gfx.height();
    const uint32_t rowBytes = w * 2, dataOfs = 14 + 40 + 12, fileSize = dataOfs + rowBytes * h;
    uint8_t hdr[66] = {0};
    auto le16 = [&](int o, uint16_t v) { hdr[o] = v & 0xFF; hdr[o + 1] = v >> 8; };
    auto le32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; i++) hdr[o + i] = (v >> (8 * i)) & 0xFF; };
    hdr[0] = 'B';
    hdr[1] = 'M';
    le32(2, fileSize);
    le32(10, dataOfs);
    le32(14, 40);
    le32(18, w);
    le32(22, h);
    le16(26, 1);
    le16(28, 16);
    le32(30, 3); // BI_BITFIELDS
    le32(34, rowBytes * h);
    le32(54, 0xF800);
    le32(58, 0x07E0);
    le32(62, 0x001F);
    web.setContentLength(fileSize);
    web.send(200, "image/bmp", "");
    if (!webPiece((const char *)hdr, sizeof(hdr))) return;
    static uint8_t line[320 * 2];
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint16_t v = gfx.readPixel(x, y);
            line[2 * x] = v & 0xFF;
            line[2 * x + 1] = v >> 8;
        }
        if (!webPiece((const char *)line, rowBytes)) return;
    }
}

static void conKey(char k);

// Trackball and touch from the PC: /key?name=up|down|left|right|select, /touch?x=&y=[&long=1]
static bool webInputName(const String &n)
{
    static const char *const N[5] = {"up", "down", "left", "right", "select"};
    static const InputType T[5] = {IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_SELECT};
    for (int i = 0; i < 5; i++) {
        if (n == N[i]) {
            input_inject({T[i], 0, 0});
            inputNow();
            return true;
        }
    }
    return false;
}

static void webTouch()
{
    if (!web.hasArg("x") || !web.hasArg("y")) return web.send(400, "text/plain", "use /touch?x=..&y=..[&long=1]\n");
    InputEvent e = {web.hasArg("long") ? IN_LONG : IN_TAP, (int16_t)web.arg("x").toInt(), (int16_t)web.arg("y").toInt()};
    input_inject(e);
    inputNow();
    web.send(200, "text/plain", "OK\n");
}

static void webKey()
{
    if (web.hasArg("name") && webInputName(web.arg("name"))) return web.send(200, "text/plain", "OK\n");
    if (web.hasArg("name") || web.hasArg("code")) {
        int c = keyByName((web.hasArg("name") ? web.arg("name") : web.arg("code")).c_str());
        if (c <= 0) return web.send(400, "text/plain", "name: enter, bksp, esc, space, tab; code: 1-255 or 0x01-0xff\n");
        conKey((char)c);
    } else if (web.hasArg("k")) {
        String k = web.arg("k");
        for (unsigned i = 0; i < k.length(); i++) conKey(k[i]);
    } else {
        return web.send(400, "text/plain", "use /key?k=<chars> or /key?name=enter\n");
    }
    web.send(200, "text/plain", "OK\n");
}

// Who may open a path: the Web interface setting (/ and /api), the Debug server setting, or either (SD files);
// always with the password. Requests that change something (GATE_ACT) also need the X-TDeck header: the
// T-Deck's pages and tools/tdeck_link.py send it, while a browser lets another site add it only after asking
// the T-Deck (CORS preflight, never answered), so links and forms of other pages cannot act with the stored
// password.
enum { GATE_WEB = 1, GATE_DEBUG = 2, GATE_FILES = 3, GATE_ACT = 4 };
static const char GATE_PASS[] = "password needed";

static const char *gateDenied(uint8_t need)
{
    if (!((need & GATE_WEB) && cfg.webUi) && !((need & GATE_DEBUG) && cfg.dbgServer))
        return (need & GATE_FILES) == GATE_DEBUG ? "debug server off" : "web interface off (settings, System tab)";
    if (web.header("Sec-Fetch-Site") == "cross-site") return "requests from other sites refused";
    if (!web.authenticate(DEBUG_USER, cfg.dbgPass)) return GATE_PASS;
    if ((need & GATE_ACT) && !web.hasHeader("X-TDeck")) return "actions only from the T-Deck page";
    return nullptr;
}

// SD files for the debug page: only absolute paths, no "..", never the map tiles (thousands of files).
static bool sdPathOk(const String &p)
{
    String l = p; // the card ignores case: /MAPS is /maps
    l.toLowerCase();
    return p.length() && p[0] == '/' && p.indexOf("..") < 0 && p.indexOf("//") < 0 && !l.startsWith("/maps") &&
           p.length() < 96;
}

// A name typed in the browser for a file on the card: no folders, no hidden files.
static bool sdNameOk(const String &n)
{
    if (!n.length() || n.length() > 64 || n[0] == '.') return false;
    for (char c : n) // no folders, no characters the card or a download header cannot take
        if ((uint8_t)c < 0x20 || strchr("/\\\":*<>?|", c)) return false;
    return true;
}

// /files?dir=/sonde -> one line per entry: name, tab, size in bytes ("-" for a folder).
static void webFiles()
{
    String dir = web.hasArg("dir") ? web.arg("dir") : "/";
    if (!sdOk) return web.send(503, "text/plain", "no SD card\n");
    if (dir != "/" && !sdPathOk(dir)) return web.send(400, "text/plain", "bad folder\n");
    File d = SD.open(dir);
    if (!d || !d.isDirectory()) return web.send(404, "text/plain", "not a folder\n");
    // Sent in chunks of about 1 KB as the folder is read: no growing String for a big folder.
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200, "text/plain", "");
    static char buf[1024];
    size_t k = 0;
    int n = 0;
    File f = d.openNextFile();
    for (; f && n < 300; f = d.openNextFile(), n++) {
        const char *nm = strrchr(f.name(), '/');
        char line[100];
        int w = f.isDirectory() ? snprintf(line, sizeof(line), "%s\t-\n", nm ? nm + 1 : f.name())
                                : snprintf(line, sizeof(line), "%s\t%lu\n", nm ? nm + 1 : f.name(), (unsigned long)f.size());
        f.close();
        if (w <= 0 || w >= (int)sizeof(line)) continue; // a name too long for the page: skipped
        if (k + w > sizeof(buf)) {
            if (!webPiece(buf, k, true)) break;
            k = 0;
        }
        memcpy(buf + k, line, w);
        k += w;
    }
    if (f) { // more than 300 entries: the page says so
        f.close();
        const char *more = "\tmore\n";
        if (k + strlen(more) > sizeof(buf)) {
            webPiece(buf, k, true);
            k = 0;
        }
        memcpy(buf + k, more, strlen(more));
        k += strlen(more);
    }
    d.close();
    if (k) webPiece(buf, k, true);
    web.sendContent(""); // end of the chunks
}

// /upload?dir=/folder (POST, multipart): files from the browser onto the SD card, for example a firmware
// to install from the Launcher. Written as <name>.part through a 16 KB buffer and renamed at the end, so a
// broken upload never leaves a half file. Runs in the main loop like /dl: no frames are received meanwhile.
static File upFile;
static String upPath, upErr;
static uint8_t *upBuf = nullptr;
static size_t upN = 0;
#define UP_BUF 16384

static bool upFlush()
{
    bool ok = !upN || upFile.write(upBuf, upN) == upN;
    upN = 0;
    if (!ok) upErr = "SD card full or write error";
    return ok;
}

static void webUploadData()
{
    HTTPUpload &u = web.upload();
    esp_task_wdt_reset();
    if (u.status == UPLOAD_FILE_START) {
        if (upFile) { // a previous upload that broke without an end: drop its half file
            upFile.close();
            SD.remove(upPath + ".part");
        }
        upErr = "";
        upPath = "";
        upN = 0;
        if (const char *e = gateDenied(GATE_FILES | GATE_ACT)) return (void)(upErr = e);
        if (!sdOk) return (void)(upErr = "no SD card");
        String dir = web.hasArg("dir") ? web.arg("dir") : "/";
        String name = u.filename;
        int cut = max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
        if (cut >= 0) name = name.substring(cut + 1);
        if (!sdNameOk(name)) return (void)(upErr = "bad file name");
        String path = (dir == "/" ? String("") : dir) + "/" + name;
        if ((dir != "/" && !sdPathOk(dir)) || !sdPathOk(path)) return (void)(upErr = "this folder cannot be written");
        if (!upBuf) upBuf = (uint8_t *)ps_malloc(UP_BUF);
        if (!upBuf) return (void)(upErr = "no memory");
        SD.remove(path + ".part");
        upFile = SD.open(path + ".part", FILE_WRITE);
        if (!upFile) return (void)(upErr = "cannot create the file (does the folder exist?)");
        upPath = path;
        crumb("web upload");
    } else if (u.status == UPLOAD_FILE_WRITE) {
        if (!upFile || upErr.length()) return;
        const uint8_t *b = u.buf;
        size_t n = u.currentSize;
        while (n) {
            size_t k = min(n, (size_t)UP_BUF - upN);
            memcpy(upBuf + upN, b, k);
            upN += k;
            b += k;
            n -= k;
            if (upN == UP_BUF && !upFlush()) break;
        }
    } else if (u.status == UPLOAD_FILE_END) {
        if (!upFile) return;
        bool ok = !upErr.length() && upFlush();
        upFile.close();
        if (ok) {
            if (SD.exists(upPath)) SD.remove(upPath);
            ok = SD.rename(upPath + ".part", upPath);
            if (!ok) upErr = "rename failed";
        }
        if (!ok) SD.remove(upPath + ".part");
        else logLine(logPath, ("# web upload " + upPath + " " + String((unsigned long)u.totalSize)).c_str());
    } else if (u.status == UPLOAD_FILE_ABORTED) {
        if (upFile) {
            upFile.close();
            SD.remove(upPath + ".part");
        }
        upErr = "upload interrupted";
    }
}

static void webUploadDone()
{
    if (upFile) { // the upload broke before its end
        upFile.close();
        SD.remove(upPath + ".part");
        if (!upErr.length()) upErr = "upload interrupted";
    }
    if (!upErr.length() && !upPath.length()) upErr = "no file received";
    if (upErr.length()) {
        web.send(upErr == GATE_PASS ? 401 : 400, "text/plain", upErr + "\n");
    } else {
        const char *base = strrchr(upPath.c_str(), '/') + 1;
        setMsgf("file saved: %.30s", base);
        web.send(200, "text/plain", "OK\n");
    }
    upPath = "";
    upErr = "";
}

// /rename?f=/folder/old&to=new (POST): a file or folder renamed inside its own folder.
static void webRename()
{
    String f = web.arg("f"), to = web.arg("to");
    to.trim();
    if (!sdOk) return web.send(503, "text/plain", "no SD card\n");
    if (!sdPathOk(f) || f == "/") return web.send(400, "text/plain", "this file cannot be renamed\n");
    if (!sdNameOk(to)) return web.send(400, "text/plain", "bad name\n");
    String dst = f.substring(0, f.lastIndexOf('/') + 1) + to;
    if (!sdPathOk(dst)) return web.send(400, "text/plain", "bad name\n");
    if (!SD.exists(f)) return web.send(404, "text/plain", "not found\n");
    if (SD.exists(dst)) return web.send(409, "text/plain", "a file with that name already exists\n");
    if (!SD.rename(f, dst)) return web.send(500, "text/plain", "rename failed\n");
    logLine(logPath, ("# web rename " + f + " -> " + dst).c_str());
    web.send(200, "text/plain", "OK\n");
}

// /delete?f=/folder/name (POST): a file, or an empty folder. The page asks first.
static void webDelete()
{
    String f = web.arg("f");
    if (!sdOk) return web.send(503, "text/plain", "no SD card\n");
    if (!sdPathOk(f) || f == "/") return web.send(400, "text/plain", "this file cannot be deleted\n");
    File h = SD.open(f);
    if (!h) return web.send(404, "text/plain", "not found\n");
    bool dir = h.isDirectory();
    h.close();
    if (!(dir ? SD.rmdir(f) : SD.remove(f)))
        return web.send(409, "text/plain", dir ? "the folder is not empty\n" : "delete failed\n");
    logLine(logPath, ("# web delete " + f).c_str());
    web.send(200, "text/plain", "OK\n");
}

// /dl?f=/path -> the file as a download. Sent in 4 KB blocks from the main loop: no frames are
// received meanwhile (the page says so).
static void webDownload()
{
    String p = web.hasArg("f") ? web.arg("f") : "";
    if (!sdOk) return web.send(503, "text/plain", "no SD card\n");
    if (!sdPathOk(p)) return web.send(400, "text/plain", "bad path\n");
    File f = SD.open(p, FILE_READ);
    if (!f || f.isDirectory()) return web.send(404, "text/plain", "not found\n");
    crumb("web download");
    const char *base = strrchr(p.c_str(), '/') + 1;
    web.sendHeader("Content-Disposition", String("attachment; filename=\"") + base + "\"");
    web.setContentLength(f.size());
    web.send(200, "application/octet-stream", "");
    static uint8_t buf[4096];
    WiFiClient cl = web.client();
    // write() takes what the send buffer has room for: keep going with the rest (a full buffer used to end
    // big downloads after a few KB), give up only after 10 s without progress.
    bool ok = true;
    while (ok && cl.connected()) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        uint32_t last = millis();
        for (int off = 0; off < n && ok;) {
            size_t w = cl.write(buf + off, n - off);
            if (w) {
                off += (int)w;
                last = millis();
            } else if (!cl.connected() || millis() - last > 10000) {
                ok = false;
            } else {
                delay(2);
            }
            esp_task_wdt_reset();
        }
    }
    f.close();
}

// File check from the PC: /sdhash?path=/maps/12/2214/1588.png -> size and FNV-1a hash (same as sdChecksum),
// to compare a tile on the card with the copy on the PC.
static uint32_t sdChecksum(const char *path, size_t *len);

static void webSdHash()
{
    if (!web.hasArg("path")) return web.send(400, "text/plain", "use /sdhash?path=/maps/z/x/y.png\n");
    String path = web.arg("path");
    if (!sdOk) return web.send(503, "text/plain", "no SD card\n");
    if (!path.startsWith("/") || path.indexOf("..") >= 0) return web.send(400, "text/plain", "bad path\n");
    if (!SD.exists(path)) return web.send(404, "text/plain", "not found\n");
    uint32_t t0 = millis();
    size_t len = 0;
    uint32_t h = sdChecksum(path.c_str(), &len);
    char b[96];
    snprintf(b, sizeof(b), "%s size=%u fnv=%08lx ms=%lu sdhz=%lu\n", path.c_str(), (unsigned)len, (unsigned long)h,
             (unsigned long)(millis() - t0), (unsigned long)sdHz);
    web.send(200, "text/plain", b);
}

// rdz_ttgo_sonde compatible /live.json for apps on the same Wi-Fi (served also without the debug server).
static void rdzFill(RdzSonde &s, RdzGps &g);

static void webLiveJson()
{
    if (cfg.apps < 2) return web.send(403, "text/plain", "hunting apps over Wi-Fi are off\n"); // it has my position
    RdzSonde s;
    RdzGps g;
    rdzFill(s, g);
    static char b[1024];
    int n = rdz_live_json(b, sizeof(b), s, g);
    web.send(200, "text/json", n > 0 ? b : "{}");
}

// Debug page: live screen plus the whole T-Deck keyboard (the text box sends any characters), /status and the log.
// Files and restart are in the web interface at /.
static const char WEB_ROOT[] = R"HTML(<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>
<title>T-Deck SondeTracker debug</title><style>
:root{--bg:#180c08;--panel:#312010;--line:#5a4431;--label:#a48d7b;--text:#fff;--dim:#e6d2cd;--accent:#ffca39}
body{font:14px system-ui,sans-serif;background:var(--bg);color:var(--text);margin:8px}h3{margin:4px 0 8px;font-weight:normal}
button{min-width:34px;height:32px;margin:2px;background:var(--line);color:var(--text);border:0;border-radius:6px;font-size:14px;cursor:pointer}
nav button{min-width:96px;background:var(--panel);color:var(--label)}nav button.on{background:var(--line);color:var(--text)}button.w{min-width:64px}
img{width:640px;max-width:100%;image-rendering:pixelated;border:2px solid var(--panel);border-radius:6px;cursor:crosshair;display:block}
.row{display:flex;flex-wrap:wrap;gap:12px;align-items:flex-start}.box{background:var(--panel);border-radius:9px;padding:6px 10px}
.box td{padding:1px 8px 1px 0}.box h4{margin:6px 0 4px;color:var(--label);font-weight:normal;text-transform:uppercase;font-size:12px;letter-spacing:.08em}.h{color:var(--dim);font-size:13px}
pre{background:var(--panel);border-radius:9px;padding:8px;white-space:pre-wrap;font:12px monospace;max-height:70vh;overflow:auto}
section{display:none}section.on{display:block}input.t{height:28px;width:220px;background:var(--bg);color:var(--text);border:1px solid var(--line);border-radius:6px}
label input{vertical-align:middle}a{color:var(--accent)}td{padding:2px 14px 2px 0}</style>
<h3>T-Deck SondeTracker v)HTML" FW_NAME R"HTML( <span class=h>debug</span> <a class=h href='/'>web interface</a></h3>
<nav><button id=bs onclick="go('s')">Screen</button><button id=bt onclick="go('t')">Status</button><button id=bl onclick="go('l')">Log</button></nav>
<section id=s><div class=row><div><img id=im>
<p class=h>One click = one touch (shift+click = long press). The image updates about a second later: wait for it before the next click.</p>
<p><input class=t id=tx placeholder='text, Enter sends'> <label><input type=checkbox id=au> auto refresh</label> <span id=ld class=h></span></p></div>
<div class=box><h4>Trackball and keys</h4><div id=nv></div><h4>Function keys</h4><table id=fk></table></div></div></section>
<section id=t><button class=w onclick="txt('t','/status')">refresh</button><pre id=tp></pre></section>
<section id=l><button class=w onclick="txt('l','/log')">refresh</button><pre id=lp></pre></section>
<script>
const $=i=>document.getElementById(i),im=$('im');let cur='',busy=0,again=0;
fetch('/api/theme').then(r=>r.json()).then(t=>{for(const k in t)document.documentElement.style.setProperty('--'+k,t[k])}).catch(()=>{});
function go(s){cur=s;for(const x of 'stl'){$(x).className=x==s?'on':'';$('b'+x).className=x==s?'on':''}
if(s=='s')shot();if(s=='t')txt('t','/status');if(s=='l')txt('l','/log')}
function shot(){if(busy){again=1;return}busy=1;$('ld').textContent='updating...';im.src='/shot.bmp?'+Date.now()}
im.onload=im.onerror=()=>{busy=0;$('ld').textContent='';if(again){again=0;shot()}else if(cur=='s'&&$('au').checked)setTimeout(shot,1500)};
$('au').onchange=()=>{if($('au').checked)shot()};
function txt(s,u){fetch(u).then(r=>r.text()).then(t=>$(s+'p').textContent=t)}
const AC={method:'POST',headers:{'X-TDeck':'1'}};
function send(q){fetch('/key?'+q,AC).then(shot)}
function btn(l,q,w){const b=document.createElement('button');b.textContent=l;if(w)b.className='w';b.onclick=()=>send(q);return b}
function line(a,w){const d=document.createElement('div');a.forEach(([l,q])=>d.append(btn(l,q,w)));$('nv').append(d)}
line([['back','name=bksp'],['↑','name=up'],['home','k=h']],1);line([['←','name=left'],['click','name=select'],['→','name=right']],1);
line([['enter','name=enter'],['↓','name=down'],['esc','name=esc']],1);line([...'12345678'].map(c=>[c,'k='+c]));
const F=[['v','hunting pages'],['m','map'],['x','scan start / stop'],['f','type a frequency'],['j','presets list'],['p','next preset'],
['a','save as preset'],['y','sonde type'],['t','manual target'],['n','compass: sonde / landing'],['i','map zoom in'],['o','map zoom out'],
['c','map centre'],['g','map QR code'],['k','KML export'],['s','sound on / off'],['u','polarity'],['r','reset statistics']];
for(let i=0;i<F.length;i+=2){const tr=$('fk').insertRow();for(const [c,d] of F.slice(i,i+2)){tr.insertCell().append(btn(c,'k='+c));tr.insertCell().textContent=d}}
im.onclick=e=>{const r=im.getBoundingClientRect(),x=Math.round((e.clientX-r.left)*320/r.width),y=Math.round((e.clientY-r.top)*240/r.height);fetch('/touch?x='+x+'&y='+y+(e.shiftKey?'&long=1':''),AC).then(shot)};
$('tx').onkeydown=e=>{if(e.key=='Enter'&&$('tx').value){send('k='+encodeURIComponent($('tx').value));$('tx').value=''}};
go('s');
</script>)HTML";

// Web interface at /: Hunt, Map (Leaflet + OpenStreetMap in the browser), Radio, SondeHub, Signal, GPS, Info,
// Settings, Files.
static const char WEB_UI[] = R"HTML(<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1,viewport-fit=cover'>
<title>T-Deck SondeTracker v)HTML" FW_NAME R"HTML(</title><style>
:root{--bg:#180c08;--panel:#312010;--line:#5a4431;--label:#a48d7b;--text:#fff;--dim:#e6d2cd;--accent:#ffca39;--good:#6ad239;--warn:#ff9e42;--bad:#ff595a;--pos:#4adaff;--pred:#e669e6;--msg:#0ff;--gap:clamp(4px,1vmin,10px)}
*{box-sizing:border-box}[hidden]{display:none!important}
html{font-size:clamp(13px,calc(1.1vmin + 6px),26px)}
html,body{margin:0;height:100%;overflow:hidden;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;line-height:1.3}
body{display:flex;flex-direction:column;height:100vh;height:100dvh}a{color:var(--accent)}
header{flex:none;display:flex;align-items:center;gap:.6em;padding:.4em .8em;font-size:1.2rem;font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden}
#led{width:.65em;height:.65em;border-radius:50%;background:var(--line);flex:none}#led.on{background:var(--good)}
.acc{color:var(--accent)}.sp{flex:1}.fl{font-size:.85em;letter-spacing:.04em}
.bat{width:1.8em;height:.9em;border:2px solid var(--text);border-radius:3px;padding:1px;position:relative;flex:none;margin-right:4px}
.bat:after{content:'';position:absolute;right:-5px;top:25%;width:3px;height:50%;background:var(--text)}.bat i{display:block;height:100%;background:var(--good)}
nav,.tabs{flex:none;display:flex;gap:var(--gap)}nav{padding:0 var(--gap)}
nav button,.tabs button{flex:1;min-width:0;background:var(--panel);color:var(--label);border-radius:.6em .6em 0 0;padding:.5em .2em;font-size:1.05rem}
nav button.sel,.tabs button.sel{background:var(--line);color:var(--text)}
main{flex:1;min-height:0;padding:var(--gap);border-top:3px solid var(--line)}
section{height:100%;display:flex;flex-direction:column;gap:var(--gap);min-height:0}
.cd{background:var(--panel);border-radius:.7em;padding:.4em .7em;min-width:0;min-height:0;overflow:hidden}
small{display:flex;gap:.6em;justify-content:space-between;align-items:center;color:var(--label);font-size:.78rem;letter-spacing:.08em;text-transform:uppercase;white-space:nowrap}
small span{text-transform:none;letter-spacing:0}
.hg{flex:1;min-height:0;display:grid;gap:var(--gap);grid-template-columns:repeat(12,minmax(0,1fr));grid-template-rows:1.7fr 1fr 1fr;
grid-template-areas:"d d d d d d d d c c c c" "a a a a v v v v h h h h" "s s s s s s g g g m m m"}
@media(max-aspect-ratio:1/1){.hg{grid-template-columns:repeat(6,minmax(0,1fr));grid-template-rows:1.5fr 1fr 1.1fr 1fr;grid-template-areas:"d d d d c c" "a a v v h h" "s s s s s s" "g g g m m m"}}
.q{container-type:size;display:flex;flex-direction:column}.q>div{flex:1;min-height:0;display:flex;flex-direction:column;justify-content:center}
.big{font-size:min(46cqh,23cqw);line-height:1;white-space:nowrap;font-variant-numeric:tabular-nums}
.mid{font-size:min(24cqh,13cqw);white-space:nowrap;font-variant-numeric:tabular-nums}
.num{font-size:min(46cqh,26cqw);line-height:1.05;white-space:nowrap;font-variant-numeric:tabular-nums}
u{text-decoration:none;color:var(--dim);font-size:.42em;margin-left:.3em}
.sm{color:var(--dim);font-size:min(15cqh,5.5cqw,1rem);font-family:ui-monospace,Consolas,monospace;white-space:nowrap}
.pos{color:var(--pos);font-size:min(28cqh,8cqw);white-space:nowrap;font-variant-numeric:tabular-nums}
.comp svg{width:100%;height:100%}
.sig{flex-direction:row!important;align-items:center;justify-content:flex-start!important;gap:3%}.sig i{width:4%;background:var(--line);border-radius:1px;align-self:center}
.sig i.on{background:var(--good)}.sig b{font-weight:400;font-size:min(40cqh,20cqw);margin-left:4%}
.warn{color:var(--warn)}.good{color:var(--good)}.bad{color:var(--bad)}.pred{color:var(--pred)}
.land{flex:none;display:flex;align-items:center;gap:.6em;flex-wrap:wrap;padding:.5em .7em}.land .pred{font-size:1.25rem;white-space:nowrap}
.btns{display:flex;gap:.4em;flex-wrap:wrap;align-items:center}
button,.btn{background:var(--line);color:var(--text);border:0;border-radius:.5em;padding:.45em .9em;font:inherit;font-size:1rem;cursor:pointer;text-decoration:none;display:inline-block;white-space:nowrap}
button:hover,.btn:hover{filter:brightness(1.2)}.pri{background:var(--accent);color:var(--bg)}
input{background:var(--bg);color:var(--text);border:1px solid var(--line);border-radius:.5em;padding:.45em;font:inherit;font-size:1rem;min-width:0}
input:focus{outline:2px solid var(--accent);border-color:transparent}
.seg button.sel{background:var(--accent);color:var(--bg)}
.grow{flex:1;min-height:0;display:flex;flex-direction:column;gap:.4em}.scroll{flex:1;min-height:0;overflow:auto}
.li{display:flex;gap:.7em;align-items:center;padding:.3em .6em;border-radius:.45em;cursor:pointer;color:var(--text);text-decoration:none;font-size:1.1rem;min-width:0}
.li span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.li:hover,.li.sel{background:var(--line)}.li.ro{cursor:default}.li.ro:hover{background:none}
.li b{color:var(--accent);font-weight:400;min-width:1.3em}.li em{font-style:normal;white-space:nowrap}.r{margin-left:auto;text-align:right}
.gh{color:var(--label);font-size:.78rem;letter-spacing:.08em;text-transform:uppercase;padding:.6em .6em .1em}
.ib{padding:.3em;line-height:0;background:none;flex:none}.ib:hover{background:var(--panel);filter:none}
.ib svg{width:1.35em;height:1.35em;fill:none;stroke:var(--text);stroke-width:2;stroke-linecap:round;stroke-linejoin:round}.ib[data-a=del] svg{stroke:var(--bad)}
.k-on{color:var(--accent)}.k-good{color:var(--good)}.k-msg{color:var(--msg)}.k-text{color:var(--text)}.k-warn{color:var(--warn)}.k-na{color:var(--label);font-style:italic}.k-dim{color:var(--dim)}.k-label{color:var(--label)}
#sbox{flex:1;min-height:0;border-radius:0 0 .7em .7em;container-type:size;padding:.3em .5em;overflow:auto}
#sr{display:grid;grid-auto-flow:column;column-gap:1em;grid-template-columns:repeat(var(--c,1),minmax(0,1fr));
grid-template-rows:repeat(var(--n,4),clamp(28px,calc(100cqh / var(--n,4)),3.4rem))}
#sr .li{font-size:clamp(13px,calc(45cqh / var(--n,4)),1.5rem)}
.fq{flex:none;display:flex;align-items:center;gap:.8em;flex-wrap:wrap}.fq .v{font-size:2.6rem;font-variant-numeric:tabular-nums}
.row2{flex:none;display:flex;gap:var(--gap);flex-wrap:wrap}.row2>.cd{flex:1 1 14em}
#map{flex:1;min-height:0;border-radius:.7em;background:var(--panel)}
.drop{border:2px dashed var(--line);border-radius:.7em;padding:.5em;text-align:center;color:var(--dim);font-size:.9rem}.drop.over{border-color:var(--accent);color:var(--text)}
.prog{flex:none;height:1.6em;background:var(--bg);border-radius:.4em;position:relative;overflow:hidden}.prog i{position:absolute;left:0;top:0;bottom:0;background:var(--good);opacity:.55}
.prog span{position:relative;padding:0 .6em;line-height:1.6em;font-size:.9rem}
footer{flex:none;display:flex;gap:.9em;align-items:center;padding:.45em .9em calc(.45em + env(safe-area-inset-bottom));border-top:2px solid var(--panel);font-size:1rem}
#clk{color:var(--dim);font-variant-numeric:tabular-nums}#bm{color:var(--msg);flex:1;overflow:hidden;white-space:nowrap;text-overflow:ellipsis}
.ov{position:fixed;inset:0;background:#000a;display:flex;align-items:center;justify-content:center;padding:1em;z-index:2000}
.pop{background:var(--panel);border:2px solid var(--accent);border-radius:.8em;padding:1em 1.3em;width:min(30em,100%);max-height:90%;display:flex;flex-direction:column;gap:.6em}
.pop p{margin:0;text-align:center}.pop .btns{justify-content:center}.pop button{min-width:5em}
#map .leaflet-popup-content-wrapper,#map .leaflet-popup-tip{background:var(--panel);color:var(--text)}
#map .leaflet-popup-content{font:inherit;font-size:.95rem;margin:.55em .8em;width:auto!important}#map a.btn{color:var(--text)}
#map .leaflet-popup-content .btns{margin-top:.4em;flex-wrap:nowrap;gap:.3em}#map .leaflet-popup-content .btn{padding:.3em .55em;font-size:.85rem}
nav{overflow-x:auto;scrollbar-width:none}nav::-webkit-scrollbar{display:none}nav button{flex:1 0 auto;padding:.5em .7em}
#sid{min-width:0;overflow:hidden;text-overflow:ellipsis}#ask{z-index:2001}.pop .ib{min-width:0}
#rcd,#nst,#ml,#gdt,#rnow,.cd>.sm{white-space:normal}.k-bad{color:var(--bad)}.k-pos{color:var(--pos)}
@media(max-width:480px){header .fl{display:none}#nl .ty{display:none}}
#a>.q{flex:2}
@media(max-width:700px),(max-height:560px){main{overflow:auto}#r,#n,#a,#g,#i{height:auto;min-height:100%}#r .grow,#n .grow{min-height:18em;flex:none}#a>.q{flex:2 0 14em}}
</style>
<header><i id=led></i><b class=acc id=sf>---.---</b><span id=sid></span><span class=sp></span><span class="fl bad" id=sup></span><span class="fl good" id=sfl></span><span class=bat><i id=sbat></i></span><button class=ib id=lk aria-label="Lock or unlock the T-Deck keys" title="Lock / unlock the T-Deck keys" onclick="act('lock&v='+(S&&S.locked?0:1))"><svg viewBox="0 0 24 24"><rect x=5 y=11 width=14 height=10 rx=2 /><path id=lkp d="M8 11V7a4 4 0 0 1 8 0v4"/></svg></button><button class=ib title=More aria-label="More: About, Restart" onclick=menu()><svg viewBox="0 0 24 24"><circle cx=12 cy=5 r=1.5 /><circle cx=12 cy=12 r=1.5 /><circle cx=12 cy=19 r=1.5 /></svg></button></header>
<nav><button data-t=h>Hunt</button><button data-t=m>Map</button><button data-t=r>Radio</button><button data-t=n>SondeHub</button><button data-t=a>Signal</button><button data-t=g>GPS</button><button data-t=i>Info</button><button data-t=s>Settings</button><button data-t=f>Files</button></nav>
<main>
<section id=h><div class=hg>
<div class="cd q" style=grid-area:d><small>Distance<span id=hsrc></span></small><div><div class=big><span id=hd>-</span><u id=hdu></u></div><div class=mid id=hb>&nbsp;</div></div></div>
<div class="cd comp" style=grid-area:c><svg viewBox="-64 -64 128 128"><circle r=58 fill=none style=stroke:var(--line) stroke-width=4 /><g id=ticks></g>
<text y=-36 text-anchor=middle dominant-baseline=middle style=fill:var(--bad) font-size=16>N</text><text x=38 text-anchor=middle dominant-baseline=middle style=fill:var(--label) font-size=13>E</text>
<text y=38 text-anchor=middle dominant-baseline=middle style=fill:var(--label) font-size=13>S</text><text x=-38 text-anchor=middle dominant-baseline=middle style=fill:var(--label) font-size=13>W</text>
<g id=ndl><path d="M0-48L15 22 0 12-15 22Z" style=fill:var(--good) /><circle r=5 style=fill:var(--bg) /></g><text id=cmode x=62 y=-56 text-anchor=end style=fill:var(--pos) font-size=11></text></svg></div>
<div class="cd q" style=grid-area:a><small>Altitude</small><div><div class=num><span id=ha>-</span><u>m</u></div></div></div>
<div class="cd q" style=grid-area:v><small>V-speed</small><div><div class=num><span id=hv>-</span><u>m/s</u></div></div></div>
<div class="cd q" style=grid-area:h><small>H-speed<span id=hhd></span></small><div><div class=num><span id=hh>-</span><u>km/h</u></div></div></div>
<div class="cd q" style=grid-area:s><small>Sonde<span id=hsa></span></small><div><div class=pos id=hpos>-</div><div class=sm id=hsx></div></div></div>
<div class="cd q" style=grid-area:g><small>Signal</small><div class=sig id=hsig></div><div class=sm id=hrx></div></div>
<div class="cd q" style=grid-area:m><small>Me</small><div><div class=mid id=hme>-</div><div class=sm id=hms></div></div></div>
</div>
<div class="cd land"><span class=pred id=hp>no landing prediction yet</span><span class=sm id=hlost></span><span class=sp></span><span class=btns id=hpn></span></div>
<div class="cd land"><span class="btns seg" id=hnv><button data-v=sonde>To sonde</button><button data-v=land>To landing</button><button data-v=srv id=hnsrv hidden>To server point</button></span><span class=sp></span>
<span class=btns><button onclick=tgtDlg()>Target</button><button onclick=kml()>KML</button><button onclick="if(confirm('Reset the counters, the received position and the prediction? A typed target stays.'))act('reset')">Reset</button><button id=hmu onclick="act('mute')">Sound off</button></span></div>
</section>
<section id=m hidden><div class=btns><span class="btns seg" id=mc><button data-c=b class=sel title="me, the sonde, its track and the landing">Both</button><button data-c=me>Me</button><button data-c=so>Sonde</button></span><label><input type=checkbox id=mf checked> follow</label><span class=sm id=ml>map from OpenStreetMap: needs internet on this device</span></div><div id=map></div></section>
<section id=r hidden>
<div class="cd fq"><span class="v acc" id=rfv>-</span><span class=sm id=rnow></span><span class=sp></span>
<span class=btns><button onclick="act('step&v=-10')">-10k</button><button onclick="act('step&v=-1')">-1k</button><button onclick="act('step&v=1')">+1k</button><button onclick="act('step&v=10')">+10k</button>
<input id=rf type=number step=0.001 min=400 max=406 inputmode=decimal placeholder=MHz style=width:7em><button class=pri onclick=setF()>Set</button></span></div>
<div class=row2><div class=cd><small>Sonde type</small><div class="btns seg" id=rt></div></div><div class=cd><small>Polarity</small><div class="btns seg" id=rpo><button data-v=auto>Auto</button><button data-v=normal>Normal</button><button data-v=inverted>Inverted</button></div></div>
<div class=cd><small>Scan<span id=rss></span></small><div class=btns><button id=rs onclick=scan()>Start scan</button></div></div></div>
<div class=cd id=rsc hidden><small>Scan 400-406 MHz<span id=rsp></span></small><canvas id=rcv style=width:100%;height:7em;display:block></canvas><div class=sm id=rcd></div></div>
<div class="cd grow"><small>Frequency presets</small><div class=btns><input id=rq placeholder="search name, group, MHz" oninput=presets() style=flex:1><button onclick="act('preset&i=-1').then(loadP)">Auto (no preset)</button><button onclick="act('pnext').then(loadP)">Next</button><button onclick=psave()>Save current</button><button onclick="act('peu').then(loadP)">Add European sites</button></div><div class=scroll id=rp></div></div>
</section>
<section id=n hidden style=gap:0><div class=tabs id=nt><button data-k=near class=sel>Near</button><button data-k=landed>Landed</button><button data-k=launches>Launches</button><button data-k=search>Search</button></div>
<div class="cd grow" style="border-radius:0 0 .7em .7em"><div class=btns id=nbar></div><div class=sm id=nst></div><div class=scroll id=nl></div></div></section>
<section id=a hidden><div class="cd q"><small>Signal now<span id=aag></span></small><div><div class=big><span id=ai>-</span><u>dBm</u></div><div class=mid id=ap>&nbsp;</div></div></div>
<div class=cd><div class=prog style=height:2.2em><i id=abar></i><b id=apk style="position:absolute;top:0;bottom:0;width:3px;background:var(--warn)"></b><span id=abt></span></div><div class=sm style=display:flex;justify-content:space-between><span>-120</span><span>-70</span><span>-20 dBm</span></div></div>
<div class=row2><div class="cd q" style=height:6em><small>Last frame</small><div><div class=mid id=alf>-</div></div></div><div class="cd q" style=height:6em><small>Sonde</small><div><div class=mid id=ads>-</div></div></div></div>
<div class="cd btns"><label><input type=checkbox id=atone> tone in this browser (pitch follows the signal)</label></div></section>
<section id=g hidden><div class=cd><div class=big id=gst style=font-size:2.2rem>-</div><div class=sm id=gdt></div></div>
<div class="cd grow"><small>Satellites in view<span id=gsum></span></small><canvas id=gcv style=width:100%;flex:1;min-height:8em;display:block></canvas></div>
<div class=cd><div class=pos id=gpos style=font-size:1.3rem>-</div><div class=sm id=gmv></div><div class=sm id=gaid></div></div></section>
<section id=i hidden><div class="cd scroll" id=il style=font-family:ui-monospace,Consolas,monospace;font-size:1rem;line-height:1.5></div></section>
<section id=s hidden style=gap:0><div class=tabs id=st></div><div class=cd id=sbox><div id=sr></div></div></section>
<section id=f hidden><div class="cd grow"><small>SD card<span class=acc id=fpath>/</span></small>
<div class=btns><button onclick="ls('/')">/</button><button onclick="ls(SDD)">my folder</button><button onclick="ls(SDD+'/sondehub')">sondehub</button><button onclick="ls(SDD+'/sonde')">sonde</button><button onclick="ls('/downloads')">/downloads</button><span class=sp></span><label class="btn pri">Upload files<input type=file id=fu multiple hidden></label></div>
<div class=drop id=fdrop>Drop files here to copy them to <b id=fdd>/</b>. A firmware .bin goes in /downloads, where the Launcher finds it. While a file is copied the T-Deck does not receive.</div>
<div class=prog id=fprog hidden><i></i><span></span></div><div class=scroll id=ft></div></div></section>
</main>
<footer><span id=clk></span><span id=bm></span><a id=dbg href=/debug hidden>debug</a></footer>
<div class=ov id=ask hidden><div class=pop><p id=a1></p><p class=sm id=a2></p><div class=btns><button class=pri onclick=ans(0)>No</button><button onclick=ans(1)>Yes</button></div></div></div>
<div class=ov id=dlg hidden><div class=pop><small id=dt></small><div class=scroll id=db></div><div class=btns id=dc><button onclick=closeDlg()>Close</button></div></div></div>
<script>
const SHDOC=')HTML" SH_DOC_URL R"HTML(',$=i=>document.getElementById(i),TYPES=['AUTO','RS41','DFM','M10/M20','iMet54'],CD=['N','NE','E','SE','S','SW','W','NW'],
SRC={radio:'received',net:'from SondeHub',memory:'last known',manual:'manual target'},TABS='hmrnagisf',PER=['3 h','12 h','24 h','7 days'];
let SDD='/',S=null,tab='h',P=null,SD=null,stab=0,thIdx=-1,LM=null,mk={},fitDone=0,mc='b',tg=-1,tn=0,mBusy=0,dir='/',note='',noteT=0,busy=0,nPoll=0;
const AC={method:'POST',headers:{'X-TDeck':'1'}};
const get=(u,o)=>fetch(u,o).then(r=>r.ok?r:r.text().then(t=>{throw new Error(t.trim()||'error '+r.status)}));
const js=u=>get(u).then(r=>r.json());
const f=(x,d)=>x==null||isNaN(x)?'-':Number(x).toFixed(d);
const esc=s=>String(s).replace(/[&<>'"]/g,c=>'&#'+c.charCodeAt(0)+';');
const cd=d=>CD[Math.round((((d%360)+360)%360)/45)%8];
const css=n=>getComputedStyle(document.documentElement).getPropertyValue('--'+n).trim();
function html(id,h){const e=$(id);if(e._h!==h){e._h=h;e.innerHTML=h}}
function txt(id,t){const e=$(id);if(e.textContent!==String(t))e.textContent=t}
function say(t){note=t;noteT=Date.now();bar()}
function bar(){txt('bm',S&&S.msg?S.msg:Date.now()-noteT<8000?note:'')}
function act(q){return get('/api/do?a='+q,AC).then(r=>r.text()).then(x=>{x=x.trim();if(x&&x!='OK')say(x);setTimeout(poll,250);return x}).catch(e=>say(e.message))}
function nav(lat,lon){return `<a class=btn target=_blank rel=noopener href="https://www.google.com/maps/dir/?api=1&destination=${lat},${lon}">Navigate</a>`}
for(let a=0;a<360;a+=30)$('ticks').innerHTML+=`<line y1=-58 y2=${a%90?-51:-47} style=stroke:var(--label) stroke-width=2 transform="rotate(${a})" />`;
for(let i=0;i<5;i++)$('hsig').innerHTML+=`<i style=height:${8+i*6}cqh></i>`;$('hsig').innerHTML+='<b id=hr>-</b>';
TYPES.forEach((t,i)=>{const b=document.createElement('button');b.textContent=t;b.onclick=()=>act('type&v='+i);$('rt').append(b)});
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>go(b.dataset.t));
function go(t){tab=t;document.querySelectorAll('nav button').forEach(b=>b.classList.toggle('sel',b.dataset.t==t));
for(const x of TABS)$(x).hidden=x!=t;if(t=='m')mapOpen();if(t=='r')loadP();if(t=='n')shLoad();if(t=='s')loadS();if(t=='f')ls(dir);if(typeof tabOpen=='function')tabOpen(t);try{localStorage.tdTab=t}catch(e){}}
let pIn=0,setG=-1;function poll(){if(busy||pIn||document.hidden)return;pIn=1;nPoll++;js('/api/state').finally(()=>pIn=0).then(s=>{S=s;if(s.sddir)SDD=s.sddir;if(s.theme!=thIdx)theme(s.theme);show()}).catch(()=>{txt('sid','T-Deck not reachable');$('led').className=''})}
function theme(i){js('/api/theme').then(t=>{thIdx=i;for(const k in t)document.documentElement.style.setProperty('--'+k,t[k]);if(LM)mapRecolor()}).catch(()=>{})}
function show(){const s=S,o=s.sonde;$('led').className=o.frame_age>=0&&o.frame_age<5?'on':'';txt('sf',f(s.freq,3));txt('sid',o.have?o.id:'');
txt('sup',[s.up.rs?'RS':'',s.up.fi?'FI':''].filter(x=>x).join(' '));txt('sfl',[s.scan?'SCAN':'',s.sd?'SD':'','G'+s.gps.sats].filter(x=>x).join(' '));
$('sbat').style.width=s.batt+'%';$('sbat').style.background=s.batt<20?css('bad'):'';txt('clk',s.time);$('dbg').hidden=!s.debug;
$('ask').hidden=!s.ask.open;txt('a1',s.ask.l1);txt('a2',s.ask.l2);
if(s.tdist!=null){const d=s.tdist,k=d>=1000;txt('hd',k?f(d/1000,d<10000?2:1):f(d,0));txt('hdu',k?'km':'m');
txt('hb',`${f(s.tbrg,0)} ${cd(s.tbrg)}`+(s.to=='sonde'&&s.elev!=null?`  elev ${s.elev>=0?'+':''}${f(s.elev,0)}°`:''));
const up=s.hdg>=0;$('ndl').setAttribute('transform',`rotate(${up?s.tbrg-s.hdg:s.tbrg})`);$('ndl').style.display='';txt('cmode',up?'AHEAD':'N UP')}
else{txt('hd','-');txt('hdu','');txt('hb',!o.have?(s.wait||'no sonde yet'):s.gps.fix?'':'waiting for my GPS fix');$('ndl').style.display='none';txt('cmode','')}
txt('hsrc',s.to=='land'?'to landing':s.to=='srv'?'to '+(s.tag||'server')+' landing':o.have?SRC[o.src]||o.src:'');
[...$('hnv').children].forEach(b=>b.classList.toggle('sel',b.dataset.v==s.nav));$('hnsrv').hidden=!s.srvok&&s.nav!='srv';if(s.tag)txt('hnsrv','To '+s.tag);
txt('hmu',s.mute?'Sound on':'Sound off');$('lkp').setAttribute('d',s.locked?'M8 11V7a4 4 0 0 1 8 0v4':'M8 11V7a4 4 0 0 1 7.5-2');$('lk').title=s.locked?'T-Deck keys locked: tap to unlock':'Lock the T-Deck keys';
txt('hlost',s.lost>0?`signal lost below ~${f(s.lost,0)} m`:'');txt('ha',o.have?f(o.alt,0):'-');txt('hv',o.have?f(o.vv,1):'-');$('hv').className=o.have&&o.vv<0?'warn':'';
txt('hh',o.have?f(o.vh,1):'-');txt('hhd',o.have&&o.vh>1?`${cd(o.dir)} ${f(o.dir,0)}`:'');
txt('hpos',o.have?`${f(o.lat,5)}  ${f(o.lon,5)}`:'-');txt('hsa',o.have?(o.src=='radio'?`${s.ssats} sat  `:'')+`age ${o.age} s`:'');$('hsa').className=s.stale?'bad':'';
txt('hsx',o.have?(s.info||[o.model,o.batt?f(o.batt,1)+'V':''].filter(x=>x).join('  ')):'');
const nb=o.rssi?Math.max(0,Math.min(5,Math.round((o.rssi+122)/10))):0;[...$('hsig').children].forEach((e,i)=>{if(i<5)e.className=i<nb?'on':''});
txt('hr',o.rssi?f(o.rssi,0):'-');txt('hrx',(o.frame_age>=0?`frame ${o.frame_age} s ago`:'no frames yet')+(s.okpct>=0?`  ok ${s.okpct}%`:''));
txt('hme',s.gps.fix?`${s.gps.sats} sat`:'no fix');txt('hms',s.gps.fix?`${f(s.gps.kmh,0)} km/h`:`${s.gps.sats} sat`);
if(s.pred){const p=s.pred,e=p.eta;txt('hp',`${p.tag?p.tag.replace(/ LAND$/,'')+' ':''}LAND ${f(p.lat,4)} ${f(p.lon,4)}`+(p.conf>=0?` (${p.conf}%${p.m50>=0?', '+f(p.m50,1)+' km':''})`:'')+(e>0?` in ${Math.floor(e/60)}m${String(e%60).padStart(2,'0')}s`:''));html('hpn',nav(p.lat,p.lon))}
else if(o.have){txt('hp',`SONDE ${f(o.lat,4)} ${f(o.lon,4)}`);html('hpn',nav(o.lat,o.lon))}
else{txt('hp','no landing prediction yet');html('hpn','')}
txt('rfv',f(s.freq,3)+' MHz');txt('rnow',`${s.rx}, ${s.pol}, BW ${f(s.bw,1)} kHz, idle ${f(s.idle,0)} dBm`);[...$('rpo').children].forEach(b=>b.classList.toggle('sel',b.dataset.v==s.pol));
$('rsc').hidden=!s.scan;if(tab=='r'&&s.scan)scanPoll();[...$('rt').children].forEach((b,i)=>b.classList.toggle('sel',i==s.stype));
txt('rs',s.scan?'Stop scan':'Start scan');$('rs').className=s.scan?'pri':'';txt('rss',s.scan?'scanning...':'');
if(tab=='m')mapPoll();if(tab=='s'&&$('dlg').hidden&&(s.setgen!=setG||nPoll%15==0)){setG=s.setgen;loadS()}if(tab=='n'&&nPoll%5==0&&!shDet)shLoad();if(tab=='g')gpsLoad();if(tab=='i')infoLoad();bar()}
// Signal: fast while the tab is open; the peak of the last 4 s as on the T-Deck; an optional tone.
let liveT=0,pk=[],actx=null,osc=null,gain=null;
function tabOpen(t){clearInterval(liveT);if(t=='a')liveT=setInterval(live,500);else toneOff();if(t=='g')gpsLoad();if(t=='i')infoLoad()}
let lIn=0;function live(){if(lIn||document.hidden)return;lIn=1;js('/api/live').finally(()=>lIn=0).then(d=>{const now=Date.now();pk=pk.filter(x=>now-x[0]<4000);pk.push([now,d.idle]);const peak=Math.max(...pk.map(x=>x[1]));
txt('ai',f(d.idle,1));txt('ap','peak '+f(peak,1)+' dBm');const pc=v=>Math.max(0,Math.min(100,(v+120)));$('abar').style.width=pc(d.idle)+'%';$('apk').style.left=pc(peak)+'%';
txt('alf',d.fage>=0?`${f(d.last,0)} dBm, ${d.fage} s ago`:'no frames yet');txt('ads',d.dist!=null?`${km(d.dist)} ${f(d.brg,0)} ${cd(d.brg)}`:'no distance');txt('aag',d.gage>=0?`position ${d.gage} s old`:'');
if($('atone').checked)tone(d.idle);else toneOff()}).catch(()=>{})}
$('atone').onchange=e=>{if(!e.target.checked)return toneOff();try{if(!actx){actx=new AudioContext();osc=actx.createOscillator();gain=actx.createGain();gain.gain.value=0;osc.connect(gain);gain.connect(actx.destination);osc.start()}actx.resume()}catch(x){}};
function tone(v){try{if(!actx)return;
osc.frequency.value=300+Math.max(0,Math.min(100,v+120))*12;gain.gain.setTargetAtTime(0.08,actx.currentTime,0.02);gain.gain.setTargetAtTime(0,actx.currentTime+0.15,0.03)}catch(e){}}
function toneOff(){if(gain&&actx)gain.gain.setTargetAtTime(0,actx.currentTime,0.02)}
let gT=0;function gpsLoad(){if(Date.now()-gT<1500)return;gT=Date.now();js('/api/gps').then(d=>{txt('gst',d.fix?(d.type==2?'2D':'3D')+' FIX':d.seen?'NO FIX':'NO GPS DATA');$('gst').className='big '+(d.fix?'good':d.seen?'warn':'bad');
txt('gdt',[d.fix?`${d.used} used  HDOP ${f(d.hdop,1)}  PDOP ${f(d.pdop,1)}`:'',d.first?`first fix after ${d.first} s`:d.seen?'searching since power-on':''].filter(x=>x).join('   '));
const tr=d.sv.filter(x=>x[4]>0);const sy={};d.sv.forEach(x=>sy[x[0]]=(sy[x[0]]||0)+1);const NM={G:'GPS',R:'GLO',E:'GAL',C:'BDS',J:'QZS'};
txt('gsum',`in view ${d.sv.length}, tracked ${tr.length}: `+Object.keys(sy).map(k=>`${NM[k]} ${sy[k]}`).join(' '));
const[x,w,h]=canv('gcv');x.fillStyle=css('panel');x.fillRect(0,0,w,h);
const bw=Math.max(6,Math.min(28,w/Math.max(1,tr.length)-4));tr.forEach((s,i)=>{const bh=Math.min(1,s[4]/50)*(h-18);x.fillStyle=css(s[4]>=35?'good':s[4]>=25?'pos':'warn');
x.fillRect(4+i*(bw+4),h-16-bh,bw,bh);if(bw>=16){x.fillStyle=css('label');x.font='11px sans-serif';x.fillText(s[0]+s[1],4+i*(bw+4),h-3)}});
txt('gpos',d.fix?`${f(d.lat,6)}  ${f(d.lon,6)}`:d.saved?`last fix ${f(d.saved[0],4)} ${f(d.saved[1],4)}`:'no position yet');
txt('gmv',d.fix?`${f(d.alt,0)} m asl  ${f(d.kmh,1)} km/h  ${f(d.crs,0)} ${cd(d.crs)}`:'');txt('gaid',`${d.clock} ${d.utc?'UTC':'local'}   aid: ${d.aid}`)}).catch(()=>{})}
let iT=0;function infoLoad(){if(Date.now()-iT<4500)return;iT=Date.now();js('/api/info').then(d=>html('il',d.l.map(x=>`<div class=k-${x[1]}>${esc(x[2])||'&nbsp;'}</div>`).join(''))).catch(()=>{})}
// SondeHub tab: the lists of the T-Deck's SondeHub page, from its own data (the query stays alive while open).
let shk='near',shp=2,shq='',shD=null,shDet=0,shLast='';
$('nt').onclick=e=>{const b=e.target.closest('button');if(!b)return;shDet=0;if(b.dataset.k=='landed'&&shk=='landed')shp=(shp+1)%4;shk=b.dataset.k;[...$('nt').children].forEach(x=>x.classList.toggle('sel',x==b));$('nl').innerHTML='';shLoad()};
const km=m=>m==null?'-':m>=10000?f(m/1000,0)+' km':m>=1000?f(m/1000,1)+' km':f(m,0)+' m';
const ago=a=>a<0?'':a<120?a+' s':a<7200?Math.round(a/60)+' min':Math.round(a/3600)+' h';
const ut=t=>{const d=new Date(t*1000);return d.toLocaleDateString([],{weekday:'short',day:'numeric'})+' '+d.toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'})};
const shl=x=>`<a class=btn target=_blank rel=noopener href="https://sondehub.org/${encodeURIComponent(x)}">SondeHub</a>`;
function shLoad(){if(shk=='search'){html('nbar',`<input id=nq placeholder="serial, e.g. X1234567" style=flex:1 value="${esc(shq)}"><button class=pri id=nqo>Search</button>`);
$('nqo').onclick=()=>{shq=$('nq').value.trim().toUpperCase();shSum()};$('nq').onkeydown=k=>{if(k.key=='Enter')$('nqo').click()};if(shq)shSum();else{txt('nst','');html('nl','')}return}
html('nbar',shk=='landed'?`<span class=sm>last</span><span class="btns seg" id=npp>${PER.map((p,i)=>`<button data-p=${i} class="${i==shp?'sel':''}">${p}</button>`).join('')}</span>`:'');
if(shk=='landed')$('npp').onclick=e=>{const b=e.target.closest('button');if(b){shp=+b.dataset.p;shLoad()}};
js('/api/sh?l='+shk+(shk=='landed'?'&p='+shp:'')).then(d=>{shD=d;if(!d.net){txt('nst','Internet via Wi-Fi is off: turn it on in Settings, Network.');html('nl','');return}
if(!d.ready){txt('nst',d.time?'asking SondeHub...':'waiting for the time from the internet...');return}
if(shk=='launches'){txt('nst',d.rows.length?'next launches within 400 km':'no launch sites nearby');
html('nl',d.rows.map(x=>`<div class=li data-i=${x.i}><span>${ut(x.time)}  ${esc(x.site)}</span><em class="r sm">${f(x.km,0)} km${x.land?' &rarr; landing':''}</em></div>`).join(''));return}
txt('nst',`${d.rows.length} ${shk=='near'?'sondes in the last 3 h':'landed in the last '+PER[d.p]}, updated ${ago(d.age)} ago`+(d.follow?`  -  following ${d.follow}`:''));
html('nl',d.rows.map(x=>`<div class=li data-s="${esc(x.s)}"><span class="${x.s==d.follow?'good':shk=='near'&&x.vv<-1.5?'warn':''}">${esc(x.s)}</span><em class="sm ty">${esc(x.t)}</em><em class="r">${x.d!=null?km(x.d)+' '+cd(x.b):''}</em><em class=sm style=min-width:9em;text-align:right>${f(x.alt,0)} m ${x.vv>=0?'+':''}${f(x.vv,1)}  ${ago(x.age)}</em></div>`).join('')||'<div class=sm>none</div>')}).catch(e=>say(e.message))}
$('nl').onclick=e=>{const r=e.target.closest('.li');if(!r||!shD)return;if(shk=='launches')return r.dataset.i!=null&&shLaunch(+r.dataset.i);const s=r.dataset.s;if(!s)return;
if(shk=='landed'){shq=s;shDet=1;return shSum()}const x=shD.rows.find(y=>y.s==s);if(!x)return;const fol=s==shD.follow;
openDlg(s,`<p>${esc(x.t)}  ${f(x.f,3)} MHz</p><p class=sm>${f(x.alt,0)} m, ${x.vv>=0?'+':''}${f(x.vv,1)} m/s, ${f(x.vh,1)} m/s, seen ${ago(x.age)} ago${x.d!=null?'<br>'+km(x.d)+' '+cd(x.b)+' from here':''}</p>
<div class=btns><button class=pri id=nfo>${fol?'Stop following':'Follow and tune'}</button>${shl(s)}${nav(x.lat,x.lon)}</div>`);$('nfo').onclick=()=>{closeDlg();act(fol?'unfollow':'follow&s='+encodeURIComponent(s)).then(shLoad)}}
function shSum(){if(shq!=shLast){shLast=shq;txt('nst','asking SondeHub for '+shq+'...');html('nl','')}js('/api/sh?l=summary&s='+encodeURIComponent(shq)).then(d=>{if(!d.net){txt('nst','Internet via Wi-Fi is off');return}
if(!d.ready){setTimeout(()=>{if(tab=='n'&&(shk=='search'||shk=='landed'))shSum()},2500);return}if(!d.found){txt('nst',shq+': not found on SondeHub');return}
txt('nst','');const p=[`<div class=gh>${esc(d.s)} ${esc(d.t)}${d.rec?' - recovered':''}</div>`,
d.launch?`<div class=li>Launch<em class=r>${ut(d.launch[2])}${d.site?' site '+esc(d.site):''}</em></div>`:'',d.burst?`<div class=li>Burst<em class=r>${f(d.burst,0)} m</em></div>`:'',
d.last?`<div class=li>Last frame<em class=r>${f(d.last[2],0)} m ${d.last[3]>=0?'+':''}${f(d.last[3],1)} m/s, ${ut(d.last[4])}</em></div><div class=li>Last position<em class=r>${f(d.last[0],5)} ${f(d.last[1],5)}${d.d!=null?', '+km(d.d)+' '+cd(d.b):''}</em></div>`:'',
d.land?`<div class=li>Predicted landing<em class="r pred">${f(d.land[0],5)} ${f(d.land[1],5)}</em></div>`:'',d.f?`<div class=li>Frequency<em class=r>${f(d.f,3)} MHz</em></div>`:'',
`<div class=btns style=padding:.5em>${shDet?'<button id=nbk>Back to the list</button>':''}<button class=pri id=nst2>Make ${d.land?'the landing':'the last position'} my target</button>${shl(d.s)}${d.land?nav(d.land[0],d.land[1]):d.last?nav(d.last[0],d.last[1]):''}</div>`];
html('nl',p.join(''));$('nst2').onclick=()=>act('shtarget&s='+encodeURIComponent(d.s));if($('nbk'))$('nbk').onclick=()=>{shDet=0;shLast='';shLoad()}}).catch(e=>say(e.message))}
function shLaunch(i){const x=shD.rows.find(y=>y.i==i);if(!x)return;
openDlg(x.site,`<p>${ut(x.time)}  ${f(x.f,3)} MHz, ${f(x.km,0)} km</p><p class=sm>burst ${f(x.burst,0)} m, ascent ${f(x.asc,1)} m/s, descent ${f(x.desc,1)} m/s</p>`+
(x.land?`<p class=pred>predicted landing ${f(x.land[0],4)} ${f(x.land[1],4)} at ${ut(x.land[2])}</p><div class=btns><button class=pri id=nlo>Make it my target</button>${nav(x.land[0],x.land[1])}</div>`:`<p class=sm>${x.tried?'no prediction for this launch':'prediction on its way...'}</p>`));
if(x.land)$('nlo').onclick=()=>{closeDlg();act('shtarget&t='+x.time)}}
function setF(){const v=$('rf').value;if(v)act('freq&v='+v);$('rf').value='';$('rf').blur()}
$('rf').onkeydown=e=>{if(e.key=='Enter')setF()};
function scan(){act(S&&S.scan?'stop':'scan')}
function ans(y){$('ask').hidden=true;act('ask&yes='+y).then(()=>{if(tab=='s')setTimeout(loadS,300);if(tab=='r')setTimeout(loadP,300)})}
function loadP(){js('/api/presets').then(p=>{P=p;presets()}).catch(e=>say(e.message))}
function presets(){if(!P)return;const q=$('rq').value.toLowerCase();let h='',g=null;
P.p.forEach((x,i)=>{if(q&&!(x.n+' '+x.g+' '+x.f.toFixed(3)+' '+x.t).toLowerCase().includes(q))return;
if(x.g!==g){g=x.g;h+=`<div class=gh>${esc(g||'Presets')}</div>`}
h+=`<div class="li${i==P.cur?' sel':''}" data-i=${i}><span>${esc(x.n)}</span><em class="r acc">${x.f.toFixed(3)}</em><em class=sm style=min-width:5em;text-align:right>${esc(x.t)}</em>${ic('del')}</div>`});
$('rp').innerHTML=h||'<div class=sm>no preset found</div>'}
$('rp').onclick=e=>{const r=e.target.closest('.li');if(!r)return;if(e.target.closest('[data-a=del]'))return act('pdel&i='+r.dataset.i);act('preset&i='+r.dataset.i).then(loadP)};
$('hnv').onclick=e=>{const b=e.target.closest('button');if(b)act('nav&v='+b.dataset.v)};
$('rpo').onclick=e=>{const b=e.target.closest('button');if(b)act('pol&v='+b.dataset.v)};
function inputDlg(t,o,cb){openDlg(t,`<div class=btns><input id=et type=${o.type||'text'} autocomplete=off${o.ml?' maxlength='+o.ml:''} placeholder="${esc(o.ph||'')}" value="${esc(o.val||'')}" style=flex:1><button class=pri id=eo>${o.btn||'Save'}</button>${o.own?'<button onclick=closeDlg()>Cancel</button>':''}</div>`+(o.note?`<div class=sm>${o.note}</div>`:''),o.own);
const e=$('et');e.focus();e.onkeydown=k=>{if(k.key=='Enter')$('eo').click()};$('eo').onclick=()=>{const v=e.value;closeDlg();cb(v)};return e}
function tgtDlg(){inputDlg('Manual target',{ph:'lat lon, e.g. 38.01 12.53',btn:'Set',note:'Distance and arrow point here until a sonde is received.'},v=>{v=v.trim();if(v)act('target&v='+encodeURIComponent(v))})}
function kml(){say('KML: exporting (the T-Deck pauses reception for a few seconds)...');act('kml').then(x=>{if(x&&x.startsWith('KML ')){const p=x.slice(4);say('KML ready');location.href='/dl?f='+encodeURIComponent(p)}})}
function psave(){inputDlg('Save the current frequency as a preset',{ph:'name',ml:24},v=>{v=v.trim();if(v)act('psave&n='+encodeURIComponent(v))})}
function menu(){openDlg('T-Deck',`<div class=btns style=flex-direction:column;align-items:stretch><button id=mab>About</button><button id=mrb>Restart the T-Deck</button></div>`);
$('mab').onclick=()=>openDlg('About',`<p>T-Deck SondeTracker ${esc(S?S.v:'')}</p><p class=sm>Radiosonde receiver and hunting tool for the LilyGO T-Deck Plus, by IT9KVB.</p><p><a target=_blank rel=noopener href="https://github.com/N1k0droid/t-deck-sonde-tracker">github.com/N1k0droid/t-deck-sonde-tracker</a></p><p class=sm>Maps: &copy; OpenStreetMap contributors.</p>`);
$('mrb').onclick=()=>{if(confirm('Restart the T-Deck now?')){closeDlg();act('reboot');say('restarting...')}}}
let scT=0;function scanPoll(){if(Date.now()-scT<1000)return;scT=Date.now();js('/api/scan').then(drawScan).catch(()=>{})}
function canv(id){const c=$(id),r=devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;c.width=w*r;c.height=h*r;const x=c.getContext('2d');x.scale(r,r);return[x,w,h]}
let lastScan=null;function drawScan(d){lastScan=d;const[x,w,h]=canv('rcv'),n=d.bins,v=(str,i)=>parseInt(str.substr(i*2,2),16)-160;
const y=db=>h-(Math.max(-130,Math.min(-40,db))+130)/90*h;x.fillStyle=css('panel');x.fillRect(0,0,w,h);
const line=(str,col,lim)=>{x.strokeStyle=col;x.beginPath();for(let i=0;i<lim;i++){const X=i*w/n,Y=y(v(str,i));i?x.lineTo(X,Y):x.moveTo(X,Y)}x.stroke()};
line(d.h,css('line'),n);line(d.r,css('good'),d.phase==1?d.pos:n);x.strokeStyle=css('warn');x.setLineDash([4,4]);x.beginPath();x.moveTo(0,y(d.noise));x.lineTo(w,y(d.noise));x.stroke();x.setLineDash([]);
x.fillStyle=css('bad');d.c.forEach(k=>{const X=(k[0]-400000)/10*w/n;x.fillRect(X-1,0,3,h)});
txt('rsp',[`presets ${d.pos}/${d.np}`,`sweeping ${Math.round(d.pos*100/n)}%`,'listening','found'][d.phase]+`  round ${d.round}  ${d.s} s`);
txt('rcd',d.c.length?d.c.map(k=>`${k[3]?'> ':''}${(k[0]/1000).toFixed(3)} MHz ${k[1]} dBm ${TYPES[k[2]]||''}`).join('   '):'no candidates yet')}
function loadS(){js('/api/settings').then(d=>{SD=d;drawS()}).catch(e=>say(e.message))}
function row(i){for(const t of SD.tabs)for(const x of t.rows)if(x.i==i)return x}
function drawS(){const d=SD,rows=d.tabs[stab].rows;html('st',d.tabs.map((t,i)=>`<button class="${i==stab?'sel':''}" data-i=${i}>${esc(t.name)}</button>`).join(''));
const sb=$('sbox'),nc=rows.length*28>sb.clientHeight&&sb.clientWidth>=560?2:1;
$('sr').style.setProperty('--n',Math.ceil(rows.length/nc));$('sr').style.setProperty('--c',nc);
html('sr',rows.map((x,n)=>`<div class="li${x.k=='info'?' ro':''}" data-i=${x.i}><b>${n+1}</b><span>${esc(x.name)}</span><em class="r k-${x.c}">${esc(x.val)}</em></div>`).join(''))}
$('st').onclick=e=>{const b=e.target.closest('button');if(b){stab=+b.dataset.i;drawS()}};
$('sr').onclick=e=>{const r=e.target.closest('.li');if(!r)return;const x=row(+r.dataset.i);if(!x)return;
if(x.k=='action')return setting(x.i);if(x.k=='radio')return go('r');if(x.k=='text')return textDlg(x);if(x.k=='list')return listDlg(x)};
let dlgId=0;function openDlg(t,h,own){dlgId++;txt('dt',t);$('db').innerHTML=h;$('dc').hidden=!!own;$('dlg').hidden=false}
function closeDlg(){$('dlg').hidden=true;$('db').onclick=null}
function textDlg(x){const pw=x.i==28||x.i==39||x.i==46;inputDlg(x.name,{type:pw?'password':'text',ph:pw?'':x.val,note:x.i==39?'user tdeck; "-" goes back to the default password':''},v=>setting(x.i,v))}
function listDlg(x){openDlg(x.name,'<div class=sm>loading...</div>');
const fail=e=>{$('db').innerHTML='<div class=sm>'+esc(e.message)+'</div>'};
if(x.i==12)js('/api/list?l=tz').then(d=>pick(d.rows.map(r=>`<span>${esc(r)}</span>`),d.cur,n=>get('/api/list?l=tz&pick='+n,AC))).catch(fail);
if(x.i==32)js('/api/list?l=saved').then(d=>{pick([`<span>+ save this network (${esc(d.ssid)})</span>`,...d.rows.map(r=>`<span>${esc(r)}</span>`+ic('del'))],-1,n=>get('/api/list?l=saved&pick='+n,AC));
const e=$('db'),pk0=e.onclick;e.onclick=ev=>{const r=ev.target.closest('.li');if(r&&ev.target.closest('[data-a=del]')){closeDlg();return act('wdel&i='+(r.dataset.n-1))}pk0(ev)}}).catch(fail);
if(x.i==25){$('db').innerHTML='<div class=sm>scanning...</div>';const my=dlgId;get('/api/list?l=wifi&scan=1',AC).then(()=>setTimeout(()=>wifi(my),2500)).catch(fail)}}
function pick(rows,cur,fn){const e=$('db');e.innerHTML=rows.length?rows.map((r,i)=>`<div class="li${i==cur?' sel':''}" data-n=${i}>${r}</div>`).join(''):'<div class=sm>nothing found</div>';
e.onclick=ev=>{const r=ev.target.closest('.li');if(!r)return;closeDlg();fn(+r.dataset.n).then(()=>setTimeout(()=>{loadS();poll()},300)).catch(er=>say(er.message))};
if(cur>=0&&e.children[cur])e.children[cur].scrollIntoView({block:'center'})}
function wifi(my){if($('dlg').hidden||dlgId!=my)return;js('/api/list?l=wifi').then(d=>{if(dlgId!=my)return;if(d.state==-1)return setTimeout(()=>wifi(my),1500);if(d.state==-2){$('db').innerHTML='<div class=sm>scan failed</div>';return}
pick(d.rows.map(x=>`<span>${esc(x.n)}</span><em class="r sm">${x.rssi} dBm ${x.lock?'lock':'open'}</em>`),-1,n=>{const w=d.rows[n];if(w&&w.lock){setTimeout(()=>pwDlg(n,w.n),50);return Promise.resolve()}return get('/api/list?l=wifi&pick='+n,AC)})}).catch(()=>{})}
// A protected network: its password first, then name and password in one request (the T-Deck joins at once).
function pwDlg(n,name){inputDlg('Password of '+name,{btn:'Join',note:'The T-Deck joins this network: this page may need its new address.'},v=>{
get('/api/list?l=wifi&pick='+n,{method:'POST',headers:{'X-TDeck':'1','Content-Type':'application/x-www-form-urlencoded'},body:'pass='+encodeURIComponent(v)}).then(()=>{say('Wi-Fi: '+name);setTimeout(()=>{loadS();poll()},300)}).catch(e=>say(e.message))})}
function setting(i,t){get('/api/setting?item='+i,t==null?AC:{method:'POST',headers:{'X-TDeck':'1','Content-Type':'application/x-www-form-urlencoded'},body:'text='+encodeURIComponent(t)}).then(r=>r.text()).then(x=>{x=x.trim();if(x=='SHINFO')shInfo();else if(x&&x!='OK')say(x);setTimeout(()=>{loadS();poll()},300)}).catch(e=>say(e.message))}
function shInfo(){openDlg('SondeHub','<p>SondeHub has yet to authorize this app.</p><p>Help by sending your data: the frames are kept in <a href=# onclick="shFiles();return false">sondehub</a> on the SD card.</p><p><a class=btn target=_blank rel=noopener href="'+SHDOC+'">How to send them</a></p>')}
function shFiles(){closeDlg();dir=SDD+'/sondehub';go('f')}
const IC={dl:'<path d="M12 3v12m-5-5 5 5 5-5M5 20h14"/>',ren:'<path d="M4 20h4L19 9l-4-4L4 16zM14 6l4 4"/>',
del:'<path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13M10 11v6M14 11v6"/>'},TIP={dl:'Download',ren:'Rename',del:'Delete'};
const ic=k=>`<button class=ib title=${TIP[k]} aria-label=${TIP[k]} data-a=${k}><svg viewBox="0 0 24 24">${IC[k]}</svg></button>`;
const kb=z=>(z=+z)>=1048576?(z/1048576).toFixed(1)+' MB':(z/1024).toFixed(1)+' KB';
const upDir=d=>d.slice(0,d.lastIndexOf('/'))||'/';
function ls(d){dir=d;txt('fpath',d);txt('fdd',d);get('/files?dir='+encodeURIComponent(d)).then(r=>r.text()).then(t=>{
const more=t.includes('\tmore'),rows=t.split('\n').filter(x=>x&&x!='\tmore').map(x=>{const[n,z]=x.split('\t');return{n,z,isd:z=='-'}}).filter(x=>!(d=='/'&&x.isd&&x.n.toLowerCase()=='maps')).sort((a,b)=>(b.isd-a.isd)||a.n.localeCompare(b.n));
let h=d!='/'?`<div class=li data-d="${esc(upDir(d))}"><b>..</b><span class=sm>up one folder</span></div>`:'';
rows.forEach(x=>{const p=(d=='/'?'':d)+'/'+x.n,a=`data-p="${esc(p)}" data-n="${esc(x.n)}"`;h+=x.isd?`<div class=li data-d="${esc(p)}" ${a}><b>&#9656;</b><span>${esc(x.n)}/</span><em class="r sm">folder</em>${ic('ren')}${ic('del')}</div>`
:`<div class="li ro" ${a}><b>&#8226;</b><span>${esc(x.n)}</span><em class="r sm">${kb(x.z)}</em>${ic('dl')}${ic('ren')}${ic('del')}</div>`});
$('ft').innerHTML=(h||'<div class=sm>empty folder</div>')+(more?'<div class=sm>only the first 300 entries are shown</div>':'')}).catch(e=>say(e.message))}
$('ft').onclick=e=>{const b=e.target.closest('.ib'),r=e.target.closest('.li');if(!r)return;if(!b){if(r.dataset.d)ls(r.dataset.d);return}
const p=r.dataset.p,n=r.dataset.n,a=b.dataset.a;if(a=='dl'){const l=document.createElement('a');l.href='/dl?f='+encodeURIComponent(p);l.download=n;l.click()}
if(a=='ren')renDlg(p,n);if(a=='del')delDlg(p,n,r.dataset.d!=null)};
function renDlg(p,n){const e=inputDlg('Rename',{val:n,btn:'Rename',own:1},v=>{v=v.trim();if(v&&v!=n)fop('/rename?f='+encodeURIComponent(p)+'&to='+encodeURIComponent(v),'renamed: '+v)}),dot=n.lastIndexOf('.');
e.setSelectionRange(0,dot>0?dot:n.length)}
function delDlg(p,n,isd){openDlg('Delete',`<p>Delete ${isd?'the folder ':''}<b>${esc(n)}</b>?</p><p class=sm>It cannot be undone.${isd?' Only an empty folder can be deleted.':''}</p>
<div class=btns><button class=pri onclick=closeDlg()>No</button><button id=eo>Yes, delete</button></div>`,1);$('eo').onclick=()=>{closeDlg();fop('/delete?f='+encodeURIComponent(p),'deleted: '+n)}}
function fop(u,ok){get(u,AC).then(()=>{say(ok);ls(dir)}).catch(e=>{say(e.message);ls(dir)})}
let upQ=[];function queue(l){upQ.push(...l);if(!busy)send(upQ)}
$('fu').onchange=e=>{queue([...e.target.files]);e.target.value=''};
const dz=$('fdrop');dz.ondragover=e=>{e.preventDefault();dz.classList.add('over')};dz.ondragleave=()=>dz.classList.remove('over');
dz.ondrop=e=>{e.preventDefault();dz.classList.remove('over');queue([...e.dataTransfer.files])};
function prog(v,t){$('fprog').hidden=false;$('fprog').firstChild.style.width=v*100+'%';$('fprog').lastChild.textContent=t}
function send(l){if(!l.length){busy=0;return}busy=1;const x=l.shift(),fd=new FormData(),q=new XMLHttpRequest();fd.append('file',x,x.name);
q.upload.onprogress=e=>{if(e.lengthComputable)prog(e.loaded/e.total,`${x.name}: ${kb(e.loaded)} of ${kb(e.total)}`)};
q.onload=()=>{if(q.status==200){prog(1,`${x.name}: copied to ${dir}`);ls(dir);send(l)}else{busy=0;prog(0,`${x.name}: ${q.responseText.trim()||'error '+q.status}`)}};
q.onerror=()=>{busy=0;prog(0,`${x.name}: connection lost`)};prog(0,`${x.name}: starting...`);q.open('POST','/upload?dir='+encodeURIComponent(dir));q.setRequestHeader('X-TDeck','1');q.send(fd)}
let mapLoading=0;function mapOpen(){if(LM){LM.invalidateSize();return mapPoll()}if(window.L)return mapInit();if(mapLoading)return;mapLoading=1;txt('ml','loading the map...');
const c=document.createElement('link');c.rel='stylesheet';c.href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css';c.integrity='sha256-p4NxAoJBhIIN+hmNHrzRCf9tD/miZyoHS5obTRR9BMY=';c.crossOrigin='';document.head.append(c);
const s=document.createElement('script');s.src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js';s.integrity='sha256-20nQCchB9co0qIjJZRGuk2/Z9VM+kNiyxNV1lvTlZBo=';s.crossOrigin='';
s.onload=mapInit;s.onerror=()=>(mapLoading=0,txt('ml','map not available: this phone or PC has no internet. Positions are on the Hunt page.'));document.head.append(s)}
function mapInit(){txt('ml','');const g=S&&S.gps&&S.gps.fix;LM=L.map('map').setView(g?[S.gps.lat,S.gps.lon]:[41.9,12.5],g?11:5);
L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19,attribution:'&copy; OpenStreetMap contributors'}).addTo(LM);
mk.path=L.polyline([],{color:css('good'),weight:3}).addTo(LM);LM.on('dragstart',()=>{$('mf').checked=false});mapPoll()}
function mapRecolor(){for(const k of ['me','so','pr','sh','sv'])if(mk[k]){LM.removeLayer(mk[k]);delete mk[k]}mk.path.setStyle({color:css('good')});if(mk.shp)mk.shp.setStyle({color:css('bad')});mapPoll()}
$('mc').onclick=e=>{const b=e.target.closest('button');if(!b)return;mc=b.dataset.c;[...$('mc').children].forEach(x=>x.classList.toggle('sel',x==b));$('mf').checked=true;center(1)}
function pt(k,ll,col,tip){if(!ll){if(mk[k]){LM.removeLayer(mk[k]);delete mk[k]}return}
if(!mk[k])mk[k]=L.circleMarker(ll,{radius:8,color:css('bg'),weight:2,fillColor:css(col),fillOpacity:1}).bindTooltip(tip).addTo(LM);else mk[k].setLatLng(ll)}
let shg=0;function mapPoll(){if(!LM||mBusy)return;mBusy=1;js(`/api/track?gen=${tg}&n=${tn}&shg=${shg}`).finally(()=>mBusy=0).then(d=>{if(d.from==0)mk.path.setLatLngs(d.p);else d.p.forEach(x=>mk.path.addLatLng(x));
tg=d.gen;tn=d.from+d.p.length;if(!mk.shp)mk.shp=L.polyline([],{color:css('bad'),weight:2,dashArray:'6 6'}).addTo(LM);if(d.shpath)mk.shp.setLatLngs(d.shpath);else if(d.shgen==null)mk.shp.setLatLngs([]);shg=d.shgen||0;pt('me',d.me,'accent','me');pt('so',d.sonde,'good','sonde');
pt('pr',d.pred,'pred','T-Deck landing prediction');pt('sh',d.shpred,'bad','SondeHub landing prediction');pt('sv',d.srvpred,'warn',esc(d.srvtag||'Server')+' landing prediction');sonPop(d.links);
if(!fitDone&&(d.sonde||d.me)){fitDone=1;center(1)}else if($('mf').checked)center(0)})}
// Click on the sonde: serial, height, links to SondeHub and radiosondy.info, navigation. Rebuilt only on change.
function sonPop(l){const o=S&&S.sonde;if(!mk.so||!o||!o.have)return;
const h=`<b>${esc(o.id)}</b> ${esc(o.model||'')}<br><span style=color:var(--dim)>${f(o.alt,0)} m, position ${o.age} s old</span><div class=btns>`+
(l&&l.sh?`<a class=btn target=_blank rel=noopener href="https://sondehub.org/${encodeURIComponent(l.sh)}">SondeHub</a>`:'')+
(l&&l.rs?`<a class=btn target=_blank rel=noopener href="https://radiosondy.info/sonde.php?sondenumber=${encodeURIComponent(l.rs)}">radiosondy.info</a>`:'')+nav(o.lat,o.lon)+'</div>';
if(mk.so._ph===h)return;mk.so._ph=h;if(mk.so.getPopup())mk.so.setPopupContent(h);else mk.so.bindPopup(h,{maxWidth:400})}
// Both: everything in view (moves only when something leaves it, unless asked); Me, Sonde: that point in the middle.
function center(now){if(!LM)return;if(mc!='b'){const m=mk[mc];if(m)now?LM.setView(m.getLatLng(),Math.max(LM.getZoom(),13)):LM.panTo(m.getLatLng());return}
const b=[];for(const k of ['me','so','pr','sh','sv'])if(mk[k])b.push(mk[k].getLatLng());mk.path.getLatLngs().forEach(x=>b.push(x));
if(!now&&b.every(x=>LM.getBounds().contains(x)))return;
if(b.length>1)LM.fitBounds(b,{padding:[30,30],maxZoom:15});else if(b.length)LM.setView(b[0],13)}
addEventListener('resize',()=>{if(LM)LM.invalidateSize();if(SD&&tab=='s')drawS();if(tab=='g'){gT=0;gpsLoad()}if(tab=='r'&&lastScan)drawScan(lastScan)});
addEventListener('keydown',e=>{if(e.key=='Escape'&&!$('dlg').hidden)closeDlg()});
let t0='h';try{t0=localStorage.tdTab||'h'}catch(e){}go(TABS.includes(t0)?t0:'h');poll();setInterval(poll,2000);setInterval(bar,1000);
</script>)HTML";

// The page compressed (about 17 KB instead of 49), and not sent again while the browser has this version.
static void webUiPage()
{
    if (web.header("If-None-Match") == WEB_UI_GZ_ETAG) return web.send(304, "text/html", "");
    web.sendHeader("ETag", WEB_UI_GZ_ETAG);
    web.sendHeader("Cache-Control", "no-cache");
    web.sendHeader("Content-Encoding", "gzip");
    webSendBig("text/html", (const char *)WEB_UI_GZ, sizeof(WEB_UI_GZ));
}

static void webRoot()
{
    webSendBig("text/html", WEB_ROOT, sizeof(WEB_ROOT) - 1);
}

// SondeHub's answer as one word for the status line ("-" when none).
static const char *shReplyWord(const char *msg)
{
    static char w[160];
    snprintf(w, sizeof(w), "%s", msg[0] ? msg : "-");
    for (char *p = w; *p; p++)
        if (*p == ' ') *p = '_';
    return w;
}

// Last batch sent to SondeHub and its answer: samples for SondeHub's developers, and what they said.
static void webUploadLast()
{
    String b, r;
    int code = 0;
    if (!uploader_last_upload(b, r, code)) return web.send(200, "text/plain", "nothing sent to SondeHub yet\n");
    web.send(200, "text/plain", "target " + String(uploader_target()) + ", HTTP " + String(code) + "\nanswer:\n" + r +
                                    "\nlast batch:\n" + b + "\n");
}

// Touch diagnosis: every address that answers, or that takes long to say no, then the touch probed again.
// ?power=1 first switches the peripherals off and on (touch, keyboard, radio, GPS: a restart is advised after).
static void webI2c()
{
    String out;
    if (web.hasArg("power")) {
        digitalWrite(BOARD_POWERON, LOW);
        delay(300);
        digitalWrite(BOARD_POWERON, HIGH);
        delay(300);
        input_note_power_cycle();
        out += "peripherals switched off and on\n";
    }
    char b[64];
    for (uint8_t a = 0x08; a < 0x78; a++) {
        uint32_t t0 = micros();
        Wire.beginTransmission(a);
        uint8_t e = Wire.endTransmission();
        uint32_t us = micros() - t0;
        if (e == 0 || us > 3000) {
            snprintf(b, sizeof(b), "0x%02x %s %lu us\n", a, e == 0 ? "answers" : "slow no", (unsigned long)us);
            out += b;
        }
    }
    uint32_t t0 = millis();
    bool ok = input_probe_now();
    InputStats is;
    input_stats(&is);
    snprintf(b, sizeof(b), "touch probe: %s addr=0x%02x %lu ms\n", ok ? "found" : "not found", is.touchAddr,
             (unsigned long)(millis() - t0));
    out += b;
    web.send(200, "text/plain", out);
}

// Polarity setting as /status and /api/state write it.
static const char *polName() { return cfg.autoInvert ? "auto" : cfg.invert ? "inverted" : "normal"; }

// Restart asked from a page: the answer first, a line in the log, then the restart.
static void webRestart(const char *answer, const char *logText)
{
    web.send(200, "text/plain", answer);
    if (logText) logLine(logPath, logText);
    delay(300);
    ESP.restart();
}

// Bench: a restart, to check what is remembered (files on the SD are closed after every write).
static void webReboot() { webRestart("OK rebooting\n", nullptr); }

// Debug pages answer only while the debug server setting is on; with only the Wi-Fi app link on,
// the HTTP server serves /live.json and nothing else.
// /set?freq=403.000&stype=dfm&full=1&pol=auto|normal|inverted&vol=0..9&fvol=0..9 : exact settings for bench scripts.
static void webSet()
{
    String err;
    if ((cfg.shUpload || cfg.aprsUp || cfg.aprsFi) && strcmp(uploader_target(), "mock")) { // bench: never real uploads
        cfg.shUpload = cfg.aprsUp = cfg.aprsFi = false;
        settingDone();
    }
#if PRED_SERVER
    if (cfg.psOn && !(PRED_SERVER_ALLOW_HTTP && !strncmp(cfg.psUrl, "http://", 7))) { // only a test build to its mock
        cfg.psOn = false;
        settingDone();
    }
#endif
    if (web.hasArg("freq")) {
        double f = web.arg("freq").toDouble();
        if (f >= 400.0 && f <= 406.0) cfg.freq = f;
        else err += "freq out of 400-406; ";
    }
    if (web.hasArg("stype")) {
        int t = sonde_type_parse(web.arg("stype").c_str());
        if (t >= 0) cfg.stype = (uint8_t)t;
        else err += "unknown stype; ";
    }
    if (web.hasArg("full")) cfg.fullFrames = web.arg("full").toInt() != 0;
    if (web.hasArg("pol")) {
        String p = web.arg("pol");
        if (p == "auto") cfg.autoInvert = true;
        else if (p == "normal" || p == "inverted") {
            cfg.autoInvert = false;
            cfg.invert = p == "inverted";
        } else err += "pol: auto, normal, inverted; ";
    }
    if (web.hasArg("vol")) cfg.vol = (uint8_t)constrain(web.arg("vol").toInt(), 0, 9);
    if (web.hasArg("fvol")) cfg.fvol = (uint8_t)constrain(web.arg("fvol").toInt(), 0, 9);
    if (web.hasArg("bw")) { // receiver bandwidth in kHz, the nearest of the list (the bench always sets the same)
        float k = web.arg("bw").toFloat();
        int best = -1;
        for (int i = 0; i < Sx1262::BW_COUNT; i++)
            if (Sx1262::BW_KHZ[i] >= 7.0f && (best < 0 || fabsf(Sx1262::BW_KHZ[i] - k) < fabsf(Sx1262::BW_KHZ[best] - k)))
                best = i;
        if (k > 0 && best >= 0) cfg.bwIdx = (uint8_t)best;
        else err += "bw: kHz; ";
    }
    radioChanged();
    cap.nFull320 = cap.nFull518 = cap.nCut = cap.nShort = 0;
    char b[220];
    snprintf(b, sizeof(b), "%sfreq=%.3f stype=%s rx=%s full=%d pol=%s bw=%.1f vol=%d fvol=%d upload=%d aprs=%d\n", err.c_str(),
             cfg.freq, sonde_type_name(cfg.stype), sonde_type_name(rxType), cfg.fullFrames,
             polName(), Sx1262::BW_KHZ[cfg.bwIdx], cfg.vol, cfg.fvol,
             cfg.shUpload, cfg.aprsUp);
    web.send(err.length() ? 400 : 200, "text/plain", b);
}

static void gate(uint8_t need, void (*handler)())
{
    const char *e = gateDenied(need);
    if (!e) return handler();
    if (e == GATE_PASS)
        return web.requestAuthentication(BASIC_AUTH, "T-Deck SondeTracker", "password needed (settings, System tab)\n");
    web.send(403, "text/plain", String(e) + "\n");
}

// ---- Web interface: JSON API for the page at / (same actions as the keys and the settings rows) ----
static char *apiBuf = nullptr;
#define API_BUF 24576

static StrOut apiOut()
{
    if (!apiBuf) apiBuf = (char *)ps_malloc(API_BUF);
    return StrOut(apiBuf, apiBuf ? API_BUF : 0);
}

// ,"kd":metres,"kb":degrees from point 1 to point 2.
static void addDistBrg(StrOut &o, const char *kd, const char *kb, double lat1, double lon1, double lat2, double lon2)
{
    double d, b;
    geo_dist_brg(lat1, lon1, lat2, lon2, &d, &b);
    o.add(",\"%s\":%.0f,\"%s\":%.0f", kd, d, kb, b);
}

static void apiSend(StrOut &o)
{
    if (o.full) return web.send(500, "text/plain", "answer too long\n");
    webSendBig("application/json", apiBuf, o.k);
}

static void apiState()
{
    StrOut o = apiOut();
    const GpsFix &g = nmea.fix;
    bool me = myFixOk();
    o.add("{\"v\":\"" FW_NAME "\",\"sddir\":\"%s\",\"freq\":%.3f,\"rx\":", sdDir, cfg.freq);
    o.str(sonde_type_name(rxType));
    o.add(",\"stype\":%d,\"pol\":\"%s\",\"scan\":%d,\"batt\":%d,\"time\":", cfg.stype,
          polName(), scanning, deckBattPct());
    char clk[12];
    localClock(clk, sizeof(clk), false);
    o.str(clk);
    o.add(",\"sonde\":{\"have\":%d", track.have);
    if (track.have) {
        o.add(",\"id\":");
        o.str(track.manual ? "TARGET" : track.id);
        o.add(",\"model\":");
        o.str(haveLast ? last.model : "");
        o.add(",\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.0f,\"vv\":%.1f,\"vh\":%.1f,\"dir\":%.0f,\"age\":%lu,\"src\":\"%s\"",
              track.lat, track.lon, track.alt, track.vV, track.vH, track.vDir,
              (unsigned long)((millis() - track.ms) / 1000),
              track.manual ? "manual" : track.fromNet ? "net" : track.restored ? "memory" : "radio");
    }
    o.add(",\"rssi\":%.1f,\"frame_age\":%ld,\"batt\":%.1f}", lastRssi,
          lastOkMs ? (long)((millis() - lastOkMs) / 1000) : -1L, haveLast ? last.battV : 0.0f);
    double sdist = -1;
    if (me && track.have) {
        sdist = geo_distance_m(g.lat, g.lon, track.lat, track.lon);
        o.add(",\"dist\":%.0f,\"brg\":%.0f,\"elev\":%.1f", sdist, geo_bearing_deg(g.lat, g.lon, track.lat, track.lon),
              geo_elevation_deg(sdist, track.alt - (g.altM + g.geoidSepM)));
    }
    { // where the T-Deck points (key n), as on the Hunt page
        double tl = track.lat, tn = track.lon, pl0, pn0;
        int e0;
        bool sh0, toPred = navPred && bestPred(&pl0, &pn0, &e0, &sh0);
        if (toPred) {
            tl = pl0;
            tn = pn0;
        }
        o.add(",\"nav\":\"%s\",\"to\":\"%s\",\"srvok\":%d", navSrv ? "srv" : navPred ? "land" : "sonde",
              toPred ? (predSrc == PSRC_SRV ? "srv" : "land") : track.manual ? "target" : "sonde", psPredOk());
        if (me && (track.have || toPred))
            addDistBrg(o, "tdist", "tbrg", g.lat, g.lon, tl, tn);
    }
    {
        char ib[64];
        sondeInfoLine(ib, sizeof(ib));
        o.add(",\"hdg\":%.0f,\"ssats\":%u,\"okpct\":%ld,\"stale\":%d,\"lost\":%.0f,\"info\":",
              ownHeadingOk() ? ownHeading : -1.0, track.sats, stats.sync ? (long)(100 * stats.eccOk / stats.sync) : -1L,
              track.have && trackStale(), signalLostM(sdist));
        o.str(ib);
        o.add(",\"wait\":");
        o.str(track.have ? "" : waitText());
    }
    o.add(",\"setgen\":%lu", (unsigned long)setGen);
    o.add(",\"idle\":%.1f,\"bw\":%.1f,\"locked\":%d,\"mute\":%d,\"tag\":", rssiIdle, Sx1262::BW_KHZ[cfg.bwIdx],
          screenLocked, !cfg.vol && !cfg.fvol);
#if PRED_SERVER
    o.str(predsrv_tag());
#else
    o.str("");
#endif
    double pl, pn;
    int eta;
    bool sh;
    if (bestPred(&pl, &pn, &eta, &sh)) {
        o.add(",\"pred\":{\"lat\":%.6f,\"lon\":%.6f,\"eta\":%d,\"sh\":%d,\"tag\":", pl, pn, eta, sh);
        o.str(predSrc == PSRC_SH ? "SH" : predSrc == PSRC_SRV ? predLabel() : "");
#if PRED_SERVER
        if (predSrc == PSRC_SRV) o.add(",\"conf\":%d,\"m50\":%.1f", psRes.pred.conf, psRes.pred.m50Km);
#endif
        o.add("}");
    }
#if PRED_SERVER
    if (cfg.psOn && psHave && psRes.hunt) {
        o.add(",\"hunt\":");
        o.str(psRes.huntSerial);
    }
#endif
    o.add(",\"gps\":{\"fix\":%d,\"sats\":%u,\"lat\":%.6f,\"lon\":%.6f,\"kmh\":%.1f}", me, g.sats, me ? g.lat : 0.0,
          me ? g.lon : 0.0, me ? g.speedKmh : 0.0f);
    o.add(",\"up\":{\"rs\":%d,\"fi\":%d,\"log\":%d,\"lines\":%lu},\"debug\":%d,\"theme\":%d,\"sd\":%d", cfg.aprsUp,
          cfg.aprsFi, cfg.shLog, (unsigned long)shLogLines, cfg.dbgServer, cfg.theme, sdOk);
    o.add(",\"msg\":");
    o.str(uiMsg[0] && millis() - uiMsgMs < 6000 ? uiMsg : "");
    o.add(",\"ask\":{\"open\":%d,\"l1\":", askKind != ASK_NONE);
    o.str(askLine1);
    o.add(",\"l2\":");
    o.str(askLine2);
    o.add("}}");
    apiSend(o);
}

// The T-Deck theme as CSS colours, so the page looks like the screen.
static void apiTheme()
{
    static const char *const K[] = {"bg", "panel", "line", "label", "text", "dim", "accent",
                                    "good", "warn", "bad", "pos", "pred", "msg"};
    const uint16_t c[] = {th->bg,     th->panel, th->line, th->label, th->text, th->dim, th->accent,
                          th->good,   th->warn,  th->bad,  th->pos,   th->pred, th->msg};
    StrOut o = apiOut();
    o.add("{");
    for (int i = 0; i < 13; i++)
        o.add("%s\"%s\":\"#%02x%02x%02x\"", i ? "," : "", K[i], ((c[i] >> 11) & 31) * 255 / 31,
              ((c[i] >> 5) & 63) * 255 / 63, (c[i] & 31) * 255 / 31);
    o.add("}");
    apiSend(o);
}

// The sonde on SondeHub (serial as it would be uploaded) and on radiosondy.info (APRS object name), for the
// map popup. The track keeps no type: it comes from the shape of the serial; a manual target has no links.
static void trackLinks(StrOut &o)
{
    if (!track.have || track.manual || !track.id[0]) return;
    static const uint8_t T[] = {ST_RS41, ST_DFM, ST_M10, ST_IMET54};
    for (uint8_t t : T) {
        if (!sh_serial_ok(t, track.id)) continue;
        o.add(",\"links\":{\"sh\":");
        o.str(track.id);
        char nm[10];
        if (aprs_object_name(t, track.id, nm)) {
            for (int i = 8; i >= 0 && nm[i] == ' '; i--) nm[i] = 0; // padded to 9 for APRS
            o.add(",\"rs\":");
            o.str(nm);
        }
        o.add("}");
        return;
    }
}

// /api/track?gen=G&n=N: the page already has N points of track generation G, so only the newer ones are
// sent; after a reset or a thinning (another gen) the whole track, at most TRK_MAX points (about 14 KB).
static void apiTrack()
{
    StrOut o = apiOut();
    int from = web.arg("gen").toInt() == trkGen ? web.arg("n").toInt() : 0;
    if (from < 0 || from > nTrk) from = 0;
    o.add("{\"gen\":%u,\"from\":%d,\"p\":[", trkGen, from);
    for (int i = from; i < nTrk; i++) o.add("%s[%.5f,%.5f]", i > from ? "," : "", trkLat[i], trkLon[i]);
    o.add("]");
    if (track.have) o.add(",\"sonde\":[%.6f,%.6f]", track.lat, track.lon);
    trackLinks(o);
    if (myFixOk()) o.add(",\"me\":[%.6f,%.6f]", nmea.fix.lat, nmea.fix.lon);
    if (pred.ok && !track.manual) o.add(",\"pred\":[%.6f,%.6f]", pred.lat, pred.lon);
    const ShPrediction *sp = shPred();
    if (shPredOk() && !sp->landed) {
        uint32_t g = sp->landTime ^ (uint32_t)sp->n; // the page has this path already: not sent again
        o.add(",\"shpred\":[%.6f,%.6f],\"shgen\":%lu", sp->landLat, sp->landLon, (unsigned long)g);
        if ((uint32_t)strtoul(web.arg("shg").c_str(), nullptr, 10) != g) {
            o.add(",\"shpath\":[");
            for (int i = 0; i < sp->n; i++) o.add("%s[%.5f,%.5f]", i ? "," : "", sp->lat[i], sp->lon[i]);
            o.add("]");
        }
    }
#if PRED_SERVER
    if (psPredOk()) {
        o.add(",\"srvpred\":[%.6f,%.6f],\"srvtag\":", psRes.pred.lat, psRes.pred.lon);
        o.str(predsrv_tag());
    }
#endif
    o.add("}");
    apiSend(o);
}

static void apiPresets()
{
    StrOut o = apiOut();
    o.add("{\"cur\":%d,\"p\":[", activePreset());
    for (int i = 0; i < nPlist; i++) {
        o.add("%s{\"f\":%.3f,\"t\":", i ? "," : "", plist[i].khz / 1000.0);
        o.str(sonde_type_name(plist[i].type));
        o.add(",\"n\":");
        o.str(plist[i].name);
        o.add(",\"g\":");
        o.str(plist[i].group);
        o.add("}");
    }
    o.add("]}");
    apiSend(o);
}

// /api/do?a=freq&v=404.5 | step&v=-10 (kHz) | preset&i=N (-1 auto) | type&v=N | scan | stop | ask&yes=1
// | pol&v=auto|normal|inverted | mute | reset | kml | nav&v=sonde|land|srv | target&v=lat lon | psave&n=name
// | pdel&i=N | peu | pnext | lock&v=0|1 | follow&s=SERIAL | unfollow | shtarget&s=SERIAL | shtarget&t=TIME | wdel&i=N
// | reboot. The answer is the T-Deck's message, or OK.
static void apiDo()
{
    String a = web.arg("a"), v = web.arg("v");
    uint32_t msg0 = uiMsgMs;
    if (a == "pol") {
        int m = v == "auto" ? 0 : v == "normal" ? 1 : v == "inverted" ? 2 : -1;
        if (m < 0) return web.send(400, "text/plain", "auto, normal or inverted\n");
        polSet(m);
    } else if (a == "mute") {
        soundToggle();
    } else if (a == "reset") {
        resetStats();
    } else if (a == "kml") {
        if (!track_id_ok(huntId())) return web.send(400, "text/plain", "no sonde received yet\n");
        char p[80];
        snprintf(p, sizeof(p), "%s/%s.kml", trackDir, huntId());
        if (sdOk) SD.remove(p); // an old file must not pass for a new one
        exportKml();
        if (!SD.exists(p)) return web.send(500, "text/plain", String(uiMsg) + "\n");
        return web.send(200, "text/plain", String("KML ") + p + "\n");
    } else if (a == "nav") {
        int m = v == "sonde" ? 0 : v == "land" ? 1 : v == "srv" ? 2 : -1;
        if (m < 0) return web.send(400, "text/plain", "sonde, land or srv\n");
        if (m == 2 && !psPredOk()) return web.send(400, "text/plain", "no server point now\n");
        navSet(m);
    } else if (a == "target") {
        setTarget(v);
    } else if (a == "psave") {
        savePresetNamed(web.arg("n")); // asks Yes/No: the page shows the question
    } else if (a == "pdel") {
        int i = web.arg("i").toInt();
        if (i < 0 || i >= nPlist) return web.send(400, "text/plain", "no such preset\n");
        deletePresetAsk(i);
    } else if (a == "peu") {
        addDefaultPresets();
    } else if (a == "pnext") {
        nextPreset();
    } else if (a == "lock") {
        setLocked(v == "1");
    } else if (a == "follow") {
        String q = web.arg("s");
        int i = 0;
        while (i < netr->nSondes && q != netr->sondes[i].serial) i++;
        if (i == netr->nSondes) return web.send(400, "text/plain", "sonde not in the SondeHub list now\n");
        followSonde(netr->sondes[i]);
    } else if (a == "wdel") {
        int i = web.arg("i").toInt();
        if (i < 0 || i >= nWifiSaved) return web.send(400, "text/plain", "no such network\n");
        savedDeleteAsk(i + 1); // asks Yes/No: the page shows the question
    } else if (a == "unfollow") {
        unfollowSonde();
    } else if (a == "shtarget") {
        if (web.hasArg("t")) {
            int i = launchIndex((uint32_t)strtoul(web.arg("t").c_str(), nullptr, 10));
            if (i < 0) return web.send(400, "text/plain", "launch no longer in the list\n");
            targetLaunch(i);
        } else {
            const ShSummary &x = netr->summary;
            if (!x.ok || web.arg("s") != netr->summaryFor) return web.send(400, "text/plain", "no data for this serial yet\n");
            targetSummary(x);
        }
    } else if (a == "reboot") {
        webRestart("restarting\n", "# restart from the web page");
    } else if (a == "freq") {
        double f = web.arg("v").toDouble();
        if (f < 400.0 || f > 406.0) return web.send(400, "text/plain", "400-406 MHz\n");
        setFreq(f, true);
    } else if (a == "step") {
        changeFreq(web.arg("v").toInt() / 1000.0);
    } else if (a == "preset") {
        int i = web.arg("i").toInt();
        if (i == -1) presetsAuto();
        else if (i >= 0 && i < nPlist) usePreset(i);
        else return web.send(400, "text/plain", "no such preset\n");
    } else if (a == "type") {
        int t = web.arg("v").toInt();
        if (t < 0 || t >= ST_COUNT) return web.send(400, "text/plain", "no such type\n");
        cfg.stype = (uint8_t)t;
        radioChanged();
        setMsgf("sonde type: %s", t == ST_AUTO ? "auto" : sonde_type_name(t));
    } else if (a == "scan") {
        if (!scanning) scanStart();
    } else if (a == "stop") {
        if (scanning) {
            scanFinish(false);
            setMsg("scan stopped");
        }
    } else if (a == "ask") {
        if (askKind != ASK_NONE) askAnswer(web.arg("yes") == "1");
    } else {
        return web.send(400, "text/plain", "unknown action\n");
    }
    drawUi();
    web.send(200, "text/plain", uiMsgMs != msg0 && uiMsg[0] ? String(uiMsg) + "\n" : String("OK\n"));
}

// SondeHub data for the web page, without touching the T-Deck's own SondeHub page:
// /api/sh?l=near | landed&p=0..3 | launches | summary&s=SERIAL. Each call keeps that query alive (90 s).
static void shRow(StrOut &o, const ShSonde &x, double rl, double rn, bool ref, long now)
{
    o.add("{\"s\":");
    o.str(x.serial);
    o.add(",\"t\":");
    o.str(x.type);
    o.add(",\"lat\":%.5f,\"lon\":%.5f,\"alt\":%.0f,\"vv\":%.1f,\"vh\":%.1f,\"f\":%.3f,\"age\":%ld",
          x.lat, x.lon, x.alt, x.velV, x.velH, x.freqMhz, now && x.time ? now - (long)x.time : -1L);
    if (ref) addDistBrg(o, "d", "b", rl, rn, x.lat, x.lon);
    o.add("}");
}

static void apiSh()
{
    String l = web.arg("l");
    StrOut o = apiOut();
    double rl, rn;
    bool ref = refPoint(&rl, &rn);
    long now = netTimeOk() ? (long)time(nullptr) : 0;
    o.add("{\"net\":%d,\"time\":%d,\"follow\":", cfg.net, netTimeOk());
    o.str(followId());
    if (l == "near" || l == "landed") {
        bool landed = l == "landed";
        if (landed) {
            int p = web.arg("p").toInt();
            if (p >= 0 && p < 4) webLandedIdx = p;
            webLandedMs = millis();
        } else {
            webNearMs = millis();
        }
        const ShSonde *arr = landed ? netr->landed : netr->sondes;
        int n = landed ? netr->nLanded : netr->nSondes;
        uint32_t ms = landed ? netr->landedMs : netr->sondesMs;
        bool ready = ms && (!landed || netr->landedLast == LANDED_PERIOD_S[webLandedIdx]);
        o.add(",\"ready\":%d,\"age\":%ld,\"p\":%d,\"rows\":[", ready, ms ? (long)((millis() - ms) / 1000) : -1L,
              webLandedIdx);
        static int16_t ord[NET_MAX_LANDED];
        static float dist[NET_MAX_LANDED];
        int k = 0;
        for (int i = 0; ready && i < n; i++) {
            if (landed && !shLanded(arr[i], now)) continue;
            float d = ref ? (float)geo_distance_m(rl, rn, arr[i].lat, arr[i].lon) : 0;
            int j = k++;
            for (; j > 0 && dist[j - 1] > d; j--) { // nearest first
                ord[j] = ord[j - 1];
                dist[j] = dist[j - 1];
            }
            ord[j] = (int16_t)i;
            dist[j] = d;
        }
        for (int j = 0; j < k; j++) {
            if (j) o.add(",");
            shRow(o, arr[ord[j]], rl, rn, ref, now);
        }
        o.add("]");
    } else if (l == "launches") {
        webLaunchMs = millis();
        o.add(",\"ready\":%d,\"rows\":[", netr->nSites > 0);
        for (int i = 0; i < netr->nLaunch; i++) {
            const NetLaunch &x = netr->launch[i];
            const ShSite &st = netr->sites[x.site];
            o.add("%s{\"i\":%d,\"site\":", i ? "," : "", i);
            o.str(st.name[0] ? st.name : st.id);
            o.add(",\"time\":%lu,\"km\":%.0f,\"f\":%.3f,\"burst\":%.0f,\"asc\":%.1f,\"desc\":%.1f",
                  (unsigned long)x.time, st.distKm, st.freqMhz, st.burst, st.ascent, st.descent);
            if (x.pred.ok)
                o.add(",\"land\":[%.5f,%.5f,%lu]", x.pred.landLat, x.pred.landLon, (unsigned long)x.pred.landTime);
            o.add(",\"tried\":%d}", x.tried);
        }
        o.add("]");
    } else if (l == "summary") {
        String q = web.arg("s");
        q.trim();
        q.toUpperCase();
        bool okSer = q.length() && q.length() <= 11;
        for (char c : q) okSer = okSer && (isalnum((unsigned char)c) || c == '-');
        if (!okSer) return web.send(400, "text/plain", "serial, e.g. X1234567\n");
        if (strcmp(webSummary, q.c_str())) snprintf(webSummary, sizeof(webSummary), "%s", q.c_str());
        webSumMs = millis();
        const ShSummary &x = netr->summary;
        bool ready = !strcmp(netr->summaryFor, webSummary);
        o.add(",\"ready\":%d,\"found\":%d", ready, ready && x.ok);
        if (ready && x.ok) {
            o.add(",\"s\":");
            o.str(x.serial);
            o.add(",\"t\":");
            o.str(x.type);
            o.add(",\"site\":");
            o.str(x.site);
            o.add(",\"f\":%.3f,\"rec\":%d", x.freqMhz, x.recovered);
            if (x.haveLaunch) o.add(",\"launch\":[%.5f,%.5f,%lu]", x.launchLat, x.launchLon, (unsigned long)x.launchTime);
            if (x.haveBurst) o.add(",\"burst\":%.0f", x.burstAlt);
            if (x.haveLast)
                o.add(",\"last\":[%.5f,%.5f,%.0f,%.1f,%lu]", x.lastLat, x.lastLon, x.lastAlt, x.lastVelV,
                      (unsigned long)x.lastTime);
            if (x.havePredLand) o.add(",\"land\":[%.5f,%.5f]", x.landLat, x.landLon);
            if (ref && x.haveLast)
                addDistBrg(o, "d", "b", rl, rn, x.lastLat, x.lastLon);
        }
    } else {
        return web.send(400, "text/plain", "near, landed, launches or summary\n");
    }
    o.add("}");
    apiSend(o);
}

// Signal page of the web: the idle level (sampled after each screen redraw), the last frame and the distance.
static void apiLive()
{
    StrOut o = apiOut();
    o.add("{\"idle\":%.1f,\"last\":%.1f,\"fage\":%ld", rssiIdle, haveLast ? lastRssi : 0.0f,
          lastOkMs ? (long)((millis() - lastOkMs) / 1000) : -1L);
    if (myFixOk() && track.have)
        addDistBrg(o, "dist", "brg", nmea.fix.lat, nmea.fix.lon, track.lat, track.lon);
    o.add(",\"gage\":%ld}", track.have ? (long)((millis() - track.ms) / 1000) : -1L);
    apiSend(o);
}

// GPS page of the web: fix, precision, satellites of every constellation, position, time, aid.
static void apiGps()
{
    StrOut o = apiOut();
    const GpsFix &g = nmea.fix;
    bool fix = myFixOk();
    char clk[12];
    localClock(clk, sizeof(clk), true);
    o.add("{\"seen\":%d,\"fix\":%d,\"type\":%u,\"used\":%u,\"hdop\":%.1f,\"pdop\":%.1f,\"first\":%lu,\"up\":%lu",
          gpsSeen, fix, g.fixType, g.sats, g.hdop, g.pdop, (unsigned long)(firstFixMs / 1000),
          (unsigned long)(millis() / 1000));
    if (fix) o.add(",\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.0f,\"kmh\":%.1f,\"crs\":%.0f", g.lat, g.lon, g.altM, g.speedKmh, g.courseDeg);
    if (myPosSaved) o.add(",\"saved\":[%.4f,%.4f]", myLat, myLon);
    o.add(",\"clock\":");
    o.str(clk);
    o.add(",\"utc\":%d,\"aid\":", !cfg.tzMin && cfg.dst == TZ_DST_OFF);
    o.str(aidSent ? aidText : gpsBaud != 38400 ? "not for this GPS model" : firstFixMs ? "not needed" : "waiting for time");
    o.add(",\"sv\":[");
    static const char SYS[GNSS_COUNT] = {'G', 'R', 'E', 'C', 'J'};
    bool first = true;
    for (int k = 0; k < GNSS_COUNT; k++)
        for (int i = 0; i < nmea.nSv[k]; i++) {
            const SatInView &v = nmea.sv[k][i];
            o.add("%s[\"%c\",%u,%d,%u,%u]", first ? "" : ",", SYS[k], v.prn, v.elev, v.az, v.snr);
            first = false;
        }
    o.add("]}");
    apiSend(o);
}

// Info page of the web: the same lines as on the T-Deck, with their colour.
static void apiInfo()
{
    StrOut o = apiOut();
    o.add("{\"l\":[");
    int n = 0;
    diagLines([&](int row, uint16_t col, const String &t) {
        const char *k = col == UI_GOOD ? "good" : col == UI_MSG ? "msg" : col == UI_ACCENT ? "on" : col == UI_WARN ? "warn"
                        : col == UI_LABEL ? "label" : col == UI_TEXT ? "text" : "dim";
        o.add("%s[%d,\"%s\",", n++ ? "," : "", row, k);
        o.str(t.c_str());
        o.add("]");
    });
    o.add("]}");
    apiSend(o);
}

// Scan progress for the web page: both sweeps as hex bytes (dBm + 160), candidates, phase.
static void apiScan()
{
    StrOut o = apiOut();
    o.add("{\"on\":%d,\"phase\":%d,\"pos\":%d,\"bins\":%d,\"round\":%d,\"noise\":%d,\"np\":%d,\"s\":%lu",
          scanning, scanPhase, scanPos, SCAN_BINS, scanRound, scanNoise, nPlist,
          scanning ? (unsigned long)((millis() - scanStartMs) / 1000) : 0UL);
    for (int k = 0; k < 2; k++) {
        const int8_t *a = k ? scanHold : scanRssi;
        o.add(k ? "\",\"h\":\"" : ",\"r\":\"");
        for (int i = 0; i < SCAN_BINS; i++) o.add("%02x", (unsigned)constrain(a[i] + 160, 0, 255));
    }
    o.add("\",\"c\":[");
    for (int i = 0; i < scanNCand; i++)
        o.add("%s[%lu,%d,%d,%d]", i ? "," : "", (unsigned long)scanCandKhz[i], scanCandRssi[i], scanCandType[i],
              i == scanIdx && scanPhase >= SCAN_TRY);
    o.add("]}");
    apiSend(o);
}

// Rows the page handles itself: text fields, and lists that on the T-Deck open over the settings.
static const char *settingKind(int item)
{
    switch (item) {
    case 2: case 3: case 4: case 28: case 39: return "text";
#if PRED_SERVER
    case 45: case 46: return "text";
#endif
    case 12: case 25: case 32: return "list";
    case 19: return "radio";
    case 20: case 14: return "info";
    default: return "action";
    }
}

// Colour of a settings value as the class of the web page (.k-...): the same names as the CSS there.
static const char *colorClass(uint16_t c)
{
    const struct { // not static: the theme can change
        uint16_t col;
        const char *cls;
    } M[] = {{UI_ACCENT, "on"}, {UI_WARN, "warn"}, {UI_BAD, "bad"}, {UI_GOOD, "good"}, {UI_POS, "pos"},
             {UI_MSG, "msg"},   {UI_LINE, "na"},   {UI_LABEL, "label"}, {UI_TEXT, "text"}};
    for (const auto &m : M)
        if (c == m.col) return m.cls;
    return "dim";
}

static void apiSettings()
{
    StrOut o = apiOut();
    static const char *vals[SET_ROWS];
    static char store[SET_ROWS][40];
    static uint16_t cols[SET_ROWS];
    for (int i = 0; i < SET_ROWS; i++) vals[i] = nullptr;
    settingsValues([](int i, uint16_t col, const char *t) {
        if (i < 0 || i >= SET_ROWS) return;
        snprintf(store[i], sizeof(store[i]), "%s", t);
        vals[i] = store[i];
        cols[i] = col;
    });
    o.add("{\"tabs\":[");
    for (int t = 0; t < SET_TABS; t++) {
        o.add("%s{\"name\":", t ? "," : "");
        o.str(SET_TAB_NAME[t]);
        o.add(",\"rows\":[");
        for (int r = 0; r < SET_TAB_N[t]; r++) {
            int it = SET_TAB_ITEMS[t][r];
            o.add("%s{\"i\":%d,\"name\":", r ? "," : "", it);
            o.str(SET_NAME[it]);
            o.add(",\"val\":");
            o.str(vals[it] ? vals[it] : "");
            o.add(",\"c\":\"%s\",\"k\":\"%s\"}", colorClass(cols[it]), settingKind(it));
        }
        o.add("]}");
    }
    o.add("]}");
    apiSend(o);
}

// /api/setting?item=N[&text=...]: the same as pressing the row on the T-Deck, or typing the field.
// A row of this build: in one of the tabs (rows 44-46 exist only with the prediction server).
static bool setRowExists(int item)
{
    for (int t = 0; t < SET_TABS; t++)
        for (int r = 0; r < SET_TAB_N[t]; r++)
            if (SET_TAB_ITEMS[t][r] == item) return true;
    return false;
}

static void apiSetting()
{
    int item = web.arg("item").toInt();
    if (!setRowExists(item)) return web.send(400, "text/plain", "no such row\n");
    uint32_t msg0 = uiMsgMs; // the T-Deck's message about this row goes back to the page
    const char *k = settingKind(item);
    if (!strcmp(k, "text")) {
        if (!web.hasArg("text")) return web.send(400, "text/plain", "text needed\n");
        String t = web.arg("text");
        if (item == 28) {
            setAprsPass(t);
        } else {
            editKind = item == 39 ? 8 : item == 45 ? 9 : item == 46 ? 10 : (uint8_t)item;
            textInput = t;
            applyText();
            textEditing = false;
        }
    } else if (!strcmp(k, "action")) {
        bool info = shInfoOpen;
        settingsAction(item);
        if (shInfoOpen && !info) { // the SondeHub explanation: shown by the page, not on the T-Deck
            shInfoOpen = false;
            drawUi();
            return web.send(200, "text/plain", "SHINFO\n");
        }
    } else {
        return web.send(400, "text/plain", "handled by the page\n");
    }
    drawUi();
    web.send(200, "text/plain", uiMsgMs != msg0 && uiMsg[0] ? String(uiMsg) + "\n" : String("OK\n"));
}

// /api/list?l=tz | wifi[&scan=1] | saved, and &pick=N to choose a row.
static void apiList()
{
    String l = web.arg("l");
    bool pick = web.hasArg("pick");
    int n = web.arg("pick").toInt();
    StrOut o = apiOut();
    if (l == "tz") {
        if (pick) {
            tzPick(n);
            return web.send(200, "text/plain", "OK\n");
        }
        o.add("{\"cur\":%d,\"rows\":[", tz_zone_index(cfg.tzMin));
        for (int i = 0; i < TZ_ZONE_COUNT; i++) {
            char z[12];
            tz_offset_name(TZ_ZONES[i].min, z, sizeof(z));
            char b[80];
            snprintf(b, sizeof(b), "%s  %s", z, TZ_ZONES[i].places);
            o.add("%s", i ? "," : "");
            o.str(b);
        }
        o.add("]}");
    } else if (l == "wifi") {
        if (web.hasArg("scan")) {
            uploader_scan_start();
            return web.send(200, "text/plain", "OK\n");
        }
        if (pick) {
            String pw = web.arg("pass");
            if (pw.length() > 63) return web.send(400, "text/plain", "password: up to 63 characters\n");
            for (char c : pw)
                if ((uint8_t)c < 0x20) return web.send(400, "text/plain", "password: no control characters\n");
            if (!wifiPick(n, false, web.hasArg("pass") ? pw.c_str() : nullptr))
                return web.send(400, "text/plain", "no such network\n");
            return web.send(200, "text/plain", "OK\n");
        }
        int c = uploader_scan_result(wifiNets, WIFI_SCAN_MAX);
        o.add("{\"state\":%d,\"rows\":[", c < 0 ? c : 0);
        for (int i = 0; i < c; i++) {
            o.add("%s{\"n\":", i ? "," : "");
            o.str(wifiNets[i].ssid);
            o.add(",\"rssi\":%d,\"lock\":%d}", wifiNets[i].rssi, wifiNets[i].secure);
        }
        o.add("]}");
    } else if (l == "saved") {
        if (pick) {
            savedPick(n); // 0: keep the network in use (asks first), else that saved network
            return web.send(200, "text/plain", "OK\n");
        }
        wifiSavedLoad();
        o.add("{\"ssid\":");
        o.str(cfg.ssid);
        o.add(",\"rows\":[");
        for (int i = 0; i < nWifiSaved; i++) {
            o.add("%s", i ? "," : "");
            o.str(wifiSaved[i].ssid);
        }
        o.add("]}");
    } else {
        return web.send(400, "text/plain", "l: tz, wifi, saved\n");
    }
    apiSend(o);
}

static void webUiPage();

// Starts the server once Wi-Fi is up, stops it when the settings are turned off.
static void pollDebugServer()
{
    bool want = (cfg.dbgServer || cfg.webUi || cfg.apps == 2) && WiFi.status() == WL_CONNECTED;
    static bool routes = false;
    static bool announced = false;
    if (want && !webRunning) {
        if (!routes) { // register once: begin/stop may repeat when the setting is toggled
            web.on("/", [] {
                if (!cfg.webUi && cfg.dbgServer) { // only the debug pages are on
                    web.sendHeader("Location", "/debug");
                    return web.send(302, "text/plain", "");
                }
                gate(GATE_WEB, webUiPage);
            });
            web.on("/debug", [] { gate(GATE_DEBUG, webRoot); });
            web.on("/api/state", [] { gate(GATE_WEB, apiState); });
            web.on("/api/track", [] { gate(GATE_WEB, apiTrack); });
            web.on("/api/theme", [] { gate(GATE_FILES, apiTheme); }); // also the debug page
            web.on("/api/presets", [] { gate(GATE_WEB, apiPresets); });
            web.on("/api/scan", [] { gate(GATE_WEB, apiScan); });
            web.on("/api/sh", [] { gate(GATE_WEB, apiSh); });
            web.on("/api/live", [] { gate(GATE_WEB, apiLive); });
            web.on("/api/gps", [] { gate(GATE_WEB, apiGps); });
            web.on("/api/info", [] { gate(GATE_WEB, apiInfo); });
            web.on("/api/settings", [] { gate(GATE_WEB, apiSettings); });
            web.on("/api/do", HTTP_POST, [] { gate(GATE_WEB | GATE_ACT, apiDo); });
            web.on("/api/setting", HTTP_POST, [] { gate(GATE_WEB | GATE_ACT, apiSetting); });
            web.on("/api/list", [] { // reading a list is free, choosing a row or scanning acts
                gate(GATE_WEB | (web.hasArg("pick") || web.hasArg("scan") ? GATE_ACT : 0), apiList);
            });
            web.on("/status", [] { gate(GATE_DEBUG, webStatus); });
            web.on("/log", [] { gate(GATE_DEBUG, webLog); });
            web.on("/shot.bmp", [] { gate(GATE_DEBUG, webShot); });
            web.on("/upload_last", [] { gate(GATE_DEBUG, webUploadLast); });
            web.on("/sdhash", [] { gate(GATE_DEBUG, webSdHash); });
            web.on("/key", [] { gate(GATE_DEBUG | GATE_ACT, webKey); });
            web.on("/set", [] { gate(GATE_DEBUG | GATE_ACT, webSet); });
            web.on("/touch", [] { gate(GATE_DEBUG | GATE_ACT, webTouch); });
            web.on("/reboot", [] { gate(GATE_DEBUG | GATE_ACT, webReboot); });
            web.on("/i2c", [] { gate(GATE_DEBUG | GATE_ACT, webI2c); });
            web.on("/files", [] { gate(GATE_FILES, webFiles); });
            web.on("/dl", [] { gate(GATE_FILES, webDownload); });
            web.on("/upload", HTTP_POST, [] { gate(GATE_FILES, webUploadDone); }, webUploadData);
            web.on("/rename", HTTP_POST, [] { gate(GATE_FILES | GATE_ACT, webRename); });
            web.on("/delete", HTTP_POST, [] { gate(GATE_FILES | GATE_ACT, webDelete); });
            web.on("/live.json", webLiveJson);
            routes = true;
        }
        web.begin();
        static const char *hdr[] = {"X-TDeck", "Sec-Fetch-Site", "If-None-Match"}; // after begin(), which resets it
        web.collectHeaders(hdr, 3);
        webRunning = true;
        announced = false;
    }
    if (webRunning && (cfg.dbgServer || cfg.webUi) && !announced) {
        announced = true;
        String ip = WiFi.localIP().toString();
        logLine(logPath, (String("# web server on http://") + ip + "/").c_str());
        setMsg((String(cfg.webUi ? "web: http://" : "debug: http://") + ip + (cfg.webUi ? "/" : "/debug")).c_str());
    }
    if (!cfg.dbgServer && !cfg.webUi) announced = false;
    if (!want && webRunning) {
        web.stop();
        webRunning = false;
    }
    if (webRunning) web.handleClient();
}

static uint32_t splashMs = 0;

// The folder of this build and the paths in it (public and private builds have their own, created when missing).
static void sdLayout()
{
#if PRED_SERVER
    snprintf(sdDir, sizeof(sdDir), "%s", predsrv_sd_dir());
#endif
    struct {
        char *p;
        size_t n;
        const char *name;
    } paths[] = {{logPath, sizeof(logPath), "log.txt"},
                 {rawPath, sizeof(rawPath), "raw.txt"},
                 {trackDir, sizeof(trackDir), "sonde"},
                 {shLogDir, sizeof(shLogDir), "sondehub"},
                 {fieldPath, sizeof(fieldPath), "field_debug.txt"},
                 {aprsfiPath, sizeof(aprsfiPath), "aprsfi.txt"},
                 {gpsdbPath, sizeof(gpsdbPath), "gpsdb.ubx"},
                 {presetPath, sizeof(presetPath), "presets.txt"},
                 {wifiPath, sizeof(wifiPath), "wifi.txt"}};
    for (auto &x : paths) snprintf(x.p, x.n, "%s/%s", sdDir, x.name);
    if (sdOk && !SD.exists(sdDir)) SD.mkdir(sdDir);
}

static void drawSplash()
{
    // fillSprite: on a sprite fillScreen uses the 240 px portrait width.
    gfx.fillSprite(UI_BG);
    // Balloon, string and sonde; the build with the prediction server has its own title and colours.
#if PRED_SERVER
    const char *title = predsrv_boot_title();
    uint16_t balloon = predsrv_boot_balloon(), sonde = predsrv_boot_sonde();
#else
    const char *title = "T-DECK SONDETRACKER";
    uint16_t balloon = UI_DIM, sonde = UI_ACCENT;
#endif
    gfx.fillCircle(160, 70, 34, balloon);
    gfx.fillCircle(149, 58, 9, TFT_WHITE);
    gfx.fillTriangle(154, 102, 166, 102, 160, 110, balloon);
    gfx.drawLine(160, 110, 160, 140, UI_LABEL);
    gfx.fillRoundRect(152, 140, 16, 12, 2, sonde);
    gfx.setTextDatum(TC_DATUM);
    gfx.setTextPadding(0);
    gfx.setTextColor(UI_TEXT, UI_BG);
    gfx.drawString(title, 160, 166, 4);
    gfx.setTextColor(UI_LABEL, UI_BG);
    gfx.drawString("Radiosonde tracker   v" FW_NAME, 160, 198, 2);
    gfx.setTextDatum(TL_DATUM);
    pushCanvas();
    splashMs = millis();
}

// ---- SD card speed ----
// Try faster clocks first and keep the
// first one that reads a known file twice with the same content; 800 kHz stays the last resort.

static uint32_t sdChecksum(const char *path, size_t *len)
{
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    static uint8_t b[512];
    uint32_t sum = 0x811C9DC5;
    *len = 0;
    int n;
    while ((n = f.read(b, sizeof(b))) > 0) {
        for (int i = 0; i < n; i++) sum = (sum ^ b[i]) * 16777619u;
        *len += n;
        if (!(*len & 0xFFFF)) esp_task_wdt_reset(); // a large file (firmware, log) takes longer than the watchdog
    }
    f.close();
    return sum;
}

static bool sdReadTest()
{
    if (SD.cardType() == CARD_NONE) return false;
    const char *path = SD.exists("/maps/0/0/0.png") ? "/maps/0/0/0.png" : SD.exists("/maps/center.txt") ? "/maps/center.txt" : nullptr;
    if (!path) {
        File root = SD.open("/");
        bool ok = root && root.isDirectory();
        if (root) root.close();
        return ok;
    }
    size_t l1 = 0, l2 = 0;
    uint32_t a = sdChecksum(path, &l1), b = sdChecksum(path, &l2);
    return l1 > 0 && l1 == l2 && a == b;
}

static bool sdStart()
{
    // 20 MHz hangs some tile reads: 10 MHz is the ceiling.
    static const uint32_t HZ[] = {10000000U, 4000000U, 800000U};
    for (uint32_t hz : HZ) {
        if (SD.begin(BOARD_SDCARD_CS, SPI, hz)) {
            if (hz == 800000U || sdReadTest()) {
                sdHz = hz;
                Serial.printf("# sd: %lu Hz\n", (unsigned long)hz);
                return true;
            }
        }
        SD.end();
    }
    return false;
}

// Shows the start-up step on the splash and on USB: if start-up ever hangs, the screen says where.
static void bootStep(const char *what)
{
    gfx.fillRect(0, 222, 320, 18, UI_BG);
    gfx.setTextDatum(TC_DATUM);
    gfx.setTextPadding(0);
    gfx.setTextColor(UI_LABEL, UI_BG);
    gfx.drawString(what, 160, 224, 2);
    gfx.setTextDatum(TL_DATUM);
    // Start-up steps run right after heavy SD and radio work: a frame was seen with its right part
    // missing (after the Launcher). Sending it twice costs 36 ms per step, only at power-on.
    pushCanvas();
    pushCanvas();
    Serial.printf("# boot: %s\n", what);
}

// HTTPS buffers (mbedTLS, about 40 KB a session) in PSRAM instead of the small internal RAM: with them inside,
// a session every 30 s left 2 KB free and the Wi-Fi stopped receiving (25/11/2026, 162 rejoins in a night).
static void *tlsCalloc(size_t n, size_t size)
{
    void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void tlsFree(void *p) { heap_caps_free(p); }

// Large tables in PSRAM, not in the small internal RAM the Wi-Fi and HTTPS need (about 18 KB moved).
static void psramArrays()
{
    trkLat = (float *)ps_calloc(TRK_MAX, sizeof(float));
    trkLon = (float *)ps_calloc(TRK_MAX, sizeof(float));
    plist = (Preset *)ps_calloc(PRESET_MAX, sizeof(Preset));
    wifiSaved = (SavedNet *)ps_calloc(WIFI_SAVED_MAX, sizeof(SavedNet));
}

void setup()
{
    mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree); // before any HTTPS
    psramArrays();
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    // After a warm restart (the Launcher, /reboot, a crash) the GT911 touch controller can stay stuck, and
    // only a power cycle brings it back (seen in v0.55): then the peripherals are switched off and on here,
    // before any of them is started. A power-on start never needs it.
    pinMode(BOARD_TOUCH_INT, INPUT);
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    Wire.setTimeOut(20); // ms: a device that does not answer must not stall the loop
    if (esp_reset_reason() != ESP_RST_POWERON && !input_touch_answers()) {
        digitalWrite(BOARD_POWERON, LOW);
        delay(300);
        digitalWrite(BOARD_POWERON, HIGH);
        delay(300); // touch controller and keyboard start again
        input_note_power_cycle();
    }
    pinMode(BOARD_SDCARD_CS, OUTPUT);
    pinMode(RADIO_CS_PIN, OUTPUT);
    pinMode(BOARD_TFT_CS, OUTPUT);
    digitalWrite(BOARD_SDCARD_CS, HIGH);
    digitalWrite(RADIO_CS_PIN, HIGH);
    digitalWrite(BOARD_TFT_CS, HIGH);
    pinMode(BOARD_SPI_MISO, INPUT_PULLUP);

    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
    Serial.setTxTimeoutMs(0);
#endif
    delay(200);

    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);
    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(UI_BG);
    gfx.setColorDepth(16);
    if (!gfx.createSprite(320, 240)) {
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.drawString("Not enough memory for the screen canvas (PSRAM?)", 4, 110, 2);
        while (true) delay(1000);
    }
    pinMode(BOARD_BL_PIN, OUTPUT);
    // A launcher may have left the backlight at another level: switch off (resets the driver), then on.
    blSet(0);
    blSet(BL_STEPS);
    drawSplash();

    bootStep("SD card...");
    sdOk = sdStart();
    sdLayout();

    Serial1.setRxBufferSize(4096); // GPS keeps talking while a frame capture blocks the loop
    Serial1.begin(gpsBaud, SERIAL_8N1, BOARD_GPS_RX_PIN, BOARD_GPS_TX_PIN);

    bootStep("keyboard...");
    Wire.beginTransmission(KB_ADDR); // Wire started at the top of setup
    kbOk = Wire.endTransmission() == 0;
    bootStep("trackball and touch...");
    input_begin();

    bootStep("settings...");
    rs_init();
    loadSettings();
    loadPresets();
    wifiSavedLoad();
    aprsFiLoad();
    th = &THEMES[cfg.theme];
    blSet(BL_LEVEL[cfg.bright]);
    loadMyPos();
    input_set_touch(cfg.touch);
    input_set_trackball_step(TB_EDGES[cfg.tbSpeed]);
    loadLastPos();
    bootStep("sound and Wi-Fi task...");
    audio_begin();
    uploader_begin();
#if PRED_SERVER
    predsrv_begin();
#endif
    netr = (NetResult *)ps_calloc(1, sizeof(NetResult));
    loadShPred();
    netsvc_begin();
    pushUploaderCfg();
    bootStep("map...");
    mapOk = mapview_begin(&tft, sdOk);
    bootStep("radio...");
    radioOk = radio.begin(1.8f);
    char b[160];
    static const char *const RST[] = {"unknown", "power-on", "ext", "software", "panic", "int-wdt", "task-wdt", "wdt",
                                      "deep-sleep", "brownout", "sdio"};
    esp_reset_reason_t rr = esp_reset_reason();
    const char *why = (unsigned)rr < sizeof(RST) / sizeof(RST[0]) ? RST[rr] : "?";
    snprintf(b, sizeof(b), "# boot v%s radio=%d status=%02x derr=%04x sd=%d sdhz=%lu kb=%d reset=%s", FW_NAME,
             radioOk, radio.status(), radio.deviceErrors(), sdOk, (unsigned long)sdHz, kbOk, why);
    logLine(logPath, b);
    // Stopped by a watchdog or a crash: say what it was doing (breadcrumb kept in RTC memory).
    if (crumbMagic == CRUMB_MAGIC &&
        (rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT || rr == ESP_RST_PANIC)) {
        crumbText[sizeof(crumbText) - 1] = 0;
        snprintf(lastRunNote, sizeof(lastRunNote), "%s during: %s", why, crumbText);
        snprintf(b, sizeof(b), "# previous run stopped (%s)", lastRunNote);
        logLine(logPath, b);
    }
    crumb("start");
    mapview_crumb = crumbTile;
    applyRadio();
    bootStep("ready");
    while (millis() - splashMs < 1500) delay(10);
    if (lastRunNote[0]) setMsg("restarted after a hang: see log");
    drawUi();
    // From here a main loop stuck for LOOP_WDT_S seconds restarts the T-Deck.
    esp_task_wdt_init(LOOP_WDT_S, true);
    esp_task_wdt_add(nullptr);
}

void loop()
{
    loopSeq++;
    esp_task_wdt_reset();
    crumb("gps");
    pollGps();
    audio_poll();
    crumb("radio");
    if (radioOk && !scanMeasuring() && cap.poll(capRes)) {
        crumb("frame");
        handleCapture(capRes);
        // Tile decoding blocks; right after a frame there is about a second before the next one must be read.
        if (screen == SCR_MAP && mapOk && !scanning && mapview_service()) mapview_service();
        crumb("keys");
        pollKeys(); // frames back to back leave no idle slot for the UI timer
        crumb("draw after frame");
        drawUi();
        lastUi = millis();
        lastInvertToggle = millis();
        return;
    }
    if (cap.busy()) return;
    crumb("console");
    pollConsole();
    crumb("web");
    pollDebugServer();
    crumb("apps");
    appsPoll();
    crumb("net");
    netPoll();
    crumb("gps aid");
    gpsAidPoll();
    crumb("net app");
    netAppPoll();
    predServerPoll();
    static uint32_t usMs = 0;
    if (millis() - usMs >= 250) { // SondeHub's answer and Wi-Fi events in the log (status copied 4 times a second)
        usMs = millis();
        static uint32_t seenSeq = 0;
        UploaderStatus us;
        uploader_status(&us);
        if (us.shReplySeq != seenSeq) {
            seenSeq = us.shReplySeq;
            char b[200];
            snprintf(b, sizeof(b), "# sondehub %d: %s", us.shReplyCode, us.shReply[0] ? us.shReply : "ok");
            logLine(logPath, b);
        }
        static uint32_t seenDrops = 0, seenResets = 0;
        static bool wasUp = false;
        if (us.wifiDrops != seenDrops) { // Wi-Fi lost: why, and how strong the network was
            seenDrops = us.wifiDrops;
            char b[96];
            snprintf(b, sizeof(b), "# wifi lost (%lu): reason %d, rssi %d dBm", (unsigned long)us.wifiDrops,
                     us.wifiReason, us.wifiRssi);
            logLine(logPath, b);
        }
        if (us.netResets != seenResets) {
            seenResets = us.netResets;
            logLine(logPath, "# wifi: connected but no server answered for 3 min, joining again");
        }
        if (us.wifiConnected != wasUp) {
            wasUp = us.wifiConnected;
            if (wasUp) {
                char b[96];
                snprintf(b, sizeof(b), "# wifi up %s after %lu attempts", us.ip, (unsigned long)us.wifiJoins);
                logLine(logPath, b);
            }
        }
    }
    static uint32_t lastStationMs = 0;
    if (millis() - lastStationMs > 2000) {
        lastStationMs = millis();
        const GpsFix &g = nmea.fix;
        // A good fix (3D, 5+ satellites, HDOP up to 3) must last FIX_WAIT_S before the position is shared:
        // the first fixes after a cold start can be hundreds of metres off. Any break starts the wait again.
        bool good = myFixOk() && g.fixType != 2 && g.sats >= 5 && (g.hdop <= 0 || g.hdop <= 3.0f);
        if (!good) fixGoodSince = 0;
        else if (!fixGoodSince) fixGoodSince = millis() | 1;
        uploader_position(fixStable(), g.lat, g.lon, g.altM + g.geoidSepM, g.speedKmh, g.courseDeg);
    }

#if FIELD_DEBUG
    if (fieldStall[0]) {
        logLine(logPath, fieldStall);
        logLine(fieldPath, fieldStall);
        fieldStall[0] = 0;
    }
    static uint32_t lastFieldMs = 0;
    if (sdOk && millis() - lastFieldMs > 30000) {
        lastFieldMs = millis();
        crumb("field debug");
        File f = SD.open(fieldPath, FILE_APPEND);
        if (f) {
            char clk[12];
            localClock(clk, sizeof(clk), true);
            f.printf("=== %s uptime %lus\n", clk, (unsigned long)(millis() / 1000));
            statusText(f);
            f.close();
        }
    }
#endif

    uint32_t now = millis();
    if (now - lastKeys > 40) {
        lastKeys = now;
        crumb("keys");
        if (pollKeys()) {
            crumb("draw after key");
            drawUi();
            lastUi = now;
        }
    }
    // No sonde on air: load map tiles whenever needed.
    if (screen == SCR_MAP && mapOk && !scanning && now - cap.lastSyncMs() > 1500 && mapview_service()) {
        crumb("draw after tile");
        drawUi();
        lastUi = now;
    }
    crumb("periodic");
    battPoll();
    savePoll();
    dimPoll();
    lockPoll();
    aprsFiPoll();
    scanService();
    if (track.have && !track.manual && !track.restored && !track.fromNet && !lostAnnounced &&
        now - track.ms > LOST_SIGNAL_MS) { // only a sonde heard by radio can be lost
        lostAnnounced = true;
        setMsg("signal lost: last position kept");
        if (cfg.vol) audio_pattern(500, 110, 2, 150);
        saveLastPos();
    }
    if (now - lastUi > (screen == SCR_APPROACH && !scanning ? 120u : 300u)) {
        crumb("redraw");
        drawUi();
        lastUi = now;
        crumb("after redraw");
        // Radio work after the redraw, not right before it (shared SPI bus, see drawUi).
        if (radioOk) rssiIdle = cap.rssiNow();
        bool rot = typeRotates();
        uint32_t dwell = rot ? AUTO_ROTATE_MS : AUTO_INVERT_MS;
        if ((cfg.autoInvert || rot) && !scanning && radioOk && now - cap.lastSyncMs() > dwell &&
            now - lastInvertToggle > dwell) {
            if (cfg.autoInvert) {
                cfg.invert = !cfg.invert;
                polFlips++;
            }
            // next decoder once both polarities were tried (or at once with a fixed polarity); it starts
            // with the polarity that last decoded it
            if (rot && (!cfg.autoInvert || polFlips >= 2)) {
                rxType = sonde_type_next(rxType);
                if (cfg.autoInvert) cfg.invert = polInv[rxType];
                polFlips = 0;
            }
            applyRadio(true); // restarts lastInvertToggle
        }
    }
}
