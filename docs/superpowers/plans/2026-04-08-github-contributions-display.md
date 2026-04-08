# GitHub Contributions Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Binance crypto monitor with a GitHub contributions heatmap display on an ESP32-C6 with ST7789 172×320px LCD, featuring animated top-activity cells, a stats screen, and a web-based config UI.

**Architecture:** Six source modules (`config`, `github_api`, `display_grid`, `display_stats`, `web_server`, `main`) wired together in `main.cpp`. All persistent settings live in NVS via `Preferences.h`. LVGL v9 drives the display; two screen objects (grid + stats) are built once and switched on a timer.

**Tech Stack:** ESP32-C6, Arduino framework, PlatformIO, LVGL v9.2.2, GFX Library for Arduino, `Preferences.h` (NVS), `WebServer.h` (built-in), `HTTPClient.h`, `WiFi.h`

---

## File Map

| Action | Path | Responsibility |
|--------|------|---------------|
| Modify | `src/main.cpp` | setup(), loop(), screen-switch timer, data-refresh timer |
| Create | `src/config.h` | Config struct, load/save declarations |
| Create | `src/config.cpp` | NVS read/write via Preferences, defaults |
| Create | `src/github_api.h` | ContributionDay struct, GithubData struct, fetch declaration |
| Create | `src/github_api.cpp` | GraphQL HTTP POST, manual JSON parsing |
| Create | `src/display_grid.h` | buildGridScreen(), updateGridData(), startGridAnimations(), stopGridAnimations() declarations |
| Create | `src/display_grid.cpp` | LVGL grid of 53×7 lv_obj_t cells, breathing lv_anim_t |
| Create | `src/display_stats.h` | buildStatsScreen(), updateStatsData() declarations |
| Create | `src/display_stats.cpp` | LVGL stats screen with streak/total/busiest labels |
| Create | `src/web_server.h` | startWebServer(), handleWebServer() declarations |
| Create | `src/web_server.cpp` | ESP32 WebServer, PROGMEM HTML config page, POST /save handler |
| Modify | `include/lv_conf.h` | Enable LV_FONT_MONTSERRAT_8, LV_FONT_MONTSERRAT_28, LV_FONT_UNSCII_8 |
| Create | `include/secrets.h.example` | Template with placeholder WiFi creds |
| Create | `.gitignore` | Ignore include/secrets.h, .pio/, .superpowers/ |

---

## Task 1: Project Scaffolding & Git Setup

**Files:**
- Create: `.gitignore`
- Create: `include/secrets.h.example`
- Modify: `include/lv_conf.h` (lines 484–514)

- [ ] **Step 1: Create .gitignore**

```
include/secrets.h
.pio/
.superpowers/
```
Save to `.gitignore` in the project root (`e:/GoogleDrive/Arduino/ESP32-C6-LCD-1.47_GithubCommits/.gitignore`).

- [ ] **Step 2: Create secrets.h.example**

```cpp
// Rename this file to secrets.h and fill in your credentials.
// secrets.h is git-ignored — never commit real credentials.
#define WIFI_SSID     "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"
```
Save to `include/secrets.h.example`.

- [ ] **Step 3: Enable required fonts in lv_conf.h**

In `include/lv_conf.h`, change:
```cpp
#define LV_FONT_MONTSERRAT_8  0
```
to:
```cpp
#define LV_FONT_MONTSERRAT_8  1
```

Change:
```cpp
#define LV_FONT_MONTSERRAT_28 0
```
to:
```cpp
#define LV_FONT_MONTSERRAT_28 1
```

Change:
```cpp
#define LV_FONT_UNSCII_8  0
```
to:
```cpp
#define LV_FONT_UNSCII_8  1
```

- [ ] **Step 4: Verify secrets.h exists (bootstrap)**

Confirm `include/secrets.h` exists with real WiFi creds. If not, copy from `secrets.h.example` and fill in. This file is only needed for first-flash bootstrap — after that NVS takes over.

---

## Task 2: Config Module

**Files:**
- Create: `src/config.h`
- Create: `src/config.cpp`

- [ ] **Step 1: Create src/config.h**

```cpp
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

void     config_load(Config &cfg);
void     config_save(const Config &cfg);
void     config_apply_defaults(Config &cfg);
```

- [ ] **Step 2: Create src/config.cpp**

