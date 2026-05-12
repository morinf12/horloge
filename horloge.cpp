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

  // Persist the cause of this reset so the web UI can show it after the
  // device reboots without a serial cable attached.
  {
    esp_reset_reason_t r = esp_reset_reason();
    Preferences p;
    p.begin("wifi", false);
    p.putUChar("rst_last", (uint8_t)r);
    uint16_t n = p.getUShort("rst_cnt", 0);
    p.putUShort("rst_cnt", n + 1);
    p.end();
    Serial.printf("[Horloge] reset reason=%d count=%u\n", (int)r, n + 1);
  }

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
    // Outside the menu: short-cut toggles for the most common options.
    // (DOWN+A still opens the menu, handled inside menu_handleButton.)
    if (!menu_isActive() && btn == BTN_UP) {
      // UP: manual day/night override until next scheduled transition.
      display_toggleNightOverride();
    } else if (!menu_isActive() && btn == BTN_LEFT && !buttons_isHeld(BTN_A)) {
      // LEFT: toggle sun/moon icons.
      bool v = !display_getShowIcons();
      display_setShowIcons(v);
      Preferences p; p.begin("wifi", false); p.putBool("icons",   v); p.end();
    } else if (!menu_isActive() && btn == BTN_RIGHT && !buttons_isHeld(BTN_A)) {
      // RIGHT: toggle seconds display.
      bool v = !display_getShowSeconds();
      display_setShowSeconds(v);
      Preferences p; p.begin("wifi", false); p.putBool("showSec", v); p.end();
    } else if (!menu_isActive() && btn == BTN_DOWN && !buttons_isHeld(BTN_A)) {
      // DOWN: toggle weather display.
      bool v = !display_getShowWeather();
      display_setShowWeather(v);
      Preferences p; p.begin("wifi", false); p.putBool("showWx",  v); p.end();
    } else if (!menu_isActive() && btn == BTN_B) {
      // B: toggle 12h/24h time format.
      bool v = !display_get12h();
      display_set12h(v);
      Preferences p; p.begin("wifi", false); p.putBool("h12", v); p.end();
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
  static uint32_t lastBatt = (uint32_t)-10000;  // fire on first iteration
  static uint32_t lastWeather = (uint32_t)-60000; // fire on first iteration
  uint32_t now = millis();
  if (now - lastBatt >= 10000) {
    lastBatt = now;
    battery_update();
  }
  // Retry weather every 15s while not yet valid, then every 60s.
  uint32_t wxInterval = weather_valid() ? 60000 : 15000;
  if (now - lastWeather >= wxInterval) {
    lastWeather = now;
    weather_update();
    if (weather_valid()) display_showTemp(weather_temp());
  }
  if (now - lastRefresh >= interval) {
    lastRefresh = now;
    display_showClock();
    if (weather_valid()) display_showTemp(weather_temp());
    display_showBattery(battery_percent(), battery_voltage());
  }
}
