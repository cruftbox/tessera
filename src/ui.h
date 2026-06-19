#pragma once
#include <lvgl.h>

void ui_init();
void ui_update_tile_state(int mosaic_idx, const char* state);
void ui_update_tile_value(int mosaic_idx, const char* value);
void ui_set_ha_connected(bool connected);
void ui_notify_touch();
bool ui_is_dimmed();
void ui_touch_pressed();        // called at the start of each new touch
void ui_swipe_page(int delta);  // +1 = next page, -1 = previous page
void ui_set_indoor_temp(int deg);
void ui_set_outdoor_temp(int deg);

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