```cpp
#include "config.h"
#include <Preferences.h>

static Preferences prefs;

void config_apply_defaults(Config &cfg) {
    if (cfg.brightness == 0)           cfg.brightness           = 200;
    if (cfg.screen_switch_secs == 0)   cfg.screen_switch_secs   = 30;
    if (cfg.refresh_interval_min == 0) cfg.refresh_interval_min = 30;
    if (cfg.anim_top_pct == 0)         cfg.anim_top_pct         = 20;
    if (cfg.anim_period_ms == 0)       cfg.anim_period_ms       = 2000;
}

void config_load(Config &cfg) {
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
    prefs.end();
    config_apply_defaults(cfg);
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
    prefs.end();
}
```

---

## Task 3: GitHub API Module

**Files:**
- Create: `src/github_api.h`
- Create: `src/github_api.cpp`

- [ ] **Step 1: Create src/github_api.h**

```cpp
#pragma once
#include <Arduino.h>

struct ContributionDay {
    uint16_t count;
    uint8_t  level; // 0–4
};

struct GithubData {
    ContributionDay days[53][7]; // [week][weekday 0=Sun]
    uint8_t         week_count;  // actual weeks returned (usually 53)
    uint16_t        total_contributions;
    uint16_t        busiest_day_count;
    uint16_t        current_streak;
    bool            valid;       // false until first successful fetch
};

// count → level using GitHub thresholds
uint8_t contribution_level(uint16_t count);

// Fetch contributions for username using token. Populates data in-place.
// Returns true on success. Calls lv_timer_handler() periodically during fetch.
bool github_fetch(const char *username, const char *token, GithubData &data);
```

- [ ] **Step 2: Create src/github_api.cpp**

```cpp
#include "github_api.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <lvgl.h>

static const char *GRAPHQL_URL = "https://api.github.com/graphql";

uint8_t contribution_level(uint16_t count) {
    if (count == 0)  return 0;
    if (count <= 3)  return 1;
    if (count <= 6)  return 2;
    if (count <= 9)  return 3;
    return 4;
}

// Parse a uint16 from JSON string like "contributionCount":7
static uint16_t parse_uint16_field(const String &body, const char *key, int start) {
    String search = String("\"") + key + "\":";
    int idx = body.indexOf(search, start);
    if (idx < 0) return 0;
    idx += search.length();
    // skip possible quote for string numbers
    if (body.charAt(idx) == '"') idx++;
    int end = idx;
    while (end < (int)body.length() && isDigit(body.charAt(end))) end++;
    return (uint16_t)body.substring(idx, end).toInt();
}

static void compute_streak_and_busiest(GithubData &data) {
    data.busiest_day_count = 0;
    data.current_streak = 0;

    // Flatten into a 1D array day-by-day (week*7+day), newest last
    // Scan backwards for streak
    bool in_streak = true;
    for (int w = (int)data.week_count - 1; w >= 0; w--) {
        for (int d = 6; d >= 0; d--) {
            uint16_t c = data.days[w][d];
            if (c > data.busiest_day_count) data.busiest_day_count = c;
            if (in_streak) {
                if (c > 0) data.current_streak++;
                else       in_streak = false;
            }
        }
    }
}

bool github_fetch(const char *username, const char *token, GithubData &data) {
    WiFiClientSecure client;
    client.setInsecure(); // no cert verification — acceptable for this use case

    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(client, GRAPHQL_URL)) return false;

    http.addHeader("Content-Type", "application/json");
    String auth = String("Bearer ") + token;
    http.addHeader("Authorization", auth);

    String query = String("{\"query\":\"{user(login:\\\"") + username +
        "\\\"){contributionsCollection{contributionCalendar{"
        "totalContributions weeks{contributionDays{"
        "contributionCount}}}}}}\"}" ;

    lv_timer_handler();
    int code = http.POST(query);
    lv_timer_handler();

    if (code != 200) {
        Serial.printf("[GitHub] HTTP %d\n", code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();
    lv_timer_handler();

    // Parse totalContributions
    data.total_contributions = parse_uint16_field(body, "totalContributions", 0);

    // Parse weeks/days — find each "contributionDays" block
    memset(data.days, 0, sizeof(data.days));
    data.week_count = 0;

    int pos = 0;
    while (data.week_count < 53) {
        int week_start = body.indexOf("\"contributionDays\":[", pos);
        if (week_start < 0) break;
        week_start += 20; // skip past the key
        pos = week_start;

        for (int d = 0; d < 7; d++) {
            int day_start = body.indexOf("{", pos);
            if (day_start < 0) break;
            int day_end = body.indexOf("}", day_start);
            if (day_end < 0) break;
            String day_obj = body.substring(day_start, day_end + 1);
            uint16_t count = parse_uint16_field(day_obj, "contributionCount", 0);
            data.days[data.week_count][d].count = count;
            data.days[data.week_count][d].level = contribution_level(count);
            pos = day_end + 1;
        }
        data.week_count++;
    }

    if (data.week_count == 0) {
        Serial.println("[GitHub] Parse failed — no weeks found");
        return false;
    }

    compute_streak_and_busiest(data);
    data.valid = true;
    Serial.printf("[GitHub] OK — %d weeks, %d contributions, streak %d\n",
                  data.week_count, data.total_contributions, data.current_streak);
    return true;
}
```

