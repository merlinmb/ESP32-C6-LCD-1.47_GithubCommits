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
#include "ui_fonts.h"
#include "screen_brightness.h"
#include "mqtt_client.h"

static Config     g_cfg;
static GithubData g_data;
static lv_obj_t  *g_screen_grid  = nullptr;
static lv_obj_t  *g_screen_stats = nullptr;
static bool       g_show_grid    = true;
static bool       g_display_ready = false;
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
    static bool backlight_channel_attached = false;
    if (!backlight_channel_attached) {
        ledcAttachChannel(GFX_BL, 1000, 8, 1);
        backlight_channel_attached = true;
    }
    ledcWrite(GFX_BL, val);
}

// Public API: set brightness by percentage (0-100). Maps to 0-255 for LEDC.
void set_screen_brightness_pct(uint8_t pct) {
    if (pct > 100) pct = 100;
    apply_brightness((uint8_t)((pct * 255UL) / 100UL));
}

// ── Screen switch timer ───────────────────────────────────────────────────────
// Grid stays visible 3× longer than stats to give the contribution view priority.

static lv_timer_t *g_screen_switch_timer = nullptr;

static void screen_switch_cb(lv_timer_t *t) {
    g_show_grid = !g_show_grid;
    lv_obj_t *next = g_show_grid ? g_screen_grid : g_screen_stats;
    if (!next) return;
    lv_scr_load_anim(next, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);

    // Grid gets 3× the configured interval; stats gets 1×
    uint32_t next_ms = g_show_grid
        ? (uint32_t)g_cfg.screen_switch_secs * 3000UL
        : (uint32_t)g_cfg.screen_switch_secs * 1000UL;
    lv_timer_set_period(t, next_ms);
}

// ── Async data refresh via FreeRTOS task ──────────────────────────────────────

static const uint32_t FETCH_TIMEOUT_MS  = 30000; // hard kill if task hangs
static const int      FETCH_MAX_RETRIES = 3;
static const uint32_t FETCH_RETRY_MS    = 5000;

// State shared between main loop and fetch task — written only by the task,
// read only by the main loop (after task exits), so no mutex needed.
enum FetchState { FETCH_IDLE, FETCH_RUNNING, FETCH_DONE_OK, FETCH_DONE_FAIL };
static volatile FetchState g_fetch_state = FETCH_IDLE;
static GithubData          g_fetch_result; // scratch buffer written by task
static TaskHandle_t        g_fetch_task   = nullptr;

static void fetch_task(void *) {
    bool ok = false;
    for (int attempt = 1; attempt <= FETCH_MAX_RETRIES; attempt++) {
        Serial.printf("[Fetch] Attempt %d/%d\n", attempt, FETCH_MAX_RETRIES);
        ok = github_fetch(g_cfg.github_username, g_cfg.github_token, g_fetch_result);
        if (ok) break;
        if (attempt < FETCH_MAX_RETRIES) {
            Serial.printf("[Fetch] Failed, retrying in %u ms...\n", FETCH_RETRY_MS);
            vTaskDelay(pdMS_TO_TICKS(FETCH_RETRY_MS));
        }
    }
    g_fetch_state = ok ? FETCH_DONE_OK : FETCH_DONE_FAIL;
    g_fetch_task  = nullptr;
    vTaskDelete(nullptr);
}

// Called from main loop — applies a completed fetch result to the display.
static void apply_fetch_result(bool ok) {
    if (ok) {
        g_last_fetch_ms = millis();
        g_data = g_fetch_result;
        if (g_display_ready) {
            display_grid_update(g_data, g_cfg);
            display_stats_update(g_data);
            display_stats_set_age(0);
        }
    } else {
        Serial.println("[Fetch] All attempts failed");
    }
    rgb_led_update_params(g_data, g_cfg);
}

// Kick off an async fetch. If one is already running, do nothing.
static void do_fetch() {
    if (g_fetch_state == FETCH_RUNNING) return;
    display_grid_stop_animations();
    g_fetch_state = FETCH_RUNNING;
    memset(&g_fetch_result, 0, sizeof(g_fetch_result));
    xTaskCreate(fetch_task, "gh_fetch", 8192, nullptr, 1, &g_fetch_task);
}

// Called every loop iteration — checks if the fetch task finished or timed out.
static uint32_t g_fetch_started_ms = 0;
static void fetch_tick() {
    if (g_fetch_state == FETCH_IDLE) return;

    if (g_fetch_state == FETCH_RUNNING) {
        // Record when we started (first tick after RUNNING is set)
        if (g_fetch_started_ms == 0) g_fetch_started_ms = millis();

        // Hard timeout: kill the task if it's been stuck too long
        if (millis() - g_fetch_started_ms >= FETCH_TIMEOUT_MS) {
            Serial.println("[Fetch] Timeout — killing stuck task");
            if (g_fetch_task) { vTaskDelete(g_fetch_task); g_fetch_task = nullptr; }
            g_fetch_state     = FETCH_DONE_FAIL;
            g_fetch_started_ms = 0;
        }
        return;
    }

    // Task finished
    g_fetch_started_ms = 0;
    bool ok = (g_fetch_state == FETCH_DONE_OK);
    g_fetch_state = FETCH_IDLE;
    apply_fetch_result(ok);
}

