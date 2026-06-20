#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include "display.h"
#include "touch.h"
#include "ui.h"
#include "ha_client.h"
#include "config.h"

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
  ha_loop();
  delay(5);
}