---

## Task 4: Display Grid Module

**Files:**
- Create: `src/display_grid.h`
- Create: `src/display_grid.cpp`

- [ ] **Step 1: Create src/display_grid.h**

```cpp
#pragma once
#include <lvgl.h>
#include "github_api.h"
#include "config.h"

// Build the grid screen. Call once after LVGL init.
lv_obj_t *display_grid_build(const char *username);

// Update cell colours and reassign animations from fresh data.
void display_grid_update(const GithubData &data, const Config &cfg);

// Stop all breathing animations (call before data refresh).
void display_grid_stop_animations();
```

- [ ] **Step 2: Create src/display_grid.cpp**

```cpp
#include "display_grid.h"
#include <Arduino.h>

#define GRID_WEEKS   53
#define GRID_DAYS    7
#define CELL_W       5
#define CELL_H       5
#define COL_GAP      1
#define ROW_GAP      2
#define FOOTER_H     18
#define LEFT_PAD     8

// Colour table: level 0–4
static const lv_color_t LEVEL_COLORS[5] = {
    lv_color_hex(0x161b22),
    lv_color_hex(0x0e4429),
    lv_color_hex(0x216e39),
    lv_color_hex(0x30a14e),
    lv_color_hex(0x39d353),
};
// Bright variants for animation peak
static const lv_color_t LEVEL_BRIGHT[5] = {
    lv_color_hex(0x161b22), // level 0 never animates
    lv_color_hex(0x1a6e40),
    lv_color_hex(0x3da860),
    lv_color_hex(0x60d880),
    lv_color_hex(0x80ff9a),
};

static lv_obj_t *s_screen      = nullptr;
static lv_obj_t *s_cells[GRID_WEEKS][GRID_DAYS];
static lv_obj_t *s_footer_label = nullptr;
static lv_anim_t s_anims[GRID_WEEKS * GRID_DAYS]; // pool
static uint8_t   s_cell_levels[GRID_WEEKS][GRID_DAYS];

// Month names for labels (every ~9 weeks = ~2 months)
static const char *MONTH_ABBR[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};

// Animation callback — sets bg colour by interpolating between base and bright
static void anim_color_cb(void *obj, int32_t val) {
    lv_obj_t *cell = (lv_obj_t *)obj;
    // val 0–255: 0=base, 255=bright
    uint8_t lvl = 0;
    // retrieve level stored in user_data
    lvl = (uint8_t)(uintptr_t)lv_obj_get_user_data(cell);
    lv_color_t base   = LEVEL_COLORS[lvl];
    lv_color_t bright = LEVEL_BRIGHT[lvl];
    lv_color_t mixed  = lv_color_mix(bright, base, (uint8_t)val);
    lv_obj_set_style_bg_color(cell, mixed, 0);
}

lv_obj_t *display_grid_build(const char *username) {
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Grid+label unit height: 12 (labels) + 3 (gap) + (7*5 + 6*2) (grid) = 62
    // Available above footer: 172 - FOOTER_H = 154
    // top of unit = (154 - 62) / 2 = 46
    const int UNIT_TOP   = 46;
    const int LABEL_H    = 12;
    const int LABEL_GAP  = 3;
    const int GRID_TOP   = UNIT_TOP + LABEL_H + LABEL_GAP;

    // Month labels — evenly spaced across 53 weeks
    // Place at weeks 0, 9, 18, 27, 36, 45 (~every 2 months)
    static const uint8_t label_weeks[6] = {0, 9, 18, 27, 36, 45};
    // We don't know which month week-0 corresponds to without date info,
    // so labels are positioned by pixel offset only; text is set in update.
    for (int i = 0; i < 6; i++) {
        lv_obj_t *lbl = lv_label_create(s_screen);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_8, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x8b949e), 0);
        lv_label_set_text(lbl, "---");
        lv_obj_set_pos(lbl, LEFT_PAD + label_weeks[i] * (CELL_W + COL_GAP), UNIT_TOP);
    }

    // Grid cells
    for (int w = 0; w < GRID_WEEKS; w++) {
        for (int d = 0; d < GRID_DAYS; d++) {
            lv_obj_t *cell = lv_obj_create(s_screen);
            lv_obj_set_size(cell, CELL_W, CELL_H);
            lv_obj_set_style_radius(cell, 1, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_set_style_bg_color(cell, LEVEL_COLORS[0], 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            int x = LEFT_PAD + w * (CELL_W + COL_GAP);
            int y = GRID_TOP + d * (CELL_H + ROW_GAP);
            lv_obj_set_pos(cell, x, y);
            s_cells[w][d] = cell;
            s_cell_levels[w][d] = 0;
        }
    }

    // Footer: username left, legend right
    lv_obj_t *footer = lv_obj_create(s_screen);
    lv_obj_set_size(footer, 320, FOOTER_H);
    lv_obj_set_pos(footer, 0, 172 - FOOTER_H);
    lv_obj_set_style_bg_color(footer, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    s_footer_label = lv_label_create(footer);
    lv_obj_set_style_text_font(s_footer_label, &lv_font_montserrat_8, 0);
    lv_obj_set_style_text_color(s_footer_label, lv_color_hex(0x8b949e), 0);
    char buf[80];
    snprintf(buf, sizeof(buf), "github.com/%s", username);
    lv_label_set_text(s_footer_label, buf);
    lv_obj_align(s_footer_label, LV_ALIGN_LEFT_MID, LEFT_PAD, 0);

    // Legend: 5 squares + "More"
    lv_obj_t *legend_cont = lv_obj_create(footer);
    lv_obj_set_style_bg_opa(legend_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(legend_cont, 0, 0);
    lv_obj_set_style_pad_all(legend_cont, 0, 0);
    lv_obj_clear_flag(legend_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(legend_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_column_dsc_array(legend_cont, NULL, 0);
    lv_obj_set_height(legend_cont, FOOTER_H);
    lv_obj_set_width(legend_cont, LV_SIZE_CONTENT);
    lv_obj_align(legend_cont, LV_ALIGN_RIGHT_MID, -LEFT_PAD, 0);

    for (int i = 0; i < 5; i++) {
        lv_obj_t *sq = lv_obj_create(legend_cont);
        lv_obj_set_size(sq, CELL_W, CELL_H);
        lv_obj_set_style_radius(sq, 1, 0);
        lv_obj_set_style_border_width(sq, 0, 0);
        lv_obj_set_style_pad_all(sq, 0, 0);
        lv_obj_set_style_bg_color(sq, LEVEL_COLORS[i], 0);
        lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, 0);
        lv_obj_set_style_margin_right(sq, 2, 0);
    }
    lv_obj_t *more_lbl = lv_label_create(legend_cont);
    lv_obj_set_style_text_font(more_lbl, &lv_font_montserrat_8, 0);
    lv_obj_set_style_text_color(more_lbl, lv_color_hex(0x8b949e), 0);
    lv_label_set_text(more_lbl, "More");

    return s_screen;
}

void display_grid_stop_animations() {
    for (int w = 0; w < GRID_WEEKS; w++) {
        for (int d = 0; d < GRID_DAYS; d++) {
            if (s_cells[w][d]) lv_anim_delete(s_cells[w][d], anim_color_cb);
        }
    }
}

void display_grid_update(const GithubData &data, const Config &cfg) {
    display_grid_stop_animations();

    // Collect all non-zero counts to compute threshold
    uint16_t counts[GRID_WEEKS * GRID_DAYS];
    int      count_n = 0;
    for (int w = 0; w < (int)data.week_count; w++) {
        for (int d = 0; d < GRID_DAYS; d++) {
            if (data.days[w][d].count > 0)
                counts[count_n++] = data.days[w][d].count;
        }
    }
    // Sort ascending (simple insertion sort — only up to 371 items)
    for (int i = 1; i < count_n; i++) {
        uint16_t key = counts[i];
        int j = i - 1;
        while (j >= 0 && counts[j] > key) { counts[j+1] = counts[j]; j--; }
        counts[j+1] = key;
    }
    // Threshold: value at (100-anim_top_pct)th percentile
    uint16_t threshold = 0xFFFF;
    if (count_n > 0 && cfg.anim_top_pct > 0) {
        int idx = (int)(count_n * (100 - cfg.anim_top_pct) / 100.0f);
        if (idx >= count_n) idx = count_n - 1;
        threshold = counts[idx];
    }

    // Update cells and assign animations
    for (int w = 0; w < GRID_WEEKS; w++) {
        for (int d = 0; d < GRID_DAYS; d++) {
            uint16_t cnt   = (w < (int)data.week_count) ? data.days[w][d].count : 0;
            uint8_t  lvl   = (w < (int)data.week_count) ? data.days[w][d].level : 0;
            s_cell_levels[w][d] = lvl;

            lv_obj_set_user_data(s_cells[w][d], (void *)(uintptr_t)lvl);
            lv_obj_set_style_bg_color(s_cells[w][d], LEVEL_COLORS[lvl], 0);

            if (cnt > 0 && cnt >= threshold) {
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, s_cells[w][d]);
                lv_anim_set_exec_cb(&a, anim_color_cb);
                lv_anim_set_values(&a, 0, 255);
                lv_anim_set_duration(&a, cfg.anim_period_ms);
                lv_anim_set_playback_duration(&a, cfg.anim_period_ms);
                lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
                lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
                lv_anim_set_delay(&a, (uint32_t)random(0, cfg.anim_period_ms));
                lv_anim_start(&a);
            }
        }
    }
}
```

