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

  Serial.printf("Connecting to %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    lv_tick_inc(500);
    lv_timer_handler();
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    configTzTime(TZ_INFO, NTP_SERVER);
    ota_init();
    ha_init();
  } else {
    Serial.println("WiFi connect failed — check WIFI_SSID/WIFI_PASSWORD in config.h");
  }
}

void loop() {
  static uint32_t last_tick = 0;
  uint32_t now = millis();
  lv_tick_inc(now - last_tick);
  last_tick = now;
  lv_timer_handler();
  ota_loop();
  ha_loop();

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
