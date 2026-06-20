#pragma once

// Initialise the RGB panel, allocate LVGL draw buffers in PSRAM, and register
// the LVGL display driver. Call once at boot, after lv_init().
void display_init();

// Set backlight brightness (0-255) via the PWM channel. Used for idle dimming.
void display_set_backlight(uint8_t brightness);

// LVGL flush callback — blits a rendered area to the panel. Registered by
// display_init(); not normally called directly.
void display_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
