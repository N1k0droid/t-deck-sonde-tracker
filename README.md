<div align="center">

<a href="README.md"><img src="https://img.shields.io/badge/-English-0a66c2?style=for-the-badge" alt="English" height="38"></a>&nbsp;&nbsp;<a href="README.it.md"><img src="https://img.shields.io/badge/-Italiano-30363d?style=for-the-badge" alt="Italiano" height="38"></a>

[![status](https://img.shields.io/badge/status-testing-orange?style=flat-square)](#)
[![release](https://img.shields.io/github/v/release/N1k0droid/t-deck-sonde-tracker?include_prereleases&sort=semver&style=flat-square)](https://github.com/N1k0droid/t-deck-sonde-tracker/releases)
[![license](https://img.shields.io/badge/license-GPL--3.0-blue?style=flat-square)](LICENSE)
[![hardware](https://img.shields.io/badge/LilyGO-T--Deck%20Plus%20433%20MHz-2ea44f?style=flat-square)](https://github.com/Xinyuan-LilyGO/T-Deck)
[![radio](https://img.shields.io/badge/ESP32--S3-SX1262-555?style=flat-square)](#)
[![PlatformIO](https://img.shields.io/badge/built%20with-PlatformIO-f5822a?style=flat-square)](https://platformio.org)

<br>

<img src="docs/img/tdeck.png" alt="T-Deck SondeTracker on a LilyGO T-Deck Plus" width="300">

### Hunt radiosondes (Vaisala RS41, Graw DFM, Meteomodem M10/M20, InterMet iMet-54) with nothing but a LilyGO T-Deck Plus (433 MHz)

<table>
  <tr>
    <td align="center"><img src="docs/img/hunt.png" width="260"><br><sub>Hunt: distance and compass</sub></td>
    <td align="center"><img src="docs/img/map.png" width="260"><br><sub>Offline map and landing prediction</sub></td>
    <td align="center"><img src="docs/img/signal.png" width="260"><br><sub>Approach: signal meter</sub></td>
  </tr>
</table>

</div>

> [!WARNING]
> **Firmware in testing phase.** It works, but some parts have not been field-tested yet: see [What still needs testing](#what-still-needs-testing).

## Contents
- [What it is](#what-it-is)
- [Project status](#project-status)
- [What you need](#what-you-need)
- [Installation](#installation)
- [First start](#first-start)
- [Wi-Fi and the web page, step by step](#wi-fi-and-the-web-page-step-by-step)
- [Controls](#controls)
- [Pages](#pages)
- [Settings](#settings)
- [Offline map](#offline-map)
- [Hunting apps on the phone](#hunting-apps-on-the-phone)
- [Internet through the phone hotspot](#internet-through-the-phone-hotspot)
- [Web interface](#web-interface)
- [Debug server](#debug-server)
- [Full frames and ozone sensor](#full-frames-and-ozone-sensor)
- [Sonde types and presets](#sonde-types-and-presets)
- [Sending data: SondeHub, radiosondy.info, aprs.fi](#sending-data-sondehub-radiosondyinfo-aprsfi)
- [SondeHub: help wanted](#sondehub-help-wanted)
- [Files on the SD card](#files-on-the-sd-card)
- [How it was tested](#how-it-was-tested)
- [What still needs testing](#what-still-needs-testing)
- [Known limits](#known-limits)
- [Glossary](#glossary)
- [Building from source](#building-from-source)
- [Credits and license](#credits-and-license)

## What it is
Radiosondes are small weather instruments carried by a balloon. They climb to 30-35 km, then the balloon bursts and the sonde falls back under a parachute. For the whole flight it transmits its position in the 400-406 MHz band, and many radio amateurs go out to find and recover it: this is "sonde hunting".

A hunt usually takes three things: a receiver, for example a TTGO board, a phone with an app, and a map. **T-Deck SondeTracker does it all with just the T-Deck Plus**, even with no phone and no internet. Here is what it does:

- with the T-Deck's built-in radio it receives and decodes **Vaisala RS41**, **Graw DFM-06/09/17**, **Meteomodem M10/M20** and **InterMet iMet-54** sondes. RS41 sondes can also be received as **whole frames**, which works better when the signal is weak;
- it reads the **OIF411 ozone sensor** on sondes that carry one;
- it shows the sonde's **distance, direction, altitude and speed**, with a compass that points the way;
- it predicts **where the sonde will land**, using the wind measured by the sonde itself;
- it has an **offline map** on the SD card, with you, the sonde, its track and the landing point;
- for the last metres it has an **approach page** with an audible signal meter;
- if you do not know the frequency, the **continuous scan** looks for the sonde: first on the preset frequencies, then across the whole band;
- it saves **each sonde's track** to the SD card as CSV, and exports it as KML for Google Earth;
- if you want, it works as a **receiver for the hunting apps** (MySondyGO, Hunter Tracker, TrovaLaSonda) and **reads SondeHub** through the phone hotspot;
- if you turn it on, it **sends the sondes** to radiosondy.info and **your station position** to radiosondy.info and aprs.fi;
- it keeps every frame on the SD card in the SondeHub format: these files are needed to get the firmware approved by SondeHub ([help wanted](docs/SONDEHUB.md)).

<p align="center">
  <img src="docs/img/home.png" width="260"> <img src="docs/img/gps.png" width="260"> <img src="docs/img/nearby.png" width="260">
</p>

## Project status
The firmware is in its **testing phase**. On the T-Deck Plus I have tested RS41 reception, also on real flights, the hunting screen, the map, the themes, SondeHub reading and the uploads to radiosondy.info and aprs.fi, and they work. I have tested the scan and the other sonde types on the bench. How I ran the tests is explained in [How it was tested](#how-it-was-tested). Other features are complete but still need field testing: the list is in [What still needs testing](#what-still-needs-testing).

The screenshots on this page reproduce the T-Deck screens with sample data, taken from a simulated hunt in Sicily.

Reports, tests and suggestions are welcome in the GitHub **Issues**.

## What you need
- A **LilyGO T-Deck Plus, 433 MHz version**, with the SX1262 LoRa radio and built-in GPS. The T-Deck Plus is sold in 433, 868 and 915 MHz versions, but only the **433 MHz** one has its radio circuit tuned close to the sonde band, 400-406 MHz. The 868 and 915 MHz versions receive the sondes much worse, or not at all.
- The plain T-Deck, without "Plus", has no GPS: the firmware still runs, but without your own position.
- A **microSD card** in FAT32. It is recommended, because it holds the logs, the tracks and the map.
- An **antenna** suited to 400-406 MHz. The 433 MHz antenna supplied with the T-Deck is fine to start with.
- Optional: a phone with a hotspot, for the internet and the hunting apps.

## Installation
Download the file from the **[Releases](https://github.com/N1k0droid/t-deck-sonde-tracker/releases)** page. It is called `t-deck-sonde-tracker-v-<version>-beta<n>.bin`, for example `t-deck-sonde-tracker-v-0.95-beta4.bin`. The 0.x versions are betas; from 1.0.0 on the name becomes `t-deck-sonde-tracker-v-<version>.bin`. It is a full image, with bootloader, partitions and program, and it must be written at address 0.

### Method 1: with the Launcher firmware (recommended)
The [Launcher](https://github.com/bmorcelli/Launcher) is an open-source firmware for ESP32 devices, T-Deck included, that acts as a boot manager. It stays on the device and lets you install and switch between firmwares kept on the SD card, without a PC. Radiosonde hunting is not an everyday activity: with the Launcher the T-Deck can run another firmware day to day and switch to T-Deck SondeTracker when there is a launch to chase.

If the Launcher is not on your T-Deck yet, install it once from the [Launcher Flasher](https://bmorcelli.github.io/Launcher/) page, with Chrome or Edge, following the instructions there.

1. Copy the `.bin` file to the `downloads` folder of the SD card, where the Launcher looks for firmwares; if the folder is missing, create it. Once T-Deck SondeTracker is installed, you can also copy the next versions from the browser, without taking the card out: see the **Files** page in [Web interface](#web-interface).
2. Put the SD card in the T-Deck and start the Launcher.
3. Choose **SD**, then the file `t-deck-sonde-tracker-v-….bin`, and confirm the installation.
4. When done, the T-Deck restarts into T-Deck SondeTracker.

### Method 2: from the browser, over USB
[ESP Web Tool](https://esptool.spacehuhn.com) is an open-source web page that writes firmware to ESP32 boards, with nothing to install. The browser talks to the T-Deck over USB through the Web Serial API, which only works in Chrome and Edge on desktop.

This method rewrites the whole flash memory, so **it replaces the Launcher** if you have it.

1. Connect the T-Deck to the PC with a USB-C cable **that carries data**: some cables only charge.
2. Open [esptool.spacehuhn.com](https://esptool.spacehuhn.com) in Chrome or Edge.
3. Press **Connect** and pick the T-Deck's port. If the port does not show up, hold the trackball down, which acts as the BOOT button, press and release the reset button on the side, then release the trackball and try again.
4. Choose the `.bin` file, check that the offset is `0x0` and press **Program**.
5. When done, press the reset button.

### Method 3: command line (esptool)
```bash
pip install esptool
esptool --chip esp32s3 write-flash 0x0 t-deck-sonde-tracker-v-0.95-beta4.bin
```
With older versions of esptool the command is `esptool.py ... write_flash ...`.

## First start
1. At power-on the logo shows for a moment, then the **Home** page.
2. Go outdoors, because the GPS needs to see the sky. The first fix can take a few minutes: on the **GPS** page you can watch the satellites come in.
3. Set the sonde frequency. On the **Hunt** page press `f`, type the frequency in MHz, for example `404.5`, and press Enter. If you do not know it, start **Scan** from the Home page.
4. When the T-Deck receives the sonde, you hear a short "tick" at each packet and the Hunt page fills with data.

> You can find the frequencies of sondes launched near you on [SondeHub](https://tracker.sondehub.org). With internet you can also find them on the T-Deck itself, in the **Nearby** page.

## Wi-Fi and the web page, step by step
Wi-Fi has two uses: connecting to the internet, for nearby sondes, the exact time and uploads, and using the T-Deck from a browser. The T-Deck only sees **2.4 GHz** networks, not 5 GHz ones. The handiest network is the phone hotspot.

1. Turn on the hotspot on the phone. On an iPhone also turn on **Maximize Compatibility**, so the hotspot uses 2.4 GHz.
2. On the T-Deck open **Settings**, **Network** tab, and press **Scan networks**. After a few seconds the list of networks appears.
3. Pick your network. If it is protected a box opens: type the password, which stays visible while you type, and press Enter.
4. To find it again next time, open **Saved networks** and press the first row, **+ save this network**.
5. For the internet turn on **Internet via Wi-Fi**, in the same tab. For the web page go to the **System** tab instead and turn on **Web interface**.
6. After a few seconds the **Web interface** row shows the T-Deck's address: four numbers separated by dots.
7. Connect the phone or PC to the same network, open the browser and type `http://` followed by that address, that is `http://<ip>/`.
8. The browser asks for a user and a password: the user is `tdeck`, the password `radiosonde`. You can change the password with the **Web password** row.

What you can do from the page is explained in [Web interface](#web-interface).

## Controls
The T-Deck is used with the **keyboard**, the **trackball** and the **touch screen**, and every function can be reached in all three ways.

- **Home:** tap a button, or move the trackball onto it and press it, or press its number.
- **At the bottom of every page** there are ◀ **Back** on the left and ⌂ **Home** on the right. On the keyboard, Back is Backspace (or Esc) and Home is `h`.
- **`v`** quickly switches between the three hunting pages, Hunt, Map and Signal; **`m`** opens the map.
- **Trackball:** it moves the selection in lists and pans the map; on the map a click changes the centre, like `c`. In Hunt, Signal and Info, up and down change the frequency by ±1 kHz. **Held for 3 seconds**, it locks or unlocks the screen.
- **`Alt`+`D`** deletes, after a confirmation, the item in focus in the preset and saved-network lists.
- **Yes/No questions:** `y` answers yes, `n` or Back answers no. Enter takes the button in focus, which is No at first.
- **Frequency at the top left:** a tap starts typing a new frequency, like `f`. This does not apply on the Map page.
- **While typing a text** you cancel with Esc, with Backspace on an empty field or with a tap on the screen.

### Keys
| Key | Function |
|---|---|
| `h` / Backspace | Home / back |
| `m` | map |
| `v` | next hunting page: Hunt, Map, Signal |
| `f` | new frequency: digits, then Enter |
| `w` or `+` / `q` or `-` | frequency +1 / −1 kHz |
| `e` / `d` | frequency +10 / −10 kHz |
| `p` | next preset of the same country |
| `a` | save the current frequency and sonde type as a new preset: type a name and confirm with `y` |
| `j` | search the presets: type a name, for example `trapani`, and press Enter |
| `y` | sonde type: auto / RS41 / DFM / M10/M20 / iMet54. In questions it means yes |
| `x` | start the scan; `x` again stops it |
| `n` | the compass points to the sonde or to the predicted landing point |
| `t` | manual target: type latitude and longitude, for example `38.0176 12.5365` |
| `s` | mutes every sound; pressed again, back to the previous volumes |
| `k` | export the sonde track as KML, for Google Earth |
| `i` / `o` | map: zoom in / out |
| `c` | map: centre on both, on you, on the sonde |
| `g` | map: QR code with the sonde position, to scan with the phone |
| `u` | signal polarity: automatic / normal / inverted. Normally leave it on automatic |
| `r` | resets the counters, the received position and the T-Deck's prediction; a target typed with `t` stays |
| `l` | raw data log on/off |
| `Alt`+`B` | keyboard backlight on/off |
| trackball held 3 s | lock / unlock keys, trackball and touch, with a padlock in the status bar. Radio, alarms and uploads keep working. Always unlocked after a restart |

## Pages

### Home
It has eight buttons: **Hunt**, **Map**, **Signal**, **GPS**, **Nearby**, **Info**, **Settings** and **Scan**. Under each name a short line tells how that page stands, for example the distance to the sonde, the map zoom, the signal in dBm, the GPS fix or how many sondes SondeHub found.

### Hunt
<img src="docs/img/hunt.png" width="320" align="right">

This is the main page while hunting. From top to bottom it shows:
- the status bar, with the packet light, the frequency, the sonde serial, your GPS satellites and the battery. Other tags appear when needed: `WEB` with the web interface on, `DBG` with the debug pages on, and a padlock when the screen is locked. `SH`, `RS` and `FI` turn red while the upload to SondeHub, radiosondy.info or aprs.fi is on; with more than one on, they are small and stacked. `BT` is for the apps, `SD` for the card;
- the **distance** to the sonde, in large digits, and the **compass**;
- the sonde's altitude, vertical speed and horizontal speed;
- at the bottom, messages, the predicted landing point and hints.

**Reading the compass.** The T-Deck has no magnetic compass: it gets the direction from the GPS while you move.
- **AHEAD** (green) means you are walking or driving. The triangle at the top is your direction, the arrow is the sonde, and the letters N E S W rotate to show the real cardinal points. Follow the arrow.
- **N UP** (yellow) means you are standing still. The arrow is referred to north, so hold the top of the screen towards north.

While the sonde descends, the bottom line shows the predicted landing point and the time to landing: `LAND` for the T-Deck's prediction, `SH LAND` for SondeHub's. It alternates with the altitude below which the signal will be lost behind the horizon ("signal lost below ~… m"). With internet the T-Deck shows SondeHub's prediction while its own lacks the winds below the sonde; below 2500 m above the ground it switches to its own.
<br clear="right">

### Map
<img src="docs/img/map.png" width="320" align="right">

It shows you in blue, the sonde in red, its track, the line between you and the sonde, and the predicted landing point. The magenta cross is the T-Deck's prediction; the light blue one, with its dashed path, is SondeHub's, which is there only with internet.
- Zoom with `i` and `o` or by tapping `+` and `−`.
- `c` centres the view and the trackball pans it.
- The **QR** button at the top left, or `g`, shows a QR code. Scan it with the phone to open navigation to the sonde or to the landing point.

The map tiles must be prepared and copied to the SD card, as explained in [Offline map](#offline-map). Without the `/maps` folder the page only shows how to prepare it. Where a single tile is missing, the background is grey.
<br clear="right">

### Signal (approach)
It is for the last metres, when the GPS is no longer enough. A bar shows the live signal with the recent peak, and the audible **signal meter** gets higher and faster as the signal grows. If you turn around on the spot, the signal is strongest towards the sonde.

### GPS
It shows the GPS status: 2D or 3D fix or still searching, the satellites with the signal strength of each, HDOP and PDOP (the lower, the more precise the position) and the time to the first fix. Then position, altitude, speed, local time or UTC, and the state of the GPS aid. To help the GPS get its fix sooner, the T-Deck tells it the time and a rough position, taken from the internet or from the sonde.

### Nearby (internet needed)
<img src="docs/img/nearby.png" width="320" align="right">

It has four tabs, which you choose by tapping them or with keys 1-4:
- **Near** lists the sondes in flight in the last 3 hours within 300 km. Descending ones are orange, the one you follow is green. A tap on a sonde follows it: it becomes the target and the radio tunes to it. A tap on the followed sonde stops following it.
- **Landed** lists the flights landed within 400 km in the last 3 hours, 12 hours, 24 hours or 7 days, with the landing point. From the details of a flight you can make that point your target.
- **Launches** lists the scheduled launches from nearby stations, with the predicted landing. It looks as far ahead as the **Launch predictions** setting says, 24 hours by default, and computes the launches when you open the tab.
- **Search** looks up a sonde by serial, for example `X1422184`.
<br clear="right">

### Scan
It searches until it finds a sonde or until you stop it with `x`, Back or a tap. It tries the preset frequencies first, then the whole 400-406 MHz band, round after round, listening to the strongest channels in between. The graph shows the current sweep over the highest level seen so far, drawn dim, together with the noise threshold. A channel already tried without finding a sonde is left alone for 10 minutes and gets a grey mark. When a sonde is decoded, the T-Deck beeps and asks whether to keep it and go to the Hunt page; with No the search goes on. When you stop the scan, the T-Deck goes back to the previous frequency.

### Info
It shows the technical reception and sonde data: decoding quality, corrections, whole frames received, sonde battery and temperature, kill timer, statistics and ozone sensor values.

### Settings page
The settings are described in the next section.

## Settings
<img src="docs/img/settings.png" width="320" align="right">

There are four tabs: **Radio**, **Network**, **Display** and **System**. Move between them with the trackball left and right, with the space bar or by tapping the tab name. Inside a tab pick a row with the trackball or by touch, and press it to change it. In tabs with up to ten rows you can also pick them with digits 1-9 and 0; the Network tab has more, so digits do not work there.

Text fields are confirmed with Enter:
- Enter with nothing typed keeps the value there was;
- a single `-` clears it;
- passwords, passcodes and tokens stay visible while you type them.

In the value column "set" means the row is not set yet, "change" that it already is.
<br clear="right">

| Tab | No. | Row | Default | What it does |
|---|---|---|---|---|
| Radio | 1 | Sonde type | auto | auto tries every type; the other choices are RS41, DFM, M10/M20 and iMet54. See below |
| | 2 | Frequency preset | — | opens the list of presets from `presets.txt` |
| | 3 | Receiver bandwidth | 14.6 kHz | width of the receive filter, 9.7 to 39 kHz. Keep 14.6: a wider filter lets in more noise, and at 19.5 kHz weak RS41 sondes are lost. M10/M20 always use at least 23.4. Shown in orange when not 14.6. Hunting apps cannot change it |
| | 4 | Full frames | on | receives whole 320/518-byte frames (see below). Off, it receives 255 bytes and uses error correction |
| | 5 | Raw frame log | on | saves the raw received data to the SD card |
| Network | 1 | Scan networks | — | lists the Wi-Fi networks around, only when pressed. The network you pick replaces the Wi-Fi name; if it is protected, a box asks for its password |
| | 2 | Saved networks | — | the first row, "+ save this network", keeps the Wi-Fi name and password now set, after a Yes/No. Below are the saved networks: the one you pick replaces the Wi-Fi name and password, also after a restart. Alt+D deletes one from the list. The networks are in `wifi.txt` (see below) |
| | 3 | Wi-Fi name | — | Wi-Fi network name, for example the phone hotspot |
| | 4 | Wi-Fi password | set | network password, stored on the device without encryption |
| | 5 | Internet via Wi-Fi | off | uses the internet through the Wi-Fi network set above. It says "online" only while a server really answers and "no internet" when none does; after 3 minutes in that state the T-Deck joins the network again by itself |
| | 6 | Wi-Fi power save | off | lets the Wi-Fi sleep between the router's beacons. It uses less current, but with some routers the link stops working |
| | 7 | Launch predictions | 24 h | how far ahead the Launches tab looks for launches: 12 h, 24 h, 3, 5 or 7 days |
| | 8 | Hunting apps | off | link to the apps: off, Bluetooth, Bluetooth + Wi-Fi |
| | 9 | Callsign | — | your callsign, the same for every upload. It is saved in capitals, for example `IT9ABC-9` |
| | 10 | APRS passcode | set | APRS-IS passcode of your callsign, needed for radiosondy.info and aprs.fi. It is a number computed from the callsign: get it from an APRS passcode calculator (there are many online) or from your APRS service. It needs an amateur radio callsign |
| | 11 | SondeHub upload | awaiting approval | **suspended**: SondeHub has not approved this firmware yet, so the row cannot be turned on. Pressing it explains how to help ([SondeHub](docs/SONDEHUB.md)) |
| | 12 | SondeHub JSON log | on | keeps every received frame, in the SondeHub format, in `sondehub/<serial>.json` on the SD card, about 2 MB per hour of reception. These are the files to send me for the approval ([how](docs/SONDEHUB.md)) |
| | 13 | radiosondy.info upload | off | sends the received sondes to radiosondy.info as APRS objects, one per sonde every 30 s. Turning it on asks for confirmation |
| | 14 | aprs.fi upload | off | sends your station position to aprs.fi, with the icon chosen below, on the main APRS-IS network (European servers). Turning it on asks for confirmation. Only the station goes there: the sondes go to radiosondy.info |
| | 15 | aprs.fi comment | on | adds a comment that fits the situation: chasing a sonde (mobile), receiving one (fixed) or looking for sondes. The texts are in `aprsfi.txt` (see below) |
| | 16 | Only sondes in flight | on | sends only sondes seen flying; off, it also sends a sonde on the ground |
| | 17 | Send my position | off | where your station position goes: off, all (radiosondy.info and aprs.fi), only radiosondy, only aprs.fi. The SondeHub choice stays greyed out until SondeHub approves the firmware. aprs.fi gets only this position, so its upload needs all or aprs.fi here. With all, your position is also written in the SondeHub JSON files |
| | 18 | Station | mobile | mobile is shown as a chase car, fixed as a receiver. On radiosondy.info and aprs.fi a fixed station always has the dish icon `` /` ``, because that is how radiosondy.info lists receivers. A mobile station also sends speed, course and altitude |
| | 19 | Mobile icon | person | icon of the mobile station on radiosondy.info and aprs.fi: person `/[`, car `/>` or `/(`, used by other sonde receivers |
| | 20 | Mobile update | 60 s | how often the position of a mobile station is sent: 30 s, 60 s, 2, 5, 10 or 15 min. On aprs.fi at most once a minute |
| | 21 | Fixed update | 30 min | how often the position of a fixed station is sent: 10, 30 or 60 min |
| | 22 | GPS fix wait | 1 min | your position is shared only after the GPS has held a good fix (3D, 5 or more satellites, HDOP up to 3) for this long without a break: 30 s, 1, 2 or 5 min. The first fixes after switching on can be far off |
| | 23 | Min distance | off | for a mobile station: no new position until it has moved this far from the last one sent. Choices: off, 200 m, 500 m, 1 km. A new comment or a new icon is still sent |
| Display | 1 | Screen brightness | 100% | display brightness: 100%, 75%, 50%, 25%, 12% for the night |
| | 2 | Screen timeout | 5 min | time without input before the screen goes off or is dimmed: always on, 1, 5 or 10 min |
| | 3 | Screen dimming | screen off | what happens at the timeout: screen off switches the screen off; light, medium and strong keep it on but dimmed, strong the darkest |
| | 4 | Theme | Desert | colours: Desert, Olive, Navy and Sun, with high contrast for sunlight |
| | 5 | Keyboard light | Alt+B | reminder of the keyboard backlight key combination |
| | 6 | Touch screen | on | turns touch input on or off |
| | 7 | Auto lock | off | locks keys and touch after a time without input: off, 1, 2 or 5 min. Hold the trackball for 3 s to unlock |
| | 8 | Trackball speed | fast | how far the ball must turn for one step: fast, medium, slow |
| System | 1 | Volume | 7 | volume of alarms, approach meter and confirmations, from 0 (mute) to 9 |
| | 2 | Frame volume | 7 | volume of the sound of each received frame, on every page, from 0 (mute) to 9 |
| | 3 | Frame sound | soft tick | sound of each frame: a soft tick, or a beep that gets higher as the signal gets stronger |
| | 4 | Time zone | UTC+1 | time zone in winter time, UTC-12 to UTC+14, picked from a list with a few cities for each zone |
| | 5 | Daylight saving | auto | summer time: off, on (+1 hour), auto with the European dates, that is the last Sunday of March and of October |
| | 6 | Web interface | off | uses the T-Deck from the browser of a phone or PC (see below). When on, it shows the address to open |
| | 7 | Debug server | off | remote control pages for tests and problem reports (see below). When on, it shows "on, /debug" |
| | 8 | Web password | default | password of the web interface and of the debug pages; the default is `radiosonde`, with user name `tdeck`. Type a new one, or `-` to go back to the default |
| | 9 | About | — | version, author and project link |

**Screen timeout.** With the screen off or dimmed the T-Deck keeps receiving, logging and **playing sounds**. The first key or touch only brings the screen back to full brightness and does nothing else, and alarms wake it on their own. Light, medium and strong cut the chosen brightness to a half, a quarter and an eighth.

<p align="center">
  <img src="docs/img/themes.png" width="640"><br>
  <i>The four themes: Desert, Olive, Navy, Sun</i>
</p>

## Offline map
The T-Deck reads the map from the SD card, split into 256-pixel PNG tiles in `/maps/<zoom>/<x>/<y>.png`. The public OpenStreetMap servers do not allow bulk tile downloads, so the project includes a script that **renders the tiles on your PC** from OpenStreetMap data. The script is in the `tools/mappack` folder of this repository: to download everything use the **Code** button at the top of this page, then **Download ZIP**.

Where a tile is missing, the T-Deck enlarges the one from the lower level. A "layered" map works best, with little detail everywhere and much detail only where you hunt.

### What you need
- **Docker** running (Docker Desktop on Windows or Mac);
- **Python 3** and, recommended, Pillow (`pip install pillow`);
- about 10 GB free during the preparation, for a country like Italy.

### Steps
1. Copy `tools/mappack/sicilia.json` under a new name and adapt it to your area:
   - in `sources` put the name of your country's [Geofabrik](https://download.geofabrik.de) extract, for example `italy`, `germany` or `france`;
   - in `layers` put, for each layer, the box `[south, west, north, east]` in degrees and the zoom levels. For example: world 0-2, continent 3-7, country 8-11, your region 12-14.
2. Check how many tiles will be made:
   ```bash
   python tools/mappack/make_maps.py tools/mappack/my_area.json --dry-run
   ```
3. Build the map. The first time it downloads the data and can take from a few minutes to a couple of hours:
   ```bash
   python tools/mappack/make_maps.py tools/mappack/my_area.json
   ```
4. Copy the generated folder, `mapwork/maps`, to the root of the SD card, named `maps`: you must have `/maps/0`, `/maps/1` and so on. Then restart the T-Deck.

To give an idea of the size: the Sicily example (world, Europe, Italy and Sicily up to zoom 14) gives about 30,000 tiles and 130 MB.

| Zoom | Screen width | Use |
|---|---|---|
| 0-2 | world | background |
| 3-7 | 4,000 to 300 km | continent, country |
| 8-11 | 150 to 19 km | region, travel |
| 12-14 | 10 to 2.4 km | roads, tracks, last stretch |

Map data © OpenStreetMap contributors (ODbL).

## Hunting apps on the phone
The T-Deck can act as the receiver for the apps you already use, without changing them. The link is chosen with the **Hunting apps** row of the Network tab:
- **Bluetooth:** the T-Deck shows up as `MySondyGO-XXXX` and speaks the MySondy GO 4.1 protocol. It works with **MySondyGO**, **Hunter Tracker** (as a MySondy receiver) and **TrovaLaSonda**: look for it among the Bluetooth devices in the app. The frequency can be changed from the app.
- **BT + Wi-Fi:** in addition, on the Wi-Fi network set in the Network tab, the T-Deck behaves like a **TTGO rdz** receiver, with `http://<ip>/live.json` and TCP port 14570, for the apps that support it.

`BT` appears at the top: grey while waiting, green when an app is connected.

> The link to the apps sends nothing to the internet, but **the apps may upload data to SondeHub on their own**: check the app's settings.

## Internet through the phone hotspot
With **Internet via Wi-Fi** on (Network tab) and the T-Deck connected to the phone hotspot you also get:
- the exact time from the internet and a faster GPS fix;
- the **Nearby** page, with nearby sondes, landings and upcoming launches;
- the sonde position from SondeHub while the radio cannot hear it, in the "SONDE (NET)" card;
- SondeHub's landing prediction on the map, kept even if the internet drops.

SondeHub asks not to poll its API, so the T-Deck limits its requests. It reads the nearby sondes every 5 minutes, and every minute only while the Near list is open, on the T-Deck or in the web page, or while you follow a sonde the radio does not hear. It asks for the prediction of the followed sonde every 3 minutes. If the followed sonde stands still for 10 minutes, because it has landed or nobody hears it any more, it goes back to every 5 minutes and its prediction is no longer asked.

The T-Deck **only reads** here: it sends data to SondeHub, radiosondy.info or aprs.fi only if you turn on their upload (see [Sending data](#sending-data-sondehub-radiosondyinfo-aprsfi)). To ask for nearby sondes it shares your position rounded to about 10 km. Without internet everything else works as usual.

## Web interface
With the **Web interface** row (System tab, off by default) you use the T-Deck from the browser of a phone or PC on the same Wi-Fi network. I designed it to run the T-Deck as a remote station: everything you do with the keys and the screen you can also do from the page.

Just open `http://<ip>/`, with the address shown in the settings. The user is `tdeck` and the password is `radiosonde`, unless you changed it with the **Web password** row. To turn it on the first time, follow [Wi-Fi and the web page, step by step](#wi-fi-and-the-web-page-step-by-step).

The interface has nine pages.

- **Hunt.** It shows the sonde data: serial, altitude, vertical and horizontal speed, satellites, battery, temperature, kill timer and ozone. Then the distance, direction and elevation from you, and the compass, which turns with your direction while you move, like **AHEAD** on the T-Deck. There are also the signal, the decoded frames, the age of the last frame, the landing prediction with a **Navigate** button for Google Maps, and the height below which the signal will be lost. **To sonde** and **To landing** choose where distance and arrow point, like the `n` key; **Target** types a point, like the `t` key. **KML** makes the track file and downloads it. **Reset** clears the counters, the received position and the prediction, but keeps a target you typed. **Sound off** silences the T-Deck. The page updates every 2 seconds.
- **Map.** It shows you, the sonde, its path and the landing predictions on an OpenStreetMap map. The phone or PC takes the map from the internet: the T-Deck only sends the points, and the offline maps of the SD card are only on its own screen. The buttons work like the `c` key on the T-Deck: **Both** keeps everything in view, that is you, the sonde, its path and the predictions, while **Me** and **Sonde** keep that point in the middle. With **follow** ticked the map keeps doing it, until you drag it. A click on the sonde opens its links on **SondeHub** and **radiosondy.info** and the **Navigate** button. The dashed line is the path SondeHub predicts for the followed sonde.
- **Radio.** Here you change the frequency, typed or in 1 and 10 kHz steps, and choose sonde type, polarity and receiver bandwidth; the noise level is shown too. You can start and stop the scan and watch the live 400-406 MHz spectrum with the channels found. There is the presets list with a search box and the buttons **Auto (no preset)**, **Next**, **Save current**, which saves the frequency in use with a name, and **Add European sites**. The bin next to each preset deletes it.
- **SondeHub.** It has the same four lists as the T-Deck's **Nearby** page: **Near**, **Landed** (last 3 h, 12 h, 24 h or 7 days), **Launches**, that is the next launches of the sites around with the predicted landing, and **Search** by serial. A click on a sonde opens a box with **Follow and tune**, which follows it and tunes to it, or **Stop following**, plus the SondeHub link and **Navigate**. From the details you can make the landing point your target. It needs **Internet via Wi-Fi** on.
- **Signal.** This is the page for the last metres. Twice a second it shows the signal level and its peak of the last 4 seconds, together with the last frame and the distance. With **Tone** the browser plays a pitch that follows the signal.
- **GPS.** It shows fix, satellites used, HDOP and PDOP, the signal of each satellite in view, position, height, speed and course, clock and GPS aid.
- **Info.** It shows the lines of the T-Deck's Info page: radio, counters, last frame, sonde status and SondeHub.
- **Settings.** It has all the rows of the four tabs, with the same values and choices as on the T-Deck. When you turn an upload on, the "Are you sure?" question appears both on the T-Deck and in the browser. The **SondeHub upload** row explains how to help get the approval. If the T-Deck has a message for you, for example why a row cannot change, you see it at the bottom of the page. In **Saved networks** each network has a bin, and **Scan networks** asks for the password of the network you pick.
- **Files.** It shows the files and folders of the SD card, each with buttons to **download**, **rename** and **delete** it. Deleting asks first and cannot be undone; a folder can only be deleted when empty. With **Upload files**, or by dragging files onto the page, you copy them from the phone or PC into the open folder of the card. For example, if you use the Launcher, you can put a new firmware `.bin` in `/downloads` and then install it from the Launcher, without taking the card out. While a file is copied, in either direction, the T-Deck does not receive: better do it after the hunt.

At the top there is a padlock that locks and unlocks the T-Deck keys; the page keeps working with the keys locked. The menu has **About** and **Restart the T-Deck**. Whatever you change from the browser changes on the T-Deck too, and the other way round.

The page uses the colours of the theme chosen on the T-Deck, with the Theme row of the Display tab. It always fills the window, from a phone to a large monitor, without scrolling. Only long lists scroll, inside their box: the presets, the files and, on a phone held sideways, the settings.

The password is only a lock against a quick look on a shared network, not real security. Use the web interface only on your own networks, for example the phone hotspot.

## Debug server
The **Debug server** row (System tab, off by default) turns on the pages for tests and problem reports, at `http://<ip>/debug`. They use the same user, password and colours as the web interface. The buttons are:
- **Screen:** the T-Deck screen, which you can click like the touch screen, with the arrows, Enter, Back, Home and the most used keys;
- **Status** and **Log:** the T-Deck state and its latest messages, useful to report problems.

Files and restart are in the web interface.

The pages ask for a **password**, which is `radiosonde` until you change it with the **Web password** row. The user name is always `tdeck`. It is only a lock against a quick look on a shared network, not real security: turn these pages on only when you need them, and only on your own networks, for example the phone hotspot. The `/live.json` data for the hunting apps (Hunting apps: BT + Wi-Fi) needs no password.

## Full frames and ozone sensor
The SX1262 radio of the T-Deck normally stops a packet at 255 bytes, while an RS41 frame is 320 bytes long, or 518 with extra sensor data. With **Full frames** (Radio tab, on by default) the T-Deck uses the long-packet method described by Semtech in application note AN1200.53: while the frame arrives, it moves the end of the packet forward, so **the whole frame is received**.

- **Better reception with weak, noisy signals.** With the whole frame the error correction can fix up to 12 wrong bytes per half frame, against 3 when only 255 bytes arrive. On the bench, with a simulated sonde and strong noise, the T-Deck decoded **97% of the frames with Full frames, against 26%** without. With good signals the two methods are equal.
- **Sondes with an ozone sensor** (Vaisala OIF411, extended 518-byte frames). The **Info** page shows the ozone partial pressure in mPa, the cell current and the pump temperature; the hunting page shows "O3" next to the sonde data; the CSV file gets the `o3_mpa` column. The mPa value is an **estimate**, because it uses the nominal pump flow and not the calibration sheet of the single sensor.
- If a frame is noticed too late to extend it, for example while the map loads many tiles, the T-Deck uses the 255-byte method for that frame. Nothing is lost compared with before.
- While a frame is being received the T-Deck does nothing else, so the radio is not disturbed. With one frame every second, the screen may therefore react a little slower.

## Sonde types and presets
**Sonde types.** Besides the Vaisala RS41 the T-Deck decodes these types:

| Type | Models | What is read |
|---|---|---|
| DFM | Graw DFM-06, DFM-09, DFM-17 | position, speeds, time, serial number, model, battery (DFM-09/17) |
| M10/M20 | Meteomodem M10, M10+, M20 | position, speeds, time, serial number, model, battery, satellites (M10) |
| iMet54 | InterMet iMet-54, iMet-50 | position, time of day, serial number, model; speeds computed from the positions |

The type is chosen with the **Sonde type** row of the Radio tab, or with the `y` key:
- **auto** (default): the receiver tries every type in turn, with both polarities, until a frame is decoded. Then it stays on that type until the signal is lost. If the preset in use names a type, that type is used straight away;
- **RS41**, **DFM**, **M10/M20** or **iMet54**: only that type. This is the fastest way when you already know what will fly.

When you follow a sonde from the Nearby page, the type is taken from SondeHub. The DFM serial number arrives only every few frames, so for the first 20-30 seconds the sonde is shown as "DFM".

**Presets.** The frequencies are kept in the file **`presets.txt`** on the SD card. The T-Deck creates it the first time, with the instructions inside and about **90 presets of European launch sites** ready to use, grouped by country. Some sites have more than one frequency, and Tunis is there too. Frequency and type are those of the sondes heard from them on SondeHub in November 2026. The T-Deck always reads the presets from this file and changes it only when you ask (key `a`, Alt+D, "+ add the European launch sites"), without losing your edits: if a site changes frequency, just correct its line. Each line is one preset:

```
[Sicily]
404.500  RS41  Trapani Birgi
402.000  DFM   Tunis (DFM-17)
[France]
403.000  M10   Trappes
[Saved]
405.100  AUTO
```

- Each line has the frequency in MHz, the type (`AUTO`, `RS41`, `DFM`, `M10`, `M20` or `iMet54`) and an optional name. `[Name]` starts a group, for example a country or a region, and `#` starts a comment. There can be up to 200 presets, and you can add your own.
- The **Frequency preset** row of the Radio tab, or the `j` key from any page, opens the list. Type part of a name, a country or a frequency (`trapani`, `germany`, `404.5`) and only the matching lines stay. Trackball left and right jumps between countries. Pick one with Enter, the trackball or a tap, and the T-Deck tunes to it.
- The preset you pick stays in use, also after a restart, until you choose **auto**, the first row of the list. From then on there is no preset and the receiver finds the sonde type by itself.
- The `p` key goes to the next preset of the same country. The `a` key saves the current frequency and type under `[Saved]`, with the name you type; a name already in use gets " 2", " 3" and so on, and the preset in use is never changed. In the list, with an empty search, Alt+D deletes the preset in focus after a confirmation.
- The last row of the list, **"+ add the European launch sites"**, adds the sites to an existing file, without duplicates: a site whose frequency you corrected is not added again.
- You can also edit the file on the PC: take the SD card out, change the file and put the card back.

## Sending data: SondeHub, radiosondy.info, aprs.fi
Every upload is **off by default**, and when you turn one on the T-Deck asks "Are you sure?" with **No** already chosen. Uploads need the Wi-Fi (Network tab) and your **Callsign**; radiosondy.info and aprs.fi also need the **APRS passcode** of the callsign.

| Service | What it gets | Rows (Network tab) |
|---|---|---|
| radiosondy.info | the received sondes, as one APRS object per sonde every 30 s, and your station | radiosondy.info upload |
| aprs.fi | only your station, with a comment that fits the situation (`aprsfi.txt`) | aprs.fi upload, aprs.fi comment |
| SondeHub | **suspended**: before approving a new software, SondeHub asks for whole flights of every sonde type. Meanwhile the frames are kept on the SD card (**SondeHub JSON log**): see [SondeHub: help wanted](docs/SONDEHUB.md) | SondeHub JSON log |

- **Your station position** goes only where **Send my position** says (off, all, or one service), and only after a stable GPS fix (**GPS fix wait**). A mobile station also sends speed and course, and with **Min distance** it skips the updates while it does not move.
- By default only sondes seen flying are sent (**Only sondes in flight**). The serial numbers of the bench simulators are never sent.
- The APRS device id of the firmware is `APTKRS`.

## SondeHub: help wanted
SondeHub enables a new software only after checking **whole flights of every sonde type** it decodes. From Sicily I receive almost only RS41 and DFM-17, so flights of **DFM, M10/M20 and iMet-54**, and more RS41 too, from other places would help a lot.

With **SondeHub JSON log** on, as it is by default, the T-Deck keeps every received frame in `sondehub/<serial>.json` on the SD card. How to send me the files is explained in **[docs/SONDEHUB.md](docs/SONDEHUB.md)**.

## Files on the SD card
All the files of the T-Deck are in the folder **`/t-deck-sonde-tracker/`**. Only the map tiles, in `/maps/`, and the firmware files, in `/downloads/`, stay outside, because they are shared with other programs.

| File | Content |
|---|---|
| `sonde/<serial>.csv` | sonde track, one line per packet. It opens in a spreadsheet; ozone sondes get the `o3_mpa` column |
| `sonde/<serial>.kml` | track for Google Earth, made with the `k` key |
| `log.txt` | log of the received packets |
| `raw.txt` | raw data for analysis (Raw frame log) |
| `presets.txt` | frequency presets (see above) |
| `wifi.txt` | saved Wi-Fi networks, one per line as `name,password` (`name,` for an open network). It can also be written by hand. The first comma splits name and password, so the name cannot contain a comma; spaces at the start and end are dropped. The passwords are in plain text: keep that in mind if you lend the card |
| `aprsfi.txt` | comments for aprs.fi, one per line as `key = text`. The keys are `chase` (mobile station hearing a sonde), `receive` (fixed station), `idle` (no sonde heard by radio), plus `chase_nosite` and `receive_nosite` for when no preset is in use. `{serial}` becomes the sonde, `{site}` the name of the preset in use. The file is written with the default English texts at the first start; edit it to change them, with up to 59 characters and no `\|` or `~` |
| `sondehub/<serial>.json` | every received frame in the SondeHub format, one line per frame (SondeHub JSON log). It holds the sonde data, your callsign and, only if Send my position is "all", your position. See [docs/SONDEHUB.md](docs/SONDEHUB.md) |
| `gpsdb.ubx` | satellite data saved from the GPS, for a faster fix |
| `/maps/` | map tiles, outside the T-Deck's folder |
| `/downloads/` | firmware to install with the Launcher, outside the T-Deck's folder |

Altitudes in the KML are those sent by the sonde, which are ellipsoidal: in Google Earth the track appears a few tens of metres higher.

## How it was tested
<img src="docs/img/bench.jpg" width="420" align="right">

I tested every version **on the bench** first and then, for the RS41, **with real sondes**.
- **RS41:** on the bench with a recovered RS41, then in the field following real flights launched from Trapani. I received whole frames down to about -113 dBm, more than 45 km away.
- **DFM, M10/M20, iMet-54**, and the RS41 too: with **simulated sondes**. A HackRF (PortaPack), joined to the T-Deck by a cable and attenuators, transmits frames built like the real ones, with fake serials, normal and inverted polarity and several noise levels. Every version goes through an automatic session of 20 tests, in which serial, model and position must always be right. In the photo the PortaPack transmits a simulated RS41 with an ozone sensor, and the T-Deck decodes it.
- **Compass letters N E S W** while moving, and the **memory** of the manual target and of SondeHub's prediction after a power-off: checked in the field.
- **Uploads:** radiosondy.info (sondes and station) and aprs.fi (station) on real RS41 flights in November 2026. Positions, altitudes and times matched those of the other stations. I tested SondeHub with test data, which SondeHub checks but does not store.
- **Quick GPS fix** with the satellite data saved to the SD card: checked by switching the T-Deck off and on, also after a trip of about a hundred kilometres.

DFM, M10/M20 and iMet-54 still need real flights, which are rare where I live. **Field tests and feedback from users are very welcome.** Please leave them in the GitHub **Issues**, with sonde type, frequency, distance, what worked and what did not.
<br clear="right">

## What still needs testing
These parts are complete in the firmware, but I have not tried them in the field yet:
- I have never used the **hunting apps** (MySondyGO, Hunter Tracker, TrovaLaSonda) on a real flight;
- I have tested **DFM, M10/M20 and iMet-54** sondes only on the bench;
- I have tested the **scan** only on the bench;
- I have never seen the **ozone sensor** on a real flight, and I have measured the **landing prediction** on only a few flights.

## Known limits
- The firmware receives only **Vaisala RS41**, **Graw DFM**, **Meteomodem M10/M20** and **InterMet iMet-54** sondes: no Vaisala RS92, iMet-1/4 or other types rare in Europe.
- Of the RS41 extra sensors (XDATA) it decodes only the OIF411 ozone sensor; the others are shown as raw data in the Info page.
- DFM, M10/M20 and iMet-54 temperature is not shown yet.
- There is no magnetic compass: when standing still, the direction is referred to north.
- The keyboard handles `Alt`+`B`, its backlight, by itself and reports no key release. That is why the screen lock is on the trackball.
- The landing prediction assumes flat ground, at your altitude.

## Glossary
- **Fix:** the GPS has computed the position. 2D without the altitude, 3D with it.
- **HDOP / PDOP:** how well the satellites are spread over the sky. The lower the number, the more precise the position (below 2 is good).
- **dBm:** the strength of the radio signal. It is a negative number: -80 is strong, -115 is at the limit.
- **Polarity:** some sondes arrive with their bits inverted. In automatic the T-Deck tries both ways.
- **APRS-IS:** the internet network of the radio amateurs using APRS. radiosondy.info and aprs.fi get their data from it.
- **APRS passcode:** the number that opens APRS-IS to your callsign.
- **Kill timer:** the time after which the sonde stops transmitting by itself.
- **XDATA:** the data of the extra sensors attached to an RS41, for example the ozone one.
- **Frame:** one data packet of the sonde. About one arrives every second.

## Building from source
1. Install [VS Code](https://code.visualstudio.com/) with the **PlatformIO IDE** extension, or `pip install platformio`.
2. Open the project folder and build:
   ```bash
   pio run
   ```
   On the first run PlatformIO downloads the toolchain and the libraries. The full image to install is created in `release/`.
3. To write the firmware straight to a T-Deck connected over USB, use `pio run -t upload`.

## Credits and license
Author: **N1k0droid (IT9KVB)** — https://github.com/N1k0droid

The code was written with the help of AI-assisted programming tools (coding agents), under my direction, and tested by me on the real device.

Thanks to:
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI), [PNGdec](https://github.com/bitbank2/PNGdec), [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino), [QRCode](https://github.com/ricmoo/QRCode);
- the MySondy GO protocol and the [rdz_ttgo_sonde](https://github.com/dl9rdz/rdz_ttgo_sonde) project, for app compatibility;
- [rs1729/RS](https://github.com/rs1729/RS) for the RS41 format documentation;
- [SondeHub](https://sondehub.org), [radiosondy.info](https://radiosondy.info) and [aprs.fi](https://aprs.fi) for the data and the maps they share;
- [Launcher](https://github.com/bmorcelli/Launcher) by bmorcelli and [ESP Web Tool](https://github.com/spacehuhntech/espwebtool) by Spacehuhn, for their tools that make installing the firmware possible and easy;
- map data © [OpenStreetMap](https://www.openstreetmap.org/copyright) contributors, [OpenMapTiles](https://openmaptiles.org), [Planetiler](https://github.com/onthegomap/planetiler), [TileServer GL](https://github.com/maptiler/tileserver-gl).

License: **GNU GPL v3.0 or later** (see [LICENSE](LICENSE)). You may use, modify and redistribute the code, keeping the same license and crediting the origin.

Like T-Deck SondeTracker? A ⭐ on the repository and a follow help the project grow.
