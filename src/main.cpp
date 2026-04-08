// GitHub Contributions Monitor
// ESP32-C6 + ST7789 172x320 IPS display, LVGL v9

#include <WiFi.h>
#include <Arduino.h>
#include <lvgl.h>
#include "PINS_ESP32-C6-LCD-1_47.h"
#include "secrets.h"
#include "config.h"
#include "github_api.h"
#include "display_grid.h"
#include "display_stats.h"
#include "web_server.h"
#include "rgb_led.h"

static Config     g_cfg;
static GithubData g_data;
static lv_obj_t  *g_screen_grid  = nullptr;
static lv_obj_t  *g_screen_stats = nullptr;
static bool       g_show_grid    = true;
static uint32_t   g_last_fetch_ms = 0;
static lv_display_t *disp = nullptr;

// ── LVGL callbacks ────────────────────────────────────────────────────────────

static uint32_t millis_cb() { return millis(); }

static void my_disp_flush(lv_display_t *d, const lv_area_t *area, uint8_t *px_map) {
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map,
                            lv_area_get_width(area), lv_area_get_height(area));
    lv_disp_flush_ready(d);
}

#if LV_USE_LOG != 0
static void my_log_print(lv_log_level_t level, const char *buf) {
    LV_UNUSED(level);
    Serial.println(buf);
}
#endif

// ── Display brightness ────────────────────────────────────────────────────────

static void apply_brightness(uint8_t val) {
    ledcAttachChannel(GFX_BL, 1000, 8, 1);
    ledcWrite(GFX_BL, val);
}

// ── Screen switch timer ───────────────────────────────────────────────────────

static void screen_switch_cb(lv_timer_t *) {
    g_show_grid = !g_show_grid;
    lv_obj_t *next = g_show_grid ? g_screen_grid : g_screen_stats;
    lv_scr_load_anim(next, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
}

// ── Data refresh ──────────────────────────────────────────────────────────────

static void do_fetch() {
    Serial.println("[Main] Fetching GitHub data...");
    display_grid_stop_animations();
    bool ok = github_fetch(g_cfg.github_username, g_cfg.github_token, g_data);
    if (ok) {
        g_last_fetch_ms = millis();
        display_grid_update(g_data, g_cfg);
        display_stats_update(g_data);
        display_stats_set_age(0);
        rgb_led_update_params(g_data, g_cfg);
    } else {
        Serial.println("[Main] Fetch failed");
    }
}

static void refresh_timer_cb(lv_timer_t *) {
    // Update age label before fetch
    uint32_t age_ms = millis() - g_last_fetch_ms;
    display_stats_set_age(age_ms / 60000UL);
    do_fetch();
}

// ── WiFi connection splash ────────────────────────────────────────────────────

static void wifi_connect_splash() {
    lv_obj_t *spinner = lv_spinner_create(lv_scr_act());
    lv_spinner_set_anim_params(spinner, 8000, 200);
    lv_obj_set_size(spinner, 80, 80);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x39d353), 0);
    lv_obj_set_width(lbl, 280);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);

    const char *ssid = g_cfg.wifi_ssid[0] ? g_cfg.wifi_ssid : WIFI_SSID;
    const char *pass = g_cfg.wifi_password[0] ? g_cfg.wifi_password : WIFI_PASSWORD;

    lv_label_set_text_fmt(lbl, "Connecting to\n%s", ssid);
    lv_obj_align_to(lbl, spinner, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) {
        lv_timer_handler();
        delay(10);
    }
    Serial.printf("[WiFi] Connected, IP: %s\n", WiFi.localIP().toString().c_str());

    lv_obj_del(spinner);
    lv_obj_del(lbl);
}

// ── AP mode splash (first boot, no creds in NVS) ─────────────────────────────

static void ap_mode_splash() {
    lv_obj_t *lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xd29922), 0);
    lv_obj_set_width(lbl, 300);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl,
        "Setup mode\n"
        "Connect Wi-Fi to:\n"
        "GithubMonitor-Setup\n"
        "Then open:\n"
        "http://192.168.4.1");
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
}

// ── LVGL init ────────────────────────────────────────────────────────────────

static void lvgl_init() {
    lv_init();
    lv_tick_set_cb(millis_cb);
#if LV_USE_LOG != 0
    lv_log_register_print_cb(my_log_print);
#endif
    uint32_t w = gfx->width();
    uint32_t h = gfx->height();
    uint32_t buf_size = w * 40;

    lv_color_t *buf = (lv_color_t *)heap_caps_malloc(buf_size * 2,
                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) buf = (lv_color_t *)heap_caps_malloc(buf_size * 2, MALLOC_CAP_8BIT);
    if (!buf) { Serial.println("LVGL buf alloc failed!"); while (true); }

    disp = lv_display_create(w, h);
    lv_display_set_flush_cb(disp, my_disp_flush);
    lv_display_set_buffers(disp, buf, NULL, buf_size * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
}

// ── setup ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    DEV_DEVICE_INIT();
    delay(2000);

    if (!gfx->begin()) { Serial.println("GFX init failed!"); while (true); }
    gfx->setRotation(1); // landscape: 320x172
    gfx->fillScreen(RGB565_BLACK);
    apply_brightness(200); // initial brightness before config loads

    lvgl_init();

    config_load(g_cfg);
    apply_brightness(g_cfg.brightness);
    rgb_led_init(g_cfg);

    if (g_cfg.wifi_ssid[0] == '\0') {
        // No WiFi creds in NVS — start AP setup mode
        WiFi.softAP("GithubMonitor-Setup");
        Serial.println("[WiFi] AP mode: GithubMonitor-Setup @ 192.168.4.1");
        ap_mode_splash();
        web_server_start(g_cfg);
        // Stay in AP loop — web server POST /save will reboot device
        while (true) {
            web_server_handle();
            lv_timer_handler();
            delay(5);
        }
    }

    wifi_connect_splash();
    web_server_start(g_cfg);

    do_fetch();

    g_screen_grid  = display_grid_build(g_cfg.github_username);
    g_screen_stats = display_stats_build(g_cfg.github_username);

    if (g_data.valid) {
        display_grid_update(g_data, g_cfg);
        display_stats_update(g_data);
        display_stats_set_age(0);
    }

    lv_scr_load(g_screen_grid);

    lv_timer_create(screen_switch_cb,
                    (uint32_t)g_cfg.screen_switch_secs * 1000UL, nullptr);
    lv_timer_create(refresh_timer_cb,
                    (uint32_t)g_cfg.refresh_interval_min * 60UL * 1000UL, nullptr);

    Serial.println("[Main] Setup complete");
}

// ── loop ──────────────────────────────────────────────────────────────────────

void loop() {
    lv_timer_handler();
    rgb_led_tick();
    web_server_handle();
    delay(5);
}