---

## Task 5: Display Stats Module

**Files:**
- Create: `src/display_stats.h`
- Create: `src/display_stats.cpp`

- [ ] **Step 1: Create src/display_stats.h**

```cpp
#pragma once
#include <lvgl.h>
#include "github_api.h"

// Build the stats screen. Call once after LVGL init.
lv_obj_t *display_stats_build(const char *username);

// Update labels from fresh data.
void display_stats_update(const GithubData &data);

// Update the "last updated Xm ago" footer. Call with minutes since last fetch.
void display_stats_set_age(uint32_t minutes_ago);
```

- [ ] **Step 2: Create src/display_stats.cpp**

```cpp
#include "display_stats.h"
#include <Arduino.h>

static lv_obj_t *s_screen       = nullptr;
static lv_obj_t *s_streak_val   = nullptr;
static lv_obj_t *s_total_val    = nullptr;
static lv_obj_t *s_busiest_val  = nullptr;
static lv_obj_t *s_age_label    = nullptr;

lv_obj_t *display_stats_build(const char *username) {
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Username top-left
    lv_obj_t *user_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(user_lbl, &lv_font_montserrat_8, 0);
    lv_obj_set_style_text_color(user_lbl, lv_color_hex(0x8b949e), 0);
    char buf[80];
    snprintf(buf, sizeof(buf), "github.com/%s", username);
    lv_label_set_text(user_lbl, buf);
    lv_obj_align(user_lbl, LV_ALIGN_TOP_LEFT, 8, 6);

    // Three stat cards in a row, centred
    struct { lv_obj_t **val_ptr; const char *label; lv_color_t color; } cards[3] = {
        { &s_streak_val,  "day streak",    lv_color_hex(0x39d353) },
        { &s_total_val,   "contribs",      lv_color_hex(0x58a6ff) },
        { &s_busiest_val, "busiest day",   lv_color_hex(0xd29922) },
    };

    // Card width = 320/3 = ~106px
    const int card_w = 106;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *cont = lv_obj_create(s_screen);
        lv_obj_set_size(cont, card_w, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cont, 0, 0);
        lv_obj_set_style_pad_all(cont, 0, 0);
        lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(cont, LV_ALIGN_CENTER, (i - 1) * card_w, 0);

        *cards[i].val_ptr = lv_label_create(cont);
        lv_obj_set_style_text_font(*cards[i].val_ptr, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(*cards[i].val_ptr, cards[i].color, 0);
        lv_label_set_text(*cards[i].val_ptr, "--");
        lv_obj_align(*cards[i].val_ptr, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t *sub = lv_label_create(cont);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_8, 0);
        lv_obj_set_style_text_color(sub, lv_color_hex(0x8b949e), 0);
        lv_label_set_text(sub, cards[i].label);
        lv_obj_align_to(sub, *cards[i].val_ptr, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
    }

    // Age label bottom-right
    s_age_label = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_age_label, &lv_font_montserrat_8, 0);
    lv_obj_set_style_text_color(s_age_label, lv_color_hex(0x8b949e), 0);
    lv_label_set_text(s_age_label, "");
    lv_obj_align(s_age_label, LV_ALIGN_BOTTOM_RIGHT, -8, -6);

    return s_screen;
}

void display_stats_update(const GithubData &data) {
    if (!s_screen) return;
    lv_label_set_text_fmt(s_streak_val,  "%d", data.current_streak);
    lv_label_set_text_fmt(s_total_val,   "%d", data.total_contributions);
    lv_label_set_text_fmt(s_busiest_val, "%d", data.busiest_day_count);
}

void display_stats_set_age(uint32_t minutes_ago) {
    if (!s_age_label) return;
    if (minutes_ago == 0)
        lv_label_set_text(s_age_label, "just updated");
    else
        lv_label_set_text_fmt(s_age_label, "updated %dm ago", minutes_ago);
}
```

