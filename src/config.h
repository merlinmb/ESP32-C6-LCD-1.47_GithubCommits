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
};

void config_load(Config &cfg);
void config_save(const Config &cfg);
void config_apply_defaults(Config &cfg);
