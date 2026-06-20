// ha_client.cpp — Home Assistant client.
//
// Maintains the authenticated HA WebSocket connection (/api/websocket): logs in
// with the long-lived token, subscribes to state_changed events, and pushes
// updates into the UI via ui_*. Tile taps and thermostat changes are sent back
// as call_service messages. Initial state and the header temperatures are
// fetched over REST from ha_loop() (not the WS callback) so they never block the
// socket. On auth_invalid it stops reconnecting, to avoid HA IP-banning the
// device in a retry loop.
//
// NOTE: the on_message ArduinoJson filter must be sized generously — an
// undersized filter silently drops the last-added keys (see CLAUDE.md gotchas).

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include "ha_client.h"
#include "ui.h"
#include "config.h"

static WebSocketsClient ws;
static bool authenticated = false;
static int msg_id = 1;

static void send_json(JsonDocument& doc) {
  String s;
  serializeJson(doc, s);
  ws.sendTXT(s);
}

// Copy the entity domain (text before the first '.') into buf, NUL-terminated.
static void entity_domain(const char* eid, char* buf, size_t n) {
  size_t i = 0;
  for (; eid[i] && eid[i] != '.' && i + 1 < n; i++) buf[i] = eid[i];
  buf[i] = '\0';
}

// Fill `doc` with the common call_service envelope (id/type/domain/service +
// service_data.entity_id). The caller adds any extra service_data fields, then send_json(doc).
static void begin_call_service(JsonDocument& doc, const char* domain,
                               const char* service, const char* entity_id) {
  doc["id"] = msg_id++;
  doc["type"] = "call_service";
  doc["domain"] = domain;
  doc["service"] = service;
  doc["service_data"]["entity_id"] = entity_id;
}

// When a tile is ON, show a richer value: brightness % for lights, speed % for
// fans. OFF tiles keep the default "Off" text set by ui_update_tile_state.
static void set_tile_value(int idx, const char* eid, const char* state, JsonVariantConst attrs) {
  if (!state || strcmp(state, "on") != 0) return;
  char domain[16]; entity_domain(eid, domain, sizeof(domain));
  char buf[12];
  if (strcmp(domain, "light") == 0 && attrs["brightness"].is<int>()) {
    int b = attrs["brightness"].as<int>();          // 0..255
    snprintf(buf, sizeof(buf), "%d%%", (b * 100 + 127) / 255);
    ui_update_tile_value(idx, buf);
  } else if (strcmp(domain, "fan") == 0 && attrs["percentage"].is<int>()) {
    snprintf(buf, sizeof(buf), "%d%%", attrs["percentage"].as<int>());
    ui_update_tile_value(idx, buf);
  }
}

// Blocking GET /api/states/<eid>, parsing the body into `doc` through `filter`.
// Returns true on HTTP 200 + successful parse. Pumps ws.loop() so the WebSocket
// stays alive across the request (these run from ha_loop(), not the WS callback).
static bool ha_get_state(const char* eid, JsonDocument& doc, const JsonDocument& filter) {
  static const String auth = String("Bearer ") + HA_TOKEN;  // built once
  String url = String("http://") + HA_HOST + ":" + HA_PORT + "/api/states/" + eid;
  HTTPClient http;
  http.begin(url);
  http.addHeader("Authorization", auth);
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    ok = deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter)) == DeserializationError::Ok;
  } else {
    Serial.printf("REST %s -> %d\n", eid, code);
  }
  http.end();
  ws.loop();
  return ok;
}

// Fetch one numeric attribute from an entity and push it to a UI setter (used for
// the header temperatures, which are not MOSAIC tiles). is<float>() rejects null
// AND non-numeric values (e.g. "unavailable") so a bad read can't show a fake 0.
static void fetch_one_temp(const char* eid, const char* attr, void (*setter)(int)) {
  StaticJsonDocument<64> filter;
  filter["attributes"][attr] = true;
  DynamicJsonDocument doc(256);
  if (ha_get_state(eid, doc, filter) && doc["attributes"][attr].is<float>())
    setter((int)lroundf(doc["attributes"][attr].as<float>()));
}

// Build a ThermoState from a climate entity's state+attributes and push to the UI
// (also updates the header indoor temp from current_temperature).
static void push_thermostat(const char* state, JsonVariantConst attrs) {
  ThermoState s;
  memset(&s, 0, sizeof(s));
  strncpy(s.mode, state ? state : "off", sizeof(s.mode) - 1);
  s.available = state && strcmp(state, "unavailable") != 0 && strcmp(state, "unknown") != 0;
  s.dual = (state && strcmp(state, "heat_cool") == 0);
  const char* act = attrs["hvac_action"];
  strncpy(s.action, act ? act : "", sizeof(s.action) - 1);
  if (attrs["current_temperature"].is<float>()) {
    s.current = (int)lroundf(attrs["current_temperature"].as<float>());
    ui_set_indoor_temp(s.current);
  }
  if (attrs["target_temp_low"].is<float>())  s.low    = (int)lroundf(attrs["target_temp_low"].as<float>());
  if (attrs["target_temp_high"].is<float>()) s.high   = (int)lroundf(attrs["target_temp_high"].as<float>());
  if (attrs["temperature"].is<float>())      s.target = (int)lroundf(attrs["temperature"].as<float>());
  ui_update_thermostat(&s);
}

