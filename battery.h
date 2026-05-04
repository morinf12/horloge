#pragma once
#include <Arduino.h>

void battery_begin();
void battery_update();       // call periodically (~1s)
float battery_voltage();     // last read voltage (V)
uint8_t battery_percent();   // estimated SoC 0-100%
