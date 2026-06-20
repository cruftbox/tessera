# Tessera — project guide for Claude

Home Assistant wall-panel controller for the **Guition ESP32-S3-4848S040** (4" 480×480
capacitive-touch). 3×3 tile grid that toggles HA entities over the WebSocket API, plus
header indoor/outdoor temps and a full Nest thermostat control view.

Repo: `github.com/cruftbox/tessera` (private, MIT).

## Hardware / toolchain
- ESP32-S3-WROOM-1 N16R8 (16MB flash, 8MB PSRAM), ST7701S RGB display, GT911 touch, CH340 USB-serial on **COM7**.
- PlatformIO + Arduino (espressif32 55.3.37, core 3.3.7). LVGL 8.4, Arduino_GFX, links2004/WebSockets, ArduinoJson. TAMC_GT911 is **vendored** (see below).

## Build / flash / monitor (Windows PowerShell)
```powershell
# Build + flash
$env:PYTHONIOENCODING = "utf-8"; cd "C:\Users\micha\OneDrive\Code\tessera"; & "C:\Users\micha\.platformio\penv\Scripts\platformio.exe" run --target upload

# Serial monitor — USE THIS (pio device monitor buffers unreliably here)
& "C:\Users\micha\.platformio\penv\Scripts\python.exe" "C:\Users\micha\OneDrive\Code\tessera\serial_read.py"
```
Each flash auto-resets the board (no manual reboot needed). `serial_read.py` toggles DTR (resets) and reads ~50s.

## File structure
```
src/   main.cpp, display.cpp/h, touch.cpp/h, ui.cpp/h, ha_client.cpp/h, mdi_icons.c (generated font)
include/ secrets.h (gitignored creds), secrets.h.example, config.h (gitignored layout), config.h.example, lv_conf.h
lib/TAMC_GT911/  vendored patched touch lib (+ PATCH.md)
tools/  icon-font generation (@mdi/font + lv_font_conv)  [node_modules gitignored]
setup_wizard.py  host-side first-run wizard (writes include/secrets.h, validates token, flashes)
serial_read.py   serial monitor helper
docs/   TODO.md (deferred design decisions)
platformio.ini, partitions_16MB_ota.csv
```

## Config & secrets
- **Split into two gitignored files** (committed `.example` templates for each):
  - `include/secrets.h` — WiFi creds, HA host/port/**token**, TZ/NTP. Written by `setup_wizard.py`. Included by `main.cpp` (WiFi/TZ) and `ha_client.cpp` (HA host/port/token). HA_HOST stays a compile-time `#define` so the `Origin:` literal-concat in `ha_init()` still works.
  - `include/config.h` — the `MOSAIC[]` tile array + header entities, display, pins, icons. **No secrets**, so it's safe to paste into an LLM.
- `setup_wizard.py` (repo root): prompts WiFi/HA/token/TZ, validates the token via `GET /api/` from the PC *before* flashing (avoids the IP-ban loop), writes `secrets.h`, optionally flashes + tails serial. Auto-detects the serial port (not tied to COM7).
- HA host = **Wintermute** (Raspberry Pi) at `192.168.4.91:8123`. Device IP is `192.168.4.117`.
- To add a device: add a row to `MOSAIC[]` (fields: `label, entity_id, page, on_pct, icon`). Icons are `ICON_*` macros — limited to glyphs compiled into `mdi_icons`. A new icon needs a font regen — the exact `lv_font_conv` command is in the README "Icons" section.

## What works (all verified on-device)
- Display (rotation 3), touch (coord transform in touch.cpp: `x=rawY, y=479-rawX`).
- HA WebSocket auth + REST initial fetch; tile tap → toggle with optimistic update (gated on `ha_is_connected()`).
- 3×3 grid, MDI icons, value text (brightness/fan %), availability pips (green/amber).
- Fan low-start (`on_pct`), instant swipe paging (handled in touch.cpp, not LVGL gesture), idle dim + wake-touch consume.
- Gradient/frosted visual design. Header: clock + indoor/outdoor temps (from HA `climate` + `weather.forecast_home`; 120s self-heal refetch).
- **Nest thermostat**: 9th-cell tile → full-screen detail (current temp, Heating/Cooling/Idle status, Heat-to/Cool-to setpoints with debounced send + echo-lock, fan timer Off/1h/2h/4h/On with active highlight). Matches front-page styling.

## Gotchas / lessons
- **TAMC_GT911 vendored** in `lib/` (patched to guard INT/RST pins = -1 → 255). Do NOT re-add `tamctec/TAMC_GT911` to `lib_deps`. See `lib/TAMC_GT911/PATCH.md`.
- Serial needs `-DARDUINO_USB_CDC_ON_BOOT=0` (already in platformio.ini).
- ArduinoJson **filter** docs must be sized generously — too-small filter silently drops the last-added keys (caused thermostat setpoints to read 0). on_message filter is `<512>`.
- LVGL `margin` styles are NOT compiled in this build — use `pad_*`.
- HA IP-bans the device after repeated failed auths (bad token). Verify token from a PC first; clear via `ip_bans.yaml` + `docker restart home-assistant` on Wintermute. Firmware stops reconnecting on `auth_invalid` to avoid ban loops.

## Roadmap / next up
- **Done:** OTA (ArduinoOTA wireless updates); setup wizard (host-side `setup_wizard.py` —
  chose Model A, a PC-side CLI that writes `secrets.h` + flashes, **not** the captive-portal+NVS
  approach originally considered); secret-free `config.h`;
  LLM-friendly "Adding devices" docs.
- **Next:** review deferred design decisions in `docs/TODO.md` (WiFi/HA recovery, thermostat
  HVAC mode selector, mid-edit desync, dual-setpoint crossing, optimistic-update correctness,
  non-blocking initial REST fetch). Optional: 0.5° thermostat steps, fan timer-remaining display.