static void fetch_thermostat() {
  StaticJsonDocument<384> filter;
  filter["state"] = true;
  filter["attributes"]["current_temperature"] = true;
  filter["attributes"]["target_temp_low"] = true;
  filter["attributes"]["target_temp_high"] = true;
  filter["attributes"]["temperature"] = true;
  filter["attributes"]["hvac_action"] = true;
  DynamicJsonDocument doc(512);
  if (ha_get_state(ENTITY_INDOOR_TEMP, doc, filter))
    push_thermostat(doc["state"], doc["attributes"]);
}

static void fetch_fan_state() {
  StaticJsonDocument<32> filter;
  filter["state"] = true;
  DynamicJsonDocument doc(128);
  if (ha_get_state(ENTITY_THERMO_FAN, doc, filter)) {
    const char* st = doc["state"];
    if (st) ui_set_fan_state(strcmp(st, "on") == 0);
  }
}

// Runs from ha_loop() (not the WS callback). Pumps LVGL between entities so the
// UI keeps ticking across the sequence of blocking REST GETs.
static void fetch_initial_states() {
  for (int i = 0; i < MOSAIC_COUNT; i++) {
    StaticJsonDocument<96> filter;
    filter["state"] = true;
    filter["attributes"]["brightness"] = true;
    filter["attributes"]["percentage"] = true;
    DynamicJsonDocument doc(384);
    if (ha_get_state(MOSAIC[i].entity_id, doc, filter)) {
      const char* state = doc["state"];
      if (state) {
        ui_update_tile_state(i, state);
        set_tile_value(i, MOSAIC[i].entity_id, state, doc["attributes"]);
      }
    }
    lv_tick_inc(10);
    lv_timer_handler();
  }
  fetch_thermostat();
  fetch_fan_state();
  fetch_one_temp(ENTITY_OUTDOOR_TEMP, "temperature", ui_set_outdoor_temp);
  Serial.println("Initial states fetched");
}

static bool needs_fetch = false;

static void on_message(uint8_t* payload, size_t length) {
  // Large doc because get_states response can be big; use filter to trim it.
  // NOTE: this filter doc must be big enough to hold ALL keys below — if it
  // overflows, ArduinoJson silently drops the last-added keys (which caused the
  // thermostat setpoints to come back as 0). Keep capacity comfortably large.
  StaticJsonDocument<512> filter;
  filter["type"] = true;
  filter["success"] = true;
  filter["event"]["event_type"] = true;
  filter["event"]["data"]["entity_id"] = true;
  filter["event"]["data"]["new_state"]["state"] = true;
  filter["event"]["data"]["new_state"]["attributes"]["brightness"] = true;
  filter["event"]["data"]["new_state"]["attributes"]["percentage"] = true;
  filter["event"]["data"]["new_state"]["attributes"]["current_temperature"] = true;
  filter["event"]["data"]["new_state"]["attributes"]["temperature"] = true;
  filter["event"]["data"]["new_state"]["attributes"]["target_temp_low"] = true;
  filter["event"]["data"]["new_state"]["attributes"]["target_temp_high"] = true;
  filter["event"]["data"]["new_state"]["attributes"]["hvac_action"] = true;
  filter["result"][0]["entity_id"] = true;
  filter["result"][0]["state"] = true;

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, payload, length, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("on_message: JSON parse failed (%s)\n", err.c_str());
    return;
  }

  const char* type = doc["type"];
  if (!type) return;

  if (strcmp(type, "auth_required") == 0) {
    StaticJsonDocument<256> auth;
    auth["type"] = "auth";
    auth["access_token"] = HA_TOKEN;
    send_json(auth);

  } else if (strcmp(type, "auth_ok") == 0) {
    authenticated = true;
    ui_set_ha_connected(true);
    Serial.println("HA authenticated");
    // Subscribe to state_changed events
    StaticJsonDocument<96> sub;
    sub["id"] = msg_id++;
    sub["type"] = "subscribe_events";
    sub["event_type"] = "state_changed";
    send_json(sub);
    // Trigger initial state fetch from ha_loop()
    needs_fetch = true;

  } else if (strcmp(type, "auth_invalid") == 0) {
    Serial.println("HA auth invalid — check HA_TOKEN in config.h");
    ui_set_ha_connected(false);
    ws.disconnect();
    ws.setReconnectInterval(0); // stop reconnecting — bad token would trigger IP ban

  } else if (strcmp(type, "event") == 0) {
    const char* etype = doc["event"]["event_type"];
    if (etype && strcmp(etype, "state_changed") == 0) {
      const char* eid = doc["event"]["data"]["entity_id"];
      const char* est = doc["event"]["data"]["new_state"]["state"];
      if (!eid || !est) return;

      // Header temperatures (not MOSAIC tiles)
      JsonVariantConst attrs = doc["event"]["data"]["new_state"]["attributes"];
      if (strcmp(eid, ENTITY_INDOOR_TEMP) == 0) {
        push_thermostat(est, attrs);   // updates header indoor temp + thermostat tile/detail
        return;
      }
      if (strcmp(eid, ENTITY_THERMO_FAN) == 0) {
        ui_set_fan_state(strcmp(est, "on") == 0);
        return;
      }
      if (strcmp(eid, ENTITY_OUTDOOR_TEMP) == 0) {
        if (attrs["temperature"].is<float>())
          ui_set_outdoor_temp((int)lroundf(attrs["temperature"].as<float>()));
        return;
      }

      for (int i = 0; i < MOSAIC_COUNT; i++) {
        if (strcmp(MOSAIC[i].entity_id, eid) == 0) {
          ui_update_tile_state(i, est);
          set_tile_value(i, eid, est, doc["event"]["data"]["new_state"]["attributes"]);
          return;
        }
      }
    }
  }
}

