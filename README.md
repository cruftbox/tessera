# Tessera

A Home Assistant wall-panel controller for the **Guition ESP32-S3-4848S040** — a 4" 480×480 capacitive-touch display. Tessera shows a grid of device tiles that toggle Home Assistant entities over the WebSocket API, plus indoor/outdoor temperatures and a full thermostat control view.

> Status: working prototype. Documentation is intentionally basic for now and will be expanded.

## Features

- **3×3 tile grid**, data-driven from a `MOSAIC[]` array in `config.h`
- **Toggle lights / switches / fans** via the HA WebSocket API, with optimistic UI updates
- **Per-tile icons** (Material Design Icons) and **live value text** (brightness %, fan speed %)
- **Availability pips** — green when the entity is reachable, amber when `unavailable`
- **Fan low-start** — optionally turn a fan on at a set speed instead of 100%
- **Header**: live clock + indoor and outdoor temperatures (read from HA, no third-party API)
- **Thermostat control** — tap the thermostat tile for a detail view with current temp, mode (Off/Heat/Cool/Auto) and setpoint +/- (dual setpoints in heat_cool)
- **Swipe paging**, idle dimming, and a wake-touch that only wakes the screen

## Hardware

- Guition ESP32-S3-4848S040 (ESP32-S3-WROOM-1 N16R8, 16 MB flash, 8 MB PSRAM)
- ST7701S RGB display, GT911 capacitive touch
- CH340 USB-serial (appears as a COM port)

## Toolchain

- [PlatformIO](https://platformio.org/) + Arduino framework
- LVGL 8.4, Arduino_GFX, TAMC_GT911, links2004/WebSockets, ArduinoJson (pinned in `platformio.ini`)

## Setup

1. **Configure your devices and secrets**
   ```sh
   cp include/config.h.example include/config.h
   ```
   Edit `include/config.h`: set your WiFi credentials, HA host, a Home Assistant
   **long-lived access token** (HA → your profile → *Long-Lived Access Tokens*),
   your timezone, and your tiles/entities in `MOSAIC[]`. `config.h` is gitignored.

2. **Build & flash** (set the correct COM port in `platformio.ini`)
   ```sh
   pio run --target upload
   ```

## Icons

Tile/header icons come from a generated LVGL font (`src/mdi_icons.c`) built from
[Material Design Icons](https://pictogrammers.com/library/mdi/). The font is
committed, so a normal build needs no extra steps. To add or change glyphs, see
`tools/` (uses `@mdi/font` + `lv_font_conv`) and regenerate with the new codepoints,
then add matching `ICON_*` defines in `config.h`.

## Known setup notes

- **GT911 patch**: the `TAMC_GT911` library calls `pinMode()` on the INT/RST pins
  even when they're unused (`-1`), which on this board logs `Invalid IO 255`. The
  fix is to guard those pin operations in `reset()`. Because the library lives under
  `.pio/libdeps/` (not committed), this patch must be re-applied after a fresh
  dependency install. (Proper fix — vendoring a patched copy under `lib/` — is TODO.)
- Serial output requires `ARDUINO_USB_CDC_ON_BOOT=0` (already set in `platformio.ini`)
  so logging goes to the CH340 UART rather than native USB-CDC.

## Project layout

```
src/        main, display, touch, ui, ha_client, mdi_icons (generated font)
include/    config.h.example, lv_conf.h
tools/      icon-font generation (@mdi/font + lv_font_conv)
platformio.ini, partitions_16MB_ota.csv
```

## License

[MIT](LICENSE).
