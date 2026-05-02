#include "buttons.h"
#include "config.h"

static const uint8_t s_pins[] = {
  BTN_UP_PIN, BTN_DOWN_PIN, BTN_LEFT_PIN,
  BTN_RIGHT_PIN, BTN_A_PIN, BTN_B_PIN
};
static const Button s_btns[] = {
  BTN_UP, BTN_DOWN, BTN_LEFT,
  BTN_RIGHT, BTN_A, BTN_B
};
static const uint8_t NUM_BTNS = sizeof(s_pins) / sizeof(s_pins[0]);

static uint32_t s_lastPress = 0;
static const uint32_t DEBOUNCE_MS = 200;

void buttons_begin() {
  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    pinMode(s_pins[i], INPUT_PULLUP);
  }
}

Button buttons_poll() {
  uint32_t now = millis();
  if (now - s_lastPress < DEBOUNCE_MS) return BTN_NONE;

  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    if (digitalRead(s_pins[i]) == LOW) {
      s_lastPress = now;
      return s_btns[i];
    }
  }
  return BTN_NONE;
}
