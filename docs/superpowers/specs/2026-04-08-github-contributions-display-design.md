# GitHub Contributions Display — Design Spec

**Date:** 2026-04-08  
**Platform:** ESP32-C6, ST7789 IPS 172×320px, LVGL v9.2.2, PlatformIO/Arduino  
**Replaces:** Binance crypto monitor (`main.cpp`)

---

## Overview

Replace the existing Binance price monitor with a GitHub contributions grid display. The device connects to WiFi, fetches contribution data from the GitHub GraphQL API, and displays:

1. **Grid screen** — the GitHub contributions heatmap grid (53 weeks × 7 days), with the top N% most-active days breathing/pulsing via animation
2. **Stats screen** — current streak, total contributions (year), and busiest day count

The two screens alternate on a configurable timer to prevent screen burn-in. All settings (WiFi, GitHub credentials, display preferences, animation) are configurable via a web page served by the device itself.

---

## File Structure

```
src/
  main.cpp           — setup(), loop(), screen-switch timer, data-refresh timer
  config.cpp/.h      — NVS read/write, Config struct, defaults
  github_api.cpp/.h  — GraphQL HTTP fetch, JSON parsing, ContributionDay array
  display_grid.cpp/.h — LVGL grid screen, breathing animation
  display_stats.cpp/.h — LVGL stats screen (streak, total, busiest day)
  web_server.cpp/.h  — ESP32 WebServer on port 80, config page HTML, save handler

include/
  PINS_ESP32-C6-LCD-1_47.h — unchanged
  lv_conf.h               — unchanged
  secrets.h               — WiFi bootstrap creds (git-ignored)
  secrets.h.example       — template with placeholder values
  atomic.h                — unchanged
  board_config.h          — unchanged

.gitignore             — must include: include/secrets.h, .pio/, .superpowers/
```

---

## Config Struct (NVS)

Stored via `Preferences.h` under namespace `"ghmon"`.

```cpp
struct Config {
    // Connection
    char     wifi_ssid[64];
    char     wifi_password[64];

    // GitHub
    char     github_token[128];      // Personal Access Token (read:user scope)
    char     github_username[64];

    // Display
    uint8_t  brightness;             // 0–255, default 200
    uint16_t screen_switch_secs;     // grid↔stats interval, default 30
    uint16_t refresh_interval_min;   // GitHub re-fetch interval, default 30

    // Animation
    uint8_t  anim_top_pct;           // top N% of days animate, default 20
    uint16_t anim_period_ms;         // breathing cycle duration ms, default 2000
};
```

---

## Boot Flow

1. Load `Config` from NVS; apply defaults for any unset keys
2. If `wifi_ssid` is empty → start soft AP (`GithubMonitor-Setup`, open, IP `192.168.4.1`) and serve config page; wait for save+reboot
3. Connect to WiFi — show LVGL spinner + "Connecting to \<ssid\>" on display
4. Start `WebServer` on port 80 (always accessible after connect)
5. Fetch GitHub contributions via GraphQL
6. Build grid screen and stats screen (both kept in memory as LVGL screen objects)
7. Show grid screen
8. Start screen-switch timer (`lv_timer_t`, fires every `screen_switch_secs * 1000` ms)
9. Start data-refresh timer (`lv_timer_t`, fires every `refresh_interval_min * 60 * 1000`)

---

## GitHub API

**Endpoint:** `POST https://api.github.com/graphql`  
**Auth:** `Authorization: Bearer <github_token>`  
**Scope required:** `read:user`

**Query:**
```graphql
{
  user(login: "<username>") {
    contributionsCollection {
      contributionCalendar {
        totalContributions
        weeks {
          contributionDays {
            contributionCount
            date
          }
        }
      }
    }
  }
}
```

**Parsed into:**
```cpp
struct ContributionDay {
    uint16_t count;   // raw contribution count
    uint8_t  level;   // 0–4, computed from count using GitHub thresholds
};
ContributionDay days[53][7];  // [week][day]
uint16_t totalContributions;
uint16_t busiestDayCount;
uint16_t currentStreak;       // computed client-side by iterating days[] backwards
```

**Colour levels** (GitHub thresholds):
| Level | Count | Colour |
|-------|-------|--------|
| 0 | 0 | `#161b22` |
| 1 | 1–3 | `#0e4429` |
| 2 | 4–6 | `#216e39` |
| 3 | 7–9 | `#30a14e` |
| 4 | 10+ | `#39d353` |

JSON parsing is done manually (no ArduinoJson) using `String::indexOf()` to keep flash usage low, consistent with existing code style.

