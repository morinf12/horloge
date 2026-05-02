#pragma once
#include <Arduino.h>
#include "buttons.h"

void menu_begin();
bool menu_isActive();
void menu_handleButton(Button btn);
void menu_draw();   // call every ~100ms while active
