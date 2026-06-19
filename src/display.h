#pragma once

void display_init();
void display_set_backlight(uint8_t brightness);
void display_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
