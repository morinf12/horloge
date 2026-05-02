#pragma once
#include <Arduino.h>

enum Button : uint8_t {
  BTN_NONE = 0,
  BTN_UP,
  BTN_DOWN,
  BTN_LEFT,
  BTN_RIGHT,
  BTN_A,
  BTN_B
};

void buttons_begin();
Button buttons_poll();   // returns pressed button (with debounce), or BTN_NONE
