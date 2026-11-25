// T-Deck SondeTracker - radiosonde tracker for the LilyGO T-Deck Plus
// Copyright (C) 2026 N1k0droid (IT9KVB) - https://github.com/N1k0droid
// SPDX-License-Identifier: GPL-3.0-or-later
// Made by N1k0droid. Like T-Deck SondeTracker? A star on
// https://github.com/N1k0droid/t-deck-sonde-tracker and a follow help the project grow.

#pragma once

// LilyGO T-Deck / T-Deck Plus pin map (from Xinyuan-LilyGO/T-Deck examples/*/utilities.h)
#define BOARD_POWERON 10
#define BOARD_I2C_SDA 18
#define BOARD_I2C_SCL 8
#define BOARD_SDCARD_CS 39
#define BOARD_TFT_CS 12
#define BOARD_TFT_DC 11
#define BOARD_BL_PIN 42
#define BOARD_SPI_MOSI 41
#define BOARD_SPI_MISO 38
#define BOARD_SPI_SCK 40
#define BOARD_GPS_TX_PIN 43
#define BOARD_GPS_RX_PIN 44
#define BOARD_BOOT_PIN 0
#define BOARD_BAT_ADC 4
#define BOARD_I2S_WS 5
#define BOARD_I2S_BCK 7
#define BOARD_I2S_DOUT 6

// Trackball (Hall sensors, one line per direction) and touch controller interrupt.
// LilyGO names: G01 = up, G03 = down, G04 = left, G02 = right.
#define BOARD_TBOX_UP 3
#define BOARD_TBOX_DOWN 15
#define BOARD_TBOX_LEFT 1
#define BOARD_TBOX_RIGHT 2
#define BOARD_TOUCH_INT 16

#define RADIO_CS_PIN 9
#define RADIO_BUSY_PIN 13
#define RADIO_RST_PIN 17
#define RADIO_DIO1_PIN 45
