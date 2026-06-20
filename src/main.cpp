// main.cpp — firmware entry point.
//
// setup() brings the stack up in order: LVGL, the ST7701S display, GT911 touch,
// the UI, then WiFi. Once WiFi is up it starts NTP time, OTA, and the Home
// Assistant client. loop() pumps LVGL, ArduinoOTA, and the HA WebSocket, and
// polls the WiFi link state for the header icon.
//
// Per-device secrets and tile layout live in include/config.h (gitignored; copy
// from config.h.example). See CLAUDE.md for the architecture overview.

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include "display.h"
#include "touch.h"
#include "ui.h"
#include "ha_client.h"
#include "ota.h"
#include "config.h"
#include "secrets.h"   // WiFi creds, TZ/NTP (gitignored; see setup wizard)

static lv_indev_drv_t indev_drv;

// One-time bring-up of the services that need WiFi (NTP, OTA, HA). Deferred to
// the first successful association in loop() so a slow or missing AP at boot
// never blocks the UI — and so it self-heals once WiFi eventually connects.
static void start_services() {
  Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
  configTzTime(TZ_INFO, NTP_SERVER);
  ota_init();
  ha_init();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Tessera starting...");

  lv_init();
  display_init();
  touch_init();

  lv_indev_drv_init(&indev_drv);
  indev_drv.type    = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read;
  lv_indev_drv_register(&indev_drv);

  ui_init();

  // Start WiFi non-blocking — don't stall setup() waiting to associate. The
  // connection state machine in loop() starts NTP/OTA/HA on the first connect
  // and re-kicks association if WiFi is slow or briefly unavailable.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  Serial.printf("Connecting to %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {
  static uint32_t last_tick = 0;
  uint32_t now = millis();
  lv_tick_inc(now - last_tick);
  last_tick = now;
  lv_timer_handler();

  // Connection state machine: bring up NTP/OTA/HA on the first WiFi connect, and
  // re-kick association if WiFi never comes up or drops for too long, so the
  // device self-heals without a power-cycle.
  static bool services_started = false;
  static uint32_t last_wifi_attempt = 0;
  if (WiFi.status() == WL_CONNECTED) {
    if (!services_started) { start_services(); services_started = true; }
  } else if (now - last_wifi_attempt > 20000) {
    last_wifi_attempt = now;
    Serial.println("WiFi down — retrying association");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  if (services_started) {   // only pump services once they have been initialised
    ota_loop();
    ha_loop();
  }

  // Poll WiFi link state for the header WiFi icon (every 2s, update only on change).
  static uint32_t last_wifi_check = 0;
  static int last_wifi = -1;
  if (now - last_wifi_check > 2000) {
    last_wifi_check = now;
    int w = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
    if (w != last_wifi) { last_wifi = w; ui_set_wifi_connected(w != 0); }
  }

  delay(5);
}
