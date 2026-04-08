#pragma once
#include <Arduino.h>

struct Config {
    char     wifi_ssid[64];
    char     wifi_password[64];
    char     github_token[128];
    char     github_username[64];
    uint8_t  brightness;
    uint16_t screen_switch_secs;
    uint16_t refresh_interval_min;
    uint8_t  anim_top_pct;
    uint16_t anim_period_ms;
    uint16_t rgb_period_min_ms;  // fastest breath period at max streak (default 1200)
    uint16_t rgb_period_max_ms;  // slowest breath period at streak 0 (default 8000)
    uint8_t  rgb_streak_max;     // streak length that achieves min period (default 30)
};

void config_load(Config &cfg);
void config_save(const Config &cfg);
void config_apply_defaults(Config &cfg);
