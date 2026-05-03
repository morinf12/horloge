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

static const uint32_t DEBOUNCE_MS     = 50;
static const uint32_t REPEAT_DELAY_MS = 1000;  // 1s before repeat starts
static const uint32_t REPEAT_RATE_MS  = 150;   // repeat interval once started

static int8_t   s_heldIdx     = -1;     // index of currently held button (-1 = none)
static uint32_t s_pressTime   = 0;      // when button was first pressed
static bool     s_fired       = false;  // initial press already reported
static bool     s_repeating   = false;  // in repeat mode
static uint32_t s_lastRepeat  = 0;      // last repeat fire time

void buttons_begin() {
  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    pinMode(s_pins[i], INPUT_PULLUP);
  }
}

bool buttons_isHeld(Button btn) {
  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    if (s_btns[i] == btn) return digitalRead(s_pins[i]) == LOW;
  }
  return false;
}

Button buttons_poll() {
  uint32_t now = millis();

  // Check if held button is still pressed
  if (s_heldIdx >= 0) {
    if (digitalRead(s_pins[s_heldIdx]) == LOW) {
      // Still held — check repeat
      if (!s_repeating) {
        if (now - s_pressTime >= REPEAT_DELAY_MS) {
          s_repeating = true;
          s_lastRepeat = now;
          return s_btns[s_heldIdx];
        }
      } else {
        if (now - s_lastRepeat >= REPEAT_RATE_MS) {
          s_lastRepeat = now;
          return s_btns[s_heldIdx];
        }
      }
      return BTN_NONE;
    } else {
      // Released
      s_heldIdx = -1;
      s_repeating = false;
    }
  }

  // Scan for new press
  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    if (digitalRead(s_pins[i]) == LOW) {
      // Debounce: wait a bit and re-check
      delay(DEBOUNCE_MS);
      if (digitalRead(s_pins[i]) == LOW) {
        s_heldIdx    = i;
        s_pressTime  = now;
        s_fired      = true;
        s_repeating  = false;
        return s_btns[i];
      }
    }
  }
  return BTN_NONE;
}
