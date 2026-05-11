#pragma once
#include <Arduino.h>

// ---------------- ST7789V3 240x280 (SPI) --------------------------------------
#define TFT_SCLK_PIN   12   // SCL
#define TFT_MOSI_PIN   11   // SDA
#define TFT_RST_PIN     9   // RES
#define TFT_DC_PIN      7   // DC
#define TFT_CS_PIN      5   // CS
#define TFT_BL_PIN      3   // BLK (backlight)

#define TFT_WIDTH     240
#define TFT_HEIGHT    280

// ---------------- Display colors -----------------------------------------------
// Day / night foreground+dim pairs (RGB565)
#define DEFAULT_DAY_FG     0xF800   // bright red
#define DEFAULT_DAY_DIM    0x2000   // dark red
#define DEFAULT_NIGHT_FG   0x001F   // bright blue
#define DEFAULT_NIGHT_DIM  0x0008   // dark blue
#define DEFAULT_DAY_MIN    480     // switch to day colors at 08:00  (8*60)
#define DEFAULT_NIGHT_MIN  1320    // switch to night colors at 22:00 (22*60)
#define DEFAULT_DAY_BL     100     // day backlight %
#define DEFAULT_NIGHT_BL   20      // night backlight %

// ---------------- Wi-Fi AP ----------------------------------------------------
#define WIFI_AP_SSID   "Horloge"
#define WIFI_AP_PASS   "horloge1234"   // must be >=8 chars
#define WIFI_AP_CHAN    6

// ---------------- Navigation buttons (active LOW, internal pull-up) -----------
#define BTN_UP_PIN      1
#define BTN_DOWN_PIN    2
#define BTN_LEFT_PIN    4
#define BTN_RIGHT_PIN   6
#define BTN_A_PIN       8
#define BTN_B_PIN      10

// ---------------- Battery ADC (Wemos D1 battery shield, BAT-A0 jumper) -------
// Wemos shield divider: 130k (high) / 100k (low) -> Vadc = Vbat * 100/230
// Multiply by 230/100 = 2.30 to recover Vbat.
#define BATT_ADC_PIN   14
#define BATT_DIVIDER   2.30f
