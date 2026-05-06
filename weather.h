#pragma once
#include <Arduino.h>

void weather_begin();
void weather_update();        // call periodically; fetches if interval elapsed
float weather_temp();         // last known temperature in °C (NAN if unavailable)
const char* weather_desc();   // short description ("clair", "nuageux", etc.)
bool weather_valid();         // true if we have a recent reading
