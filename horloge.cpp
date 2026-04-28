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

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n[Horloge] boot"));

  display_begin();
  display_showBoot();

  webui_begin();
}

void loop() {
  webui_loop();

  static uint32_t lastRefresh = 0;
  uint32_t now = millis();
  if (now - lastRefresh >= 500) {
    lastRefresh = now;
    display_showClock();
  }
}