---

## Display — Grid Screen

**Orientation:** Landscape (rotation=1 already set), 320×172px

**Layout:**
- **Centre zone** (flex column, vertically centred in space above footer):
  - Month label row: 6 labels (every ~2 months), font size 9px, grey `#8b949e`, directly above grid
  - Grid: 53 columns × 7 rows, 5×5px cells, 1px column gap, 2px row gap
- **Footer row** (fixed 18px, bottom of display):
  - Left: `github.com/<username>` — grey 9px
  - Right: five legend squares (levels 0→4) + "More" label — grey 9px

**Cell sizing maths:**
- Width: 53 × (5+1) − 1 = 317px, left-padded 8px → fits 320px ✓
- Height of grid: 7×5 + 6×2 = 47px
- Height of labels: ~12px
- Combined unit: 62px, centred in (172 − 18) = 154px available → top offset = 46px ✓

---

## Display — Stats Screen

**Layout:** Three stat cards side-by-side, vertically centred

| Card | Value | Colour |
|------|-------|--------|
| Current streak | N days | `#39d353` green |
| Total contributions | N | `#58a6ff` blue |
| Busiest day | N | `#d29922` amber |

- Large number (~28px font), small label below each
- Username top-left (grey 9px)
- "last updated Xm ago" bottom-right (grey 9px)

---

## Breathing Animation

- On data load, all `ContributionDay` counts are sorted; threshold = count at the `(100 - anim_top_pct)`th percentile
- Any cell with `count >= threshold && count > 0` gets an `lv_anim_t` assigned
- Animation tweens cell background colour between its level colour and a brightened variant:
  - Level 4: `#39d353` ↔ `#80ff9a`
  - Level 3: `#30a14e` ↔ `#60d880`
  - (lower levels unlikely to meet top-% threshold but handled if they do)
- Path: `lv_anim_path_ease_in_out` (sine-like)
- Period: `anim_period_ms` (default 2000ms); each animated cell gets a randomised phase offset (`delay = random(0, anim_period_ms)`) so cells breathe out of sync — organic, rippling feel
- On data refresh: all animations deleted, recalculated and re-assigned

---

## Web Config Page

- Served from PROGMEM `const char*` in `web_server.cpp`
- **Style:** dark GitHub aesthetic — `#0d1117` background, `#39d353` green accents, `#e6edf3` text, clean sans-serif, styled inputs and submit button
- **Fields:**

| Label | Input type | NVS key |
|-------|-----------|---------|
| WiFi SSID | text | `wifi_ssid` |
| WiFi Password | password | `wifi_pass` |
| GitHub Username | text | `gh_user` |
| GitHub Token (PAT) | password | `gh_token` |
| Refresh interval (min) | number | `refresh_min` |
| Screen switch (sec) | number | `switch_sec` |
| Brightness | range 0–255 | `brightness` |
| Animate top % | range 1–100 | `anim_pct` |
| Animation period (ms) | number | `anim_ms` |

- On `POST /save`: parse form fields, write to NVS, respond with "Saved — rebooting…" page, call `ESP.restart()` after 1s delay
- Page header shows device IP address
- First-boot AP mode: same page served at `192.168.4.1` with a banner: "Connect to Wi-Fi first"

---

## Screen Switching

- `lv_timer_t` fires every `screen_switch_secs * 1000` ms
- Calls `lv_scr_load_anim(nextScreen, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false)`
- Alternates between grid screen and stats screen objects (both kept alive in memory)
- No re-fetch on switch

---

## Data Refresh

- `lv_timer_t` fires every `refresh_interval_min * 60 * 1000` ms
- Sequence: stop animations → HTTP fetch → parse → recalculate streak/stats → update LVGL cell colours → reassign animations → update stats labels
- `lv_timer_handler()` called during HTTP fetch to keep display responsive
- "last updated" label on stats screen updated after each successful fetch

---

## Secrets & Git

- `include/secrets.h` — git-ignored, contains WiFi bootstrap credentials (used only on first flash before NVS is populated)
- `include/secrets.h.example` — committed, contains:
  ```cpp
  #define WIFI_SSID     "your_ssid"
  #define WIFI_PASSWORD "your_password"
  ```
- `.gitignore` additions: `include/secrets.h`, `.pio/`, `.superpowers/`

---

## Out of Scope

- HTTPS certificate verification (ESP32 will use insecure mode for GitHub API — acceptable for this use case)
- OTA firmware updates
- Multiple GitHub accounts
- Touch input
