#pragma once

void ha_init();
void ha_loop();
void ha_toggle(const char* entity_id);
void ha_fan_turn_on_pct(const char* entity_id, float pct);
void ha_climate_set_mode(const char* mode);
void ha_climate_set_temp_dual(int low, int high);
void ha_climate_set_temp_single(int target);
bool ha_is_connected();