static void refresh_timer_cb(lv_timer_t *) {
    if (g_last_fetch_ms != 0) {
        uint32_t age_ms = millis() - g_last_fetch_ms;
        display_stats_set_age(age_ms / 60000UL);
    }
    do_fetch();
}

// ── WiFi connection splash ────────────────────────────────────────────────────

static void wifi_connect_splash() {
    lv_obj_t *spinner = lv_spinner_create(lv_scr_act());
    lv_spinner_set_anim_params(spinner, 8000, 200);
    lv_obj_set_size(spinner, 80, 80);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lbl, ui_font_label(), 0);
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
    lv_obj_set_style_text_font(lbl, ui_font_label(), 0);
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
    gfx->setRotation(1); // landscape: 320x172 (pre-config default)
    gfx->fillScreen(RGB565_BLACK);
    set_screen_brightness_pct(100); // full brightness during splash

    lvgl_init();
    ui_fonts_init();

    config_load(g_cfg);
    gfx->setRotation(g_cfg.flip_screen ? 3 : 1); // apply saved orientation
    set_screen_brightness_pct(g_cfg.brightness);
    rgb_led_init(g_cfg);

    if (g_cfg.wifi_ssid[0] == '\0') {
        // No WiFi creds in NVS — start AP setup mode
        WiFi.mode(WIFI_AP);
        WiFi.softAP("GithubMonitor-Setup", nullptr, 6, false, 4);
        Serial.println("[WiFi] AP mode: GithubMonitor-Setup @ 192.168.4.1");
        ap_mode_splash();
        web_server_start(g_cfg, [](const Config &cfg) {
            gfx->setRotation(cfg.flip_screen ? 3 : 1);
            set_screen_brightness_pct(cfg.brightness);
            rgb_led_set_brightness_pct(cfg.rgb_brightness);
        });
        // Stay in AP loop — web server POST /save will reboot device
        while (true) {
            web_server_handle();
            lv_timer_handler();
            delay(5);
        }
    }

    wifi_connect_splash();
    web_server_start(g_cfg, [](const Config &cfg) {
        gfx->setRotation(cfg.flip_screen ? 3 : 1);
        set_screen_brightness_pct(cfg.brightness);
        rgb_led_set_brightness_pct(cfg.rgb_brightness);
        rgb_led_update_params(g_data, cfg);
        mqtt_client_reinit();
    });
    mqtt_client_init(g_cfg);

    g_screen_grid  = display_grid_build(g_cfg.github_username);
    g_screen_stats = display_stats_build(g_cfg.github_username);
    g_display_ready = (g_screen_grid != nullptr && g_screen_stats != nullptr);

    lv_scr_load(g_screen_grid ? g_screen_grid : lv_scr_act());

    if (g_display_ready) {
        do_fetch();
    }

    // Grid is shown first, so start with 3× interval
    g_screen_switch_timer = lv_timer_create(screen_switch_cb,
                    (uint32_t)g_cfg.screen_switch_secs * 3000UL, nullptr);
    lv_timer_create(refresh_timer_cb,
                    (uint32_t)g_cfg.refresh_interval_min * 60UL * 1000UL, nullptr);

    Serial.println("[Main] Setup complete");
}

// ── WiFi watchdog ─────────────────────────────────────────────────────────────
// The ESP32 WiFi driver auto-reconnects, but it can silently stall after a
// prolonged outage. This watchdog calls WiFi.reconnect() if the link has been
// down for more than WIFI_RECONNECT_MS, which kicks the driver without a reboot.

static const uint32_t WIFI_RECONNECT_MS = 30000; // 30 s before forcing reconnect
static uint32_t g_wifi_lost_ms = 0;

static void wifi_watchdog_tick() {
    if (WiFi.status() == WL_CONNECTED) {
        g_wifi_lost_ms = 0;
        return;
    }
    uint32_t now = millis();
    if (g_wifi_lost_ms == 0) {
        g_wifi_lost_ms = now;
        Serial.println("[WiFi] Lost connection");
        return;
    }
    if (now - g_wifi_lost_ms >= WIFI_RECONNECT_MS) {
        Serial.println("[WiFi] Reconnecting...");
        WiFi.reconnect();
        g_wifi_lost_ms = now; // reset timer so we don't spam reconnect
    }
}

// ── loop ──────────────────────────────────────────────────────────────────────

void loop() {
    wifi_watchdog_tick();
    fetch_tick();
    lv_timer_handler();
    rgb_led_tick();
    web_server_handle();
    mqtt_client_tick();
    delay(5);
}
