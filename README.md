# Horloge (ESP32-S2 Clock)

7-segment clock on a 240×280 TFT display with NTP time sync.

## Features
- Large **7-segment LED-style** clock display (red on black)
- 240×280 **ST7789V3** TFT, landscape mode
- **Wi-Fi** for NTP time synchronization
- **Web UI** for Wi-Fi config and time sync
- **OTA** firmware update at `/update`

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

## Build (PlatformIO)
1. Install PlatformIO.
2. `pio run` to build, `pio run -t upload` to flash.

## Use
1. After flashing, the device starts a Wi-Fi AP:
   - SSID: **Horloge**
   - Password: **horloge1234**
2. Connect and open `http://192.168.4.1/`
3. Configure Wi-Fi to connect to your home network for NTP.
4. For firmware updates, use `http://<ip>/update` and upload a `.bin`.

## File map
- `horloge.cpp` – entry point
- `config.h` – pins, constants
- `display.h/.cpp` – ST7789 7-segment clock display
- `webui.h/.cpp` – Web server, Wi-Fi config, OTA
- `SevenSeg128.h` – generated 7-segment font
- `convert_font.py` – font conversion script
