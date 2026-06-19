#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "display.h"
#include "config.h"

// Pin map sourced from GFX Library 1.6.6 Arduino_GFX_dev_device.h ESP32_4848S040_86BOX_GUITION
static Arduino_DataBus *bus = new Arduino_SWSPI(
  GFX_NOT_DEFINED /* DC */, 39 /* CS */,
  48 /* SCK */, 47 /* MOSI */, GFX_NOT_DEFINED /* MISO */
);

static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
  11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0  /* R4 */,
  8  /* G0 */, 20 /* G1 */, 3  /* G2 */, 46 /* G3 */, 9  /* G4 */, 10 /* G5 */,
  4  /* B0 */, 5  /* B1 */, 6  /* B2 */, 7  /* B3 */, 15 /* B4 */,
  1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
  1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
  0 /* pclk_active_neg */, 12000000 /* prefer_speed */, false /* useBigEndian */,
  0 /* de_idle_high */, 0 /* pclk_idle_high */, 480 * 10 /* bounce_buffer_size_px */
);

static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480 /* width */, 480 /* height */, rgbpanel, 3 /* rotation */, true /* auto_flush */,
  bus, GFX_NOT_DEFINED /* RST */, st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);

static lv_color_t *draw_buf1 = nullptr;
static lv_color_t *draw_buf2 = nullptr;
static lv_disp_draw_buf_t draw_buf_dsc;
static lv_disp_drv_t disp_drv;

void display_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
  lv_disp_flush_ready(disp);
}

void display_set_backlight(uint8_t brightness) {
  ledcWrite(PIN_BACKLIGHT, brightness);
}

void display_init() {
  ledcAttach(PIN_BACKLIGHT, 5000, 8);
  ledcWrite(PIN_BACKLIGHT, BRIGHT_FULL);

  gfx->begin();
  gfx->fillScreen(0x0000);

  size_t buf_size = 480 * 20 * sizeof(lv_color_t);
  draw_buf1 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  draw_buf2 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (!draw_buf1 || !draw_buf2) {
    Serial.println("ERROR: Failed to allocate LVGL draw buffers in PSRAM");
    return;
  }

  lv_disp_draw_buf_init(&draw_buf_dsc, draw_buf1, draw_buf2, buf_size / sizeof(lv_color_t));

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = 480;
  disp_drv.ver_res  = 480;
  disp_drv.flush_cb = display_flush;
  disp_drv.draw_buf = &draw_buf_dsc;
  lv_disp_drv_register(&disp_drv);
}
