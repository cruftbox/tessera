# Tessera — TODO

## Code review follow-up (2026-06-19)

A full code review was run across the firmware. The mechanical fixes and dedup
refactors below were applied; the **design decisions** still need a call and are
queued for review.

### Done in this pass
- GT911 `read()` clamps `touches` to 5 before the `points[5]` loop (overflow guard).
- GT911 `calculateChecksum()` initialises its accumulator (`= 0`).
- Numeric HA attributes parsed with `is<float>()` (rejects null **and** non-numeric),
  so a bad/`unavailable` read no longer shows a fake `0°`.
- `on_message` logs the error on a failed JSON parse instead of dropping silently.
- Refactors (behavior-preserving): `begin_call_service()` helper (was 6 hand-built
  envelopes), `ha_get_state()` REST helper (was 4–5 copies), `entity_domain()` helper,
  and a shared `frosted_fill()` style.

### Deferred — design decisions (need a call before implementing)

1. **WiFi / HA / OTA recovery** — `main.cpp`. If WiFi fails to associate in the ~20 s
   boot window, `ha_init()`/`ota_init()`/NTP never run and are never retried; only a
   power-cycle recovers. *Decision:* a connection state machine that (re)establishes
   WiFi and lazily (re)inits HA/OTA on first success. (Pairs with the setup-wizard work.)

2. **Thermostat HVAC mode selector** — `ui.cpp` detail view. `ha_climate_set_mode()`
   exists but has no UI caller, so there's no on-device way to turn the system back on
   once HA reports `off` (the setpoint rows hide). *Decision:* add a Heat/Cool/Off/Auto
   selector to the detail view and wire it to `ha_climate_set_mode()`.

3. **Thermostat mid-edit desync** — `ui.cpp:~715`. `ui_update_thermostat` preserves
   edited setpoints but overwrites `mode`/`dual` from an HA echo; a concurrent external
   mode change can make the debounced flush send the wrong-shape setpoints. *Decision:*
   also lock `mode`/`dual` during the edit window, or correlate the echo to the in-flight
   command id.

4. **Dual-setpoint crossing behavior** — `ui.cpp:~251`. When one setpoint is moved past
   the other, the code drags the *un-edited* setpoint to match and sends both to HA.
   *Decision:* define intended behavior (clamp the edited one only? send only the edited
   field?) and stop moving the untouched setpoint silently.

5. **Optimistic-update correctness** — `ui.cpp` / `ha_client.cpp`. Tile taps restyle
   optimistically, but `call_service` results are never correlated to `msg_id` and HA
   errors aren't handled; a rejected/dropped call leaves the tile lying about state.
   *Decision:* track the in-flight id and revert/flag on failure or a timeout.

6. **Non-blocking initial fetch** — `ha_client.cpp` `fetch_initial_states()` (and the
   120 s refetch). The initial REST fetch is `MOSAIC_COUNT+3` sequential **blocking**
   GETs from `ha_loop()`, freezing LVGL/touch for seconds. *Decision:* drive it as a
   one-entity-per-loop state machine, or switch to a single WS `get_states`; reconsider
   whether the 120 s REST refetch is needed at all given the WS subscription.

### Lower priority / maintainability
- Consolidate the per-fetch ArduinoJson filter/doc capacity magic-numbers (documented
  foot-gun — undersizing silently drops keys).
- LVGL draw buffers are 480×20 in PSRAM; a taller buffer and/or an internal-SRAM buffer
  would cut full-screen-redraw overhead.
- `delay(5)` in `loop()` and the `fan_off_at==0` guard were reviewed and **left as-is**
  on purpose (the `delay` yields to FreeRTOS; the guard is harmless defensive code).
