# Horloge (ESP32-S2 Clock)

7-segment clock on a 240×280 TFT display with NTP time sync, weather, web UI
and full local navigation (6 buttons + on-device menu).

## Features

### Display
- Large **7-segment** clock (DSEG7 Classic, regular or italic) with **4-bpp
  grayscale anti-aliasing**.
- ST7789V3 240×280 TFT in landscape (280×240).
- Day / night color and brightness profiles, switched on a configurable
  schedule.
- **Manual day/night override** (UP button) until next scheduled transition.
  State persisted in NVS.
- **Sun / moon icons**, **seconds** display, **weather temperature** — each can
  be toggled individually.
- **Rainbow** color cycling with adjustable speed (1–32).
- **Eco mode** with reduced refresh rate and dim digit intensity.
- **Rotation 180°** for upside-down mounting.
- **12h / 24h** time format.

### Connectivity
- Wi-Fi STA + AP fallback (`Horloge` / `horloge1234`).
- NTP time sync with timezone configurable from the web UI.
- mDNS hostname.
- OpenWeatherMap integration (city + API key in web UI).
- Web UI with embedded DSEG7 font (matches the TFT look).
- Debug page: virtual TFT mock + virtual button pad with keyboard shortcuts.
- OTA firmware update at `/update`.

### Local UI
- 6-button navigation: UP / DOWN / LEFT / RIGHT / A / B.
- Multi-page menu (Jour, Nuit, Affichage, Wi-Fi).
- Hue-only color picker on the TFT (full-saturation hue 0–359°).
- All settings persisted to NVS under namespace `wifi`.

## Button shortcuts (outside the menu)
| Button | Action |
|---|---|
| **UP** | Toggle day / night override |
| **DOWN** | Toggle weather display |
| **LEFT** | Toggle sun / moon icons |
| **RIGHT** | Toggle seconds display |
| **B** | Toggle 12h / 24h time format |
| **DOWN + A** | Open the menu |

In the menu: UP/DOWN navigate, LEFT/RIGHT change field, A confirms / enters,
B goes back.

## Hardware

### ESP32-S2 → ST7789V3 (240×280)
| Display | ESP32-S2 |
|---|---|
| SCL | GPIO 12 |
| SDA | GPIO 11 |
| RES | GPIO  9 |
| DC  | GPIO  7 |
| CS  | GPIO  5 |
| BLK | GPIO  3 |
| VCC | 3V3 |
| GND | GND |

### Buttons (active LOW, internal pull-up)
| Button | GPIO |
|---|---|
| UP    | 1 |
| DOWN  | 2 |
| LEFT  | 4 |
| RIGHT | 6 |
| A     | 8 |
| B     | 10 |

### Battery monitoring (Wemos D1 Battery Shield)
| Shield | ESP32-S2 |
|---|---|
| A0 (BAT-A0 jumper closed) | GPIO 14 |
| GND | GND |
| 5V (boost out) | 5V / VBUS |

The shield's on-board divider is **130k / 100k** → ratio **2.30**
(set in `config.h` as `BATT_DIVIDER`). Close the soldable `BAT-A0` jumper on
the shield to expose the battery voltage on its A0 pad. Verify the divider
values on your shield revision (older boards may use 220k/100k → 3.20).

## Build (PlatformIO)
1. Install PlatformIO.
2. `pio run` to build.
3. `pio run -t upload --upload-port COMx` to flash.

The version string shown by the firmware is stamped from the latest git tag
by `version_build.py` (pre-build script).

## Releases
Use `release.ps1`:
```powershell
powershell -ExecutionPolicy Bypass -File .\release.ps1 -Version vX.Y.Z -Notes "..."
```
The script commits, tags, pushes, rebuilds and uploads `firmware.bin` to a
GitHub release via `gh`.

## First use
1. After flashing, the device starts a Wi-Fi AP:
   - SSID: **Horloge**
   - Password: **horloge1234**
2. Connect and open `http://192.168.4.1/`
3. Configure Wi-Fi to connect to your home network for NTP.
4. (Optional) Set OpenWeatherMap API key + city for weather.
5. For firmware updates, use `http://<ip>/update` and upload a `.bin`.

## File map
- `horloge.cpp` – entry point and main loop (button dispatch, refresh).
- `config.h` – pin map and defaults.
- `display.h/.cpp` – ST7789 rendering, 7-segment clock, AA blender,
  day/night state, override, icons, weather, rainbow.
- `menu.h/.cpp` – on-device menu (Jour / Nuit / Affichage / Wi-Fi).
- `buttons.h/.cpp` – debounced button polling, hold detection.
- `battery.h/.cpp` – ADC voltage measurement and SoC estimation.
- `weather.h/.cpp` – OpenWeatherMap client.
- `webui.h/.cpp` – HTTP server, web UI, debug page, OTA, JSON API.
- `DSEG7_80.h` / `DSEG7_80_Italic.h` – embedded 4-bpp grayscale font glyphs.
- `font_data.h` – DSEG7 TTF blobs served by the web UI.
- `convert_font.py` – TTF → C array converter (1-bpp or 4-bpp grayscale).
- `version_build.py` – pre-build script that stamps `FW_RELEASE` from git.
- `release.ps1` – release helper (tag + push + build + upload).
