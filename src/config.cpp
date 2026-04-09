#include "config.h"
#include <Preferences.h>

static Preferences prefs;

void config_apply_defaults(Config &cfg) {
    // Clamp brightness: 0 = unset → 100%, >100 = legacy 0-255 value → reset to 100%
    if (cfg.brightness == 0 || cfg.brightness > 100) cfg.brightness = 100;
    if (cfg.screen_switch_secs == 0)   cfg.screen_switch_secs   = 30;
    if (cfg.refresh_interval_min == 0) cfg.refresh_interval_min = 30;
    if (cfg.anim_top_pct == 0)         cfg.anim_top_pct         = 20;
    if (cfg.anim_period_ms == 0)       cfg.anim_period_ms       = 2000;
    if (cfg.rgb_period_min_ms == 0) cfg.rgb_period_min_ms = 1200;
    if (cfg.rgb_period_max_ms == 0) cfg.rgb_period_max_ms = 8000;
    if (cfg.rgb_streak_max    == 0) cfg.rgb_streak_max    = 30;
    if (cfg.mqtt_port == 0) cfg.mqtt_port = 1883;
    if (cfg.mqtt_combined_topic[0] == '\0')
        strncpy(cfg.mqtt_combined_topic, "cmnd/mcmddevices/brightnesspercentage",
                sizeof(cfg.mqtt_combined_topic) - 1);
    if (cfg.mqtt_lcd_topic[0] == '\0')
        strncpy(cfg.mqtt_lcd_topic, "cmnd/mcmddevices/lcdbrightness",
                sizeof(cfg.mqtt_lcd_topic) - 1);
    if (cfg.mqtt_led_brightness_topic[0] == '\0')
        strncpy(cfg.mqtt_led_brightness_topic, "cmnd/mcmddevices/ledbrightness",
                sizeof(cfg.mqtt_led_brightness_topic) - 1);
    if (cfg.rgb_brightness == 0) cfg.rgb_brightness = 100;
}

void config_load(Config &cfg) {
    memset(&cfg, 0, sizeof(cfg));
    prefs.begin("ghmon", true); // read-only
    prefs.getString("wifi_ssid",  cfg.wifi_ssid,       sizeof(cfg.wifi_ssid));
    prefs.getString("wifi_pass",  cfg.wifi_password,   sizeof(cfg.wifi_password));
    prefs.getString("gh_token",   cfg.github_token,    sizeof(cfg.github_token));
    prefs.getString("gh_user",    cfg.github_username, sizeof(cfg.github_username));
    cfg.brightness           = prefs.getUChar("brightness",   0);
    cfg.screen_switch_secs   = prefs.getUShort("switch_sec",  0);
    cfg.refresh_interval_min = prefs.getUShort("refresh_min", 0);
    cfg.anim_top_pct         = prefs.getUChar("anim_pct",     0);
    cfg.anim_period_ms       = prefs.getUShort("anim_ms",     0);
    cfg.rgb_period_min_ms = prefs.getUShort("rgb_pmin", 0);
    cfg.rgb_period_max_ms = prefs.getUShort("rgb_pmax", 0);
    cfg.rgb_streak_max    = prefs.getUChar( "rgb_smax", 0);
    prefs.getString("mqtt_host",     cfg.mqtt_broker,              sizeof(cfg.mqtt_broker));
    cfg.mqtt_port       = prefs.getUShort("mqtt_port", 0);
    prefs.getString("mqtt_ctopic",   cfg.mqtt_combined_topic,      sizeof(cfg.mqtt_combined_topic));
    prefs.getString("mqtt_lcd",      cfg.mqtt_lcd_topic,           sizeof(cfg.mqtt_lcd_topic));
    prefs.getString("mqtt_ltopic",   cfg.mqtt_led_brightness_topic,sizeof(cfg.mqtt_led_brightness_topic));
    cfg.rgb_brightness  = prefs.getUChar("rgb_bright", 0);
    prefs.end();
    config_apply_defaults(cfg);
}

void config_reset() {
    prefs.begin("ghmon", false);
    prefs.clear();
    prefs.end();
}

void config_save(const Config &cfg) {
    prefs.begin("ghmon", false); // read-write
    prefs.putString("wifi_ssid",  cfg.wifi_ssid);
    prefs.putString("wifi_pass",  cfg.wifi_password);
    prefs.putString("gh_token",   cfg.github_token);
    prefs.putString("gh_user",    cfg.github_username);
    prefs.putUChar( "brightness",   cfg.brightness);
    prefs.putUShort("switch_sec",   cfg.screen_switch_secs);
    prefs.putUShort("refresh_min",  cfg.refresh_interval_min);
    prefs.putUChar( "anim_pct",     cfg.anim_top_pct);
    prefs.putUShort("anim_ms",      cfg.anim_period_ms);
    prefs.putUShort("rgb_pmin", cfg.rgb_period_min_ms);
    prefs.putUShort("rgb_pmax", cfg.rgb_period_max_ms);
    prefs.putUChar( "rgb_smax", cfg.rgb_streak_max);
    prefs.putString("mqtt_host",   cfg.mqtt_broker);
    prefs.putUShort("mqtt_port",   cfg.mqtt_port);
    prefs.putString("mqtt_ctopic", cfg.mqtt_combined_topic);
    prefs.putString("mqtt_lcd",    cfg.mqtt_lcd_topic);
    prefs.putString("mqtt_ltopic", cfg.mqtt_led_brightness_topic);
    prefs.putUChar( "rgb_bright",  cfg.rgb_brightness);
    prefs.end();
}
