# ESP32-C6 GitHub Contributions Monitor

A PlatformIO/Arduino firmware that displays your GitHub contribution graph and stats on an ESP32-C6 with a 1.47" ST7789 IPS LCD. Configured entirely through a browser-based web portal — no re-flashing needed to change credentials or settings.

![Platform](https://img.shields.io/badge/platform-ESP32--C6-blue)
![Framework](https://img.shields.io/badge/framework-Arduino-orange)
![LVGL](https://img.shields.io/badge/LVGL-v9.2-green)

---

## Features

- **Contribution grid** — full 53-week GitHub contribution heatmap rendered with GitHub's green colour scale
- **Stats screen** — total contributions, current streak, busiest day, last-updated age
- **Auto-rotate** — alternates between grid and stats views on a configurable interval
- **Web config portal** — change WiFi, GitHub token, username, brightness, and timing via any browser
- **AP setup mode** — on first boot (no credentials stored), device acts as a WiFi access point so you can configure it without editing code
- **NVS persistence** — all settings survive power cycles via ESP32 non-volatile storage

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU board | ESP32-C6-DevKitC-1 |
| Display | 1.47" ST7789 172×320 IPS (e.g. Waveshare ESP32-C6-LCD-1.47) |
| Interface | SPI (hardware) |

### Pin Wiring

| Signal | GPIO |
|--------|------|
| SPI SCK | 7 |
| SPI MOSI | 6 |
| SPI MISO | 5 |
| Display DC | 15 |
| Display CS | 14 |
| Display RST | 21 |
| Backlight (PWM) | 22 |
| SD card CS | 4 |
| NeoPixel | 8 |
| Button A | 9 |

---

## Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- The `pioarduino` community platform is used to enable full Arduino support on ESP32-C6. It is fetched automatically on first build — no manual install required.

---

## Getting Started

### Option A — Developer path (edit code, compile, flash)

Best if you want to hardcode default WiFi credentials or modify the firmware.

1. **Clone the repo**
   ```
   git clone https://github.com/merlinmb/ESP32-C6-LCD-1.47_GithubCommits.git
   cd ESP32-C6-LCD-1.47_GithubCommits
   ```

2. **Create `include/secrets.h`** from the example:
   ```
   cp include/secrets.h.example include/secrets.h
   ```
   Fill in your credentials:
   ```c
   #define WIFI_SSID     "your_wifi_ssid"
   #define WIFI_PASSWORD "your_wifi_password"
   ```
   These are compile-time fallbacks. The web portal can override them at runtime.

3. **Build and flash** via PlatformIO:
   ```
   pio run --target upload
   ```
   Or use the PlatformIO sidebar in VS Code.

4. On first boot the device connects to the WiFi from `secrets.h`, then opens the web config portal. Visit `http://<device-ip>` to enter your GitHub username and PAT.

---

### Option B — No-code path (flash pre-built, configure via browser)

Best if you just want to flash and configure without touching code.

1. Flash the firmware (pre-built binary or via PlatformIO with a blank `secrets.h`).

2. On first boot — because no WiFi credentials are stored in NVS — the device enters **AP setup mode**:
   - LCD shows: `Connect Wi-Fi to: GithubMonitor-Setup`
   - An access point named **`GithubMonitor-Setup`** is broadcast

3. On your phone or laptop, connect to the `GithubMonitor-Setup` WiFi network.

4. Open a browser and go to **`http://192.168.4.1`**

5. Fill in all fields (see [Web Config Portal](#web-config-portal)) and click **Save**. The device reboots and connects to your home network.

---

## GitHub Personal Access Token (PAT)

The firmware uses the GitHub GraphQL API to fetch contribution data. A PAT is required.

**Steps:**
1. Go to **GitHub → Settings → Developer settings → Personal access tokens → Tokens (classic)**
2. Click **Generate new token (classic)**
3. Set an expiry and select the scope:
   - **`public_repo`** — if your GitHub profile/contributions are on public repositories
   - **`repo`** (full) — if your contributions include private repositories
4. Copy the generated token — you only see it once
5. Paste it into the GitHub Token field in the web config portal (or `secrets.h` equivalent if hardcoding)

> The token is stored in ESP32 NVS flash. It is never transmitted anywhere except to `api.github.com` over HTTPS.

---

## Web Config Portal

Accessible at `http://<device-ip>` any time the device is on your network. In AP setup mode, use `http://192.168.4.1`.

| Field | NVS key | Default | Description |
|-------|---------|---------|-------------|
| WiFi SSID | `wifi_ssid` | — | Your network name |
| WiFi Password | `wifi_pass` | — | Your network password |
| GitHub Username | `gh_user` | — | GitHub username to monitor |
| GitHub Token | `gh_token` | — | PAT with `public_repo` or `repo` scope |
| Brightness | `brightness` | 200 (of 255) | LCD backlight level |
| Screen switch interval | `switch_sec` | 30 s | How often to alternate between grid and stats |
| Refresh interval | `refresh_min` | 30 min | How often to re-fetch from GitHub API |
| Animation top % | `anim_pct` | 20% | Top percentage of contribution cells to animate |
| Animation period | `anim_ms` | 2000 ms | Breathing animation cycle duration |

Clicking **Save** writes all values to NVS and reboots the device.

---

## Project Structure

```
├── include/
│   ├── secrets.h.example     # Template — copy to secrets.h and fill in WiFi creds
│   ├── secrets.h             # Git-ignored — your actual WiFi fallback credentials
│   ├── PINS_ESP32-C6-LCD-1_47.h  # Pin definitions and GFX driver init
│   ├── lv_conf.h             # LVGL v9 configuration
│   └── board_config.h        # Board-level compile-time checks
├── src/
│   ├── main.cpp              # Setup, loop, WiFi connect, screen/timer orchestration
│   ├── config.h / .cpp       # Config struct, NVS load/save, defaults
│   ├── github_api.h / .cpp   # GraphQL fetch, JSON parse, contribution level mapping
│   ├── display_grid.h / .cpp # LVGL contribution grid screen
│   ├── display_stats.h / .cpp# LVGL stats screen (streak, total, age)
│   └── web_server.h / .cpp   # Embedded HTTP config portal
└── platformio.ini            # PlatformIO build config
```

---

## Troubleshooting

**Display stays black after flashing**
- Confirm the board selected in PlatformIO is `esp32-c6-devkitc-1`
- Ensure `ARDUINO_USB_CDC_ON_BOOT=1` is set (it is in `platformio.ini` by default)
- Check serial output at 115200 baud for error messages

**Device doesn't connect to WiFi**
- If using Option A: verify `secrets.h` has correct SSID/password
- If using Option B: complete the AP setup flow first — the device won't connect until credentials are saved via the portal
- Check that your network is 2.4 GHz (ESP32-C6 supports both 2.4 GHz and the 802.15.4 radio, but WiFi is 2.4 GHz only)

**GitHub data not loading / fetch failed**
- Confirm the PAT is entered correctly in the web portal (no extra spaces)
- Verify the PAT has not expired
- Check that the correct scope is selected: `public_repo` for public contributions, `repo` for private
- Serial monitor will print `[Main] Fetch failed` with HTTP error details on failure

**Web portal not reachable**
- Find the device IP from your router's DHCP table, or from the serial monitor output (`[WiFi] Connected, IP: x.x.x.x`)
- Ensure your computer is on the same network as the device

---

## License

MIT — see [LICENSE](LICENSE) for details.
