#pragma once

// Wireless firmware updates over the network (ArduinoOTA / espota).
// Call ota_init() once after WiFi is connected, then ota_loop() every loop().
void ota_init();
void ota_loop();
