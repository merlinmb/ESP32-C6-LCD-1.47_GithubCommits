#pragma once
#include <Arduino.h>

struct Config {
    char     wifi_ssid[64];
    char     wifi_password[64];
    char     github_token[128];
    char     github_username[64];
    uint8_t  brightness;            // 0-100 percent (100 = full, default)
    uint16_t screen_switch_secs;
    uint16_t refresh_interval_min;
    uint8_t  anim_top_pct;
    uint16_t anim_period_ms;
    uint16_t rgb_period_min_ms;     // fastest breath period at max streak (default 1200)
    uint16_t rgb_period_max_ms;     // slowest breath period at streak 0 (default 8000)
    uint8_t  rgb_streak_max;        // streak length that achieves min period (default 30)
    // MQTT brightness control
    char     mqtt_broker[64];                // MQTT broker hostname/IP (empty = disabled)
    uint16_t mqtt_port;                      // MQTT broker port (default 1883)
    char     mqtt_combined_topic[128];       // sets BOTH LCD + LED brightness (applied first)
    char     mqtt_lcd_topic[128];            // LCD brightness only (0-100)
    char     mqtt_led_brightness_topic[128]; // LED brightness only (0-100)
    // RGB LED brightness
    uint8_t  rgb_brightness;                 // 0-100 percent (default 100)
    // Display orientation
    uint8_t  flip_screen;                    // 0 = normal, 1 = 180° flipped
};

void config_load(Config &cfg);
void config_save(const Config &cfg);
void config_apply_defaults(Config &cfg);
void config_reset();   // clears all NVS preferences, forcing initial setup on next boot
