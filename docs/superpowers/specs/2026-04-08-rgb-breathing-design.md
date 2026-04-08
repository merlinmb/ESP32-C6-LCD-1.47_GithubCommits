# RGB Breathing LED — Design Spec
**Date:** 2026-04-08  
**Board:** Waveshare ESP32-C6-LCD-1.47 (NeoPixel on pin 8)

---

## Overview

The onboard RGB NeoPixel breathes (sine-wave brightness pulse) at a speed and color that reflects the user's current GitHub commit streak. Longer streak = faster, greener, brighter. Broken streak = slow dim white-blue idle pulse. All mapping parameters are user-configurable via the existing web UI.

---

## Architecture

### New module: `src/rgb_led.h` / `src/rgb_led.cpp`

Owns the `Adafruit_NeoPixel` instance (pin 8, 1 pixel). Exposes:

```cpp
void rgb_led_init(const Config &cfg);
void rgb_led_update_params(const GithubData &data, const Config &cfg);
void rgb_led_tick();
```

### Integration points in existing files

| File | Change |
|------|--------|
| `src/main.cpp` | `rgb_led_init()` in `setup()` after config load; `rgb_led_update_params()` in `do_fetch()` on success; `rgb_led_tick()` in `loop()` |
| `src/config.h` | Three new fields in `Config` struct |
| `src/config.cpp` | Load/save/default the three new fields via NVS |
| `src/web_server.cpp` | New "RGB LED" section in config form; parse + save three fields |
| `platformio.ini` | Add `adafruit/Adafruit NeoPixel` to `lib_deps` |

---

## Breathing Engine

`rgb_led_tick()` runs every loop iteration (~5ms). It computes:

```
t        = millis()
phase    = (t % period_ms) / (float)period_ms   // 0.0 → 1.0
brightness = (sin(phase * 2π - π/2) + 1.0) / 2.0  // 0.0 → 1.0
pixel_color = base_color * brightness
```

`base_color` and `period_ms` are module-level state set by `rgb_led_update_params()`. The NeoPixel is written only when the computed color changes by ≥1 on any channel (avoids redundant SPI writes).

---

## Streak → Parameter Mapping

### Period (breathing speed)

Linear interpolation between `rgb_period_max_ms` (streak 0) and `rgb_period_min_ms` (streak ≥ `rgb_streak_max`):

```
ratio    = clamp(streak / rgb_streak_max, 0.0, 1.0)
period   = rgb_period_max_ms - ratio * (rgb_period_max_ms - rgb_period_min_ms)
```

### Color (base_color at full brightness)

| Streak | Color | Notes |
|--------|-------|-------|
| 0 | `(20, 20, 40)` | Dim white-blue, fixed — not interpolated |
| 1 → streak_max | `(0, 60, 0)` → `(80, 255, 0)` | Linear interpolation per channel |

When streak = 0, `base_color` is set to the idle white-blue regardless of interpolation. The period also uses `rgb_period_max_ms` (slowest).

---

## Config Fields

Three new fields added to `Config`:

```cpp
uint16_t rgb_period_min_ms;  // default: 1200  — fastest breath (high streak)
uint16_t rgb_period_max_ms;  // default: 8000  — slowest breath (no streak)
uint8_t  rgb_streak_max;     // default: 30    — streak that achieves min period
```

NVS keys: `rgb_pmin`, `rgb_pmax`, `rgb_smax`.

---

## Web UI

New "RGB LED" collapsible section added to the existing config form in `web_server.cpp`, below the existing display settings:

- **Min period (ms):** number input, range 400–4000, default 1200
- **Max period (ms):** number input, range 2000–20000, default 8000
- **Streak max:** number input, range 1–365, default 30

After POST `/save`, `rgb_led_update_params()` is called with the new config so changes apply immediately without reboot.

---

## Library Dependency

`adafruit/Adafruit NeoPixel` added to `platformio.ini` `lib_deps`. Single pixel, RGB order `NEO_GRB + NEO_KHZ800`, pin `NEOPIXEL_PIN` (8) from `PINS_ESP32-C6-LCD-1_47.h`.

---

## Non-Goals

- No per-day color animation (just streak-driven)
- No brightness config knob (amplitude always 0→full base_color)
- No second-core/RTOS task — `millis()` sine in main loop is sufficient
