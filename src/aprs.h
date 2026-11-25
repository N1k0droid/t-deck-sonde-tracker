// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sondehub.h"

// APRS-IS lines for radiosondy.info and aprs.fi: login, one object per sonde, the station beacon with its comment.
// Portable, tested on host. The connections are in uploader.cpp.

// Device id of the project (T-deK RadioSonde), free in the aprs-deviceid registry; registration asked by the author.
// Up to v0.85 the experimental APZTDK.
#define APRS_TOCALL "APTKRS"

// APRS-IS passcode of a callsign (the SSID is ignored), as the servers check it.
int aprs_passcode(const char *call);
// Callsign for APRS: 3-6 letters and digits with a digit, optional -SSID (1-15 or 1-2 letters/digits).
bool aprs_call_ok(const char *call);
// "user CALL pass N vers T-Deck-SondeTracker <ver>\r\n". 0 when it does not fit.
size_t aprs_login(char *buf, size_t n, const char *call, int pass, const char *ver);
// Object name (9 characters, space padded): RS41 serial, DFM "D" + number, M10/M20 "ME" + digits, iMet "IMET" +
// last 5 digits. False when the serial does not give one.
bool aprs_object_name(uint8_t type, const char *serial, char out[10]);
// Live object of a sonde, ending with \r\n. 0 for a frame that cannot be sent (or does not fit).
size_t aprs_object(char *buf, size_t n, const char *call, const ShFrame &f);
// Station icons (primary table '/'): fixed always the dish, needed to be listed as a receiver on radiosondy.info;
// mobile chosen in the settings: person, car, or the mobile satellite station of the sonde chase receivers.
#define APRS_SYM_FIXED '`'
#define APRS_MOBILE_ICONS 3
extern const char APRS_MOBILE_SYM[APRS_MOBILE_ICONS];
extern const char *const APRS_MOBILE_NAME[APRS_MOBILE_ICONS];
// Movement of the station for its position: course/speed (only a mobile station) and altitude.
struct AprsMotion {
    bool moving;     // add course and speed (CSE/SPD after the symbol)
    float courseDeg; // over ground
    float speedKmh;  // under 3 km/h sent as 000/000 (GPS course is noise when standing still)
    bool hasAlt;
    double altM;     // above sea level, sent as /A= in feet
};
// Station position with symbol '/<symbol>' and a comment: nullptr = "T-Deck SondeTracker <ver>", "" = none.
size_t aprs_beacon(char *buf, size_t n, const char *call, double lat, double lon, char symbol, const char *ver,
                   const char *comment = nullptr, const AprsMotion *m = nullptr);

// Comments for aprs.fi, by situation: chasing a sonde heard by radio (mobile station), receiving one (fixed
// station), or none heard. Texts in /aprsfi.txt, one "key = text" per line; {serial} and {site} are filled in.
enum AprsSituation { APRS_CHASE, APRS_RECEIVE, APRS_IDLE };
#define APRS_COMMENT_MAX 60
#define APRS_COMMENT_FILE_MAX 2048
extern const char *const APRS_COMMENT_TEMPLATE; // the file written at the first start
// Text for the situation: the "_nosite" key when site is empty; the built-in text when the file lacks the key.
// Characters APRS does not allow in a comment (| ~ and non-ASCII) are dropped, the text is cut at max-1.
void aprs_comment(const char *file, AprsSituation s, const char *serial, const char *site, char *out, size_t max);