---

## Task 6: Web Server Module

**Files:**
- Create: `src/web_server.h`
- Create: `src/web_server.cpp`

- [ ] **Step 1: Create src/web_server.h**

```cpp
#pragma once
#include "config.h"

// Start the web server. cfg is used to pre-fill the form.
// on_save callback is called when new config is saved (before reboot).
void web_server_start(Config &cfg);

// Must be called in loop() to handle incoming requests.
void web_server_handle();
```

- [ ] **Step 2: Create src/web_server.cpp**

```cpp
#include "web_server.h"
#include <WebServer.h>
#include <Arduino.h>

static WebServer server(80);
static Config   *s_cfg = nullptr;

// PROGMEM HTML page — dark GitHub aesthetic
static const char HTML_HEAD[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GitHub Monitor Config</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0d1117;color:#e6edf3;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;padding:20px}
h1{color:#39d353;font-size:20px;margin-bottom:4px}
.sub{color:#8b949e;font-size:13px;margin-bottom:24px}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:20px;margin-bottom:16px}
.card h2{color:#e6edf3;font-size:14px;margin-bottom:14px;border-bottom:1px solid #30363d;padding-bottom:8px}
label{display:block;color:#8b949e;font-size:12px;margin-bottom:4px}
input[type=text],input[type=password],input[type=number]{
  width:100%;background:#0d1117;border:1px solid #30363d;border-radius:6px;
  color:#e6edf3;padding:8px 10px;font-size:14px;outline:none;margin-bottom:12px}
input:focus{border-color:#39d353}
input[type=range]{width:100%;accent-color:#39d353;margin-bottom:4px}
.range-row{display:flex;align-items:center;gap:10px;margin-bottom:12px}
.range-row input{flex:1;margin:0}
.range-val{color:#39d353;font-size:13px;min-width:36px;text-align:right}
button{background:#238636;color:#fff;border:none;border-radius:6px;
  padding:10px 24px;font-size:15px;cursor:pointer;width:100%;margin-top:8px}
button:hover{background:#2ea043}
.ip{color:#8b949e;font-size:12px;margin-top:16px;text-align:center}
</style></head><body>
<h1>GitHub Monitor</h1>
<div class="sub">Device IP: )rawhtml";

static const char HTML_FORM[] PROGMEM = R"rawhtml(</div>
<form method="POST" action="/save">
<div class="card"><h2>WiFi</h2>
<label>SSID</label><input type="text" name="wifi_ssid" value=")rawhtml";

static const char HTML_TAIL[] PROGMEM = R"rawhtml(">
</div></form></body></html>)rawhtml";

// Build full page — assembled from parts with current config values
static String build_page(bool ap_mode) {
    String ip = ap_mode ? "192.168.4.1" : WiFi.localIP().toString();
    String html;
    html.reserve(4096);
    html += FPSTR(HTML_HEAD);
    html += ip;
    if (ap_mode) html += " &nbsp;<span style='color:#d29922'>⚠ AP setup mode</span>";
    html += F("</div><form method='POST' action='/save'>");

    // WiFi card
    html += F("<div class='card'><h2>WiFi</h2>"
              "<label>SSID</label><input type='text' name='wifi_ssid' value='");
    html += s_cfg->wifi_ssid;
    html += F("'><label>Password</label><input type='password' name='wifi_pass' value='");
    html += s_cfg->wifi_password;
    html += F("'></div>");

    // GitHub card
    html += F("<div class='card'><h2>GitHub</h2>"
              "<label>Username</label><input type='text' name='gh_user' value='");
    html += s_cfg->github_username;
    html += F("'><label>Personal Access Token (read:user)</label>"
              "<input type='password' name='gh_token' value='");
    html += s_cfg->github_token;
    html += F("'></div>");

    // Display card
    html += F("<div class='card'><h2>Display</h2>"
              "<label>Brightness (0–255)</label>"
              "<div class='range-row'><input type='range' name='brightness' min='0' max='255' value='");
    html += s_cfg->brightness;
    html += F("' oninput='this.nextElementSibling.textContent=this.value'>"
              "<span class='range-val'>");
    html += s_cfg->brightness;
    html += F("</span></div>"
              "<label>Screen switch interval (seconds)</label>"
              "<input type='number' name='switch_sec' min='5' max='3600' value='");
    html += s_cfg->screen_switch_secs;
    html += F("'><label>Data refresh interval (minutes)</label>"
              "<input type='number' name='refresh_min' min='1' max='1440' value='");
    html += s_cfg->refresh_interval_min;
    html += F("'></div>");

    // Animation card
    html += F("<div class='card'><h2>Animation</h2>"
              "<label>Animate top % of active days (1–100)</label>"
              "<div class='range-row'><input type='range' name='anim_pct' min='1' max='100' value='");
    html += s_cfg->anim_top_pct;
    html += F("' oninput='this.nextElementSibling.textContent=this.value+\"%\"'>"
              "<span class='range-val'>");
    html += s_cfg->anim_top_pct;
    html += F("%</span></div>"
              "<label>Animation period (ms)</label>"
              "<input type='number' name='anim_ms' min='500' max='10000' value='");
    html += s_cfg->anim_period_ms;
    html += F("'></div>");

    html += F("<button type='submit'>Save &amp; Reboot</button>"
              "</form></body></html>");
    return html;
}

static void handle_root() {
    server.send(200, "text/html", build_page(false));
}

static void handle_save() {
    if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }

    auto get = [&](const char *key, char *dst, size_t len) {
        if (server.hasArg(key)) {
            String v = server.arg(key);
            strncpy(dst, v.c_str(), len - 1);
            dst[len - 1] = '\0';
        }
    };

    get("wifi_ssid", s_cfg->wifi_ssid,       sizeof(s_cfg->wifi_ssid));
    get("wifi_pass", s_cfg->wifi_password,    sizeof(s_cfg->wifi_password));
    get("gh_user",   s_cfg->github_username,  sizeof(s_cfg->github_username));
    get("gh_token",  s_cfg->github_token,     sizeof(s_cfg->github_token));

    if (server.hasArg("brightness"))   s_cfg->brightness           = (uint8_t)server.arg("brightness").toInt();
    if (server.hasArg("switch_sec"))   s_cfg->screen_switch_secs   = (uint16_t)server.arg("switch_sec").toInt();
    if (server.hasArg("refresh_min"))  s_cfg->refresh_interval_min = (uint16_t)server.arg("refresh_min").toInt();
    if (server.hasArg("anim_pct"))     s_cfg->anim_top_pct         = (uint8_t)server.arg("anim_pct").toInt();
    if (server.hasArg("anim_ms"))      s_cfg->anim_period_ms       = (uint16_t)server.arg("anim_ms").toInt();

    config_save(*s_cfg);

    server.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<style>body{background:#0d1117;color:#39d353;font-family:sans-serif;"
        "display:flex;align-items:center;justify-content:center;height:100vh;margin:0}"
        "h1{font-size:24px}</style></head>"
        "<body><h1>Saved — rebooting\u2026</h1></body></html>");

    delay(1000);
    ESP.restart();
}

void web_server_start(Config &cfg) {
    s_cfg = &cfg;
    server.on("/",     HTTP_GET,  handle_root);
    server.on("/save", HTTP_POST, handle_save);
    server.begin();
    Serial.printf("[Web] Server started at http://%s/\n", WiFi.localIP().toString().c_str());
}

void web_server_handle() {
    server.handleClient();
}
```

