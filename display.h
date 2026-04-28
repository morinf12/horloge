#pragma once
#include <Arduino.h>

void display_begin();
void display_showBoot();
void display_showClock();
void display_setSchedule(uint16_t dayMin, uint16_t nightMin);
void display_setColors(uint16_t dayFg, uint16_t nightFg);
void display_setBacklight(uint8_t dayPct, uint8_t nightPct);
