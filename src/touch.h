#pragma once
#include <lvgl.h>

// Initialise the GT911 touch controller (I2C pins + rotation). Call once at boot.
void touch_init();

// LVGL input-device read callback. Polls the GT911, applies the rotation-3
// coordinate transform, and reports press/release + position to LVGL. Also
// handles swipe-paging and wake-from-dim touch consumption — see touch.cpp.
void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data);
