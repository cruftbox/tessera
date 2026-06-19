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

// When a tile is ON, show a richer value: brightness % for lights, speed % for
// fans. OFF tiles keep the default "Off" text set by ui_update_tile_state.
static void set_tile_value(int idx, const char* eid, const char* state, JsonVariantConst attrs) {
  if (!state || strcmp(state, "on") != 0) return;
  char buf[12];
  if (strncmp(eid, "light.", 6) == 0 && !attrs["brightness"].isNull()) {
    int b = attrs["brightness"].as<int>();          // 0..255
    snprintf(buf, sizeof(buf), "%d%%", (b * 100 + 127) / 255);
    ui_update_tile_value(idx, buf);
  } else if (strncmp(eid, "fan.", 4) == 0 && !attrs["percentage"].isNull()) {
    snprintf(buf, sizeof(buf), "%d%%", attrs["percentage"].as<int>());
    ui_update_tile_value(idx, buf);
  }
}

// Fetch initial state for each MOSAIC entity via REST.
// Called from ha_loop() (not from WS callback) to avoid blocking the WS stack.
// Calls ws.loop() between requests to keep the connection alive.
// Fetch one numeric attribute from an entity and push it to a UI setter (used
// for the header temperatures, which are not MOSAIC tiles).
static void fetch_one_temp(const char* eid, const char* attr, void (*setter)(int)) {
  String url = String("http://") + HA_HOST + ":" + HA_PORT + "/api/states/" + eid;
  HTTPClient http;
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  if (http.GET() == 200) {
    String body = http.getString();
    StaticJsonDocument<64> filter;
    filter["attributes"][attr] = true;
    DynamicJsonDocument doc(256);
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) == DeserializationError::Ok
        && !doc["attributes"][attr].isNull()) {
      setter((int)lroundf(doc["attributes"][attr].as<float>()));
    }
  }
  http.end();
  ws.loop();
}

// Build a ThermoState from a climate entity's state+attributes and push to the UI
// (also updates the header indoor temp from current_temperature).
static void push_thermostat(const char* state, JsonVariantConst attrs) {
  ThermoState s;
  memset(&s, 0, sizeof(s));
  strncpy(s.mode, state ? state : "off", sizeof(s.mode) - 1);
  s.available = state && strcmp(state, "unavailable") != 0 && strcmp(state, "unknown") != 0;
  s.dual = (state && strcmp(state, "heat_cool") == 0);
  if (!attrs["current_temperature"].isNull()) {
    s.current = (int)lroundf(attrs["current_temperature"].as<float>());
    ui_set_indoor_temp(s.current);
  }
  if (!attrs["target_temp_low"].isNull())  s.low    = (int)lroundf(attrs["target_temp_low"].as<float>());
  if (!attrs["target_temp_high"].isNull()) s.high   = (int)lroundf(attrs["target_temp_high"].as<float>());
  if (!attrs["temperature"].isNull())      s.target = (int)lroundf(attrs["temperature"].as<float>());
  ui_update_thermostat(&s);
}

static void fetch_thermostat() {
  String url = String("http://") + HA_HOST + ":" + HA_PORT + "/api/states/" + ENTITY_INDOOR_TEMP;
  HTTPClient http;
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  if (http.GET() == 200) {
    String body = http.getString();
    StaticJsonDocument<192> filter;
    filter["state"] = true;
    filter["attributes"]["current_temperature"] = true;
    filter["attributes"]["target_temp_low"] = true;
    filter["attributes"]["target_temp_high"] = true;
    filter["attributes"]["temperature"] = true;
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
      push_thermostat(doc["state"], doc["attributes"]);
    }
  }
  http.end();
  ws.loop();
}

