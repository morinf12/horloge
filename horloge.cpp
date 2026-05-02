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
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n[Horloge] boot"));

  display_begin();
  display_showBoot();
  buttons_begin();

  webui_begin();

  // Show IP address on boot screen
  IPAddress ip = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
  display_showIP(ip.toString().c_str());

  delay(5000);
}

void loop() {
  webui_loop();

  Button btn = buttons_poll();
  if (btn != BTN_NONE) {
    Serial.printf("[BTN] %d\n", btn);
    // TODO: handle menu navigation
  }

  static uint32_t lastRefresh = 0;
  uint32_t now = millis();
  if (now - lastRefresh >= 500) {
    lastRefresh = now;
    display_showClock();
  }
}
