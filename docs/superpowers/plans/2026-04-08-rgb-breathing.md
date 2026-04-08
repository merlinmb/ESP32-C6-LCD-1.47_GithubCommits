# RGB Breathing LED Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the onboard NeoPixel (pin 8) breathe at a speed and color that reflects the user's current GitHub commit streak, with parameters configurable from the web UI.

**Architecture:** A new `rgb_led` module owns the NeoPixel and exposes init/update/tick functions. `rgb_led_tick()` runs every loop iteration and computes brightness via a sine wave keyed off `millis()`. Streak length maps linearly to breathing period (slow→fast) and green color intensity; streak 0 produces a slow dim white-blue idle pulse.

**Tech Stack:** Arduino/ESP32-C6, `Adafruit NeoPixel` library, existing `Preferences` NVS, existing `WebServer` config form.

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `src/rgb_led.h` | Create | Public interface: init, update_params, tick |
| `src/rgb_led.cpp` | Create | Sine-wave breathing engine, streak→color/period mapping |
| `src/config.h` | Modify | Add 3 new fields to `Config` struct |
| `src/config.cpp` | Modify | Load/save/default the 3 new NVS keys |
| `src/web_server.cpp` | Modify | Add RGB LED card to config form + parse POST args |
| `src/main.cpp` | Modify | Wire in rgb_led_init, rgb_led_update_params, rgb_led_tick |
| `platformio.ini` | Modify | Add Adafruit NeoPixel to lib_deps |

---

## Task 1: Add Adafruit NeoPixel library dependency

**Files:**
- Modify: `platformio.ini`

- [ ] **Step 1: Add the library**

Open `platformio.ini`. The current `lib_deps` block ends with:
```ini
lib_deps =
    lvgl/lvgl @ ^9.2.2
    moononournation/GFX Library for Arduino @ ^1.5.9
```

Change it to:
```ini
lib_deps =
    lvgl/lvgl @ ^9.2.2
    moononournation/GFX Library for Arduino @ ^1.5.9
    adafruit/Adafruit NeoPixel @ ^1.12.3
```

- [ ] **Step 2: Verify library resolves**

Run:
```bash
pio pkg install
```
Expected: output includes `Adafruit NeoPixel` with no errors.

- [ ] **Step 3: Commit**

```bash
git add platformio.ini
git commit -m "build: add Adafruit NeoPixel library dependency"
```

---

## Task 2: Add RGB config fields to Config struct

**Files:**
- Modify: `src/config.h`

- [ ] **Step 1: Add three fields to the Config struct**

Current end of struct in `src/config.h`:
```cpp
    uint8_t  anim_top_pct;
    uint16_t anim_period_ms;
};
```

Change to:
```cpp
    uint8_t  anim_top_pct;
    uint16_t anim_period_ms;
    uint16_t rgb_period_min_ms;  // fastest breath period at max streak (default 1200)
    uint16_t rgb_period_max_ms;  // slowest breath period at streak 0 (default 8000)
    uint8_t  rgb_streak_max;     // streak length that achieves min period (default 30)
};
```

- [ ] **Step 2: Commit**

```bash
git add src/config.h
git commit -m "feat: add RGB LED config fields to Config struct"
```

---

## Task 3: Persist RGB config fields via NVS

**Files:**
- Modify: `src/config.cpp`

- [ ] **Step 1: Add defaults**

In `config_apply_defaults()`, after the existing defaults block, add:
```cpp
    if (cfg.rgb_period_min_ms == 0) cfg.rgb_period_min_ms = 1200;
    if (cfg.rgb_period_max_ms == 0) cfg.rgb_period_max_ms = 8000;
    if (cfg.rgb_streak_max    == 0) cfg.rgb_streak_max    = 30;
```

- [ ] **Step 2: Load from NVS**

In `config_load()`, after `prefs.getUShort("anim_ms", 0)`, add:
```cpp
    cfg.rgb_period_min_ms = prefs.getUShort("rgb_pmin", 0);
    cfg.rgb_period_max_ms = prefs.getUShort("rgb_pmax", 0);
    cfg.rgb_streak_max    = prefs.getUChar( "rgb_smax", 0);
```

- [ ] **Step 3: Save to NVS**

In `config_save()`, after `prefs.putUShort("anim_ms", cfg.anim_period_ms)`, add:
```cpp
    prefs.putUShort("rgb_pmin", cfg.rgb_period_min_ms);
    prefs.putUShort("rgb_pmax", cfg.rgb_period_max_ms);
    prefs.putUChar( "rgb_smax", cfg.rgb_streak_max);
```

- [ ] **Step 4: Commit**

```bash
git add src/config.cpp
git commit -m "feat: persist RGB LED config fields to NVS"
```

---

## Task 4: Create the rgb_led module

