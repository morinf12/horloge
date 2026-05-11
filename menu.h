#pragma once
#include <Arduino.h>
#include "buttons.h"

void menu_begin();
bool menu_isActive();
void menu_open();
void menu_handleButton(Button btn);
void menu_draw();   // call every ~100ms while active
void menu_getStateJson(String& out);

// Open-menu button chord: false = A + DOWN held together (default, less prone
// to accidental opens); true = A alone is enough.
bool menu_getOpenWithA();
void menu_setOpenWithA(bool on);
