#pragma once
#include <lvgl.h>

// Build the full UI — header, swipeable 3x3 tile grid, thermostat view.
// Call once at boot, after lv_init() and the display/touch drivers.
void ui_init();

// --- Live state, pushed in from ha_client ---
void ui_update_tile_state(int mosaic_idx, const char* state);  // on/off + availability
void ui_update_tile_value(int mosaic_idx, const char* value);  // value line, e.g. "60%"
void ui_set_ha_connected(bool connected);    // HA WebSocket -> header status dot
void ui_set_wifi_connected(bool connected);  // WiFi link    -> header WiFi icon
void ui_set_indoor_temp(int deg);            // header indoor temperature
void ui_set_outdoor_temp(int deg);           // header outdoor temperature

// --- Input / idle handling, driven by touch.cpp ---
void ui_notify_touch();          // activity: reset the idle-dim timer / wake the screen
bool ui_is_dimmed();             // true while the screen is idle-dimmed
void ui_touch_pressed();         // called at the start of each new touch
void ui_swipe_page(int delta);   // +1 = next page, -1 = previous page

// Thermostat (climate entity) state pushed from ha_client.
typedef struct {
  char mode[12];   // off / heat / cool / heat_cool
  int  current;    // current_temperature
  int  low;        // target_temp_low  (heat_cool)
  int  high;       // target_temp_high (heat_cool)
  int  target;     // temperature      (heat or cool single setpoint)
  bool dual;       // true in heat_cool (use low/high), false use target
  bool available;
  char action[10]; // hvac_action: idle / heating / cooling / off
} ThermoState;
void ui_update_thermostat(const ThermoState* s);
void ui_set_fan_state(bool on);
