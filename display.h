#pragma once
#include <Arduino.h>

void display_begin();
void display_showBoot(const char* hostname);
void display_showIP(const char* ip);
void display_showClock();
void display_resetClock();
void display_setSchedule(uint16_t dayMin, uint16_t nightMin);
void display_setColors(uint16_t dayFg, uint16_t nightFg);
void display_setBacklight(uint8_t dayPct, uint8_t nightPct);

// Getters for menu
uint16_t display_getDayMin();
uint16_t display_getNightMin();
uint16_t display_getDayFg();
uint16_t display_getNightFg();
uint8_t  display_getDayBl();
uint8_t  display_getNightBl();
bool     display_getShowIcons();
void     display_setShowIcons(bool show);
bool     display_getRainbow();
void     display_setRainbow(bool on);
bool     display_getEcoMode();
void     display_setEcoMode(bool on);
uint8_t  display_getDimLevel();
void     display_setDimLevel(uint8_t pct);
bool     display_getRotation180();
void     display_setRotation180(bool on);
bool     display_getShowSeconds();
void     display_setShowSeconds(bool on);
bool     display_getShowWeather();
void     display_setShowWeather(bool on);
void     display_sleep(bool on);
void     display_showTemp(float tempC);  // show external temperature on TFT

// Direct TFT access for menu drawing
#include <Adafruit_ST7789.h>
Adafruit_ST7789& display_getTft();