static void fetch_initial_states() {
  String auth_header = String("Bearer ") + HA_TOKEN;
  for (int i = 0; i < MOSAIC_COUNT; i++) {
    String url = String("http://") + HA_HOST + ":" + HA_PORT + "/api/states/" + MOSAIC[i].entity_id;
    HTTPClient http;
    http.begin(url);
    http.addHeader("Authorization", auth_header);
    int code = http.GET();
    if (code == 200) {
      String body = http.getString();
      StaticJsonDocument<96> filter;
      filter["state"] = true;
      filter["attributes"]["brightness"] = true;
      filter["attributes"]["percentage"] = true;
      DynamicJsonDocument doc(384);
      if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
        const char* state = doc["state"];
        if (state) {
          ui_update_tile_state(i, state);
          set_tile_value(i, MOSAIC[i].entity_id, state, doc["attributes"]);
        }
      }
    } else {
      Serial.printf("REST %s -> %d\n", MOSAIC[i].entity_id, code);
    }
    http.end();
    ws.loop(); // keep WS alive during fetch
    lv_tick_inc(10);
    lv_timer_handler();
  }
  fetch_thermostat();
  fetch_one_temp(ENTITY_OUTDOOR_TEMP, "temperature", ui_set_outdoor_temp);
  Serial.println("Initial states fetched");
}

static bool needs_fetch = false;

static void on_message(uint8_t* payload, size_t length) {
  // Large doc because get_states response can be big; use filter to trim it
  StaticJsonDocument<128> filter;
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
  filter["result"][0]["entity_id"] = true;
  filter["result"][0]["state"] = true;

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, payload, length, DeserializationOption::Filter(filter)) != DeserializationError::Ok)
    return;

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
      if (strcmp(eid, ENTITY_OUTDOOR_TEMP) == 0) {
        if (!attrs["temperature"].isNull())
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
    fetch_one_temp(ENTITY_OUTDOOR_TEMP, "temperature", ui_set_outdoor_temp);
  }
}

void ha_toggle(const char* entity_id) {
  if (!authenticated) return;
  char domain[16] = "homeassistant";
  if      (strncmp(entity_id, "light.",  6) == 0) strcpy(domain, "light");
  else if (strncmp(entity_id, "switch.", 7) == 0) strcpy(domain, "switch");
  else if (strncmp(entity_id, "fan.",    4) == 0) strcpy(domain, "fan");

  StaticJsonDocument<256> doc;
  doc["id"] = msg_id++;
  doc["type"] = "call_service";
  doc["domain"] = domain;
  doc["service"] = "toggle";
  doc["service_data"]["entity_id"] = entity_id;
  send_json(doc);
}

void ha_fan_turn_on_pct(const char* entity_id, float pct) {
  if (!authenticated) return;
  StaticJsonDocument<256> doc;
  doc["id"] = msg_id++;
  doc["type"] = "call_service";
  doc["domain"] = "fan";
  doc["service"] = "turn_on";
  doc["service_data"]["entity_id"] = entity_id;
  doc["service_data"]["percentage"] = pct;
  send_json(doc);
}

void ha_climate_set_mode(const char* mode) {
  if (!authenticated) return;
  StaticJsonDocument<256> doc;
  doc["id"] = msg_id++;
  doc["type"] = "call_service";
  doc["domain"] = "climate";
  doc["service"] = "set_hvac_mode";
  doc["service_data"]["entity_id"] = ENTITY_INDOOR_TEMP;
  doc["service_data"]["hvac_mode"] = mode;
  send_json(doc);
}

void ha_climate_set_temp_dual(int low, int high) {
  if (!authenticated) return;
  StaticJsonDocument<256> doc;
  doc["id"] = msg_id++;
  doc["type"] = "call_service";
  doc["domain"] = "climate";
  doc["service"] = "set_temperature";
  doc["service_data"]["entity_id"] = ENTITY_INDOOR_TEMP;
  doc["service_data"]["target_temp_low"] = low;
  doc["service_data"]["target_temp_high"] = high;
  send_json(doc);
}

void ha_climate_set_temp_single(int target) {
  if (!authenticated) return;
  StaticJsonDocument<256> doc;
  doc["id"] = msg_id++;
  doc["type"] = "call_service";
  doc["domain"] = "climate";
  doc["service"] = "set_temperature";
  doc["service_data"]["entity_id"] = ENTITY_INDOOR_TEMP;
  doc["service_data"]["temperature"] = target;
  send_json(doc);
}

bool ha_is_connected() {
  return authenticated;
}
