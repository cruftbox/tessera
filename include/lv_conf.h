#if 1

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc

#define LV_MEM_SIZE (64 * 1024U)

#define LV_DISP_DEF_REFR_PERIOD  10
#define LV_INDEV_DEF_READ_PERIOD 10

#define LV_DPI_DEF 130

#define LV_SPRINTF_CUSTOM 0

// Enable widgets we need
#define LV_USE_BTN      1
#define LV_USE_LABEL    1
#define LV_USE_IMG      1
#define LV_USE_LINE     1
#define LV_USE_ARC      1
#define LV_USE_SLIDER   1
#define LV_USE_TILEVIEW 1

// Fonts
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

// Logging
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

// Animation
#define LV_USE_ANIMATION 1

// Shadow / complex draw (needed for gradients, shadows, rounded corners)
#define LV_DRAW_COMPLEX 1

// Allow 3-stop gradients (blue -> violet -> pink background)
#define LV_GRADIENT_MAX_STOPS 3

#endif
#endif
