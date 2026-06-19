#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <lvgl.h>
#include "display.h"
#include "touch.h"
#include "ui.h"
#include "ha_client.h"
#include "config.h"

static lv_indev_drv_t indev_drv;

void setup() {
  Serial.begin(115200);
  delay(2000); // wait for monitor to attach
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

    // Verify token via REST API /api/ endpoint
    {
      HTTPClient http;
      String url = String("http://") + HA_HOST + ":" + HA_PORT + "/api/";
      http.begin(url);
      http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
      int code = http.GET();
      Serial.printf("Token check: HTTP %d\n", code);
      if (code > 0) Serial.println(http.getString().substring(0, 80));
      http.end();
    }

    ha_init();
  } else {
    Serial.println("WiFi connect failed — check WIFI_SSID/WIFI_PASSWORD in config.h");
  }
}

void loop() {
  lv_tick_inc(5);
  lv_timer_handler();
  ha_loop();
  delay(5);
}
