#pragma once
// SpoolBuddy v3 — Pin Definitions
// ESP32-S3-DevKitC-1 + ST7796S 4" TFT + FT6336U Touch + PN5180 + HX711

#include <Arduino.h>

// ─── ST7796S TFT Display (dedicated SPI3_HOST bus) ──────────────────────
#define PIN_TFT_CS       7
#define PIN_TFT_DC      40
#define PIN_TFT_RST      4
#define PIN_TFT_MOSI    41
#define PIN_TFT_SCK     42
#define PIN_TFT_MISO    -1
#define PIN_TFT_BL      14

// ─── FT6336U Capacitive Touch (I2C) ─────────────────────────────────────
#define PIN_TOUCH_SDA   15
#define PIN_TOUCH_SCL   16
#define PIN_TOUCH_INT   17
#define PIN_TOUCH_RST   -1   // Physically tied to GPIO4 with TFT reset.
#define TOUCH_I2C_ADDR  0x38

// ─── PN5180 NFC Reader (dedicated SPI2_HOST bus) ────────────────────────
#define PIN_NFC_CS      10
#define PIN_NFC_MOSI    11
#define PIN_NFC_SCK     12
#define PIN_NFC_MISO    13
#define PIN_NFC_BUSY     9
#define PIN_NFC_RST      8

// ─── HX711 Load Cell ─────────────────────────────────────────────────────
#define PIN_HX711_DOUT   1
#define PIN_HX711_SCK    2

// ─── Miscellaneous ───────────────────────────────────────────────────────
#define PIN_BOOT_BUTTON  0
#define PIN_BUZZER      21
#define PIN_RGB_LED     48

// ─── ESP32-S3-WROOM-1-N16R8 Notes ────────────────────────────────────────
// GPIO19/20 are reserved for native USB, GPIO26-32 are wired to flash,
// and GPIO33-37 are wired to PSRAM. Do not assign application peripherals
// to those pins on this module.