Note: `WiFi.h` must be included before this translation unit is compiled. It is included in `main.cpp` which includes this header transitively, but add `#include <WiFi.h>` at the top of `web_server.cpp` above the other includes.

- [ ] **Step 3: Add `#include <WiFi.h>` to top of web_server.cpp**

Edit `src/web_server.cpp` — insert as the very first include:
```cpp
#include <WiFi.h>
```

---

## Task 7: Main Orchestration

**Files:**
- Modify: `src/main.cpp` (full replacement)

- [ ] **Step 1: Replace src/main.cpp entirely**

```cpp
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

#define SCREEN_BRIGHTNESS_DEFAULT 200

static Config     g_cfg;
static GithubData g_data;
static lv_obj_t  *g_screen_grid  = nullptr;
static lv_obj_t  *g_screen_stats = nullptr;
static bool       g_show_grid    = true;
static uint32_t   g_last_fetch_ms = 0;
static lv_display_t *disp = nullptr;

// ── LVGL callbacks ───────────────────────────────────────────────────────────

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
    } else {
        Serial.println("[Main] Fetch failed");
    }
}

static void refresh_timer_cb(lv_timer_t *) {
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
    lv_label_set_text_fmt(lbl, "Connecting to\n%s", g_cfg.wifi_ssid[0] ? g_cfg.wifi_ssid : WIFI_SSID);
    lv_obj_align_to(lbl, spinner, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    const char *ssid = g_cfg.wifi_ssid[0] ? g_cfg.wifi_ssid : WIFI_SSID;
    const char *pass = g_cfg.wifi_password[0] ? g_cfg.wifi_password : WIFI_PASSWORD;

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

// ── AP mode (first boot, no creds) ───────────────────────────────────────────

static void ap_mode_splash() {
    lv_obj_t *lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xd29922), 0);
    lv_obj_set_width(lbl, 280);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl, "Setup mode\nConnect to:\nGithubMonitor-Setup\nThen visit:\n192.168.4.1");
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

// ── setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    DEV_DEVICE_INIT();
    delay(2000);

    if (!gfx->begin()) { Serial.println("GFX init failed!"); while (true); }
    gfx->setRotation(1); // landscape
    gfx->fillScreen(RGB565_BLACK);
    apply_brightness(SCREEN_BRIGHTNESS_DEFAULT);

    lvgl_init();

    config_load(g_cfg);
    apply_brightness(g_cfg.brightness);

    if (g_cfg.wifi_ssid[0] == '\0') {
        // No WiFi creds — start AP mode
        WiFi.softAP("GithubMonitor-Setup");
        Serial.println("[WiFi] AP mode: GithubMonitor-Setup @ 192.168.4.1");
        ap_mode_splash();
        web_server_start(g_cfg);
        // Stay in AP mode loop — web server handles save+reboot
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

// ── loop ─────────────────────────────────────────────────────────────────────

void loop() {
    lv_timer_handler();
    web_server_handle();
    delay(5);
}
```

