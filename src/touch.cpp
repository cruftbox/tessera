// touch.cpp — GT911 capacitive-touch driver bridged into LVGL's input system.
//
// Beyond a plain touch read, this file owns three behaviours:
//   - Coordinate transform: the GT911 reports points in the panel's native
//     orientation, but the display runs at rotation 3 (270°), so raw points are
//     remapped (x = rawY, y = 479 - rawX) before they reach LVGL.
//   - Swipe paging: horizontal swipes are detected here on release and turned
//     into page changes, instead of LVGL's gesture system (which was unreliable
//     over the full-screen tileview).
//   - Wake-from-dim: the touch that wakes a dimmed screen is swallowed whole, so
//     it only brightens the panel and never also taps a tile or fires a swipe.
//
// The GT911 library is the vendored, patched TAMC_GT911 (see lib/TAMC_GT911/PATCH.md).

#include <Arduino.h>
#include <Wire.h>
#include <TAMC_GT911.h>
#include <lvgl.h>
#include "touch.h"
#include "ui.h"
#include "config.h"

static TAMC_GT911 ts(PIN_TOUCH_SDA, PIN_TOUCH_SCL, -1, -1, 480, 480);

#define SWIPE_THRESH 60  // px of horizontal travel needed to count as a page swipe

static bool was_pressed = false;
static int  start_x = 0, start_y = 0;  // where the current touch began
static int  last_x = 0,  last_y = 0;   // latest position during the touch
static bool consume_touch = false;     // swallow a whole touch (the wake-from-dim one)

void touch_init() {
  ts.begin();
  ts.setRotation(ROTATION_NORMAL);
  Serial.println("Touch init done");
}

void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  ts.read();
  if (ts.isTouched && ts.touches > 0) {
    if (ui_is_dimmed()) consume_touch = true;  // touch that wakes the screen
    ui_notify_touch();
    if (consume_touch) {
      // Swallow the ENTIRE wake-up touch until the finger lifts, so it only
      // wakes the screen and never taps a tile or triggers a swipe.
      was_pressed = false;
      data->state = LV_INDEV_STATE_REL;
      return;
    }
    // Touch panel is in native orientation; display is rotation 3 (270°).
    // Map raw -> displayed: x = rawY, y = (479 - rawX).
    int x = ts.points[0].y;
    int y = 479 - ts.points[0].x;
    if (!was_pressed) {        // start of a new touch
      was_pressed = true;
      start_x = x; start_y = y;
      ui_touch_pressed();
    }
    last_x = x; last_y = y;
    data->point.x = x;
    data->point.y = y;
    data->state   = LV_INDEV_STATE_PR;
  } else {
    consume_touch = false;     // finger lifted — next touch is a real interaction
    if (was_pressed) {         // touch just released — check for a swipe
      was_pressed = false;
      int dx = last_x - start_x;
      int dy = last_y - start_y;
      if (abs(dx) > SWIPE_THRESH && abs(dx) > abs(dy)) {
        // swipe left (dx<0) -> next page; swipe right (dx>0) -> previous page
        ui_swipe_page(dx < 0 ? +1 : -1);
      }
    }
    data->state = LV_INDEV_STATE_REL;
  }
}
