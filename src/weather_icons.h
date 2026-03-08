#pragma once

#include "lvgl.h"

/**
 * Draw a weather icon on a 48x48 canvas based on WMO weather code.
 * Canvas must already have a buffer set (48x48 LV_IMG_CF_TRUE_COLOR).
 */
void weather_icon_draw(lv_obj_t *canvas, int weather_code);