static void ws_event(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("HA WebSocket connected");
      break;
    case WStype_DISCONNECTED:
      authenticated = false;
      needs_fetch = false;
      ui_set_ha_connected(false);
      Serial.println("HA WebSocket disconnected");
      break;
    case WStype_TEXT:
      on_message(payload, length);
      break;
    default:
      break;
  }
}

void ha_init() {
  ws.begin(HA_HOST, HA_PORT, "/api/websocket");
  ws.setExtraHeaders("Origin: http://" HA_HOST);
  ws.onEvent(ws_event);
  ws.setReconnectInterval(5000);
}

void ha_loop() {
  ws.loop();
  if (needs_fetch) {
    needs_fetch = false;
    fetch_initial_states();
  }
  // Periodic safety-net refetch of the header temps, so they self-heal if the
  // initial fetch landed while HA was still populating (e.g., right after an HA
  // restart left the temperature attributes null).
  static uint32_t last_temp_refresh = 0;
  if (authenticated && millis() - last_temp_refresh > 120000) {
    last_temp_refresh = millis();
    fetch_thermostat();
    fetch_fan_state();
    fetch_one_temp(ENTITY_OUTDOOR_TEMP, "temperature", ui_set_outdoor_temp);
  }
}

void ha_toggle(const char* entity_id) {
  if (!authenticated) return;
  char domain[16]; entity_domain(entity_id, domain, sizeof(domain));
  // light/switch/fan toggle through their own domain; anything else uses the generic one.
  if (strcmp(domain, "light") && strcmp(domain, "switch") && strcmp(domain, "fan"))
    strcpy(domain, "homeassistant");
  StaticJsonDocument<256> doc;
  begin_call_service(doc, domain, "toggle", entity_id);
  send_json(doc);
}

void ha_fan_turn_on_pct(const char* entity_id, float pct) {
  if (!authenticated) return;
  StaticJsonDocument<256> doc;
  begin_call_service(doc, "fan", "turn_on", entity_id);
  doc["service_data"]["percentage"] = pct;
  send_json(doc);
}

void ha_thermo_fan(bool on) {
  if (!authenticated) return;
  StaticJsonDocument<256> doc;
  begin_call_service(doc, "fan", on ? "turn_on" : "turn_off", ENTITY_THERMO_FAN);
  send_json(doc);
}

void ha_climate_set_mode(const char* mode) {
  if (!authenticated) return;
  StaticJsonDocument<256> doc;
  begin_call_service(doc, "climate", "set_hvac_mode", ENTITY_INDOOR_TEMP);
  doc["service_data"]["hvac_mode"] = mode;
  send_json(doc);
}

void ha_climate_set_temp_dual(int low, int high) {
  if (!authenticated) return;
  StaticJsonDocument<256> doc;
  begin_call_service(doc, "climate", "set_temperature", ENTITY_INDOOR_TEMP);
  doc["service_data"]["target_temp_low"] = low;
  doc["service_data"]["target_temp_high"] = high;
  send_json(doc);
}

void ha_climate_set_temp_single(int target) {
  if (!authenticated) return;
  StaticJsonDocument<256> doc;
  begin_call_service(doc, "climate", "set_temperature", ENTITY_INDOOR_TEMP);
  doc["service_data"]["temperature"] = target;
  send_json(doc);
}

bool ha_is_connected() {
  return authenticated;
}
