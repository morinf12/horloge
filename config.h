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