---

## Task 8: Build, Fix, Flash

**Files:** none new — compile and deploy

- [ ] **Step 1: Run PlatformIO build**

```bash
cd "e:/GoogleDrive/Arduino/ESP32-C6-LCD-1.47_GithubCommits"
pio run -e esp32-c6-devkitc-1
```
Expected: `SUCCESS` with no errors. If errors appear, fix them before continuing.

- [ ] **Step 2: Fix any compile errors**

Common issues to look for:
- Missing `#include <WiFi.h>` in `web_server.cpp` — add it as the first include
- `lv_obj_set_style_margin_right` not available in LVGL v9 — replace with `lv_obj_set_style_pad_right` on the legend square
- `lv_obj_set_style_column_dsc_array` not correct for flex — remove that line from `display_grid.cpp`
- `lv_obj_get_user_data` / `lv_obj_set_user_data` — in LVGL v9 these are available; if not, store level in a parallel array lookup instead

- [ ] **Step 3: Flash to device on COM4**

```bash
pio run -e esp32-c6-devkitc-1 --target upload --upload-port COM4
```
Expected: `SUCCESS` — device reboots.

- [ ] **Step 4: Open serial monitor to verify boot**

```bash
pio device monitor --port COM4 --baud 115200
```
Expected output:
```
[WiFi] Connected, IP: 192.168.x.x
[GitHub] OK — 53 weeks, NNNN contributions, streak NN
[Web] Server started at http://192.168.x.x/
[Main] Setup complete
```

