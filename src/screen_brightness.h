#pragma once
#include <Arduino.h>

// Set LCD backlight brightness by percentage (0 = off, 100 = full).
// Safe to call at any time after gfx->begin().
void set_screen_brightness_pct(uint8_t pct);
