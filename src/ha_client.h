#pragma once

// Start the HA WebSocket client (connects + authenticates). Call once after WiFi.
void ha_init();
// Pump the WebSocket and run deferred REST fetches. Call every loop().
void ha_loop();
// True once authenticated to HA — tile/thermostat actions are gated on this.
bool ha_is_connected();

// --- Outgoing service calls (no-ops until connected) ---
void ha_toggle(const char* entity_id);                     // toggle a light/switch/fan
void ha_fan_turn_on_pct(const char* entity_id, float pct);
void ha_light_turn_on_kelvin(const char* entity_id, uint16_t kelvin); // turn a fan on at a % speed
void ha_climate_set_mode(const char* mode);                // off/heat/cool/heat_cool
void ha_climate_set_temp_dual(int low, int high);          // heat_cool low/high setpoints
void ha_climate_set_temp_single(int target);               // single heat or cool setpoint
void ha_thermo_fan(bool on);                               // thermostat fan on/off