- [ ] **Step 5: Verify display visually**

- Grid screen shows: 53×7 coloured cells, top-% cells breathing, bottom footer with username and legend
- After `screen_switch_secs` seconds: fades to stats screen with streak/total/busiest numbers
- After another interval: fades back to grid

- [ ] **Step 6: Verify web config page**

Open `http://<device-ip>/` in a browser. Should show styled dark config page with all fields pre-filled from current NVS values.

---

## Self-Review Notes

- **Spec coverage check:**
  - ✓ Config NVS load/save with all 9 fields
  - ✓ Boot flow including AP fallback
  - ✓ GitHub GraphQL fetch with manual JSON parsing
  - ✓ Grid screen: 53×7 cells, 5×5px, correct padding, month labels above grid centred as unit, footer with username + legend + "More"
  - ✓ Stats screen: streak (green), total (blue), busiest (amber), username, age label
  - ✓ Breathing animation: top-% threshold, random phase offsets, ease_in_out, level colour → bright variant
  - ✓ Screen switching via lv_scr_load_anim with FADE_ON
  - ✓ Data refresh timer stops animations, re-fetches, reassigns
  - ✓ Web config page: all 9 fields, PROGMEM, dark styled, save+reboot
  - ✓ secrets.h.example + .gitignore
  - ✓ lv_conf.h font enables (montserrat_8, montserrat_28)

- **Type consistency:**
  - `GithubData::week_count` is `uint8_t` — used as loop bound in `display_grid_update` with cast to `int` ✓
  - `Config` field names match between `config.cpp` NVS keys and `web_server.cpp` form field names ✓
  - `display_grid_update(data, cfg)` signature matches declaration ✓
  - `display_stats_update(data)` signature matches declaration ✓
