# Tessera Setup Wizard — Plan

Status: **planned, not yet built.** Keep it minimal.

## Scope decision
The wizard handles only: **initial flash, WiFi connection, HA connection (host/port/token).**
It does **not** edit devices/tiles. Devices stay in code (`MOSAIC[]` in `config.h`); the user
edits code to add devices, ideally with an LLM's help — so the schema must stay clean and
self-describing.

This deliberately avoids full web-config complexity: no dynamic tile arrays, and the
icon-font limitation is moot (tiles stay compile-time).

## Key win: config.h becomes secret-free
Move WiFi SSID/pw + HA host/port/token (+ TZ, NTP) **out of config.h into runtime NVS**
(set via the wizard). `config.h` then holds only device layout (`MOSAIC[]`), display
settings, pins, and icon macros — **no secrets**. The "sanitize config.h before committing"
problem disappears and config.h can be committed as the example.

## Components
1. **Flashing (optional nicety): ESP Web Tools** — a browser "Install" button on a GitHub
   Pages site, flashing over WebSerial. Caveat: requires Chrome/Edge (no Safari/Firefox).
   Otherwise PlatformIO/esptool as today.
2. **First-boot provisioning: captive portal.** No WiFi stored → device starts an AP +
   captive portal; user joins and enters WiFi + HA host/port/token (+ TZ). Candidate lib:
   **WiFiManager (tzapu)** with custom parameters for the HA fields. WiFiManager stores WiFi
   itself; custom params must be persisted by us.
3. **Storage:** ESP32 **Preferences (NVS)** for the scalars (HA host/port/token/TZ; WiFi via
   WiFiManager). LittleFS only needed if tiles ever move to runtime — out of scope.
4. **Boot-path refactor:** read creds from NVS instead of `#define`s. Today `main.cpp` uses
   `WIFI_SSID`/`WIFI_PASSWORD` and `ha_client` uses `HA_HOST`/`HA_PORT`/`HA_TOKEN` macros —
   these become runtime getters.
5. **Escape hatch:** re-enter setup when WiFi/token changes — fall back to the captive portal
   on WiFi or HA-auth failure, plus an on-panel "Re-run setup" action and/or a reset gesture.

## The LLM-friendly schema (the real deliverable)
- Keep `MOSAIC[]`, but make each row maximally self-describing; comment every field
  (`label`, `entity_id`, `page`, `on_pct`, `icon`).
- Inline-document the available `ICON_*` macros (icons limited to glyphs compiled into the
  `mdi_icons` font).
- README "How to add a device": schema + examples + note that a brand-new icon means
  regenerating the font via `tools/` (`@mdi/font` + `lv_font_conv`; add codepoint to the
  `--range`, add an `ICON_` define).
- Goal: paste `config.h` + README into an LLM, say "add my garage light," and it has
  everything it needs.

## Gotchas
- Icon set limited to compiled glyphs.
- HA token travels over plain HTTP on the LAN and the provisioning AP is briefly open —
  acceptable for home use; note it.
- Pairs naturally with **OTA** (ArduinoOTA) — both are "stop needing the USB cable." Build
  them together.

## Phasing
- **Phase 1 (first):** NVS-backed credentials + captive-portal provisioning + boot-path
  refactor. Core value; pairs with OTA.
- **Phase 2:** none planned — tile editing stays in code by design.
- Decide an expanded icon set early if going beyond current glyphs.

## Libraries to evaluate (pin versions at build time)
WiFiManager (tzapu/WiFiManager), Preferences (built-in), ArduinoJson (already used),
ArduinoOTA (built-in, for the paired OTA work).
