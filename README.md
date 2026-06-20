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
- LVGL 8.4, Arduino_GFX, links2004/WebSockets, ArduinoJson (pinned in `platformio.ini`); `TAMC_GT911` is vendored under `lib/` (patched — see [Known setup notes](#known-setup-notes))

## Requirements

Before you start, you'll need:

- **A running Home Assistant instance** on your network, reachable over HTTP
  (default port `8123`) with the WebSocket API enabled (on by default). Tessera is
  a controller — it does **not** run Home Assistant itself.
- **A Home Assistant long-lived access token** (HA → your profile → *Long-Lived
  Access Tokens*) for the panel to authenticate with.
- **The entities you want to control** already configured in Home Assistant —
  lights, switches, fans, a `climate` thermostat, and a `weather` entity for the
  outdoor temperature.
- **A 2.4 GHz WiFi network** — the ESP32-S3 does not support 5 GHz. The panel and
  Home Assistant must be reachable on the same network.
- **A computer with [PlatformIO](https://platformio.org/)** and a USB cable for the
  initial flash (later updates can go over the network via OTA).

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

## Working with an LLM (recommended)

Tessera is built to be configured and extended with an AI coding assistant
(Claude, etc.) — and doing so is genuinely the easiest path. The config and the
code are deliberately self-describing for exactly this.

- **Setup & install** — paste this README and `include/config.h.example` into an
  LLM and describe your setup ("Home Assistant at `192.168.x.x`, these devices…").
  It can fill in `MOSAIC[]`, pick `ICON_*` glyphs, and walk you through getting a
  long-lived token and flashing.
- **Adding or changing devices** — paste your `config.h` and say *"add my garage
  light, entity `switch.garage`."* Every field (`label`, `entity_id`, `page`,
  `on_pct`, `icon`) is commented and the available icons are listed, so the model
  has what it needs to produce a correct `MOSAIC[]` row.
- **Modifying the firmware** — each source file carries a module-header comment
  describing its role and gotchas, so an assistant can orient quickly to make UI
  or behavior changes.
- **Troubleshooting** — share serial output or symptoms; common pitfalls (the
  touch coordinate transform, ArduinoJson filter sizing, HA token / IP-ban) are
  documented in the code and below.

You don't *need* an LLM — everything is editable by hand — but the project is
structured to make AI-assisted setup and extension fast and reliable.

## Icons

Tile/header icons come from a generated LVGL font (`src/mdi_icons.c`) built from
[Material Design Icons](https://pictogrammers.com/library/mdi/). The font is
committed, so a normal build needs no extra steps. To add or change glyphs, see
`tools/` (uses `@mdi/font` + `lv_font_conv`) and regenerate with the new codepoints,
then add matching `ICON_*` defines in `config.h`.

## Known setup notes

- **GT911 patch**: the upstream `TAMC_GT911` library calls `pinMode()` on the
  INT/RST pins even when they're unused (`-1`), which on this board logs
  `Invalid IO 255`. A patched copy is **vendored under `lib/TAMC_GT911`** (the
  INT/RST pin operations in `reset()` are guarded), so a fresh `git clone` + build
  works with no manual patching — and it is intentionally **not** in `lib_deps`, so
  a dependency install can't overwrite it. See `lib/TAMC_GT911/PATCH.md`.
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
