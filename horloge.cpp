// =============================================================================
// Horloge — ESP32-S2 clock with 7-segment display
//   - Display: ST7789V3 240x280 (SPI), landscape 280x240
//   - Time: NTP via Wi-Fi, or set from browser
//   - Control: Web UI for Wi-Fi config + OTA firmware update
// =============================================================================
#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "webui.h"
#include "buttons.h"
#include "menu.h"
#include "battery.h"
#include "weather.h"
#include <WiFi.h>
#include <Preferences.h>

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n[Horloge] boot"));

  display_begin();

  // Load hostname and rotation for splash screen
  Preferences prefs;
  prefs.begin("wifi", true);
  String hostname = prefs.getString("hostname", "horloge");
  bool rot180 = prefs.getBool("rot180", false);
  prefs.end();
  if (rot180) display_setRotation180(true);
  display_showBoot(hostname.c_str());

  buttons_begin();
  menu_begin();
  battery_begin();
  weather_begin();

  webui_begin();

  // Show IP address on boot screen
  IPAddress ip = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
  display_showIP(ip.toString().c_str());

  // First weather fetch right away
  weather_update();

  delay(5000);
}

void loop() {
  webui_loop();

  Button btn = buttons_poll();
  if (btn != BTN_NONE) {
    // Any button press wakes display if eco sleep
    if (display_getEcoMode()) {
      display_sleep(false);
    }
    // Outside the menu: UP toggles a manual day/night override that
    // lasts until the next scheduled transition.
    if (!menu_isActive() && btn == BTN_UP) {
      display_toggleNightOverride();
    } else {
      menu_handleButton(btn);
    }
  }

  if (menu_isActive()) {
    menu_draw();
    return;
  }

  // Eco mode: apply CPU/WiFi power saving and slower refresh
  static bool s_ecoApplied = false;
  bool eco = display_getEcoMode();
  if (eco && !s_ecoApplied) {
    setCpuFrequencyMhz(80);
    if (WiFi.getMode() != WIFI_OFF) WiFi.setSleep(WIFI_PS_MAX_MODEM);
    s_ecoApplied = true;
  } else if (!eco && s_ecoApplied) {
    setCpuFrequencyMhz(240);
    if (WiFi.getMode() != WIFI_OFF) WiFi.setSleep(WIFI_PS_NONE);
    display_sleep(false);
    s_ecoApplied = false;
  }

  uint32_t interval = eco ? 5000 : 500;
  static uint32_t lastRefresh = 0;
  static uint32_t lastBatt = 0;
  static uint32_t lastWeather = 0;
  uint32_t now = millis();
  if (now - lastBatt >= 10000) {
    lastBatt = now;
    battery_update();
  }
  if (now - lastWeather >= 60000) {  // check every 60s (module throttles to 10min)
    lastWeather = now;
    weather_update();
    if (weather_valid()) display_showTemp(weather_temp());
  }
  if (now - lastRefresh >= interval) {
    lastRefresh = now;
    display_showClock();
    if (weather_valid()) display_showTemp(weather_temp());
  }
}