**Files:**
- Create: `src/rgb_led.h`
- Create: `src/rgb_led.cpp`

- [ ] **Step 1: Create the header `src/rgb_led.h`**

```cpp
#pragma once
#include "config.h"
#include "github_api.h"

void rgb_led_init(const Config &cfg);
void rgb_led_update_params(const GithubData &data, const Config &cfg);
void rgb_led_tick();
```

- [ ] **Step 2: Create the implementation `src/rgb_led.cpp`**

```cpp
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

void rgb_led_init(const Config &cfg) {
    s_pixel.begin();
    s_pixel.setBrightness(255);
    s_pixel.clear();
    s_pixel.show();
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

    uint8_t r = (uint8_t)(s_base_r * brightness);
    uint8_t g = (uint8_t)(s_base_g * brightness);
    uint8_t b = (uint8_t)(s_base_b * brightness);

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
```

- [ ] **Step 3: Commit**

```bash
git add src/rgb_led.h src/rgb_led.cpp
git commit -m "feat: add rgb_led breathing module"
```

---

## Task 5: Wire rgb_led into main.cpp

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add include**

After the existing includes at the top of `src/main.cpp` (after `#include "web_server.h"`), add:
```cpp
#include "rgb_led.h"
```

- [ ] **Step 2: Call rgb_led_init in setup()**

In `setup()`, after `apply_brightness(g_cfg.brightness);` (line 166), add:
```cpp
    rgb_led_init(g_cfg);
```

- [ ] **Step 3: Call rgb_led_update_params after fetch**

In `do_fetch()`, after `display_stats_set_age(0);` inside the `if (ok)` block, add:
```cpp
        rgb_led_update_params(g_data, g_cfg);
```

- [ ] **Step 4: Call rgb_led_tick in loop()**

In `loop()`, after `lv_timer_handler();`, add:
```cpp
    rgb_led_tick();
```

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: wire rgb_led init/tick/update into main loop"
```

---

## Task 6: Add RGB LED card to the web config UI

**Files:**
- Modify: `src/web_server.cpp`

- [ ] **Step 1: Add RGB LED card to build_page()**

In `build_page()`, after the Animation card block (after the `html += F("'></div>");` that closes the Animation card, around line 100), add:

```cpp
    // RGB LED card
    html += F("<div class='card'><h2>RGB LED</h2>"
              "<label>Min breath period ms (fastest, high streak)</label>"
              "<input type='number' name='rgb_pmin' min='400' max='4000' value='");
    html += s_cfg->rgb_period_min_ms;
    html += F("'><label>Max breath period ms (slowest, no streak)</label>"
              "<input type='number' name='rgb_pmax' min='2000' max='20000' value='");
    html += s_cfg->rgb_period_max_ms;
    html += F("'><label>Streak max (streak that achieves min period)</label>"
              "<input type='number' name='rgb_smax' min='1' max='365' value='");
    html += s_cfg->rgb_streak_max;
    html += F("'></div>");
```

- [ ] **Step 2: Parse RGB fields in handle_save()**

In `handle_save()`, after `if (server.hasArg("anim_ms")) ...` (around line 135), add:
```cpp
    if (server.hasArg("rgb_pmin")) s_cfg->rgb_period_min_ms = (uint16_t)server.arg("rgb_pmin").toInt();
    if (server.hasArg("rgb_pmax")) s_cfg->rgb_period_max_ms = (uint16_t)server.arg("rgb_pmax").toInt();
    if (server.hasArg("rgb_smax")) s_cfg->rgb_streak_max    = (uint8_t) server.arg("rgb_smax").toInt();
```

- [ ] **Step 3: Commit**

```bash
git add src/web_server.cpp
git commit -m "feat: add RGB LED section to web config UI"
```

---

## Task 7: Build and flash

- [ ] **Step 1: Build**

```bash
pio run
```
Expected: exits with `SUCCESS`. Zero errors. Warnings about unused variables are acceptable.

- [ ] **Step 2: Flash**

```bash
pio run --target upload
```
Expected: upload completes, device reboots.

- [ ] **Step 3: Verify on device**

- Watch the NeoPixel after boot: it should begin a slow white-blue breathing pulse while connecting to WiFi (streak not yet fetched → defaults apply → idle state).
- After the first GitHub fetch completes, the NeoPixel should change color/speed based on `current_streak`.
- Open `http://<device-ip>/` in a browser — confirm the "RGB LED" card is visible with three numeric fields.
- Change a value (e.g. set Streak max to 1) and save. Device reboots. Verify the LED breathes at the new speed.

- [ ] **Step 4: Commit final state if any tweaks were needed**

```bash
git add -p
git commit -m "fix: rgb_led tuning after device verification"
```
(Skip if no changes were made.)
