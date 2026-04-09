#include "rgb_led.h"
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <math.h>

#define NEOPIXEL_PIN 8
#define NEOPIXEL_COUNT 1

static Adafruit_NeoPixel s_pixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Current breathing parameters (updated by rgb_led_update_params)
static uint32_t s_period_ms   = 8000;
static uint8_t  s_base_r      = 20;
static uint8_t  s_base_g      = 20;
static uint8_t  s_base_b      = 40;

// Previous written color (to avoid redundant writes)
static uint8_t s_prev_r = 255;
static uint8_t s_prev_g = 255;
static uint8_t s_prev_b = 255;

// Current brightness scalar 0-100 (applied as multiplier in tick)
static uint8_t s_brightness_pct = 100;

void rgb_led_init(const Config &cfg) {
    s_pixel.begin();
    s_pixel.setBrightness(255);
    s_pixel.clear();
    s_pixel.show();
    s_brightness_pct = cfg.rgb_brightness;
}

void rgb_led_update_params(const GithubData &data, const Config &cfg) {
    uint16_t streak = data.valid ? data.current_streak : 0;

    if (streak == 0) {
        // Idle: slow white-blue pulse
        s_period_ms = cfg.rgb_period_max_ms;
        s_base_r    = 20;
        s_base_g    = 20;
        s_base_b    = 40;
        return;
    }

    // Map streak to period via linear interpolation
    float ratio = (float)streak / (float)cfg.rgb_streak_max;
    if (ratio > 1.0f) ratio = 1.0f;

    s_period_ms = (uint32_t)(cfg.rgb_period_max_ms -
                  ratio * (cfg.rgb_period_max_ms - cfg.rgb_period_min_ms));

    // Map streak to color: (0,60,0) -> (80,255,0)
    s_base_r = (uint8_t)(0   + ratio * 80);
    s_base_g = (uint8_t)(60  + ratio * (255 - 60));
    s_base_b = 0;
}

void rgb_led_tick() {
    uint32_t t = millis();
    float phase      = (float)(t % s_period_ms) / (float)s_period_ms;
    float brightness = (sinf(phase * 2.0f * M_PI - M_PI / 2.0f) + 1.0f) / 2.0f;
    float led_scale  = (float)s_brightness_pct / 100.0f;

    uint8_t r = (uint8_t)(s_base_r * brightness * led_scale);
    uint8_t g = (uint8_t)(s_base_g * brightness * led_scale);
    uint8_t b = (uint8_t)(s_base_b * brightness * led_scale);

    // Only write when color changes by at least 1 on any channel
    if (abs((int)r - (int)s_prev_r) < 1 &&
        abs((int)g - (int)s_prev_g) < 1 &&
        abs((int)b - (int)s_prev_b) < 1) {
        return;
    }

    s_prev_r = r;
    s_prev_g = g;
    s_prev_b = b;
    s_pixel.setPixelColor(0, s_pixel.Color(r, g, b));
    s_pixel.show();
}

void rgb_led_set_brightness_pct(uint8_t pct) {
    if (pct > 100) pct = 100;
    s_brightness_pct = pct;
    // Force a redraw on the next tick by invalidating the cache
    s_prev_r = 255; s_prev_g = 255; s_prev_b = 255;
}
